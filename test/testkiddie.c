#include <stdlib.h>
#include <stdio.h>
#include "pool.c"

// #define assert_true(expected, actual, message) \
//     if (expected == actual) { \
//         fprintf(stderr, "Assertion Error: %s\nExpected: \nActual: \nFile: %s\nLine: %d\n", message, __FILE__, __LINE__); \
//         exit(1); \
//     }

#define assert(condition, message) \
    if (!(condition)) { \
        fprintf(stderr, "Assertion Error: %s\nFile: %s\nLine: %d\n", message, __FILE__, __LINE__); \
        exit(1); \
    }

static void init_pool_test();
static void destroy_pool_test();
static void add_work_test();

int main() {
    add_work_test();
    return 0;
}

static void add_work_test() {
    char *work_test_1 = "valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./testwork 8";

    int rc = system(work_test_1);

    assert(rc == 0, "Error while testing work for pool.");
}

static void destroy_pool_test() {
    char *val_test_1 = "valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./testmem 8";

    int rc = system(val_test_1);

    assert(rc == 0, "Error while testing memory for pool.");   
}

static void init_pool_test() {
    // KiddiePool *pool;
    // size_t num_groups = 4;

    // assert(init_pool(&pool, num_groups) == 0, "Error init pool.");
    // assert(pool != NULL, "Pool cannot be NULL");
    // assert(pool->num_groups == num_groups, "Invalid num groups.");
    // assert(pool->kiddie_groups != NULL, "Invalid kiddie groups.");

    // KiddieGroup **kgs = pool->kiddie_groups;
    // for (size_t i = 0; i < num_groups; i++)
    // {
    //     KiddieGroup *kg = kgs[i];

    //     assert(kg != NULL, "Kiddie group cannot be NULL.");
    //     assert(kg->thread_count == 2, "Invalid thread count.");
    //     assert(kg->active_threads == 0, "Invalid active threads.");
    //     assert(kg->kill == 0, "Invalid active threads.");
    //     assert(kg->threads != NULL, "Threads cannot be NULL.");
    //     assert(kg->q != NULL, "Q cannot be NULL.");
    //     assert(kg->pool == pool, "Group must be assigned to the correct pool ptr.");
    // }
}

