#ifndef POOL_H
#define POOL_H

#include <pthread.h>

#include "il.h"

#if __STDC_VERSION__ < 201112L || __STDC_NO_ATOMICS__ == 1
/**
 * @todo    add some sort of implementation here if atomics are not supported
*/
#else
#include <stdatomic.h>
#endif

#define DYNAMIC 0x01
#define FIXED 0x02
#define CLOSE 0x04
#define CLEAN 0x08
#define SOFT_KILL 0x10
#define HARD_KILL 0x20

typedef void (*work_func)(void *work_arg);

typedef enum {
    RUNNING = 0x01,
    IDLE = 0x02,
} State;

typedef struct Work Work;

typedef struct TPool {
    pthread_mutex_t mutexPool;
    pthread_cond_t condPool;

    atomic_int totalThrds;
    unsigned int thrdMax;

    LL groups;

    pthread_t *manager;
} TPool;

typedef struct TGroup {
    IL move;
    
    pthread_mutex_t mutexGrp;

    volatile int flags;

    struct Q *q;

    LL idleThrds;
    LL activeThrds;

    unsigned int thrdMax;
    unsigned int thrdMin;

    TPool *pool;

    size_t numThrds;
    pthread_t *thrds;
} TGroup;

typedef struct TThread {
    IL move;

    pthread_t id;

    pthread_mutex_t mutexThrd;
    pthread_cond_t condThrd;

    volatile State state;

    Work *currTask;

    TGroup *tg;
} TThread;

int init_pool(TPool **tp, unsigned int maxThrds);
void destroy_pool(TPool *tp);

TGroup *add_group(TPool *tp, unsigned int min, unsigned int max, int flags);
void destroy_group(TGroup *tg);

int add_thread(TGroup *tg);

void init_work(Work **work);
void add_work(Work *work, work_func func, void *arg);
int do_work(TGroup *tg, Work *work);

#endif //POOL_H