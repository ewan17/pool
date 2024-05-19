#ifndef POOL_H
#define POOL_H

#include <pthread.h>
#include "il.h"

#if __STDC_VERSION__ < 201112L || __STDC_NO_ATOMICS__ == 1
/**
 * @todo    add some sort of implementation here if atomics is not supported
*/
#else
#include <stdatomic.h>
#endif
 
typedef void (*work_func)(void *work_arg);

#define MAX_THREADS 100

#define DYNAMIC 0x01
#define FIXED 0x02
#define CLOSED 0x08
#define CLEAN 0x10
#define TERMINATE 0x20

struct Node Node;

typedef enum {
    RUNNING = 0x01,
    IDLE = 0x02,
    SOFT_KILL = 0x04,
    HARD_KILL = 0x08
} State;

typedef struct TThread {
    struct IL move;

    pthread_t id;

    pthread_mutex_t mutexThrd;
    pthread_cond_t condThrd;

    volatile State state;

    struct Node *task;

    TGroup *tg;
} TThread;

typedef struct TGroup {
    struct IL move;

    int id;
    
    pthread_mutex_t mutexGrp;

    volatile int flags;

    struct Q *q;

    LL idleThrds;
    LL activeThrds;

    unsigned int thrdMax;
    unsigned int thrdMin;

    TPool *pool;
} TGroup;

typedef struct TPool {
    pthread_mutex_t mutexPool;
    pthread_cond_t condPool;

    atomic_int totalThrds;
    unsigned int thrdMax;
    unsigned int groupMax;

    LL groups;

    pthread_t *manager;
} TPool;
 
int init_pool(TPool **tp, unsigned int maxThrds, unsigned int size);
void destroy_pool(TPool *tp);

TGroup *add_group(TPool *tp, int id, unsigned int numThrds, unsigned int min, unsigned int max);
int add_work(TGroup *tg, work_func wf, void *work_arg);

/**
 * @note    this will destroy all threads within a group
*/
void destroy_group(TGroup *tg);

int add_thread(TGroup *tg);
void destroy_thread(TThread *tt);    

#endif //POOL_H