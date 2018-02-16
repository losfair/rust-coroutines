#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "sched_helper.h"
#include <semaphore.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct coroutine;
struct task_pool;

typedef void (*coroutine_entry)(struct coroutine *crt);
typedef void (*coroutine_async_entry)(struct coroutine *crt, void *user_data);

struct coroutine {
    char *stack_begin;
    char *stack_end;
    long local_rsp;
    long caller_rsp;
    int terminated;

    // async_detached == 1 && async_target != NULL && async_user_data is valid
    // OR async_detached == 0 && async_target == NULL && async_user_data == NULL
    int async_detached;
    coroutine_async_entry async_target;
    void *async_user_data;

    struct task_pool *pool;

    coroutine_entry entry;
    void *user_data;
};

void coroutine_yield(
    struct coroutine *crt
);

void coroutine_init(
    struct coroutine *crt,
    size_t stack_size,
    struct task_pool *pool,
    coroutine_entry entry,
    void *user_data
);

void coroutine_run(
    struct coroutine *crt
);

void coroutine_async_enter(
    struct coroutine *crt,
    coroutine_async_entry entry,
    void *user_data
);

void coroutine_async_exit(
    struct coroutine *crt
);

struct task_node {
    struct coroutine *crt;
    struct task_node *prev;
    struct task_node *next;
};

struct task_pool {
    struct task_node *head;
    struct task_node *tail;

    sem_t elem_notify;
    int concurrent;
    pthread_mutex_t lock;
};

void task_node_init(struct task_node *node);
void task_node_destroy(struct task_node *node);
void task_pool_init(struct task_pool *pool, int concurrent);
void task_pool_destroy(struct task_pool *pool);
void task_pool_push_node(struct task_pool *pool, struct task_node *node);
struct task_node * task_pool_pop_node(struct task_pool *pool);

struct scheduler {
    struct task_pool *pool;
};

void scheduler_init(struct scheduler *sch, struct task_pool *pool);
void scheduler_destroy(struct scheduler *sch);
void scheduler_take_coroutine(struct scheduler *sch, struct coroutine *crt);
void scheduler_run(struct scheduler *sch);

void start_coroutine(
    struct task_pool *pool,
    size_t stack_size,
    coroutine_entry entry,
    void *user_data
);

struct coroutine * current_coroutine();

#ifdef __cplusplus
}
#endif

#endif
