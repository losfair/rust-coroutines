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
        task_list_push_node(&crt -> pinned_scheduler -> local_tasks, crt);
    } else {
        task_pool_push_node(crt -> pool, crt);
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

void task_list_init(struct task_list *list, int concurrent) {
    // list -> head should never be used as a real coroutine.
    list -> head = (struct coroutine *) malloc(sizeof(struct coroutine));
    list -> head -> prev = NULL;
    list -> head -> next = NULL;
    list -> tail = list -> head;

    list -> n_pop_awaiters = 0;
    list -> concurrent = concurrent;
    sem_init(&list -> elem_notify, 0, 0);
    pthread_mutex_init(&list -> lock, NULL);
}

void task_list_destroy(struct task_list *list) {
    struct coroutine *current, *next;

    assert(list -> n_pop_awaiters == 0);

    current = list -> head;
    assert(current != NULL);
    assert(current -> prev == NULL);

    next = current -> next;
    free(current);
    current = next;

    while(current) {
        next = current -> next;
        coroutine_destroy(current);
        free(current);
        current = next;
    }

    list -> head = NULL;
    list -> tail = NULL;

    sem_destroy(&list -> elem_notify);
    pthread_mutex_destroy(&list -> lock);
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

void task_list_push_node(struct task_list *list, struct coroutine *node) {
    if(list -> concurrent) pthread_mutex_lock(&list -> lock);

    assert(list -> tail -> next == NULL);
    assert(node -> prev == NULL && node -> next == NULL);

    list -> tail -> next = node;
    node -> prev = list -> tail;
    list -> tail = node;

    if(list -> concurrent) pthread_mutex_unlock(&list -> lock);
    sem_post(&list -> elem_notify);
}

int task_list_is_empty(struct task_list *list) {
    int ret;

    if(list -> concurrent) pthread_mutex_lock(&list -> lock);

    assert(list -> head -> prev == NULL);
    if(list -> head -> next == NULL) {
        ret = 1;
    } else {
        ret = 0;
    }

    if(list -> concurrent) pthread_mutex_unlock(&list -> lock);

    return ret;
}

struct coroutine * task_list_pop_node(struct task_list *list) {
    struct coroutine *ret;

    __atomic_fetch_add(&list -> n_pop_awaiters, 1, __ATOMIC_RELAXED);

    sem_wait(&list -> elem_notify);
    if(list -> concurrent) pthread_mutex_lock(&list -> lock);

    __atomic_fetch_sub(&list -> n_pop_awaiters, 1, __ATOMIC_RELAXED);

    assert(list -> head -> prev == NULL);
    assert(list -> head -> next != NULL);
    ret = list -> head -> next;
    list -> head -> next = ret -> next;
    if(ret -> next) ret -> next -> prev = list -> head;
    ret -> prev = NULL;
    ret -> next = NULL;

    if(ret == list -> tail) {
        list -> tail = list -> head;
    }

    if(list -> concurrent) pthread_mutex_unlock(&list -> lock);
    return ret;
}

void task_pool_push_node(struct task_pool *pool, struct coroutine *node) {
    task_list_push_node(&pool -> tasks, node);
}

struct coroutine * task_pool_pop_node(struct task_pool *pool) {
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
    struct coroutine *current, *pinned;
    struct coroutine *target_crt;

    // There are two kinds of pinning:
    // 1) Temporary pinning (while a coroutine is being executed)
    //    indicated by the `pinned` local variable
    // 2) permanent pinning (specified by `target_crt -> pinned_scheduler`)

    has_perm_pinning = 0;
    pinned = NULL;
    assert(current_co == NULL); // nested schedulers are not allowed

    while(1) {
        if(pinned) {
            current = pinned;
        } else if(has_perm_pinning) {
            current = task_list_pop_node(&sch -> local_tasks);
            pinned = current;
        } else {
            current = task_pool_pop_node(sch -> pool);
            pinned = current;
            //printf("Pinning %p to scheduler %p\n", current, sch);
        }

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
            pinned = NULL;
        } else if(current -> async_detached) {
            target_crt = current;
            current = NULL;
            pinned = NULL;
            target_crt -> async_target(target_crt, target_crt -> async_user_data);
        } else {
            //task_pool_push_node(sch -> pool, current);
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
    struct coroutine *crt = malloc(sizeof(struct coroutine));

    coroutine_init(crt, stack_size, pool, entry, user_data);

    task_pool_push_node(pool, crt);
}

struct coroutine * current_coroutine() {
    return current_co;
}
