#ifndef _POOL_H_
#define _POOL_H_

#include "queue.h"

struct resource_pool {
    struct queue q;
};

static void resource_pool_init(struct resource_pool *pool, int element_size) {
    queue_init(&pool -> q, element_size, 0);
}

static void resource_pool_destroy(struct resource_pool *pool) {
    queue_destroy(&pool -> q, NULL);
}

static struct queue_node * resource_pool_take(struct resource_pool *pool) {
    struct queue_node *node;

    node = queue_pop(&pool -> q);
    if(node == NULL) {
        node = (struct queue_node *) malloc(sizeof(struct queue_node) + pool -> q.element_size);
    }

    return node;
}

static void resource_pool_return(struct resource_pool *pool, struct queue_node *node) {
    if(queue_len(&pool -> q) < 256) {
        queue_push(&pool -> q, node);
    } else {
        free(node);
    }
}

#endif
