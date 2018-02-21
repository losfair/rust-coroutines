#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "scheduler.h"

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

    if(crt -> pinned_scheduler != NULL) {
        task_list_push_node(&crt -> pinned_scheduler -> local_tasks, queue_node_restore(crt));
    } else {
        task_pool_push_node(crt -> pool, queue_node_restore(crt));
    }
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
    struct task_pool *pool,
    coroutine_entry entry,
    void *user_data
) {
    int i;

    crt -> stack_begin = (char *) malloc(stack_size);
    crt -> stack_end = crt -> stack_begin + stack_size;
    crt -> local_rsp = (long) crt -> stack_end;
    crt -> caller_rsp = 0;
    crt -> initialized = 0;
    crt -> terminated = 0;

    crt -> async_detached = 0;
    crt -> async_target = NULL;
    crt -> async_user_data = NULL;
    crt -> async_return_data = NULL;

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
    crt -> pinned_scheduler = NULL;

    crt -> pool = pool;
    crt -> entry = entry;
    crt -> user_data = user_data;

    crt -> prev = NULL;
    crt -> next = NULL;
}

void coroutine_destroy(
    struct coroutine *crt
) {
    int i;
    cls_destructor dtor;

    if(crt -> stack_begin != NULL) {
        free(crt -> stack_begin);
    }
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
    assert(crt -> current_scheduler != NULL);
    assert(crt -> n_pin_reasons >= 0);
    if(crt -> n_pin_reasons == 0) {
        crt -> n_pin_reasons = 1;
        assert(crt -> pinned_scheduler == NULL);
        crt -> pinned_scheduler = crt -> current_scheduler;
    } else {
        crt -> n_pin_reasons ++;
    }
}

void coroutine_dec_n_pin_reasons(
    struct coroutine *crt
) {
    assert(crt -> current_scheduler != NULL);
    assert(crt -> n_pin_reasons > 0 && crt -> pinned_scheduler != NULL);
    if(crt -> n_pin_reasons == 1) {
        crt -> n_pin_reasons = 0;
        crt -> pinned_scheduler = NULL;
    } else {
        crt -> n_pin_reasons --;
    }
}

void task_list_init(struct task_list *list, int _concurrent /* unused */) {
    queue_init(&list -> q, sizeof(struct coroutine), 1);
}

static void _task_list_destroy_node_cb(struct queue_node *node) {
    struct coroutine *co = queue_node_unwrap(node);
    coroutine_destroy(co);
    free(node);
}

void task_list_destroy(struct task_list *list) {
    queue_destroy(&list -> q, _task_list_destroy_node_cb);
}

void task_pool_init(struct task_pool *pool, int concurrent) {
    task_list_init(&pool -> tasks, concurrent);

    pool -> n_cls_slots = 0;
    pool -> n_schedulers = 0;
    pool -> n_busy_schedulers = 0;
    pool -> n_period_sched_status_updates = 0;
    dyn_array_init(&pool -> cls_destructors, sizeof(cls_destructor));
    pthread_mutex_init(&pool -> cls_destructors_lock, NULL);
}

void task_pool_destroy(struct task_pool *pool) {
    task_list_destroy(&pool -> tasks);

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

struct queue_node * task_list_pop_node(struct task_list *list) {
    return queue_pop(&list -> q);
}

void task_pool_push_node(struct task_pool *pool, struct queue_node *node) {
    task_list_push_node(&pool -> tasks, node);
}

struct queue_node * task_pool_pop_node(struct task_pool *pool) {
    return task_list_pop_node(&pool -> tasks);
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

// TODO: Graceful cleanup (?)
void scheduler_run(struct scheduler *sch) {
    int has_perm_pinning;
    struct queue_node *current_task_node;
    struct coroutine *current;

    has_perm_pinning = 0;
    assert(current_co == NULL); // nested schedulers are not allowed

    while(1) {
        if(has_perm_pinning) {
            current_task_node = task_list_pop_node(&sch -> local_tasks);
        } else {
            current_task_node = task_pool_pop_node(sch -> pool);
            //printf("Pinning %p to scheduler %p\n", current, sch);
        }

        current = queue_node_unwrap(current_task_node);

        __atomic_fetch_add(&sch -> pool -> n_busy_schedulers, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&sch -> pool -> n_period_sched_status_updates, 1, __ATOMIC_RELAXED);
        //printf("Scheduler %p got task\n", sch);

        current_co = current;
        current -> current_scheduler = sch;
        coroutine_run(current);
        current -> current_scheduler = NULL;
        current_co = NULL;

        if(current -> pinned_scheduler) {
            assert(current -> pinned_scheduler == sch);
            /*if(!has_perm_pinning) {
                printf("Permanent pinning begin\n");
            }*/
            has_perm_pinning = 1;
        } else {
            /*if(has_perm_pinning) {
                printf("Permanent pinning end\n");
            }*/
            has_perm_pinning = 0;
        }

        if(current -> terminated) {
            coroutine_destroy(current);
            free(current_task_node);
        } else if(current -> async_detached) {
            current -> async_target(current, current -> async_user_data);
        } else {
            task_pool_push_node(sch -> pool, current_task_node);
        }

        __atomic_fetch_sub(&sch -> pool -> n_busy_schedulers, 1, __ATOMIC_RELAXED);
    }
}

void start_coroutine(
    struct task_pool *pool,
    size_t stack_size,
    coroutine_entry entry,
    void *user_data
) {
    struct queue_node *node = malloc(sizeof(struct queue_node) + sizeof(struct coroutine));
    queue_node_init(node, &pool -> tasks.q);
    coroutine_init(queue_node_unwrap(node), stack_size, pool, entry, user_data);
    task_pool_push_node(pool, node);
}

struct coroutine * current_coroutine() {
    return current_co;
}
