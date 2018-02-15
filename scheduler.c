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

// To be used in the circular list.
struct coroutine_node {
    struct coroutine *crt;
    struct coroutine_node *prev;
    struct coroutine_node *next;
};

static void node_init(struct coroutine_node *node, struct coroutine_node *prev, struct coroutine_node *next) {
    node -> crt = NULL;
    node -> prev = prev;
    node -> next = next;
    prev -> next = node;
    next -> prev = node;
}

static void node_detach(struct coroutine_node *node) {
    node -> prev -> next = node -> next;
    node -> next -> prev = node -> prev;
    node -> prev = NULL;
    node -> next = NULL;
}

static void node_destroy(struct coroutine_node *node) {
    assert(node -> prev == NULL && node -> next == NULL); // detached
    coroutine_destroy(node -> crt);
    free(node -> crt);
    node -> crt = NULL;
}

void scheduler_init(struct scheduler *sch) {
    sch -> head = malloc(sizeof(struct coroutine_node));
    node_init(sch -> head, sch -> head, sch -> head);
}

void scheduler_destroy(struct scheduler *sch) {
    struct coroutine_node *current, *next;

    current = sch -> head -> next;
    while(current != sch -> head) {
        next = current -> next;
        coroutine_destroy(current -> crt);
        free(current);
        current = next;
    }
    free(sch -> head);
}

void scheduler_take_coroutine(struct scheduler *sch, struct coroutine *crt) {
    struct coroutine_node *node;

    node = malloc(sizeof(struct coroutine_node));
    node_init(node, sch -> head -> prev, sch -> head);
    node -> crt = crt;
}

void scheduler_run(struct scheduler *sch) {
    struct coroutine_node *current, *next;
    int terminated;

    current = NULL;
    next = sch -> head;

    while(1) {
        current = next;
        next = current -> next; // `current` may be modified
        if(current == next) {
            if(current == sch -> head) break;
            else {
                fprintf(stderr, "ERROR: Internal data corrupted\n");
                abort();
            }
        }
        if(current == sch -> head) continue;

        terminated = coroutine_run(current -> crt);
        if(terminated) {
            node_detach(current);
            node_destroy(current);
            free(current);
        }
    }
}
