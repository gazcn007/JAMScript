
#include <stdio.h>
#include <stdlib.h>
#include "toml.h"
#include "tparser.h"
#include "files.h"


/**
 * This is a tester for the TOML C parser.
 *
 * call like the following:
 *      check_tcount toml_file expected_count list-of-attributes
 * If the list-of-attributes is NULL, we count the root object.
 * It counts the number of elements in the TOML structure at the given level
 * If the specified level is not found, the count is 0.
 */

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Invalid Usage:: check_tcount toml_file expected_count optional-list-of-attributes\n");
        exit(EXIT_FAILURE);
    }

    int count = atoi(argv[2]);

    char *buf;
    int s;

    if (read_file_to_buffer(argv[1], &buf, &s) == 0) {
        t_init_parse(buf);
        if (t_parse_doc() != T_VALID_VALUE) {
            printf("\n\n Parsing error... Exiting.\n");
            exit(1);
        }
        TOMLValue *t = t_get_value();
        TOMLValue *u;
        switch (argc - 3) {
            case 0:
                u = t;
                break;
            case 1:
                u = t_query_value(t, "s", argv[3]);
                break;
            case 2:
                u = t_query_value(t, "ss", argv[3], argv[4]);
                break;
            case 3:
                u = t_query_value(t, "sss", argv[3], argv[4], argv[5]);
                break;
            case 4:
                u = t_query_value(t, "ssss", argv[3], argv[4], argv[5], argv[6]);
                break;
            default:
                exit(1);
        }

        printf("File [%s] Count check: ", argv[1]);
        if (t_count(u) == count)
            printf("PASSED\n");
        else
            printf("FAILED\n");
    }
}
