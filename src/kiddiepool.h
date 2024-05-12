#ifndef KIDDIEPOOL_H
#define KIDDIEPOOL_H

#define DEFAULT_NUM_GROUPS 16
#define DEFAULT_THREADS_PER_GROUP 2 

#include <pthread.h>

typedef void (*work_func)(void *work_arg);

typedef struct KiddiePool KiddiePool;
typedef struct KiddieGroup KiddieGroup;
 
int init_pool(KiddiePool **pool, unsigned int numGrps);

int add_group(KiddiePool *pool, unsigned int numThrds);
/**
 * @note    this will destroy all threads within a group
*/
void destroy_group(KiddiePool *pool, int groupId);

int add_work(KiddiePool *pool, int groupId, work_func wf, void *work_arg);

void destroy_pool(KiddiePool *pool);

#endif //KIDDIEPOOL_H