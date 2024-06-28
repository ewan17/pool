#include <assert.h>
#include <stdlib.h>
#include "pool.h"

static TPool *init_test(unsigned int thrds);
static void destroy_test(TPool *tp);

static void *thread_func(void *arg);

void init_pool_test() {
    TPool *tp;
    tp = init_test(8);
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

        rc = do_work(tg, work);
        assert(rc == 0);
    }

    destroy_test(tp);
}

int main(int argc, char *argv[]) {
    init_pool_test();
    add_group_test();
    add_work_test();
    return 0;    
}

static void *thread_func(void *arg) {
    int sum = 0;
    for (int i = 0; i < 50; i++)
    {
        sum++;
    }
    printf("sum %d\n", sum);
    return;
}

static TPool *init_test(unsigned int thrds) {
    TPool *tp;
    int rc;

    rc = init_pool(&tp, 8);
    assert(rc == 0);

    return tp;
}

static void destroy_test(TPool *tp) {
    destroy_pool(tp);
}