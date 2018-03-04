#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "scheduler.h"

#ifdef HAS_TM
#define CRITICAL_ENTER(lock) __transaction_atomic {
#define CRITICAL_EXIT(lock) }
#else
#define CRITICAL_ENTER(lock) pthread_spin_lock(lock)
#define CRITICAL_EXIT(lock) pthread_spin_unlock(lock)
#endif

static __thread struct coroutine *current_co = NULL;

void coroutine_yield(
    struct coroutine *crt
) {
    yield_now(&crt -> local_rsp, &crt -> caller_rsp);
}

void * coroutine_async_enter(
    struct coroutine *crt,
    coroutine_async_entry entry,
    void *user_data
) {
    assert(crt -> async_detached == 0);
    crt -> async_detached = 1;

    crt -> async_target = entry;
    crt -> async_user_data = user_data;

    assert(crt -> async_return_data == NULL);

    coroutine_yield(crt);

    void *return_data = crt -> async_return_data;
    crt -> async_return_data = NULL;

    return return_data;
}

void coroutine_async_exit(
    struct coroutine *crt,
    void *data
) {
    assert(crt -> async_detached == 1);
    crt -> async_detached = 0;

    crt -> async_target = NULL;
    crt -> async_user_data = NULL;
    crt -> async_return_data = data;

    queue_push(&crt -> current_scheduler -> local_tasks.q, queue_node_restore(crt));
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
    struct task_pool *pool,
    coroutine_entry entry,
    void *user_data
) {
    int i;

    crt -> local_rsp = (long) crt -> stack + pool -> stack_size;
    crt -> caller_rsp = 0;
    crt -> initialized = 0;
    crt -> terminated = 0;

    crt -> async_detached = 0;
    crt -> async_target = NULL;
    crt -> async_user_data = NULL;
    crt -> async_return_data = NULL;

    crt -> migratable = 1;

    pthread_mutex_lock(&pool -> cls_destructors_lock);

    crt -> cls.n_slots = pool -> n_cls_slots;
    assert(crt -> cls.n_slots >= 0);
    if(crt -> cls.n_slots > 0) {
        crt -> cls.slots = (struct cls_slot *) malloc(sizeof(struct cls_slot) * (crt -> cls.n_slots));
        for(i = 0; i < crt -> cls.n_slots; i++) {
            crt -> cls.slots[i].data = NULL;
            dyn_array_index(&pool -> cls_destructors, (char **) &crt -> cls.slots[i].dtor, i);
        }
    } else {
        crt -> cls.slots = NULL;
    }

    pthread_mutex_unlock(&pool -> cls_destructors_lock);

    crt -> n_pin_reasons = 0;
    crt -> current_scheduler = NULL;

    crt -> pool = pool;
    crt -> entry = entry;
    crt -> user_data = user_data;

}

void coroutine_destroy(
    struct coroutine *crt
) {
    int i;
    cls_destructor dtor;

    if(crt -> cls.slots != NULL) {
        assert(crt -> cls.n_slots > 0);
        for(i = 0; i < crt -> cls.n_slots; i++) {
            dtor = crt -> cls.slots[i].dtor;
            if(dtor) dtor(crt -> cls.slots[i].data);
        }
        free(crt -> cls.slots);
    }
}

void coroutine_run(
    struct coroutine *crt
) {
    if(crt -> terminated) {
        fprintf(stderr, "ERROR: Attempting to call coroutine_run() on a terminated coroutine\n");
        abort();
    }
    if(crt -> initialized) {
        yield_now(&crt -> caller_rsp, &crt -> local_rsp);
    } else {
        init_co_stack(&crt -> caller_rsp, &crt -> local_rsp, coroutine_target_init, (void *) crt);
        crt -> initialized = 1;
    }
}

void coroutine_inc_n_pin_reasons(
    struct coroutine *crt
) {
    assert(crt -> n_pin_reasons >= 0);
    if(crt -> n_pin_reasons == 0) {
        crt -> n_pin_reasons = 1;
        crt -> migratable = 0;
    } else {
        crt -> n_pin_reasons ++;
    }
}

void coroutine_dec_n_pin_reasons(
    struct coroutine *crt
) {
    if(crt -> n_pin_reasons == 1) {
        crt -> n_pin_reasons = 0;
        crt -> migratable = 1;
    } else {
        crt -> n_pin_reasons --;
    }
}

void task_list_init(struct task_list *list, int _concurrent /* unused */) {
    queue_init(&list -> q, sizeof(struct coroutine), 0);
}

static void _task_list_destroy_node_cb(struct queue_node *node) {
    struct coroutine *co = queue_node_unwrap(node);
    coroutine_destroy(co);
    free(node);
}

void task_list_destroy(struct task_list *list) {
    queue_destroy(&list -> q, _task_list_destroy_node_cb);
}

void task_pool_init(struct task_pool *pool, int stack_size, int concurrent) {
    task_list_init(&pool -> tasks, concurrent);
    resource_pool_init(&pool -> coroutine_pool, sizeof(struct coroutine) + stack_size);

    pool -> stack_size = stack_size;

    pool -> perf.max_sched = NULL;
    pool -> perf.max_sched_len = -1;
    pool -> perf.min_sched = NULL;
    pool -> perf.min_sched_len = -1;

    pthread_spin_init(&pool -> perf.lock, 0);

    pool -> migration_count = 0;

    // work-stealing is not safe for Rust (non-Send types).
    pool -> disable_work_stealing = 1;

    pool -> n_cls_slots = 0;
    pool -> n_schedulers = 0;
    pool -> n_busy_schedulers = 0;
    pool -> n_period_sched_status_updates = 0;
    dyn_array_init(&pool -> cls_destructors, sizeof(cls_destructor));
    pthread_mutex_init(&pool -> cls_destructors_lock, NULL);
}

void task_pool_destroy(struct task_pool *pool) {
    task_list_destroy(&pool -> tasks);
    resource_pool_destroy(&pool -> coroutine_pool);

    pthread_spin_destroy(&pool -> perf.lock);

    dyn_array_destroy(&pool -> cls_destructors);
    pthread_mutex_destroy(&pool -> cls_destructors_lock);
}

int task_pool_get_and_reset_n_period_sched_status_updates(struct task_pool *pool) {
    return __atomic_exchange_n(&pool -> n_period_sched_status_updates, 0, __ATOMIC_RELAXED);
}

int task_pool_get_n_available_schedulers(struct task_pool *pool) {
    int total = __atomic_load_n(&pool -> n_schedulers, __ATOMIC_RELAXED);
    int busy = __atomic_load_n(&pool -> n_busy_schedulers, __ATOMIC_RELAXED);
    assert(total >= busy);
    assert(busy >= 0);
    return total - busy;
}

/*
void task_list_debug_print(struct task_list *list) {
    struct coroutine *current;

    current = list -> head;
    assert(current -> prev == NULL);

    printf("----- BEGIN -----\n");
    while(current) {
        printf("current=%p prev=%p next=%p\n", current, current -> prev, current -> next);
        current = current -> next;
    }
    printf("----- END -----\n");
}
*/

void task_list_push_node(struct task_list *list, struct queue_node *node) {
    queue_push(&list -> q, node);
}

int task_list_is_empty(struct task_list *list) {
    return queue_len(&list -> q) == 0;
}

void task_pool_push_node(struct task_pool *pool, struct queue_node *node) {
    task_list_push_node(&pool -> tasks, node);
}

int task_pool_get_n_cls_slots(struct task_pool *pool) {
    int ret;

    pthread_mutex_lock(&pool -> cls_destructors_lock);
    ret = pool -> n_cls_slots;
    pthread_mutex_unlock(&pool -> cls_destructors_lock);

    return ret;
}

int task_pool_add_cls_slot(struct task_pool *pool, cls_destructor dtor) {
    int n_slots;

    pthread_mutex_lock(&pool -> cls_destructors_lock);

    n_slots = pool -> n_cls_slots;
    assert(n_slots == dyn_array_n_elems(&pool -> cls_destructors));

    dyn_array_push(&pool -> cls_destructors, (char *) dtor);
    pool -> n_cls_slots ++;

    pthread_mutex_unlock(&pool -> cls_destructors_lock);

    return n_slots;
}

void scheduler_init(struct scheduler *sch, struct task_pool *pool) {
    sch -> pool = pool;
    task_list_init(&sch -> local_tasks, 1); // Does it have to be concurrent?
    __atomic_fetch_add(&pool -> n_schedulers, 1, __ATOMIC_RELAXED);
}

void scheduler_destroy(struct scheduler *sch) {
    task_list_destroy(&sch -> local_tasks);
    __atomic_fetch_sub(&sch -> pool -> n_schedulers, 1, __ATOMIC_RELAXED);
}

static void scheduler_try_migrate(struct scheduler *sch) {
    const int THRESHOLD = 3;

    int self_len = queue_len(&sch -> local_tasks.q);
    struct task_pool *pool = sch -> pool;
    struct scheduler *migration_target = NULL;

    CRITICAL_ENTER(&pool -> perf.lock);
    if(pool -> perf.max_sched == NULL || self_len > pool -> perf.max_sched_len) {
        pool -> perf.max_sched = sch;
        pool -> perf.max_sched_len = self_len;
    }
    if(pool -> perf.min_sched == NULL || self_len < pool -> perf.min_sched_len) {
        pool -> perf.min_sched = sch;
        pool -> perf.min_sched_len = self_len;
    }
    if(pool -> perf.max_sched_len - self_len > THRESHOLD) {
        migration_target = pool -> perf.max_sched;
        pool -> perf.max_sched = NULL;
        pool -> perf.max_sched_len = -1;
    }
    CRITICAL_EXIT(&pool -> perf.lock);

    if(migration_target) {
        struct queue_node *tail = queue_try_take_tail(&migration_target -> local_tasks.q);
        if(tail) {
            struct coroutine *tail_co = queue_node_unwrap(tail);
            if(tail_co -> migratable) {
                queue_push(&sch -> local_tasks.q, tail);
                __atomic_fetch_add(&pool -> migration_count, 1, __ATOMIC_RELAXED);
            } else {
                queue_push(&migration_target -> local_tasks.q, tail);
            }
        }
    }
}

// TODO: Graceful cleanup (?)
void scheduler_run(struct scheduler *sch) {
    int it_count = 0;
    int sleep_time = 0;
    struct queue_node *current_task_node;
    struct coroutine *current;

    assert(current_co == NULL); // nested schedulers are not allowed

    while(1) {
        int disable_work_stealing = __atomic_load_n(
            &sch -> pool -> disable_work_stealing,
            __ATOMIC_RELAXED
        );
        if(it_count == 0 && !disable_work_stealing) {
            scheduler_try_migrate(sch);
        }
        it_count++;
        if(it_count == 80) it_count = 0;

        current_task_node =  queue_try_pop(&sch -> local_tasks.q);
        if(!disable_work_stealing) {
            if(!current_task_node) {
                scheduler_try_migrate(sch);
                current_task_node = queue_try_pop(&sch -> local_tasks.q);
            }
        }
        if(!current_task_node) {
            current_task_node = queue_try_pop(&sch -> pool -> tasks.q);
        }
        if(!current_task_node) {
            if(sleep_time < 50000) {
                sleep_time += 10;
            }
            if(sleep_time >= 1000) {
                usleep(sleep_time);
            }
            continue;
        }

        sleep_time = 0;

        current = queue_node_unwrap(current_task_node);

        __atomic_fetch_add(&sch -> pool -> n_busy_schedulers, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&sch -> pool -> n_period_sched_status_updates, 1, __ATOMIC_RELAXED);
        //printf("Scheduler %p got task\n", sch);

        current_co = current;
        current -> current_scheduler = sch;
        coroutine_run(current);
        current_co = NULL;

        if(current -> terminated) {
            coroutine_destroy(current);
            resource_pool_return(&sch -> pool -> coroutine_pool, current_task_node);
        } else if(current -> async_detached) {
            current -> async_target(current, current -> async_user_data);
        } else {
            queue_push(&sch -> local_tasks.q, current_task_node);
        }

        __atomic_fetch_sub(&sch -> pool -> n_busy_schedulers, 1, __ATOMIC_RELAXED);
    }
}

void start_coroutine(
    struct task_pool *pool,
    coroutine_entry entry,
    void *user_data
) {
    struct queue_node *node = resource_pool_take(&pool -> coroutine_pool);
    queue_node_init(node, &pool -> tasks.q);
    coroutine_init(queue_node_unwrap(node), pool, entry, user_data);
    task_pool_push_node(pool, node);
}

struct coroutine * current_coroutine() {
    return current_co;
}
