#ifndef KIDDIEPOOL_H
#define KIDDIEPOOL_H

typedef void (*work_func)(void *work_arg);

#define MAX_THREADS 100

#define DEFAULT_NUM_GROUPS 16
#define DEFAULT_THREADS_PER_GROUP 2 

#define RUNNING 0x01
#define IDLE 0x02
#define SOFT_KILL 0x03
#define KILL 0x03

typedef struct TThread {
    pthread_t id;

    pthread_mutex_t mutexThrd;
    pthread_cond_t condThrd;

    volatile int state;

    struct Node *task;

    TGroup *tg;
} TThread;

typedef struct TGroup {
    int id;
    
    pthread_mutex_t mutexGrp;

    volatile int flag;

    struct Q *q;

    TThread **tThrd;
    volatile unsigned int thrdCount;
    volatile unsigned int tThrdSize;

    unsigned int thrdLimit;
    unsigned int thrdMin;

    TPool *pool;
} TGroup;

typedef struct TPool {
    pthread_mutex_t mutexPool;

    volatile unsigned int totalThrds;
    volatile unsigned int totalGrps;

    unsigned int maxThrds;

    TGroup **tGrp;
    volatile unsigned int tGrpSize;

    TThread *manager;
} TPool;
 
int init_pool(TPool **pool, unsigned int maxThrds, unsigned int size);
void destroy_pool(TPool *pool);

TGroup *add_group(TPool *pool, int id, unsigned int numThrds, unsigned int min, unsigned int max);
int add_work(TGroup *tg, work_func wf, void *work_arg);

/**
 * @note    this will destroy all threads within a group
*/
void destroy_group(TGroup *tg);

int add_thread(TGroup *tg);
void destroy_thread(TThread *tt);



#endif //KIDDIEPOOL_H