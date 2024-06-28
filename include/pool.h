#ifndef POOL_H
#define POOL_H

#define GROUP_DYNAMIC 0x01
#define GROUP_FIXED 0x02

typedef struct TPool TPool;
typedef struct TGroup TGroup;
typedef struct Work Work;

typedef void (*work_func)(void *work_arg);

int init_pool(TPool **tp, unsigned int maxThrds);
void destroy_pool(TPool *tp);

TGroup *add_group(TPool *tp, unsigned int min, unsigned int max, int flags);
void destroy_group(TGroup *tg);

void init_work(Work **work);
void add_work(Work *work, work_func func, void *arg);
int do_work(TGroup *tg, Work *work);

#endif //POOL_H