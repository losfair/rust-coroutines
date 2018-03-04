#ifndef _LINKED_LIST_H_
#define _LINKED_LIST_H_

#include <semaphore.h>
#include <string.h>
#include <pthread.h>

// STM doesn't provide real benefit here...

//#ifdef HAS_TM
//#define CRITICAL_ENTER(lock) __transaction_atomic {
//#define CRITICAL_EXIT(lock) }
//#else
#define CRITICAL_ENTER(lock) pthread_mutex_lock(lock)
#define CRITICAL_EXIT(lock) pthread_mutex_unlock(lock)
//#endif

struct queue;
struct queue_node;

struct queue {
    struct queue_node *head;
    struct queue_node *tail;
    int element_size;
    int n_elements;
    int sync; // (sync && notify.initialized) || (!sync && !notify.initialized)
    sem_t notify;
    pthread_mutex_t lock;
};

struct queue_node {
    struct queue *q;
    struct queue_node *prev;
    struct queue_node *next;
    char data[];
};

static void queue_node_init(struct queue_node *node, struct queue *q) {
    node -> q = q;
    node -> prev = NULL;
    node -> next = NULL;
};

static inline void * queue_node_unwrap(struct queue_node *node) {
    return (void *) node -> data;
}

static inline struct queue_node * queue_node_restore(void *inner) {
    return (struct queue_node *) ((char *) inner - sizeof(struct queue_node));
}

static void queue_init(struct queue *q, int element_size, int sync) {
    // The 'head' element shouldn't have any associated data.
    q -> head = (struct queue_node *) malloc(sizeof(struct queue_node));
    q -> tail = q -> head;

    q -> element_size = element_size;
    q -> n_elements = 0;
    q -> sync = sync;

    if(sync) sem_init(&q -> notify, 0, 0);
    pthread_mutex_init(&q -> lock, 0);

    queue_node_init(q -> head, q);
}

static void queue_destroy(struct queue *q, void (*destructor)(struct queue_node *node)) {
    struct queue_node *current, *next;

    current = q -> head;

    next = current -> next;
    free(current);
    current = next;

    while(current) {
        next = current -> next;
        if(destructor) destructor(current);
        current = next;
    }
    q -> head = NULL;
    q -> tail = NULL;

    if(q -> sync) sem_destroy(&q -> notify);
    pthread_mutex_destroy(&q -> lock);
}

static int queue_len(struct queue *q) {
    int n_elements;
    CRITICAL_ENTER(&q -> lock);
    n_elements = q -> n_elements;
    CRITICAL_EXIT(&q -> lock);
    return n_elements;
}

static void queue_push(struct queue *q, struct queue_node *node) {
    CRITICAL_ENTER(&q -> lock);
    node -> prev = q -> tail;
    q -> tail -> next = node;
    q -> tail = node;
    q -> n_elements ++;
    CRITICAL_EXIT(&q -> lock);

    if(q -> sync) sem_post(&q -> notify);
}

static struct queue_node * queue_try_take_tail(struct queue *q) {
    struct queue_node *current;
    struct queue_node *ret = NULL;

    CRITICAL_ENTER(&q -> lock);
    current = q -> tail;
    if(current == q -> head) ret = NULL;
    else {
        ret = current;
        ret -> prev -> next = NULL;
        q -> tail = ret -> prev;
        ret -> prev = NULL;
        q -> n_elements --;
    }
    CRITICAL_EXIT(&q -> lock);

    if(ret && q -> sync) sem_wait(&q -> notify);
    return ret;
}

static struct queue_node * queue_try_pop(struct queue *q) {
    struct queue_node *node;

    CRITICAL_ENTER(&q -> lock);
    node = q -> head -> next;
    if(node != NULL) {
        q -> head -> next = node -> next;
        if(node -> next) node -> next -> prev = q -> head;
        if(node == q -> tail) q -> tail = q -> head;
        node -> prev = NULL;
        node -> next = NULL;
        q -> n_elements --;
    }
    CRITICAL_EXIT(&q -> lock);

    if(node && q -> sync) sem_wait(&q -> notify);
    return node;
}

static struct queue_node * queue_pop(struct queue *q) {
    struct queue_node *node;

    if(q -> sync) sem_wait(&q -> notify);

    CRITICAL_ENTER(&q -> lock);
    node = q -> head -> next;
    if(node != NULL) {
        q -> head -> next = node -> next;
        if(node -> next) node -> next -> prev = q -> head;
        if(node == q -> tail) q -> tail = q -> head;
        node -> prev = NULL;
        node -> next = NULL;
        q -> n_elements --;
    }
    CRITICAL_EXIT(&q -> lock);

    return node;
}

#undef CRITICAL_ENTER
#undef CRITICAL_EXIT

#endif
