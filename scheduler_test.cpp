#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"
#include <thread>
#include <atomic>

std::atomic<int> exec_count(0);

void chk_exec_count() {
    if(exec_count.load() == 5) {
        exit(0);
    }
}

void crt1_fn(struct coroutine *crt) {
    long long sum = 0;
    int i;

    for(i = 1; i <= 10000000; i++) {
        sum += (long long) i;
        coroutine_yield(crt);
    }

    printf("%lld\n", sum);
    exec_count.fetch_add(1);
    chk_exec_count();
}

void crt2_fn(struct coroutine *crt) {
    unsigned long long result = 1;
    for(int i = 1; i <= 10000000; i++) {
        result *= (unsigned long long) i;
        if(result == 0) result = 1;
        coroutine_yield(crt);
    }
    printf("%llu\n", result);
    exec_count.fetch_add(1);
    chk_exec_count();
}

int fib(struct coroutine *crt, int n) {
    if(n == 1 || n == 2) {
        return 1;
    }
    coroutine_yield(crt);
    return fib(crt, n - 1) + fib(crt, n - 2);
}

void fib_n(struct coroutine *crt) {
    int n = (int) (long) crt -> user_data;
    int result = fib(crt, n);
    printf("fib(%d) = %d\n", n, result);
    exec_count.fetch_add(1);
    chk_exec_count();
}

int main() {
    struct task_pool pool;
    task_pool_init(&pool, 1);

    start_coroutine(&pool, 4096, crt1_fn, NULL);
    start_coroutine(&pool, 4096, crt2_fn, NULL);
    start_coroutine(&pool, 4096, fib_n, (void *) 37);
    start_coroutine(&pool, 4096, fib_n, (void *) 32);
    start_coroutine(&pool, 4096, fib_n, (void *) 27);

    for(int i = 0; i < 3; i++) {
        std::thread([&pool]() {
            struct scheduler sch;
            scheduler_init(&sch, &pool);
            scheduler_run(&sch);
        }).detach();
    }

    struct scheduler sch;
    scheduler_init(&sch, &pool);
    scheduler_run(&sch);

    scheduler_destroy(&sch);
    task_pool_destroy(&pool);

    return 0;
}
