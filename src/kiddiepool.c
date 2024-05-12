#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
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

struct KiddieGroup {
    pthread_mutex_t mutex_kg;
    pthread_cond_t cond_kg;
    pthread_cond_t kill_cond_kg;

    pthread_t *threads;

    int id;
    size_t thread_count;
    size_t active_threads;
    int kill;

    struct Q *q;

    KiddiePool *pool;
};

struct KiddiePool {
    pthread_mutex_t mutex_kp;

    unsigned int totalNumThrds;
    unsigned int totalNumGrps;

    KiddieGroup **kiddie_groups;
};

static int resize_pool(KiddiePool *pool, unsigned int numgrps, bool additional);
static int internal_create_group(KiddiePool *kp, KiddieGroup **kg, unsigned int numthrds, int groupId);
static void *thread_function(void *pool);

static int q_init(struct Q **q, size_t capacity);
static void q_destroy(struct Q **q);
static void q_clear(struct Q *q);
static void q_append(struct Q *q, struct Node *node);
static struct Node *q_fetch(struct Q *q);
static int q_length(struct Q *q);

static void node_kill(struct Node *node);

int init_pool(KiddiePool **pool, unsigned int numgrps) {
    if(pool == NULL) {
        return -1;
    }

    *pool = (KiddiePool *)malloc(sizeof(KiddiePool));
    if(*pool == NULL) {
        fprintf(stderr, "Failed to allocate memory for KiddiePool\n");
        return -1;
    }

    KiddiePool *kp = *pool;
    kp->kiddie_groups = NULL;
    kp->totalNumGrps = 0;

    if(resize_pool(kp, numgrps, true) != 0) {
		free(kp);
        kp = NULL;
        return -1;
    }

    pthread_mutex_init(&(kp->mutex_kp), NULL);
    
    for (size_t i = 0; i < numgrps; i++)
    {    
        KiddieGroup *kg;

        int rc;
        rc = internal_create_group(kp, &kg, DEFAULT_THREADS_PER_GROUP, i);

        if(rc != 0) {
            /**
             * @note    all groups get initialized or none of them
            */
            destroy_pool(*pool);
            *pool = NULL;
            return -1;
        };
    }
    return 0;
}

/**
 * @note    currently we have no limit on the number of threads and groups
 * @todo    will need to add limit checks later
*/
int add_group(KiddiePool *pool, unsigned int numthrds) {
    KiddieGroup *kg;
    internal_create_group(pool, &kg, numthrds, pool->totalNumGrps);

    pthread_mutex_lock(&pool->mutex_kp);
    if(resize_pool(pool, 1, true) != 0) {
        return -1;
    }

    unsigned int index = pool->totalNumGrps;

    pool->totalNumGrps++;
    pool->totalNumThrds += numthrds;
    pool->kiddie_groups[index] = kg;
    pthread_mutex_unlock(&pool->mutex_kp);

    return 0;
}

void destroy_group(KiddiePool *pool, int groupId) {
    KiddieGroup *kg;
    size_t total_thread_count;

    pthread_mutex_lock(&pool->mutex_kp);
    if(groupId < 0 || groupId > (pool->totalNumGrps - 1)) {
        pthread_mutex_unlock(&pool->mutex_kp);
        return;
    }
    kg = pool->kiddie_groups[groupId];
    pthread_mutex_unlock(&pool->mutex_kp);

    pthread_mutex_lock(&(kg->mutex_kg));
    total_thread_count = kg->thread_count;
    kg->kill = 1;

    while(kg->thread_count != 0) {
        pthread_cond_broadcast(&(kg->cond_kg));
        pthread_cond_wait(&(kg->kill_cond_kg), &(kg->mutex_kg));
    }

    for (size_t i = 0; i < total_thread_count; i++) {
        pthread_join(kg->threads[i], NULL);
    }

    free(kg->threads);
    kg->threads = NULL;

    q_destroy(&(kg->q));

    pthread_mutex_destroy(&(kg->mutex_kg));
    pthread_cond_destroy(&(kg->cond_kg));
    pthread_cond_destroy(&(kg->kill_cond_kg));

    free(kg);

    pthread_mutex_lock(&pool->mutex_kp);
    resize_pool(pool, 1, false);

    pool->totalNumThrds -= total_thread_count;
    pool->totalNumGrps--;
    pthread_mutex_unlock(&pool->mutex_kp);
}

int add_work(KiddiePool *pool, int groupId, work_func wf, void *work_arg) {
    if(wf == NULL) {
        return -1;
    }

    pthread_mutex_lock(&(pool->mutex_kp));
    if(groupId < 0 || groupId > (pool->totalNumGrps - 1)) {
        pthread_mutex_unlock(&(pool->mutex_kp));
        return -1; 
    }

    KiddieGroup *kg;
    kg = pool->kiddie_groups[groupId];
    pthread_mutex_unlock(&(pool->mutex_kp));

    struct Node *node;
    node = (struct Node *)malloc(sizeof(struct Node));
    if(node == NULL) {
        fprintf(stderr, "Failed to allocate memory for KiddieGroups\n");
        return -1;
    }
    node->wf = wf;
    node->work_arg = work_arg;
    node->prev = NULL;

    pthread_mutex_lock(&(kg->mutex_kg));
    q_append(kg->q, node);
    pthread_cond_signal(&(kg->cond_kg));
    pthread_mutex_unlock(&(kg->mutex_kg));

    return 0;
}

void destroy_pool(KiddiePool *pool) {
    if(pool != NULL) {
        pthread_mutex_lock(&(pool->mutex_kp));
        for (size_t i = 0; i < pool->num_groups; i++)
        {
            destroy_group(&(pool->kiddie_groups[i]));
        }
        free(pool->kiddie_groups);
        pool->kiddie_groups = NULL;

        pthread_mutex_destroy(&(pool->mutex_kp));

        free(pool);
    }
}

static int resize_pool(KiddiePool *pool, unsigned int numgrps, bool additional) {
    unsigned int currNumGrps = pool->totalNumGrps;
    if(additional) {
        currNumGrps += additional;
    } else {
        currNumGrps -= additional;
    }

    pool->kiddie_groups = (KiddieGroup **) realloc(pool->kiddie_groups, currNumGrps * sizeof(KiddieGroup *));
    if(pool->kiddie_groups == NULL) {
        return -1;
    }
    
    pool->totalNumGrps = currNumGrps;

    return 0;
}

static int internal_create_group(KiddiePool *kp, KiddieGroup **kg, unsigned int numthrds, int groupId) {
    if(kp == NULL) {
        return -1;
    }

    *kg = (KiddieGroup *)malloc(sizeof(KiddieGroup));
    if(kg == NULL) {
        return -1;
    }

    (*kg)->threads = (pthread_t *)malloc(numthrds * sizeof(pthread_t));
	if ((*kg)->threads == NULL){
		free(kg);
		return -1;
	}
    
    /**
     * @note    queue size can be changed later. Picked random size.
    */
    struct Q *q;
    if(q_init(&q, 1024) != 0) {
        free((*kg)->threads);
        free(kg);
        return -1;
    };

    (*kg)->q = q;
    (*kg)->id = groupId;
    (*kg)->kill = 0;
    (*kg)->pool = kp;
    (*kg)->thread_count = numthrds;
    (*kg)->active_threads = 0;
    pthread_mutex_init(&((*kg)->mutex_kg), NULL);
    pthread_cond_init(&((*kg)->kill_cond_kg), NULL);
    pthread_cond_init(&((*kg)->cond_kg), NULL);

    for (size_t i = 0; i < numthrds; i++)
    {
        if(pthread_create(&((*kg)->threads[i]), NULL, thread_function, kg) != 0) {
            return -1;
        }
    }

    return 0;
}

static void *thread_function(void *arg) {
    KiddieGroup *kg;
    kg = (KiddieGroup *)arg;

    while(1) {
        struct Node *node;

        pthread_mutex_lock(&(kg->mutex_kg));    
        int tasks = q_length(kg->q);
        while(tasks == 0 && !kg->kill) {
            pthread_cond_wait(&(kg->cond_kg), &(kg->mutex_kg));
            tasks = q_length(kg->q);
        }

        if(kg->kill && tasks == 0) {
            kg->thread_count--;
            if(kg->thread_count == 0) {
                pthread_cond_signal(&(kg->kill_cond_kg));
            }
            pthread_mutex_unlock(&(kg->mutex_kg));
            break;
        }
        
        node = q_fetch(kg->q);
        kg->active_threads++;
        pthread_mutex_unlock(&(kg->mutex_kg));

        work_func func = node->wf;
        void *arg = node->work_arg;
        func(arg);
        node_kill(node);
        node = NULL;

        pthread_mutex_lock(&(kg->mutex_kg));
        kg->active_threads--;
        pthread_mutex_unlock(&(kg->mutex_kg));
    }

    pthread_exit(NULL);
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
    if(q != NULL || q->head != NULL) {
        if(q->head == q->tail) {
            q->tail = NULL;
        }
        struct Node *tmp = q->head;
        q->head = q->head->prev;
        tmp->prev = NULL;
        (q->length)--;

        return tmp;
    }
    return NULL;
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
