/*
 * Author: Xiru Zhu
 * Date: July 4 2016
 * Updated: Jan 6 2017
 * 
 * Notes:
 *  To initialize this system,
 *  create a separate pthread and then run jdata_event_loop
 *  This should initialize jdata system. 
 */
#include "jdata_updated.h"
char app_id[256];

//redis server connection parameters
char *redis_serv_IP;
int redis_serv_port;

//jamstate variable to be kept in reference
jamstate_t *j_s;

//For Async calls to logger. 
redisAsyncContext *jdata_async_context;

//For sync calls to logger. 
redisContext *jdata_sync_context;

//Event Loop. This responds to messages
struct event_base *base;

//Linked List System for current jdata elements. 
//This is because we need to look up which jdata is updated
jdata_list_node *jdata_list_head = NULL;
jdata_list_node *jdata_list_tail = NULL;

/*
Function to initialize values and attach them to the event loop
Inputs
    jamstate
    application unique id
    redis server ip
    redis server port
*/
void jdata_attach(jamstate_t *js, char *application_id, char *serv_ip, int serv_port){
  sprintf(app_id, "%s", application_id);
  j_s = js;

  //These are the initial redis server numbers. 
  redis_serv_IP = strdup(serv_ip);
  redis_serv_port = serv_port; //Default Port Number

  //Initialize event base
  base = event_base_new();
 
  //Initialize an async context
  jdata_async_context = redisAsyncConnect(redis_serv_IP, redis_serv_port);
  if (jdata_async_context->err) {
      printf("Error: %s\n", jdata_async_context->errstr);
  }

  //Attach async context to base
  redisLibeventAttach(jdata_async_context, base);
  redisAsyncSetConnectCallback(jdata_async_context, jdata_default_connection);
  redisAsyncSetDisconnectCallback(jdata_async_context, jdata_default_disconnection);
  
  //Initialize sync context
  struct timeval timeout = { 1, 500000 }; // Sync timeout time
  //Initialize sync context
  jdata_sync_context = redisConnectWithTimeout(redis_serv_IP, redis_serv_port, timeout);
  if (jdata_sync_context == NULL || jdata_sync_context->err) {
      if (jdata_sync_context) {
          printf("Connection error: %s\n", jdata_sync_context->errstr);
          redisFree(jdata_sync_context);
      } else {
          printf("Connection error: can't allocate redis context\n");
      }
  }
}

/*
Initializes jdata system 
This has to be run in a separate thread otherwise will block the current thread. 
This thread created as to be pthread, otherwise will block all other process. 
Input:
    jamstate
*/
void *jdata_init(void *args, char *application_id, char *serv_ip, int serv_port){
  jamstate_t *js = (jamstate_t *)args;
  jdata_attach(js, application_id, serv_ip, serv_port);
  #ifdef DEBUG_LVL1
    printf("JData initialized...\n");
  #endif
  event_base_dispatch(base);
  return NULL;
}

/*
 * This is the default connection callback. 
 * This is utilized when the connection callback for jdata is not defined
 * You do not need to use this anywhere, this is only utilized in the library a default callback. 
 */
void jdata_default_connection(const redisAsyncContext *c, int status) {
  if (status != REDIS_OK) {
      printf("Connection Error: %s\n", c->errstr);
      return;
  }
  #ifdef DEBUG_LVL1
    printf("Connected...\n");
  #endif
}

/*
 * This is the default disconnection callback. 
 * This is utilized when the disconnection callback for jdata is not defined
 * You do not need to use this anywhere, this is only utilized in the library a default callback. 
 */
void jdata_default_disconnection(const redisAsyncContext *c, int status) {
  if (status != REDIS_OK) {
      printf("Disconnection Error: %s\n", c->errstr);
      return;
  }
  #ifdef DEBUG_LVL1
    printf("Disconnected...\n");
  #endif
}

/*
 * This is the default jdata received callback. 
 * This is utilized when a jdata is called but no callback was assigned. It will simply print the value. 
 * You do not need to use this anywhere, this is only utilized in the library as a default callback. 
 */
void jdata_default_msg_received(redisAsyncContext *c, void *reply, void *privdata) {
  redisReply *r = reply;
  if (reply == NULL) return;
  if (r->type == REDIS_REPLY_ARRAY) {
      for (int j = 0; j < r->elements; j++) {
          printf("%u) %s\n", j, r->element[j]->str);
      }
  }else if(r->type == REDIS_REPLY_ERROR){
    printf("%s\n", r->str);
  }else{
    printf("BUGS .... \n");
  }
    #ifdef DEBUG_LVL1
    printf("Broadcast received...\n");
  #endif
}

/*
 * jdata function to log to server.
 * requires jdata to have been initialized first. Otherwise will segfault. 
 * 
 * Inputs 
 *  key -> The jdata variable name 
 *  value -> the value to be logged
 *  callback -> the callback function if you want a custom callback to signal when a log succeeds. 
                        Can be null
 * jdata grade = 'A'
 * -> jdata_log_to_server("grade", 'A', null);
 */
void jdata_log_to_server(char *key, char *value, msg_rcv_callback callback){
  if(callback == NULL)
    callback = jdata_default_msg_received;

  int length = strlen(value) + strlen(DELIM) + strlen(app_id) + strlen(DELIM) + 10;
  char newValue[length];
  sprintf(newValue , "%s%s%s", value, DELIM, app_id);
  redisAsyncCommand(jdata_async_context, callback, NULL, "EVAL %s 1 %s %s", "redis.replicate_commands(); \
                                                          local t = (redis.call('TIME'))[1]; \
                                                          local insert_order =  redis.call('ZCARD', KEYS[1]) + 1; \
                                                          redis.call('ZADD', KEYS[1], t, ARGV[1] .. \"$$$\" .. insert_order .. \"$$$\" .. t); \
                                                          return {t}", key, newValue);
  #ifdef DEBUG_LVL1
    printf("Logging executed...\n");
  #endif
}
/*
 * jdata function to remove a logged value in redis.
 * requires jdata to have been initialized first. Otherwise will segfault. 
 * 
 * Inputs 
 *  key -> The jdata variable name 
 *  value -> the value to be removed
 *  callback -> the callback function if you want a custom callback to signal when a delete succeeds. 
                        Can be null
 * jdata grade = 'A'
 * jdata grade = 'B'
 *
 * This is something you can do if you don't want people seeing the 'A' value in the logs
 * -> jdata_log_to_server("grade", 'A', null)
 * -> jdata_remove_element("grade", 'A', null);
 * -> jdata_log_to_server("grade", 'B', null)
 */
void jdata_remove_element(char *key, char *value, msg_rcv_callback callback){
  if(callback == NULL)
    callback = jdata_default_msg_received;  
  redisAsyncCommand(jdata_async_context, callback, NULL, "ZREM %s %s", key, value);
  #ifdef DEBUG_LVL1
    printf("Element Removed...\n");
  #endif
}

/*
 * jdata function to subscribe to a value
 * This function should be called when we want to subscribe a value. That way, when the value is updated from somewhere else, 
 * the jdata gets notified about it. 
 * This function is utilized by jbroadcaster which receives the data on the c side while the logger logs on the c side.  
 *
 * Inputs 
 *  key -> The jdata variable name 
 *  value -> the value to be removed
 *  on_msg -> the callback function if you want a custom callback to do something when someone logs into that jdata
 *  connect -> the callback function if you want a custom callback for checking the connection. Can inform when connect attempt fails. 
 *  disconnect -> the callback function if you want a custom callback to notify for disconnections. 
 * 
 * Returns the context c, which should be saved when we free the jdata value. 
 */
redisAsyncContext *jdata_subscribe_to_server(char *key, msg_rcv_callback on_msg, connection_callback connect, connection_callback disconnect){
  char cmd[512];
  //Create new context for the jdata. One unique connection for each variable. 
  redisAsyncContext *c = redisAsyncConnect(redis_serv_IP, redis_serv_port);
  if (c->err) {
      printf("error: %s\n", c->errstr);
      return NULL;
  }
  if(connect != NULL)
    redisAsyncSetConnectCallback(c, connect);
  else
    redisAsyncSetConnectCallback(c, jdata_default_connection);
  if(disconnect != NULL)
    redisAsyncSetDisconnectCallback(c, disconnect);
  else
    redisAsyncSetConnectCallback(c, jdata_default_disconnection);

  redisLibeventAttach(c, base);

  if(on_msg == NULL)
    on_msg = jdata_default_msg_received;
  sprintf(cmd, "SUBSCRIBE %s", key);
  redisAsyncCommand(c, on_msg, NULL, cmd);
  #ifdef DEBUG_LVL1
    printf("Subscribe executed...\n");
  #endif
  return c;
}

/*
Helper function for running custom async commands on redis. 
*/
void jdata_run_async_cmd(char *cmd, msg_rcv_callback callback){
  if(callback == NULL)
    callback = jdata_default_msg_received;
  redisAsyncCommand(jdata_async_context, callback, NULL, cmd);
  #ifdef DEBUG_LVL1
    printf("Async Command Run...\n");
  #endif
}

/*
Helper function for running custom async commands on redis. 
*/
redisReply *jdata_run_sync_cmd(char *cmd){
  redisReply * ret = redisCommand(jdata_sync_context, cmd);
  if(ret == NULL){
    printf("Error, NULL return....");
  }
  #ifdef DEBUG_LVL1
    printf("Sync Command Run...\n");
  #endif
  return ret;
}

/*
 * Function to free jbroadcaster variable. 
 * Removes it also from the jdata linked list kept in memory
 * 
 */
void free_jbroadcaster(jbroadcaster *j){
  //To do
  jdata_list_node *k;
  for(jdata_list_node *i = jdata_list_head; i != NULL;){
    k = i;
    i = i->next;
    if(i->data.jbroadcaster_data == j){
      k->next = i->next;
      free(i);
      break;
    }
  }
  free(j->data);
  threadsem_free(j->write_sem);
  free(j->key);
  free(j);
}

/*
Initializes a jbroadcaster. This specific variable is what receives values on the c side.
Should be utilized when declaring a jbroadcaster

Input:
    type => type of the jbroadcaster. Currently supports int, string, float
            Though the data will always be in string format and must be converted at a later time.  
            This is due to redis limitation being only able to store strings. 
    variable_name => name of the jbroadcaster, must be unique unfortunately. 
            With jbroadcaster, you cannot shadow a jdata variable name. 
    activitycallback_f => callback for when a broadcast is received. 
            What you would like the program to do in such case. 
*/
jbroadcaster *jbroadcaster_init(int type, char *variable_name, activitycallback_f usr_callback){
  jbroadcaster *ret;
  char buf[256];
  switch(type){
    case JBROADCAST_INT: break;
    case JBROADCAST_STRING: break;
    case JBROADCAST_FLOAT: break;
    default:
      printf("Invalid type...\n");
      return NULL;
  }
  ret = (jbroadcaster *)calloc(1, sizeof(jbroadcaster));
  ret->type = type;
  ret->write_sem = threadsem_new();
  ret->data = NULL;
  ret->key = strdup(variable_name);
  ret->usr_callback = usr_callback;
  //Now we need to add it to the list
  if(jdata_list_head == NULL){
    jdata_list_head = (jdata_list_node *)calloc(1, sizeof(jdata_list_node));
    jdata_list_head->data.jbroadcaster_data = ret;
    jdata_list_tail = jdata_list_head;
    jdata_list_tail->next = NULL;
  }else{
    jdata_list_tail->next = (jdata_list_node *)calloc(1, sizeof(jdata_list_node));
    jdata_list_tail = jdata_list_tail->next;
    jdata_list_tail->data.jbroadcaster_data = ret;
    jdata_list_tail->next = NULL;
  }
  ret->context = jdata_subscribe_to_server( variable_name, jbroadcaster_msg_rcv_callback, NULL, NULL);
  sprintf(buf, "jbroadcast_func_%s", variable_name);
  
  //IMPORTANT
  //REGISTERS the usercallback as a jasync callback to be called. 
  //This allows us to call the user defined callbacks for jbroadcaster
  activity_regcallback(j_s->atable, buf, ASYNC, "v", usr_callback);
  return ret;
}

/*
 * The jbroadcaster callback that we utilize to process broadcasts. 
 * This should not be called outside of this library. 
 * Now, the problem is that this function is run in a separate thread from the main activity thread. 
 * Thus we have to insert such callback activity in the main activity thread rather than simply running it here. 
 * In this function, we simply return the most up to date jbroadcast value. 
 * We do not save older values. 
*/
void jbroadcaster_msg_rcv_callback(redisAsyncContext *c, void *reply, void *privdata){
  redisReply *r = reply;
  char *result;
  char *var_name;
  char buf[256];
  if (reply == NULL) return;
  if (r->type == REDIS_REPLY_ARRAY) {
    var_name = r->element[1]->str;
    result = r->element[2]->str;
    if(result != NULL){
      for(jdata_list_node *i = jdata_list_head; i != NULL; i = i->next){
        if(strcmp(i->data.jbroadcaster_data->key, var_name) == 0){
          result = strdup(result);
          //At this point, we may need to add a lock to prevent race condition
          void *to_free = i->data.jbroadcaster_data->data;
          i->data.jbroadcaster_data->data = result;
          free(to_free);
          if(i->data.jbroadcaster_data->usr_callback != NULL){
            //So here instead of executing this function here, we need to insert this into the work queue
            sprintf(buf, "jbroadcast_func_%s", i->data.jbroadcaster_data->key);
            //Here, we defined a unique REXEC-JDATA to signal a jdata callback that needs to be executed. 
            command_t *rcmd = command_new("REXEC-JDATA", "ASY", buf, "__", "0", "p", i->data.jbroadcaster_data);
            queue_enq(j_s->atable->globalinq, rcmd, sizeof(command_t));
            thread_signal(j_s->atable->globalsem);
          }
          return;
        }
      }
      printf("Variable not found ... \n");
    }
  }
}

//Returns the last updated jbroadcast value given a jbroadcaster. 
void *get_jbroadcaster_value(jbroadcaster *j){
  if(j->data == NULL)
    printf("Invalid get attempt ...\n");
  assert(j->data != NULL);
  return j->data;
}

/* 
 * Initializes jshuffler
 * 
 * Input
 *  type -> data type of shuffler
 *  var_name -> name of shuffler
 *  usr_callback -> function callback for shuffler data received. 
 */
jshuffler *jshuffler_init(int type, char *var_name, activitycallback_f usr_callback){
  jshuffler *ret = calloc(1, sizeof(jshuffler));
  char buf[256];
  ret->key = strdup(var_name);
  sprintf(ret->rr_queue, "JSHUFFLER_rr_queue|%s", app_id);
  sprintf(ret->data_queue, "JSHUFFLER_data_queue|%s", app_id);
  sprintf(ret->subscribe_key, "JSHUFFLER|%s", app_id);
  ret->data = NULL;
  ret->usr_callback = usr_callback;
  sem_init(&ret->lock, 0, 0);
  redisAsyncContext *subscriber_context = redisAsyncConnect(redis_serv_IP, redis_serv_port);
  if (subscriber_context->err) {
      printf("error: %s\n", subscriber_context->errstr);
      return NULL;
  }
  if(jdata_list_head == NULL){
    jdata_list_head = (jdata_list_node *)calloc(1, sizeof(jdata_list_node));
    jdata_list_head->data.jshuffler_data = ret;
    jdata_list_tail = jdata_list_head;
    jdata_list_tail->next = NULL;
  }else{
    jdata_list_tail->next = (jdata_list_node *)calloc(1, sizeof(jdata_list_node));
    jdata_list_tail = jdata_list_tail->next;
    jdata_list_tail->data.jshuffler_data = ret;
    jdata_list_tail->next = NULL;
  }

  redisAsyncSetConnectCallback(subscriber_context, jdata_default_connection);
  redisAsyncSetConnectCallback(subscriber_context, jdata_default_disconnection);
  redisLibeventAttach(subscriber_context, base);
  redisAsyncCommand(subscriber_context, jshuffler_callback, NULL, "SUBSCRIBE %s", ret->subscribe_key);
  sprintf(buf, "jshuffler_func_%s", var_name);
  activity_regcallback(j_s->atable, var_name, ASYNC, "v", usr_callback);
  return ret;
}

/*
 * Callback for jshuffler. Should not be called by anything outside this library. 
 * Functions the same manner as jbroadcast_callback
 */
void jshuffler_callback(redisAsyncContext *c, void *reply, void *privdata){
  redisReply *r = (redisReply *)reply;
  char var_name[256];
  char buf[256];
  char *result;
  char *result_ptr;
  if(r == NULL) return;
  if (r->type == REDIS_REPLY_ARRAY) {
    if(strcmp(r->element[0]->str, "message") == 0){
      result_ptr = strstr(r->element[2]->str, "$$$");
      memcpy(var_name, r->element[2]->str, result_ptr - r->element[2]->str);
      var_name[result_ptr - r->element[2]->str] = '\0';
      result = calloc(strlen(r->element[2]->str) - strlen(var_name), sizeof(char));
      sprintf(result, "%s", result_ptr + 3);

      for(jdata_list_node *i = jdata_list_head; i != NULL; i = i->next){
        if(strcmp(i->data.jshuffler_data->key, var_name) == 0){
          //At this point, we may need to add a lock to prevent race condition
          void *to_free = i->data.jshuffler_data->data;
          i->data.jshuffler_data->data = result;
          free(to_free);
          if(i->data.jshuffler_data->usr_callback != NULL){
            sprintf(buf, "jshuffler_func_%s", i->data.jbroadcaster_data->key);
            command_t *rcmd = command_new("REXEC-JDATA", "ASY", buf, "__", "0", "p", i->data.jshuffler_data);
            queue_enq(j_s->atable->globalinq, rcmd, sizeof(command_t));
            thread_signal(j_s->atable->globalsem);
          }
          #ifdef DEBUG_LVL1
            printf("Result Received:%s\n", result);
          #endif
          return;
        }
      }
      printf("Variable name not found ...\n");
    }
  }
}

/*
 * Pushes a value into the shuffler. We assume the pushed data is in string format for now. 
 * This can be changed later on. 
 */
void jshuffler_push(jshuffler *j, char *data){
  #ifdef DEBUG_LVL1
    printf("%s %s %s\n", j->subscribe_key, j->rr_queue,  j->data_queue);
  #endif
  redisAsyncCommand(jdata_async_context, jdata_default_msg_received, NULL,
                    "EVAL %s 3 %s %s %s %s",
                    "redis.replicate_commands(); \
                    local send_to = redis.call('LLEN', KEYS[1]); \
                    if (send_to == 0) or (send_to == nil) then \
                      redis.call('RPUSH', KEYS[3], ARGV[1]); \
                      return {0}; \
                    else \
                      local var_name = redis.call('RPOP', KEYS[1]); \
                      redis.call('PUBLISH', KEYS[2] , var_name .. '$$$' .. ARGV[1]); \
                      return {send_to}; \
                    end", j->rr_queue, j->subscribe_key, j->data_queue, data);
}

/*
 * Polls a value 
 */
void *jshuffler_poll(jshuffler *j){
  //#ifdef DEBUG_LVL1
    printf("%s %s %s\n", j->subscribe_key, j->rr_queue,  j->data_queue);
  //#endif
    redisReply *ret  = redisCommand(jdata_sync_context,
                      "EVAL %s 2 %s %s %s",
                      "redis.replicate_commands(); \
                      local rr_queue_size = redis.call('LLEN', KEYS[1]); \
                      local data_queue_size = redis.call('LLEN', KEYS[2]); \
                      if (rr_queue_size == 0 or rr_queue_size == nil) and (data_queue_size > 0) then \
                        local ret = redis.call('RPOP', KEYS[2]); \
                        return {ret}; \
                      else \
                        redis.call('RPUSH', KEYS[1], ARGV[1]); \
                        return {'JSHUFFLER_WAIT'}; \
                      end", j->rr_queue, j->data_queue, j->key);
  if (ret->type == REDIS_REPLY_ARRAY) {
  //We receive JSHUFFLER_WAIT when the queue is empty and there is nothing to poll out. 
  //So we continuously poll until we get it. 
  if(strcmp(ret->element[0]->str, "JSHUFFLER_WAIT") == 0){
    printf("Polling ... \n");
    sleep(1);
    return jshuffler_poll(j);
  }
    j->data = ret;
  }else{
      return NULL;
  }
  return j->data;
}

/*
 * Logs an jdata activity 
 * This is for some logging service that was supposed to be called whenever an activity gets called. 
 * 
 */
void jcmd_log_pending_activity(char *app_id, char *actid, int index){
    char key[256];
    char actid_expanded[128];
    sprintf(key, "%s%s%s", CMD_LOGGER, DELIM, app_id);
    sprintf(actid_expanded, "%s|%d", actid, index);
    redisAsyncCommand(jdata_async_context, jdata_default_msg_received, NULL, "EVAL %s 1 %s %s", "redis.replicate_commands(); \
                                                            local t = (redis.call('TIME'))[1]; \
                                                            redis.call('ZADD', KEYS[1], t, ARGV[1]); \
                                                            return {t}", key, actid);
}
/*
 * The log inserted can beg removed when the activity is acknowledged. 
 *
 */
void jcmd_remove_acknowledged_activity(char *app_id, char *actid, int index){
    char key[256];
    char actid_expanded[128];
    sprintf(actid_expanded, "%s|%d", actid, index);
    sprintf(key, "%s%s%s", CMD_LOGGER, DELIM, app_id);
    jdata_remove_element(key, actid, NULL);
}

/*
 * Removes completely the activity from redis include all logs of it. 
 */
void jcmd_delete_pending_activity_log(char *key, msg_rcv_callback callback){
  if(callback == NULL)
    callback = jdata_default_msg_received;
  redisAsyncCommand(jdata_async_context, callback, NULL, "DEL %s", key);
}

/*
 * Returns a list of array of all logs for a particular key. 
 */
char **jcmd_get_pending_activity_log(char *key, msg_rcv_callback callback){
  if(callback == NULL)
    callback = jdata_default_msg_received;
   redisReply * r = redisCommand(jdata_sync_context, "ZRANGE %s 0 -1", key);
   if (r->type == REDIS_REPLY_ARRAY) {
     char **ret = (char **)calloc(r->elements, sizeof(char *));
     for (int j = 0; j < r->elements; j++) {
       ret[j] = strdup(r->element[j]->str);
     }
     return ret;
   }
   return NULL;
}