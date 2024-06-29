#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "pool.h"

static TPool *init_test(unsigned int thrds);
static void destroy_test(TPool *tp);

static void thread_func(void *arg);

void init_pool_test(unsigned int thrds) {
    TPool *tp;
    tp = init_test(thrds);
    destroy_test(tp);
}

void add_group_test() {
    TPool *tp;
    tp = init_test(8);

    TGroup *tg1, *tg2;
    tg1 = add_group(tp, 2, 4, GROUP_DYNAMIC);
    tg2 = add_group(tp, 2, 4, GROUP_DYNAMIC);

    destroy_group(tg1);
    destroy_group(tg2);

    destroy_test(tp);
}

void add_work_test() {
    int rc;
    TPool *tp;
    tp = init_test(8);

    TGroup *tg;
    tg = add_group(tp, 2, 4, GROUP_DYNAMIC);

    for (size_t i = 0; i < 10; i++)
    {
        Work *work;
        init_work(&work);
        add_work(work, thread_func, NULL);

        // since the max threads are 4, the q size is 8.
        // so if we exceed 8, the do_work will return GROUP_FULL
        rc = do_work(tg, work);
        assert(rc != -1);
        if(rc == GROUP_FULL) {
            free(work);
        }
    }

    destroy_test(tp);
}

void add_work_test2() {
    int rc;
    TPool *tp;
    tp = init_test(24);

    size_t numGroups = 6;
    TGroup *tg[numGroups];
    for (size_t i = 0; i < numGroups; i++)
    {
        TGroup *grp;
        grp = add_group(tp, 2, 4, GROUP_DYNAMIC);
        tg[i] = grp;
    }

    int arr[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    size_t len = 20;

    for (size_t i = 0; i < len; i++)
    {
        Work *work;
        size_t index;
        int rc;

        init_work(&work);
        add_work(work, thread_func, NULL);

        index = i % numGroups;
        
        rc = do_work(tg[index], work);
        assert(rc == 0);
    }

    destroy_test(tp);
}

int main(int argc, char *argv[]) {
    // init_pool_test(8);
    // add_group_test();
    // add_work_test();
    add_work_test2();
    return 0;    
}

static void thread_func(void *arg) {
    int sum = 0;
    for (int i = 0; i < 50; i++)
    {
        sum++;
    }
    printf("sum %d\n", sum);
}

static TPool *init_test(unsigned int thrds) {
    TPool *tp;
    int rc;

    rc = init_pool(&tp, thrds);
    assert(rc == 0);

    return tp;
}

static void destroy_test(TPool *tp) {
    destroy_pool(tp);
}