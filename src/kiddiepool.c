#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include "kiddiepool.h"

struct Node {
    work_func wf;
    void *work_arg;
    struct Node *prev;
};

struct Q {
    struct Node *head;
    struct Node *tail;
    size_t length;
    size_t capacity;
};

static int resize_pool(TPool *pool, unsigned int numGrps, bool additional);
static int resize_group(TGroup *tg, unsigned int numThrds, bool additional);
static bool within_limits(unsigned int amnt, unsigned int currSize, unsigned int min, unsigned int max, bool additional);
static unsigned int resize_factor(unsigned int proposedSize, unsigned int currSize, unsigned int lowerLimit, unsigned int upperLimit);

static void internal_maintenance_group(TGroup *tg, int flag);
static TGroup *internal_create_group(TPool *kp, unsigned int numthrds, unsigned int min, unsigned int max, int groupId);

static void internal_destroy_thread(TThread *tt, TGroup *tg);

static void *thread_function(void *pool);

static int q_init(struct Q **q, size_t capacity);
static void q_destroy(struct Q **q);
static void q_clear(struct Q *q);
static void q_append(struct Q *q, struct Node *node);
static struct Node *q_fetch(struct Q *q);
static int q_length(struct Q *q);

static void node_kill(struct Node *node);

int init_pool(TPool **pool, unsigned int maxThrds, unsigned int size) {
    if(pool == NULL) {
        return -1;
    }

    *pool = (TPool *)malloc(sizeof(TPool));
    if(*pool == NULL) {
        return -1;
    }

    TPool *tp = *pool;
    bool isDefault = false;

    tp->maxThrds = (maxThrds > 0 && maxThrds < MAX_THREADS) ? maxThrds : MAX_THREADS;
    tp->totalThrds = 0;
    tp->totalGrps = 0;
    tp->tGrp = NULL;
    pthread_mutex_init(&tp->mutexPool, NULL);

    if(resize_pool(tp, size, true) != 0) {
		free(tp);
        tp = NULL;
        return -1;
    }
    
    return 0;
}

/**
 * @note    currently we have no limit on the number of threads and groups
 * @todo    will need to add limit checks later
*/
TGroup *add_group(TPool *pool, int id, unsigned int numthrds, unsigned int min, unsigned int max) {
    TGroup *tg;

    pthread_mutex_lock(&pool->mutexPool);
    if(resize_pool(pool, 1, true) != 0) {
        pthread_mutex_unlock(&pool->mutexPool);
        return -1;
    }
    pthread_mutex_unlock(&pool->mutexPool);

    tg = internal_create_group(pool, numthrds, min, max, id);
    if(tg == NULL) {
        return -1;
    }

    pthread_mutex_lock(&pool->mutexPool);
    unsigned int index = pool->totalGrps;
    pool->totalGrps++;
    pool->totalThrds += numthrds;
    pool->tGrp[index] = tg;
    pthread_mutex_unlock(&pool->mutexPool);

    return tg;
}

/**
 * @todo    the internal maintenance function needs to be fixed for this to work
*/
void destroy_group(TGroup *tg) {
    if(tg == NULL) {
        return;
    }

    internal_maintenance_group(tg, SOFT_KILL);
}

int add_work(TGroup *tg, work_func wf, void *work_arg) {
    if(wf == NULL) {
        return -1;
    }

    TThread *tt;

    struct Node *task;
    task = (struct Node *)malloc(sizeof(struct Node));
    if(task == NULL) {
        return -1;
    }
    task->wf = wf;
    task->work_arg = work_arg;
    task->prev = NULL;

    pthread_mutex_lock(&tg->mutexGrp);
    /**
     * @todo    check the idle list for a thread
    */
    if(tt != NULL) {
        /**
         * @todo    move thread into the active threads
        */
    } else {
        // currently there are no idle threads
        q_append(tg->q, task);
        pthread_mutex_unlock(&tg->mutexGrp);
        return 0;
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    pthread_mutex_lock(&tt->mutexThrd);
    tt->task = tt;
    tt->state = RUNNING;
    pthread_cond_signal(&tt->condThrd);
    pthread_mutex_unlock(&tt->mutexThrd);

    return 0;
}

/**
 * @todo    needs fixing with all the other destory methods
*/
void destroy_pool(TPool *pool) {
    if(pool != NULL) {
        return;
    }
    
    pthread_mutex_lock(&(pool->mutexPool));
    
    pthread_mutex_destroy(&(pool->mutexPool));

    free(pool);
}

int add_thread(TGroup *tg) {
    TThread *tt;

    tt = (TThread *)malloc(sizeof(TThread));
    if(tt == NULL) {
        return -1;
    }

    tt->state = RUNNING;
    tt->tg = tg;
    tt->task = NULL;

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
    if(!within_limits(1, tg->tThrdSize, tg->thrdMin, tg->thrdLimit, true)) {
        goto error;
    }

    if(resize_group(tg, 1, true) != 0) {
        pthread_mutex_unlock(&tg->mutexGrp);
        goto error;
    };

    if(pthread_create(&tt->id, NULL, thread_function, tt) != 0) {
        pthread_mutex_unlock(&tg->mutexGrp);
        goto error;
    }

    tg->tThrd[tg->thrdCount] = tt;
    tg->thrdCount++;
    pthread_mutex_unlock(&tg->mutexGrp);

    return 0;

error:
    free(tt);
    return -1;
}

void destroy_thread(TThread *tt) {
    pthread_mutex_lock(&tt->mutexThrd);
    tt->state = KILL;
    pthread_cond_signal(&tt->condThrd);
    pthread_mutex_unlock(&tt->mutexThrd);
}

static void internal_destroy_thread(TThread *tt, TGroup *tg) {
    pthread_mutex_lock(&tg->mutexGrp);
    tg->thrdCount--;
    if(resize_group(tg, 1, false)) {
        /**
         * @todo    add error handling
         * @note    error reallocating
        */
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    TPool *tp;
    tp = tg->pool;
    pthread_mutex_lock(&tp->mutexPool);
    tp->totalThrds--;
    if(resize_pool(tp, 1, false)) {
        /**
         * @todo    add error handling
         * @note    error reallocating
        */
    }
    pthread_mutex_unlock(&tp->mutexPool);

    free(tt);
}

/**
 * @note    do not lock a thread that is in the active thread list
 * @note    only lock a thread that is within the idle thread list
*/
static void *thread_function(void *arg) {
    TGroup *tg;
    TThread *tt;
    tt = (TThread *) arg;
    tg = tt->tg;

    while(1) {
        struct Node *task;

        pthread_mutex_lock(&tt->mutexThrd);
        if(tt->task == NULL) {
            pthread_mutex_lock(&tg->mutexGrp);
            /**
             * @todo    can add a check here to see if we are at the min thrd limit and can change the thread flag
            */
            if(tg->flag == SOFT_KILL) {
                tt->state == SOFT_KILL;
            }

            task = q_fetch(tg->q);
            
            if(task == NULL) {
                tt->state = IDLE;
                /**
                 * @todo    add the thread to the idle list
                */
            } else {
                tt->task = task;
                pthread_mutex_unlock(&tg->mutexGrp);
                pthread_mutex_unlock(&tt->mutexThrd);
                goto execute_func;
            }
            pthread_mutex_unlock(&tg->mutexGrp);
        }
        
        while(tt->task == NULL && tt->state == IDLE) {            
            pthread_cond_wait(&tt->condThrd, &tt->mutexThrd);
        }
        
        if(tt->state == SOFT_KILL && task == NULL) {
            internal_destroy_thread(tt, tg);
            break;
        }
        if(tt->state == KILL) {            
            internal_destroy_thread(tt, tg);
            break;                
        }

        task = tt->task;
        pthread_mutex_unlock(&tt->mutexThrd);

execute_func:
        work_func func = task->wf;
        void *arg = task->work_arg;
        func(arg);
        node_kill(task);
        task = NULL;

        pthread_mutex_lock(&tt->mutexThrd);
        free(tt->task);
        tt->task = NULL;
        pthread_mutex_unlock(&tt->mutexThrd);
    }
    pthread_exit(NULL);
}

static TGroup *internal_create_group(TPool *tp, unsigned int numThrds, unsigned int min, unsigned int max, int groupId) {
    if(tp == NULL) {
        return -1;
    }

    TGroup *tg;
    tg = (TGroup *)malloc(sizeof(TGroup));
    if(tg == NULL) {
        return -1;
    }

    tg->id = groupId;
    tg->pool = tp;
    tg->thrdMin = min;
    tg->thrdLimit = max;

    tg->thrdCount = 0;
    tg->tThrdSize = 0;
    tg->tThrd = NULL;

    if(pthread_mutex_init(&tg->mutexGrp, NULL) != 0) {
        goto error;
    }

    if(resize_group(tg, numThrds, true) != 0) {
        goto error;
    }

    /**
     * @note    queue size can be changed later. Picked random size.
    */
    struct Q *q;
    if(q_init(&q, 1024) != 0) {
        goto error;
    }

    for (size_t i = 0; i < numThrds; i++)
    {
        /**
         * @note    all the threads should be created or none of them
         * @todo    error handling needs fixing
        */
        if(add_thread(tg) != 0) {
            goto error;
        }
    }

    return 0;

error:
    free(tg);
	return -1;
}

/**
 * @todo    this will accpet flags that can either clean the group, kill the group, softkill the group, etc.
*/
static void internal_maintenance_group(TGroup *tg, int flag) {
    
}

/**
 * @note    this should be called after within limits has been called
*/
static int resize_pool(TPool *tp, unsigned int numGrps, bool additional) {
    unsigned int grpSize = tp->totalGrps;
    if(additional) {
        grpSize += numGrps;
        if(grpSize <= tp->tGrpSize) {
            return 0;
        }
    } else {
        grpSize -= numGrps;
    }

    tp->tGrp = (TGroup **) realloc(tp->tGrp, grpSize * sizeof(TGroup *));
    if(tp->tGrp == NULL) {
        return -1;
    }
    
    tp->tGrpSize = grpSize;

    return 0;
}

/**
 * @note    this should be called after within limits has been called
*/
static int resize_group(TGroup *tg, unsigned int numThrds, bool additional) {
    unsigned int newSize;
    unsigned int thrdSize = tg->thrdCount;

    if(additional) {
        thrdSize += numThrds;
        if(thrdSize <= tg->tThrdSize) {
            return 0;
        }
    } else {
        thrdSize -= numThrds;
    }

    newSize = resize_factor(thrdSize, tg->tThrdSize, tg->thrdMin, tg->thrdLimit);
    if(newSize == tg->tThrdSize) {
        return 0;
    }

    tg->tThrd = (TThread **) realloc(tg->tThrd, newSize * sizeof(TThread *));
    if(tg->tThrd == NULL) {
        return -1;
    }

    tg->tThrdSize = newSize;
}

static bool within_limits(unsigned int amnt, unsigned int currSize, unsigned int min, unsigned int max, bool additional) {
    if(additional) {
        if(currSize + amnt > max) {
            return false;
        }
    } else {
        if(currSize - amnt < min) {
            return false;
        }
    }
    return true;
}

static unsigned int resize_factor(unsigned int proposedSize, unsigned int currSize, unsigned int lowerLimit, unsigned int upperLimit) {
    if(proposedSize == currSize) {
        return currSize;
    }
    
    unsigned int baseLimit = (upperLimit + lowerLimit) / 2;
    if(currSize == 0 || proposedSize <= baseLimit || proposedSize > upperLimit) {
        return baseLimit;
    }

    unsigned int avgSize = currSize;
    unsigned int prevSize = baseLimit;
    while(avgSize != prevSize) {
        if(proposedSize > avgSize) {
            avgSize = (currSize + upperLimit) / 2;
        } else {
            avgSize = (currSize + baseLimit) / 2;
        }
    }

    return avgSize;
}



























/*---- Queue ----*/
static int q_init(struct Q **q, size_t capacity) {
    if(q == NULL) {
        return -1;
    }

    *q = (struct Q *)malloc(sizeof(struct Q));
    if(*q == NULL) {
        perror("malloc error at queue");
        return -1;
    }

    struct Q *tmp_q = *q;
    tmp_q->head = NULL;
    tmp_q->tail = NULL;
    tmp_q->length = 0;
    tmp_q->capacity = capacity;

    return 0;
};

static void q_destroy(struct Q **q) {
    if(q != NULL) {
        q_clear(*q);
        free(*q);
        *q = NULL;
    }
};

/**
 * @todo    needs to signal to the threads to execute the items in the q.
*/
static void q_clear(struct Q *q) {

};

static void q_append(struct Q *q, struct Node *node) {
    if(q != NULL && node != NULL) {
        if(q->length < (q->capacity - 1)) {
            node->prev = NULL;
            if(q->head == NULL) {
                q->head = q->tail = node;
            } else {
                q->tail->prev = node;
                q->tail = node;
            }
            (q->length)++;
        }
    }
};

static struct Node *q_fetch(struct Q *q) {
    if(q_length(q) == 0) {
        return NULL;
    }
    
    if(q->head == q->tail) {
        q->tail = NULL;
    }

    struct Node *tmp;
    tmp = q->head;
    q->head = q->head->prev;
    tmp->prev = NULL;
    (q->length)--;

    return tmp;
};

static int q_length(struct Q *q) {
    return q->length;
}

/*---- Node ----*/
/**
 * @note    call the work function and then you can free the node
 * @note    it is the work functions job to free and set the work arg to NULL
*/
static void node_kill(struct Node *node) {
    if(node != NULL && node->prev == NULL) {
        free(node);
    }
}
