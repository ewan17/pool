#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "pool.h"

#define THREAD_COUNT(tg) (tg->idleThrds.len + tg->activeThrds.len)
#define GROUP_COUNT(tp) (tp->groups.len)

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

static void internal_destroy_group(TGroup *tg);
static void internal_destroy_thread(TThread *tt);

static void *worker_thread_function(void *arg);
static void *manager_thread_function(void *arg);

static int q_init(struct Q **q, size_t capacity);
static void q_destroy(struct Q **q);
static void q_append(struct Q *q, Work *node);
static Work *q_fetch(struct Q *q);
static int q_length(struct Q *q);

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

    pthread_mutex_init(&(*tp)->mutexPool, NULL);
    pthread_cond_init(&(*tp)->condPool, NULL);
    
    return 0;
}

/**
 * @note    the limit of threads for all groups should not exceed the max
 * @todo    add a check to make sure the numthrds is within thrdMax - totaLThrds
 * @note    currently we have no limit on the number of threads and groups
 * @todo    will need to add limit checks later
*/
TGroup *add_group(TPool *tp, unsigned int min, unsigned int max, int flags) {
    TGroup *tg;

    if(min == 0) { 
        min = 1;
    }

    pthread_mutex_lock(&tp->mutexPool);
    unsigned int availableThrds = (tp->thrdMax - tp->totalThrds);
    if(availableThrds < max) {
        max = availableThrds;
    }

    if(tp == NULL || max == 0 || min > max) {
        pthread_mutex_unlock(&tp->mutexPool);
        return NULL;
    }
    pthread_mutex_unlock(&tp->mutexPool);

    tg = (TGroup *)malloc(sizeof(TGroup));
    if(tg == NULL) {
        return NULL;
    }

    tg->thrds = (pthread_t *)malloc(max*sizeof(pthread_t));
    if(tg->thrds == NULL) {
        goto error;
    }
    tg->numThrds = 0;
    tg->pool = tp;
    
    if(flags == FIXED || min == max) {
        tg->flags = FIXED;
        tg->thrdMin = tg->thrdMax = min;
    } else {    
        tg->flags = DYNAMIC;
        tg->thrdMin = min;
        tg->thrdMax = max;  
    }

    init_il(&tg->move);

    init_list(&tg->idleThrds);
    init_list(&tg->activeThrds);

    pthread_mutex_init(&tg->mutexGrp, NULL);

    // /**
    //  * @note    queue size can be changed later. Picked random size.
    // */
    if(q_init(&tg->q, 1024) != 0) {
        goto error;
    }

    for (size_t i = 0; i < min; i++) {
        /**
         * @note    all the threads should be created or none of them
         * @todo    error handling needs fixing
        */
        if(add_thread(tg) != 0) {
            goto error;
        }
    }

    pthread_mutex_lock(&tp->mutexPool);
    list_append(&tp->groups, &tg->move);
    pthread_mutex_unlock(&tp->mutexPool);

    return tg;

error:
    free(tg);
	return NULL;
}

/**
 * @note    you will only be able to terminate idle threads
 * @todo    add more parameters to support terminating a certain number of threads or maybe only idle threads
 * @note    for now hard code the CLOSE TERMINATE and SOFT_KILL
 */
void destroy_group(TGroup *tg) {
    if(tg == NULL) {
        return;
    }

    TPool *tp = tg->pool;

    internal_destroy_group(tg);

    pthread_mutex_lock(&tp->mutexPool);
    item_remove(&tg->move);
    tp->groups.len--;
    pthread_mutex_unlock(&tp->mutexPool);

    free(tg);
}

void init_work(Work **work) {
    if(work == NULL) {
        return;
    }

    *work = (Work *)malloc(sizeof(Work));
    if(*work == NULL) {
        assert(0);
    }
}

void add_work(Work *work, work_func func, void *arg) {
    work->wf = func;
    work->work_arg = arg;
    work->prev = NULL;
}

int do_work(TGroup *tg, Work *work) {
    if(work == NULL || tg == NULL) {
        return -1;
    }

    TThread *tt;

    pthread_mutex_lock(&tg->mutexGrp);
    if(tg->flags & CLOSE) {
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
    tt->state = RUNNING;
    pthread_cond_signal(&tt->condThrd);
    pthread_mutex_unlock(&tt->mutexThrd);

    return 0;
}

/**
 * @todo    needs fixing with all the other destory methods
*/
void destroy_pool(TPool *tp) {
    if(tp == NULL) {
        return;
    }
    
    pthread_mutex_lock(&tp->mutexPool);
    /**
     * @todo    close the manager thread
    */
    IL *curr;
    while((curr = list_pop(&tp->groups)) != NULL) {
        TGroup *tg = CONTAINER_OF(curr, TGroup, move);
        internal_destroy_group(tg);
        free(tg);
    }
    pthread_mutex_unlock(&tp->mutexPool);
    // may not need the condition
    pthread_cond_destroy(&tp->condPool);
    pthread_mutex_destroy(&tp->mutexPool);

    free(tp);
}

int add_thread(TGroup *tg) {
    if(tg == NULL) {
        return -1;
    }

    TThread *tt;
    TPool *tp;

    tp = tg->pool;

    tt = (TThread *)malloc(sizeof(TThread));
    if(tt == NULL) {
        return -1;
    }

    tt->state = RUNNING;
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

    tp->totalThrds++;

    return 0;

error:
    free(tt);
    return -1;
}

static void internal_destroy_group(TGroup *tg) {
    TPool *tp = tg->pool;

    pthread_t *threads;
    size_t numThrds = 0;

    pthread_mutex_lock(&tg->mutexGrp);
    tg->flags |= (CLOSE | SOFT_KILL);

    threads = tg->thrds;
    numThrds = tg->numThrds;

    // can only loop through idle threads since active threads will result in a deadlock
    IL *curr;
    while((curr = list_pop(&tg->idleThrds)) != NULL) {
        TThread *tt;
        tt = CONTAINER_OF(curr, TThread, move);

        list_append(&tg->activeThrds, curr);

        pthread_mutex_lock(&tt->mutexThrd);
        tt->state = RUNNING;
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
 * @note    the last thread destroys the group
*/
static void internal_destroy_thread(TThread *tt) {
    TGroup *tg = tt->tg;
    TPool *tp = tg->pool;

    pthread_mutex_lock(&tg->mutexGrp);
    item_remove(&tt->move);

    if(tt->state != IDLE) {
        tg->activeThrds.len--;
    } else {
        // all threads terminating should be in a running state
        assert(0);
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    pthread_cond_destroy(&tt->condThrd);
    pthread_mutex_destroy(&tt->mutexThrd);

    free(tt);

    tp->totalThrds--;
}

/**
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
        
        if(tg->flags & CLEAN) {
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
        if(tt->state == RUNNING) {
            // append thread to idle list
            item_remove(&tt->move);
            tg->activeThrds.len--;
            tt->state = IDLE;
            list_append(&tg->idleThrds, &tt->move);
        }
        pthread_mutex_unlock(&tg->mutexGrp);

        while(tt->state == IDLE) {            
            pthread_cond_wait(&tt->condThrd, &tt->mutexThrd);
        }
        pthread_mutex_unlock(&tt->mutexThrd);        
    }

    pthread_mutex_unlock(&tt->mutexThrd);
    internal_destroy_thread(tt);
}

static void *manager_thread_function(void *arg) {
//     struct timespec timeout;
//     clock_gettime(CLOCK_REALTIME, &timeout);
//     timeout.tv_sec += 5;

//     TPool *tp;

//     while(1) {
//         pthread_mutex_lock(&tp->mutexPool);
//         int rc;
//         rc = pthread_cond_timedwait(&tp->condPool, &tp->mutexPool, &timeout);
        
//         if(rc == 0 && ) { 
//             break;
//         }

//         pthread_mutex_unlock(&tp->mutexPool);
//     }

//     pthread_exit(NULL);
    return;
}

/*---- Queue ----*/
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

static int q_length(struct Q *q) {
    return q->length;
}
