#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include "kiddiepool.h"

size_t pool_num_group_helper(int argc, char *argv[]) {
    size_t num_groups;
    if(argc == 0) {
        perror("Invalid parameters passed to mem test.");
        exit(1);
    }

    if(argc == 1) {
        num_groups = DEFAULT_NUM_GROUPS;
    } else {
        char *endptr;
        size_t num = strtoul(argv[1], &endptr, 10);

        if (*endptr != '\0') {
            num_groups = DEFAULT_NUM_GROUPS;
        } else {
            num_groups = num;
        }
    }

    return num_groups;
}

#endif //TEST_H