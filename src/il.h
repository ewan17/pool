#ifndef IL_H
#define IL_H

#include <stdlib.h>
#include <stddef.h>

typedef struct Node Node; 

struct Node {
    Node *next;
    Node *prev;
};

typedef struct LL {
    Node head;
    int len;
} LL;

static inline void list_append(LL *list, Node *node) {
    node_append(&list->head, node);
    list->len++;
}

static inline struct Node *list_pop(LL *list, Node *node) {
    if(list->len == 0) {
        return NULL;
    }

    struct Node *rc;
    rc = node_pop(&list->head);
    list->len--;

    return rc;
}

static inline void node_append(struct Node *n1, struct Node *n2) {
    n1->prev->next = n2;
    n2->prev = n1->prev;
    n1->prev = n2;
    n2->next = n1;
}

static inline struct Node *node_pop(struct Node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->next = node->prev = node;

    return node;
}

static inline int is_empty(LL *list) {
    return list->len == 0;
}

#define INIT_LIST(list) \
    list = {{&(list).head, &(list).head}, 0}

#define for_each(head, curr) \
    for(curr = (head)->next; curr != head; curr = (curr)->next)

#define CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#endif //IL_H