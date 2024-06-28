#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

/**
 * @note    will remove this later
 */
#include <assert.h>

#include "pool.h"
#include "il.h"

#define Q_SIZE_MULT 2

#define GROUP_CLOSE 0x04
#define GROUP_CLEAN 0x08
#define SOFT_KILL 0x10
#define HARD_KILL 0x20

struct Work {
    work_func wf;
    void *work_arg;
    struct Work *prev;
};

struct Q {
    Work *head;
    Work *tail;
    size_t length;
    size_t capacity;
};

typedef enum {
    well, 
    moderate, 
    poor
} Health;

typedef enum {
    running,
    idle
} State;

struct TPool {
    pthread_mutex_t mutexPool;
    pthread_cond_t condPool;

    // total threads will represent all threads that are active or idle
    unsigned int totalThrds;
    // max number of threads that the pool can contain
    unsigned int thrdMax;

    LL groups;

    // manager thread for the pool to be dynamic
    pthread_t manager;
};

struct TGroup {
    IL move;
    
    pthread_mutex_t mutexGrp;

    int flags;

    // work queue
    struct Q *q;

    LL idleThrds;
    LL activeThrds;

    // min and max thread limits
    unsigned int thrdMax;
    unsigned int thrdMin;

    TPool *pool;

    // number of threads currently created
    unsigned int numThrds;
    pthread_t *thrds;
};

typedef struct TThread {
    IL move;

    pthread_t id;

    pthread_mutex_t mutexThrd;
    pthread_cond_t condThrd;

    // current state of a thread
    State state;

    Work *currTask;

    TGroup *tg;
} TThread;

/*  --Internal Functions--  */
static int internal_add_thread(TGroup *tg);
static Health internal_health_check(TGroup *tg);

static void internal_destroy_group(TGroup *tg);
static void internal_destroy_thread(TThread *tt);

static void *worker_thread_function(void *arg);
static void *manager_thread_function(void *arg);

static int q_init(struct Q **q, size_t capacity);
static void q_destroy(struct Q **q);
static void q_append(struct Q *q, Work *node);
static Work *q_fetch(struct Q *q);
static size_t q_length(struct Q *q);

/**
 * 
 */
int init_pool(TPool **tp, unsigned int maxThrds) {    
    if(tp == NULL) {
        return -1;
    }

    *tp = (TPool *)malloc(sizeof(TPool));
    if(*tp == NULL) {
        return -1;
    }

    if(maxThrds == 0) {
        return -1;
    }
    (*tp)->thrdMax = maxThrds;
    (*tp)->totalThrds = 0;

    init_list(&(*tp)->groups);

    /**
     * @todo    uncomment this when the manager thread is ready
     */
    // rc = pthread_create(&(*tp)->manager, NULL, manager_thread_function, NULL);
    // assert(rc == 0);

    pthread_mutex_init(&(*tp)->mutexPool, NULL);
    pthread_cond_init(&(*tp)->condPool, NULL);
    
    return 0;
}

void destroy_pool(TPool *tp) {
    if(tp == NULL) {
        return;
    }
    
    pthread_mutex_lock(&tp->mutexPool);
    IL *curr;
    while((curr = list_pop(&tp->groups)) != NULL) {
        TGroup *tg = CONTAINER_OF(curr, TGroup, move);
        internal_destroy_group(tg);
        free(tg);
    }
    pthread_mutex_unlock(&tp->mutexPool);

    /**
     * @todo    uncomment this when the manager thread is ready
     */
    // if(pthread_join(tp->manager, NULL) != 0) {
    //     assert(0);
    // }

    pthread_cond_destroy(&tp->condPool);
    pthread_mutex_destroy(&tp->mutexPool);

    free(tp);
}

/**
 * 
*/
TGroup *add_group(TPool *tp, unsigned int min, unsigned int max, int flags) {
    TGroup *tg;

    if(tp == NULL) {
        return NULL;
    }

    if(min == 0) { 
        min = 1;
    }

    pthread_mutex_lock(&tp->mutexPool);
    unsigned int availableThrds = (tp->thrdMax - tp->totalThrds);
    if(availableThrds < max) {
        max = availableThrds;
    }

    if(max == 0 || min > max) {
        pthread_mutex_unlock(&tp->mutexPool);
        return NULL;
    }
    pthread_mutex_unlock(&tp->mutexPool);

    tg = (TGroup *)malloc(sizeof(TGroup));
    if(tg == NULL) {
        return NULL;
    }

    tg->thrds = (pthread_t *)malloc(max * sizeof(pthread_t));
    if(tg->thrds == NULL) {
        goto error;
    }
    tg->numThrds = 0;
    tg->pool = tp;
    
    if(flags == GROUP_FIXED || min == max) {
        tg->flags = GROUP_FIXED;
        tg->thrdMin = tg->thrdMax = min;
    } else {    
        tg->flags = GROUP_DYNAMIC;
        tg->thrdMin = min;
        tg->thrdMax = max;  
    }

    init_il(&tg->move);

    init_list(&tg->idleThrds);
    init_list(&tg->activeThrds);

    pthread_mutex_init(&tg->mutexGrp, NULL);

    /**
     * @note    queue size can be changed later
     * @note    picked random size
    */
    size_t qSize = max * Q_SIZE_MULT;
    if(q_init(&tg->q, qSize) != 0) {
        goto error;
    }

    int rc;
    for (size_t i = 0; i < min; i++) {
        /**
         * @note    all the threads should be created or none of them
         * @todo    error handling needs fixing
        */
        rc = internal_add_thread(tg);
        assert(rc == 0);
    }

    pthread_mutex_lock(&tp->mutexPool);
    list_append(&tp->groups, &tg->move);
    tp->totalThrds += max;
    pthread_mutex_unlock(&tp->mutexPool);

    return tg;

error:
    free(tg);
	return NULL;
}

/**
 * 
 */
void destroy_group(TGroup *tg) {
    if(tg == NULL) {
        return;
    }

    TPool *tp = tg->pool;

    internal_destroy_group(tg);

    pthread_mutex_lock(&tp->mutexPool);
    item_remove(&tg->move);
    tp->totalThrds -= tg->thrdMax;
    tp->groups.len--;
    pthread_mutex_unlock(&tp->mutexPool);

    free(tg);
}

/**
 * Initialize a work struct before adding work to it.
 */
void init_work(Work **work) {
    if(work == NULL) {
        return;
    }

    *work = (Work *)malloc(sizeof(Work));
    if(*work == NULL) {
        assert(0);
    }
}

/**
 * Accepts a void function and a void arg.
 */
void add_work(Work *work, work_func func, void *arg) {
    work->wf = func;
    work->work_arg = arg;
    work->prev = NULL;
}

/**
 * Assign the work to a specific group.
 * Do the work.
 */
int do_work(TGroup *tg, Work *work) {
    if(work == NULL || tg == NULL) {
        return -1;
    }

    TThread *tt;

    pthread_mutex_lock(&tg->mutexGrp);
    if(tg->flags & GROUP_CLOSE) {
        pthread_mutex_unlock(&tg->mutexGrp);
        return -1;
    }

    // remove thread from idle list
    IL *il = list_pop(&tg->idleThrds);
    if(il == NULL) {
        // currently there are no idle threads
        // add work and return
        q_append(tg->q, work);
        pthread_mutex_unlock(&tg->mutexGrp);
        return 0;
    }

    tt = CONTAINER_OF(il, TThread, move);
    // add thread to active list
    list_append(&tg->activeThrds, il);
    pthread_mutex_unlock(&tg->mutexGrp);

    pthread_mutex_lock(&tt->mutexThrd);
    tt->currTask = work;
    tt->state = running;
    pthread_cond_signal(&tt->condThrd);
    pthread_mutex_unlock(&tt->mutexThrd);

    return 0;
}

/*  --Internal Functions--  */

/**
 * Adds a thread to a group.
 * Checks to make sure that the number of threads has not been exceeded before adding
 */
static int internal_add_thread(TGroup *tg) {
    if(tg == NULL) {
        return -1;
    }

    TThread *tt;

    tt = (TThread *)malloc(sizeof(TThread));
    if(tt == NULL) {
        return -1;
    }

    tt->state = running;
    tt->tg = tg;
    tt->currTask = NULL;

    init_il(&tt->move);
    if(pthread_mutex_init(&tt->mutexThrd, NULL)) {
        goto error;
    }
    if(pthread_cond_init(&tt->condThrd, NULL)) {
        goto error;
    }

    /**
     * @todo    add limit error on the number of threads that can be added to the group
    */
    pthread_mutex_lock(&tg->mutexGrp);
    if(tg->numThrds == tg->thrdMax) {
        pthread_mutex_unlock(&tg->mutexGrp);
        goto error;
    }

    list_append(&tg->activeThrds, &tt->move);

    if(pthread_create(&tt->id, NULL, worker_thread_function, tt) != 0) {
        pthread_mutex_unlock(&tg->mutexGrp);
        goto error;
    }
    tg->thrds[tg->numThrds] = tt->id;
    tg->numThrds++;
    pthread_mutex_unlock(&tg->mutexGrp);

    return 0;

error:
    free(tt);
    return -1;
}

/**
 * This function assumes that the group is already locked.
 * For now it is only used in the manager thread function to check the health of a group.
 * 
 * @todo    this function is not complete yet
 * @todo    add the functionality to determine the groups health relative to work load
 */
static Health internal_health_check(TGroup *tg) {
    size_t amntWork; 
    amntWork = q_length(tg->q);
    return well;
}

/**
 * 
 * This functionality locks the group then the thread
 * This will result in a deadlock if we try to lock a thread that is in the active list
 * Because of this, we only loop through the idle list since we know the idle list will never lock itself
 * 
 * @note    this will not free the group
 */
static void internal_destroy_group(TGroup *tg) {
    TPool *tp = tg->pool;

    pthread_t *threads;
    size_t numThrds = 0;

    pthread_mutex_lock(&tg->mutexGrp);
    tg->flags |= (GROUP_CLOSE | SOFT_KILL);

    threads = tg->thrds;
    numThrds = tg->numThrds;

    // can only loop through idle threads since active threads will result in a deadlock
    IL *curr;
    while((curr = list_pop(&tg->idleThrds)) != NULL) {
        TThread *tt;
        tt = CONTAINER_OF(curr, TThread, move);

        list_append(&tg->activeThrds, curr);

        pthread_mutex_lock(&tt->mutexThrd);
        tt->state = running;
        pthread_cond_signal(&tt->condThrd);
        pthread_mutex_unlock(&tt->mutexThrd);
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    for (size_t i = 0; i < numThrds; i++) {
        if(pthread_join(threads[i], NULL) != 0) {
            assert(0);
        }
    }

    pthread_mutex_destroy(&tg->mutexGrp);
    q_destroy(&tg->q);
    free(tg->thrds);
}

/**
 * Called whenever a thread in the worker function breaks from the while loop.
 * Remove the thread from the group list.
 * Update the number of total thrds that the pool has.
*/
static void internal_destroy_thread(TThread *tt) {
    TGroup *tg = tt->tg;
    TPool *tp = tg->pool;

    pthread_mutex_lock(&tg->mutexGrp);
    item_remove(&tt->move);

    if(tt->state != idle) {
        tg->activeThrds.len--;
    } else {
        // all threads terminating should be in a running state
        assert(0);
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    pthread_cond_destroy(&tt->condThrd);
    pthread_mutex_destroy(&tt->mutexThrd);

    free(tt);

    pthread_mutex_lock(&tp->mutexPool);
    tp->totalThrds--;
    pthread_mutex_unlock(&tp->mutexPool);
}

/**
 * Executes the tasks that are added to a groups queue.
 * 
 * @note    do not lock a thread that is in the active thread list
 * @note    only lock a thread that is within the idle thread list
*/
static void *worker_thread_function(void *arg) {
    TGroup *tg;
    TThread *tt;
    tt = (TThread *) arg;
    tg = tt->tg;

    while(1) {
        Work *task;

        pthread_mutex_lock(&tt->mutexThrd);
top:
        task = tt->currTask;
        if(task != NULL) {
            pthread_mutex_unlock(&tt->mutexThrd);
            // begin executing the task
            work_func func = task->wf;
            void *arg = task->work_arg;
            func(arg);

            // we do not unlock this since after we free the task we grab more tasks
            pthread_mutex_lock(&tt->mutexThrd);
            free(tt->currTask);
            tt->currTask = NULL;
        }

        // the group lock is within the thread lock
        // this will deadlock only if a someone tries to lock any thread in the active thread list
        // the only threads that can be locked are threads that are in the idle list
        pthread_mutex_lock(&tg->mutexGrp);
        if(tg->flags & HARD_KILL) {
            pthread_mutex_unlock(&tg->mutexGrp);
            break;
        }
        
        if(tg->flags & GROUP_CLEAN) {
            assert(0);
            /**
             * @todo    add clean later
             */
            // tt->state = (THREAD_COUNT(tg) == tg->thrdMin) ? RUNNING : SOFT_KILL;
        }

        // grab a new task
        tt->currTask = q_fetch(tg->q);
        if(tt->currTask != NULL) {
            pthread_mutex_unlock(&tg->mutexGrp);
            goto top;
        }

        if(tg->flags & SOFT_KILL) {
            pthread_mutex_unlock(&tg->mutexGrp);
            break;
        }

        // this position is reached when the task is null
        if(tt->state == running) {
            // append thread to idle list
            item_remove(&tt->move);
            tg->activeThrds.len--;
            tt->state = idle;
            list_append(&tg->idleThrds, &tt->move);
        }
        pthread_mutex_unlock(&tg->mutexGrp);

        while(tt->state == idle) {            
            pthread_cond_wait(&tt->condThrd, &tt->mutexThrd);
        }
        pthread_mutex_unlock(&tt->mutexThrd);        
    }

    pthread_mutex_unlock(&tt->mutexThrd);
    internal_destroy_thread(tt);
    return;
}

/**
 * Acts as a manager for the entire pool. Will check groups and make sure they are healthy.
 * Depending ont he health status, the manager may need to create or delete threads within a group.
 * The arg passed in currently is NULL.
 * 
 * @note    maybe later the arg can be a manager struct
 * @note    struct could determine when threads should be created or deleted based on some sort of policy
 * 
 * @todo    this function is not complete yet
 */
static void *manager_thread_function(void *arg) {
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    TPool *tp;

    pthread_mutex_lock(&tp->mutexPool);
    while(1) {
        int rc;
        rc = pthread_cond_timedwait(&tp->condPool, &tp->mutexPool, &timeout);
        
        if(rc == -1 && errno != ETIMEDOUT) {
            assert(0);
        }

        if(empty(&tp->groups)) {
            continue;
        }

        IL *curr;
        TGroup *tg;
        for_each(&tp->groups.head, curr) {
            tg = CONTAINER_OF(curr, TGroup, move);

            pthread_mutex_lock(&tg->mutexGrp);
            rc = internal_health_check(tg);
            switch (rc) {
                case well:
                    break;
                
                // 
                case moderate:
                    /* code */
                    break;

                // create as many threads as we can until we reach thrdMax for the group
                case poor:
                    /* code */
                    break;
            }
            // creating a thread is expensive
            // threads created will need to stay alive for awhile before being destroyed
            pthread_mutex_unlock(&tg->mutexGrp);
        }
    }
    pthread_mutex_unlock(&tp->mutexPool);

    pthread_exit(NULL);
    return;
}

/*  --Queue--   */
/**
 * @todo    may need to add a resize function for the queue
 */
static int q_init(struct Q **q, size_t capacity) {
    if(q == NULL) {
        return -1;
    }

    *q = (struct Q *)malloc(sizeof(struct Q));
    if(*q == NULL) {
        return -1;
    }

    struct Q *tmp_q = *q;
    tmp_q->head = NULL;
    tmp_q->tail = NULL;
    tmp_q->length = 0;
    tmp_q->capacity = capacity;

    return 0;
}

static void q_destroy(struct Q **q) {
    if(q != NULL && *q != NULL) {
        free(*q);
        *q = NULL;
    }
}

static void q_append(struct Q *q, Work *work) {
    if(q != NULL && work != NULL) {
        if(q->length < (q->capacity - 1)) {
            work->prev = NULL;
            if(q->head == NULL) {
                q->head = q->tail = work;
            } else {
                q->tail->prev = work;
                q->tail = work;
            }
            (q->length)++;
        }
    }
}

static Work *q_fetch(struct Q *q) {
    if(q_length(q) == 0) {
        return NULL;
    }
    
    if(q->head == q->tail) {
        q->tail = NULL;
    }

    Work *tmp;
    tmp = q->head;
    q->head = q->head->prev;
    tmp->prev = NULL;
    (q->length)--;

    return tmp;
}

static size_t q_length(struct Q *q) {
    return q->length;
}