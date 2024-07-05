#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include "pool.h"
#include "jhs/thpool.h"

#define BENCH_ITERATIONS 10000
#define STR_NUM(x) #x
#define STR(x) STR_NUM(x)
#define MESSAGE "Mean time for "STR(BENCH_ITERATIONS)" iterations"

void mean_calc(double *mean, double times[], size_t len) {
    double sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += times[i];
    }
    *mean = (sum/len);
}

void std_dev_calc(double *stdev, double times[], size_t len, double mean) {
    double result = 0;
    for (size_t i = 0; i < len; i++) {
        double diff = times[i] - mean;
        result += pow(diff, 2);
    }

    result = result / len;
    *stdev = sqrt(result);
}

void confidence_interval(double mean, double stdev, size_t size) {
    double upper = mean + (1.96*stdev/sqrt(size));
    double lower = mean - (1.96*stdev/sqrt(size));
    printf("95%% Confidence Interval (%.6f,%.6f)\n", lower, upper);
}

double elapsed_time(struct timespec start, struct timespec finish) {
    double timeDiff;
    timeDiff = (finish.tv_sec - start.tv_sec);
    timeDiff += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    
    return timeDiff;
}

void count_func(size_t input) {
    size_t maxSize = (size_t)-1;
    size_t result = 0;
    for (size_t i = 1; i <= input; i++) {
        if(i > (maxSize - result)) {
            break;
        }
        result += i;
    }
}

size_t factorial_func(size_t input) {
    size_t maxSize = (size_t)-1;
    size_t result = 1;
    for (size_t i = 1; i <= input; i++) {
        if(i > (maxSize/result)) {
            break;
        }
        result *= i;
    }

    return result;
}

/**
 * Calculates the factorial for a number
 */
void thread_function(void* arg) {
    int input = (int)(intptr_t)arg;

    // factorial_func(input);
    count_func(input);
}

void single_threaded(int arr[], size_t len) {
    double times[BENCH_ITERATIONS];
    double mean;
    double stdev;

    struct timespec start, finish;
    for (size_t i = 0; i < BENCH_ITERATIONS; i++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t j = 0; j < len; j++) {
            // make sure this function matches the thread function
            /**
             * @todo    make cleaner later
             */
            count_func(arr[j]);
        }
        clock_gettime(CLOCK_MONOTONIC, &finish);
        times[i] = elapsed_time(start, finish);
    }

    mean_calc(&mean, times, BENCH_ITERATIONS);
    std_dev_calc(&stdev, times, BENCH_ITERATIONS, mean);
    printf("Mean time for single thread: %.6f seconds\n", mean);
    confidence_interval(mean, stdev, BENCH_ITERATIONS);
    printf("\n");
}

void multi_threaded_ewan17(int arr[], size_t len) {
    double times[BENCH_ITERATIONS];
    double mean;
    double stdev;

    struct timespec start, finish;
    TPool *pool;
    init_pool(&pool, 30);

    size_t numGroups = 5;
    TGroup *tg[numGroups];
    for (size_t i = 0; i < numGroups; i++) {
        TGroup *grp;
        grp = add_group(pool, 3, 6, GROUP_DYNAMIC);
        assert(grp != NULL);
        tg[i] = grp;
    }

    for (size_t i = 0; i < BENCH_ITERATIONS; i++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t j = 0; j < len; j++) {
            Work *work;
            size_t index;

            init_work(&work);
            add_work(work, thread_function, (void *)(intptr_t)arr[j]);

            index = j % numGroups;
            do_work(tg[index], work);
        }
        wait_pool(pool);
        clock_gettime(CLOCK_MONOTONIC, &finish);
        times[i] = elapsed_time(start, finish);
    }

    destroy_pool(pool);

    mean_calc(&mean, times, BENCH_ITERATIONS);
    std_dev_calc(&stdev, times, BENCH_ITERATIONS, mean);
    printf("%s (ewan17): %.6f seconds\n", MESSAGE, mean);
    confidence_interval(mean, stdev, BENCH_ITERATIONS);
    printf("\n");
}

void multi_threaded_jhs(int arr[], size_t len) {
    double times[BENCH_ITERATIONS];
    double mean;
    double stdev;

    struct timespec start, finish;
    threadpool thpool = thpool_init(30);

    for (size_t i = 0; i < BENCH_ITERATIONS; i++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t j = 0; j < len; j++) {
            thpool_add_work(thpool, thread_function, (void *)(intptr_t)arr[j]);
        }
        thpool_wait(thpool);
        clock_gettime(CLOCK_MONOTONIC, &finish);
        times[i] = elapsed_time(start, finish);
    }
    thpool_destroy(thpool);

    mean_calc(&mean, times, BENCH_ITERATIONS);
    std_dev_calc(&stdev, times, BENCH_ITERATIONS, mean);
    printf("%s (jhs): %.6f seconds\n", MESSAGE, mean);
    confidence_interval(mean, stdev, BENCH_ITERATIONS);
    printf("\n");
}

int main(int argc, char *argv[]) {
    /**
     * @todo    better to read the possible scenarios from an input file
     * @todo    for now hard coded scenarios
     */
    // if(argc == 0) {
    //     numCores = sysconf(_SC_NPROCESSORS_CONF);
    // }
    printf("Running bench mark...\n");
    /**
     * @todo    handle input args later
     */
    // this will result in values larger than size_t, but we make sure the size_t variable will not overflow
    int arr[] = {1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000
                    ,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000
                        ,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000
                            ,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000};
    size_t len = 80;

    // single_threaded(arr, len);
    multi_threaded_jhs(arr, len);
    multi_threaded_ewan17(arr, len);

    return 0;
}