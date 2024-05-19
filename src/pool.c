#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#include "pool.h"

#define THREAD_COUNT(tg) (tg->idleThrds.len + tg->activeThrds.len)
#define GROUP_COUNT(tp) (tp->groups.len)

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

static bool room_for_more_threads(TGroup *tg);
static bool room_for_more_groups(TPool *tp);

static TGroup *internal_create_group(TPool *kp, unsigned int numthrds, unsigned int min, unsigned int max, int groupId);

static void internal_destroy_thread(TThread *tt);
static void internal_destroy_group(TGroup *tg);

static void *thread_function(void *pool);

static int q_init(struct Q **q, size_t capacity);
static void q_destroy(struct Q **q);
static void q_clear(struct Q *q);
static void q_append(struct Q *q, struct Node *node);
static struct Node *q_fetch(struct Q *q);
static int q_length(struct Q *q);

static void node_kill(struct Node *node);

int init_pool(TPool **tp, unsigned int maxThrds, unsigned int size) {
    if(tp == NULL) {
        return -1;
    }

    *tp = (TPool *)malloc(sizeof(TPool));
    if(*tp == NULL) {
        return -1;
    }

    (*tp)->thrdMax = (maxThrds > 0 && maxThrds < MAX_THREADS) ? maxThrds : MAX_THREADS;
    (*tp)->totalThrds = 0;

    init_list(&(*tp)->groups);

    pthread_mutex_init(&(*tp)->mutexPool, NULL);
    
    return 0;
}

/**
 * @note    the limit of threads for all groups should not exceed the max
 * @todo    add a check to make sure the numthrds is within thrdMax - totaLThrds
 * @note    currently we have no limit on the number of threads and groups
 * @todo    will need to add limit checks later
*/
TGroup *add_group(TPool *tp, int id, unsigned int numthrds, unsigned int min, unsigned int max) {
    TGroup *tg;

    pthread_mutex_lock(&tp->mutexPool);
    if(!room_for_more_groups(tp)) {
        pthread_mutex_unlock(&tp->mutexPool);
        return -1;
    }
    pthread_mutex_unlock(&tp->mutexPool);

    tg = internal_create_group(tp, numthrds, min, max, id);
    if(tg == NULL) {
        return -1;
    }

    pthread_mutex_lock(&tp->mutexPool);
    list_append(&tp->groups, &tg->move);
    pthread_mutex_unlock(&tp->mutexPool);

    return tg;
}

/**
 * @todo    the internal maintenance function needs to be fixed for this to work
*/
void destroy_group(TGroup *tg) {
    if(tg == NULL) {
        return;
    }

    TPool *tp;
    tp = tg->pool;

    pthread_mutex_lock(&tg->mutexGrp);
    tg->flags |= (CLOSED | TERMINATE);
    pthread_mutex_unlock(&tg->mutexGrp);

    pthread_mutex_lock(&tp->mutexPool);
    pthread_cond_signal(&tp->condPool);
    pthread_mutex_unlock(&tp->mutexPool);

}

int add_work(TGroup *tg, work_func wf, void *work_arg) {
    if(wf == NULL) {
        return -1;
    }

    TThread *tt;
    struct Node *task;

    pthread_mutex_lock(&tg->mutexGrp);
    if(tg->flags & CLOSED) {
        pthread_mutex_unlock(&tg->mutexGrp);
        return -1;
    }

    task = (struct Node *)malloc(sizeof(struct Node));
    if(task == NULL) {
        return -1;
    }
    task->wf = wf;
    task->work_arg = work_arg;
    task->prev = NULL;

    if(is_empty(&tg->idleThrds)) {
        // currently there are no idle threads
        q_append(tg->q, task);
        pthread_mutex_unlock(&tg->mutexGrp);
        return 0;
    } else {
        struct IL *il;
        // remove thread from idle list
        il = list_pop(&tg->idleThrds);

        tt = CONTAINER_OF(il, TThread, move);
        // add thread to active list
        list_append(&tg->activeThrds, il);
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    pthread_mutex_lock(&tt->mutexThrd);
    tt->task = task;
    tt->state = RUNNING;
    pthread_cond_signal(&tt->condThrd);
    pthread_mutex_unlock(&tt->mutexThrd);

    return 0;
}

/**
 * @todo    needs fixing with all the other destory methods
*/
void destroy_pool(TPool *tp) {
    if(tp != NULL) {
        return;
    }
    
    pthread_mutex_lock(&tp->mutexPool);
    /**
     * @todo    close the manager thread
    */
    struct IL *curr;
    for_each(&tp->groups.head, curr) {
        TGroup *tg;
        tg = CONTAINER_OF(curr, TGroup, move);
        destroy_group(tg);
    }

    /**
     * @note    this seems sus
     * @todo    maybe use the signal instead
    */
    while(tp->totalThrds != 0) {
        pthread_cond_wait(&tp->condPool, &tp->mutexPool);
    }

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
    tt->task = NULL;

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
    if(!room_for_more_threads(tg)) {
        pthread_mutex_unlock(&tg->mutexGrp);
        goto error;
    }

    if(pthread_create(&tt->id, NULL, thread_function, tt) != 0) {
        pthread_mutex_unlock(&tg->mutexGrp);
        goto error;
    }
    pthread_mutex_unlock(&tg->mutexGrp);

    tp->totalThrds++;

    return 0;

error:
    free(tt);
    return -1;
}

void destroy_thread(TThread *tt) {
    pthread_mutex_lock(&tt->mutexThrd);
    tt->state = HARD_KILL;
    pthread_cond_signal(&tt->condThrd);
    pthread_mutex_unlock(&tt->mutexThrd);
}

/**
 * @note    the thread should also be locked before calling this function
 * @note    function should only be called on a thread that is in the idle thread list
*/
static void internal_destroy_thread(TThread *tt) {
    TGroup *tg;
    tg = tt->tg;

    item_remove(&tt->move);
    pthread_mutex_destroy(&tt->mutexThrd);
    pthread_cond_destroy(&tt->condThrd);

    free(tt);

    tg->pool->totalThrds--;

    pthread_mutex_lock(&tg->mutexGrp);
    tg->idleThrds.len--;

    if(THREAD_COUNT(tg) == 0) {
        // no more threads in the group, destroy it
        internal_destroy_group(tg);
    }
    pthread_mutex_unlock(&tg->mutexGrp);
}

/**
 * @note    the group should already be locked before calling this function
*/
static void internal_destroy_group(TGroup *tg) {
    TPool *tp;
    tp = tg->pool;

    item_remove(&tg->move);
    pthread_mutex_destroy(&tg->mutexGrp);
    q_destroy(&tg->q);

    pthread_mutex_lock(&tp->mutexPool);
    tp->groups.len--;
    pthread_mutex_unlock(&tp->mutexPool);
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

            if(tg->flags & TERMINATE) {
                pthread_mutex_unlock(&tg->mutexGrp);
                break;
            }
            
            if(tg->flags & CLEAN) {
                tt->state = (THREAD_COUNT(tg) == tg->thrdMin) ? RUNNING : SOFT_KILL;
            }

            task = q_fetch(tg->q);
            
            if(task == NULL) {
                struct IL *il;
                item_remove(&tt->move);
                tg->activeThrds.len--;

                list_append(&tg->idleThrds, &tt->move);
                tt->state = IDLE;
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
            break;
        }
        if(tt->state == HARD_KILL) {
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

    internal_destroy_thread(tt);
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

    init_il(&tg->move);

    init_list(&tg->idleThrds);
    init_list(&tg->activeThrds);

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

static bool room_for_more_threads(TGroup *tg) {
    return THREAD_COUNT(tg) <= tg->thrdMin && THREAD_COUNT(tg) >= tg->thrdMax;
}

static bool room_for_more_groups(TPool *tp) {
    return GROUP_COUNT(tp) >= tp->groupMax;
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
