#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "dyn_array.h"
#include "sched_helper.h"
#include <semaphore.h>
#include <pthread.h>
#include "queue.h"
#include "pool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct scheduler;
struct coroutine;
struct task_pool;

typedef void (*coroutine_entry)(struct coroutine *crt);
typedef void (*coroutine_async_entry)(struct coroutine *crt, void *user_data);
typedef void (*cls_destructor)(void *data);

struct task_list {
    struct queue q;
};

struct cls_slot {
    void *data;
    cls_destructor dtor;
};

struct coroutine_local_storage {
    int n_slots;
    struct cls_slot *slots;
};

struct coroutine {
    long local_rsp;
    long caller_rsp;
    int initialized;
    int terminated;

    // async_detached == 1 && async_target != NULL && async_user_data is valid
    // OR async_detached == 0 && async_target == NULL && async_user_data == NULL
    int async_detached;
    coroutine_async_entry async_target;
    void *async_user_data;
    void *async_return_data;

    int n_pin_reasons;

    struct scheduler *current_scheduler;

    int migratable;

    struct task_pool *pool;
    struct coroutine_local_storage cls;

    coroutine_entry entry;
    void *user_data;

    char stack[];
};

void coroutine_yield(
    struct coroutine *crt
);

void coroutine_init(
    struct coroutine *crt,
    struct task_pool *pool,
    coroutine_entry entry,
    void *user_data
);

void coroutine_run(
    struct coroutine *crt
);

void * coroutine_async_enter(
    struct coroutine *crt,
    coroutine_async_entry entry,
    void *user_data
);

void coroutine_async_exit(
    struct coroutine *crt,
    void *data
);

void coroutine_inc_n_pin_reasons(
    struct coroutine *crt
);

void coroutine_dec_n_pin_reasons(
    struct coroutine *crt
);

struct perf_info {
    // The scheduler with the longest task queue
    struct scheduler *max_sched;
    int max_sched_len;

    // shortest
    struct scheduler *min_sched;
    int min_sched_len;

    pthread_spinlock_t lock;
};

struct task_pool {
    struct task_list tasks;
    struct resource_pool coroutine_pool;

    int stack_size;

    struct perf_info perf;

    int disable_work_stealing;
    int n_cls_slots;
    int n_schedulers;
    int n_busy_schedulers;
    int n_period_sched_status_updates;
    struct dyn_array cls_destructors;
    pthread_mutex_t cls_destructors_lock;
};

void task_list_init(struct task_list *list, int concurrent);
void task_list_destroy(struct task_list *list);
void task_list_debug_print(struct task_list *list);
void task_list_push_node(struct task_list *list, struct queue_node *node);
struct queue_node * task_list_pop_node(struct task_list *list);
int task_list_is_empty(struct task_list *list);

void task_pool_init(struct task_pool *pool, int stack_size, int concurrent);
void task_pool_destroy(struct task_pool *pool);
void task_pool_push_node(struct task_pool *pool, struct queue_node *node);
struct queue_node * task_pool_pop_node(struct task_pool *pool);
int task_pool_get_n_cls_slots(struct task_pool *pool);
int task_pool_add_cls_slot(struct task_pool *pool, cls_destructor dtor);
int task_pool_get_and_reset_n_period_sched_status_updates(struct task_pool *pool);
int task_pool_get_n_available_schedulers(struct task_pool *pool);

struct scheduler {
    struct task_pool *pool;
    struct task_list local_tasks;
};

void scheduler_init(struct scheduler *sch, struct task_pool *pool);
void scheduler_destroy(struct scheduler *sch);
void scheduler_take_coroutine(struct scheduler *sch, struct coroutine *crt);
void scheduler_run(struct scheduler *sch);

void start_coroutine(
    struct task_pool *pool,
    coroutine_entry entry,
    void *user_data
);

struct coroutine * current_coroutine();

#ifdef __cplusplus
}
#endif

#endif
