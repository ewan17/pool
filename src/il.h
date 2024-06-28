#ifndef IL_H
#define IL_H

#include <stddef.h>
#include <stdlib.h>

typedef struct IL {
    struct IL *next;
    struct IL *prev;
} IL;

typedef struct LL {
    IL head;
    size_t len;
} LL;

static inline void init_il(IL *il) {
    il->next = il->prev = il;
}

static inline void init_list(LL *list) {
    list->head.next = &(list)->head;
    list->head.prev = &(list)->head;
    list->len = 0;
}

static inline int empty(LL *list) {
    return list->len == 0;
}

static inline void item_append(IL *item1, IL *item2) {
    item1->prev->next = item2;
    item2->prev = item1->prev;
    item1->prev = item2;
    item2->next = item1;
}

static inline void list_append(LL *list, struct IL *item) {
    item_append(&list->head, item);
    list->len++;
}

static inline IL *item_remove(IL *item) {
    item->prev->next = item->next;
    item->next->prev = item->prev;

    item->next = item->prev = item;

    return item;
}

static inline IL *list_pop(LL *list) {
    if(empty(list)) {
        return NULL;
    }

    IL *rc;
    rc = item_remove(list->head.next);
    list->len--;

    return rc;
}

#define INIT_LIST(list) \
    list = {{&(list).head, &(list).head}, 0}

#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#define for_each(head, curr) \
    for(curr = (head)->next; curr != head; curr = (curr)->next)

#endif //IL_H