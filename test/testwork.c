#include "test.h"

unsigned int tasks = 30;

static void test_work_func(void *work_arg);

int main(int argc, char *argv[]) {
    size_t num_groups;
    num_groups = pool_num_group_helper(argc, argv);

    KiddiePool *pool;
    if(init_pool(&pool, num_groups) != 0) {
        return -1;
    }

    for (size_t i = 0; i < tasks; i++)
    {
        kg_id id;
        id = tasks % num_groups;

        int *work_arg = malloc(sizeof(int));
        *work_arg = i;

        if(add_work(pool, id, test_work_func, work_arg) != 0) {
            return -1;
        };

        printf("task %ld\n", i);
    }

    destroy_pool(pool);
    pool = NULL;

    return 0;
}

static void test_work_func(void *work_arg) {
    int *task;
    task = (int *)work_arg;
    printf("Executing task %d\n", *task);
    free(work_arg);
}