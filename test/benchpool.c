#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "pool.h"
#include "jhs/thpool.h"

#define BENCH_ITERATIONS 100
#define MESSAGE "Average time for 100 iterations"

double elapsed_time(struct timespec start, struct timespec finish) {
    double timeDiff;
    timeDiff = (finish.tv_sec - start.tv_sec);
    if(finish.tv_nsec >= start.tv_nsec) {
        timeDiff += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    } else {
        timeDiff += (1000000000.0 + finish.tv_nsec - start.tv_nsec) / 1000000000.0;
        timeDiff -= 1;
    }

    return timeDiff;
}

size_t factorial_func(size_t input) {
    size_t result = 1;
    for (size_t i = 1; i <= input; i++)
    {
        result *= i;
    }

    return result;
}

/**
 * Calculates the factorial for a number
 */
void thread_function(void* arg) {
    int input = (int)(intptr_t)arg;

    factorial_func(input);
}

void single_threaded(int arr[], size_t len) {
    struct timespec start, finish;
    double result; 

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < len; i++)
    {
        factorial_func(arr[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &finish);
    result = elapsed_time(start, finish);
    printf("Single thread: %.6f seconds\n", result);
}

void multi_threaded_ewan17(int arr[], size_t len) {
    struct timespec start, finish;
    double sum;

    TPool *pool;
    init_pool(&pool, 15);

    size_t numGroups = 3;
    TGroup *tg[numGroups];
    for (size_t i = 0; i < numGroups; i++)
    {
        TGroup *grp;
        grp = add_group(pool, 5, 5, GROUP_FIXED);
        assert(grp != NULL);
        tg[i] = grp;
    }

    for (size_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t i = 0; i < len; i++)
        {
            Work *work;
            size_t index;

            init_work(&work);
            add_work(work, thread_function, (void *)(intptr_t)arr[i]);

            index = i % numGroups;
            do_work(tg[index], work);
        }
        clock_gettime(CLOCK_MONOTONIC, &finish);
        sum += elapsed_time(start, finish);
    }
    
    printf("%s (ewan17): %.6f seconds\n", MESSAGE, (sum/BENCH_ITERATIONS));

    destroy_pool(pool);
}

void multi_threaded_jhs(int arr[], size_t len) {
    struct timespec start, finish;
    double sum;

    threadpool thpool = thpool_init(15);

    for (size_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t i = 0; i < len; i++)
        {
            thpool_add_work(thpool, thread_function, (void *)(intptr_t)arr[i]);
        }
        clock_gettime(CLOCK_MONOTONIC, &finish);
        sum += elapsed_time(start, finish);
    }

    printf("%s (jhs): %.6f seconds\n", MESSAGE, (sum/BENCH_ITERATIONS));

    thpool_destroy(thpool);
}

int main(int argc, char *argv[]) {
    /**
     * @todo    better to read the possible scenarios from an input file
     * @todo    for now hard coded scenarios
     */
    // if(argc == 0) {
    //     numCores = sysconf(_SC_NPROCESSORS_CONF);
    // }

    /**
     * @todo    handle input args later
     */

    int arr[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    size_t len = 20;

    // single_threaded(arr, len);
    multi_threaded_jhs(arr, len);
    multi_threaded_ewan17(arr, len);

    return 0;
}