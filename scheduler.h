#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "sched_helper.h"

struct coroutine;

typedef void (*coroutine_entry)(struct coroutine *crt);

struct coroutine {
    char *stack_begin;
    char *stack_end;
    long local_rsp;
    long caller_rsp;
    int terminated;

    coroutine_entry entry;
    void *user_data;
};

void coroutine_yield(
    struct coroutine *crt
);

void coroutine_init(
    struct coroutine *crt,
    size_t stack_size,
    coroutine_entry entry,
    void *user_data
);

int coroutine_run(
    struct coroutine *crt
);

struct scheduler {
    struct coroutine_node *head;
};

void scheduler_init(struct scheduler *sch);
void scheduler_destroy(struct scheduler *sch);
void scheduler_take_coroutine(struct scheduler *sch, struct coroutine *crt);
void scheduler_run(struct scheduler *sch);

void start_coroutine(
    struct scheduler *sch,
    size_t stack_size,
    coroutine_entry entry,
    void *user_data
);

#endif
