#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "scheduler.h"

void coroutine_yield(
    struct coroutine *crt
) {
    yield_now(&crt -> local_rsp, &crt -> caller_rsp);
}

static void coroutine_target_init(void *raw_crt) {
    struct coroutine *crt = (struct coroutine *) raw_crt;
    coroutine_yield(crt);
    crt -> entry(crt);
    crt -> terminated = 1;
    coroutine_yield(crt);
}

void coroutine_init(
    struct coroutine *crt,
    size_t stack_size,
    coroutine_entry entry,
    void *user_data
) {
    crt -> stack_begin = (char *) malloc(stack_size);
    crt -> stack_end = crt -> stack_begin + stack_size;
    crt -> local_rsp = (long) crt -> stack_end;
    crt -> caller_rsp = 0;
    crt -> terminated = 0;
    crt -> entry = entry;
    crt -> user_data = user_data;
    init_co_stack(&crt -> caller_rsp, &crt -> local_rsp, coroutine_target_init, (void *) crt);
}

void coroutine_destroy(
    struct coroutine *crt
) {
    if(crt -> stack_begin != NULL) {
        free(crt -> stack_begin);
    }
}

int coroutine_run(
    struct coroutine *crt
) {
    if(crt -> terminated) {
        fprintf(stderr, "ERROR: Attempting to call coroutine_run() on a terminated coroutine\n");
        abort();
    }
    yield_now(&crt -> caller_rsp, &crt -> local_rsp);
    return crt -> terminated;
}

void task_node_init(struct task_node *node) {
    node -> crt = NULL;
    node -> prev = NULL;
    node -> next = NULL;
}

void task_node_destroy(struct task_node *node) {
    if(node -> crt) {
        coroutine_destroy(node -> crt);
        node -> crt = NULL;
    }
    node -> prev = NULL;
    node -> next = NULL;
}

void task_pool_init(struct task_pool *pool, int concurrent) {
    pool -> head = (struct task_node *) malloc(sizeof(struct task_node));
    task_node_init(pool -> head);
    pool -> tail = pool -> head;
    pool -> concurrent = concurrent;
    sem_init(&pool -> elem_notify, 0, 0);
    pthread_mutex_init(&pool -> lock, NULL);
}

void task_pool_destroy(struct task_pool *pool) {
    struct task_node *current, *next;

    current = pool -> head;
    assert(current -> prev == NULL);

    while(current) {
        next = current -> next;
        task_node_destroy(current);
        free(current);
        current = next;
    }

    pool -> head = NULL;
    pool -> tail = NULL;
    sem_destroy(&pool -> elem_notify);
    pthread_mutex_destroy(&pool -> lock);
}

void task_pool_debug_print(struct task_pool *pool) {
    struct task_node *current;

    current = pool -> head;
    assert(current -> prev == NULL);

    printf("----- BEGIN -----\n");
    while(current) {
        printf("current=%p crt=%p prev=%p next=%p\n", current, current -> crt, current -> prev, current -> next);
        current = current -> next;
    }
    printf("----- END -----\n");
}

void task_pool_push_node(struct task_pool *pool, struct task_node *node) {
    if(pool -> concurrent) pthread_mutex_lock(&pool -> lock);

    assert(pool -> tail -> next == NULL);
    assert(node -> prev == NULL && node -> next == NULL);

    pool -> tail -> next = node;
    node -> prev = pool -> tail;
    pool -> tail = node;

    if(pool -> concurrent) pthread_mutex_unlock(&pool -> lock);
    sem_post(&pool -> elem_notify);
}

struct task_node * task_pool_pop_node(struct task_pool *pool) {
    struct task_node *ret;

    sem_wait(&pool -> elem_notify);
    if(pool -> concurrent) pthread_mutex_lock(&pool -> lock);

    assert(pool -> head -> prev == NULL);
    assert(pool -> head -> next != NULL);
    ret = pool -> head -> next;
    pool -> head -> next = ret -> next;
    if(ret -> next) ret -> next -> prev = pool -> head;
    ret -> prev = NULL;
    ret -> next = NULL;

    if(ret == pool -> tail) {
        pool -> tail = pool -> head;
    }

    if(pool -> concurrent) pthread_mutex_unlock(&pool -> lock);
    return ret;
}

void scheduler_init(struct scheduler *sch, struct task_pool *pool) {
    sch -> pool = pool;
}

void scheduler_destroy(struct scheduler *sch) {

}

void scheduler_run(struct scheduler *sch) {
    struct task_node *current, *pinned;
    int terminated;

    pinned = NULL;

    while(1) {
        if(pinned) {
            current = pinned;
        } else {
            current = task_pool_pop_node(sch -> pool);
            pinned = current;
            //printf("Pinning %p to scheduler %p\n", current, sch);
        }

        //printf("Scheduler %p got task\n", sch);

        terminated = coroutine_run(current -> crt);
        if(terminated) {
            task_node_destroy(current);
            free(current);
            pinned = NULL;
        } else {
            //task_pool_push_node(sch -> pool, current);
        }
    }
}

void start_coroutine(
    struct task_pool *pool,
    size_t stack_size,
    coroutine_entry entry,
    void *user_data
) {
    struct coroutine *crt = malloc(sizeof(struct coroutine));
    struct task_node *node = malloc(sizeof(struct task_node));

    coroutine_init(crt, stack_size, entry, user_data);
    task_node_init(node);

    node -> crt = crt;
    task_pool_push_node(pool, node);
}
