#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>

// https://stackoverflow.com/questions/4792449/c0x-has-no-semaphores-how-to-synchronize-threads
class semaphore
{
private:
    std::mutex mutex_;
    std::condition_variable condition_;
    unsigned long count_ = 0; // Initialized as locked.

public:
    void notify() {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        ++count_;
        condition_.notify_one();
    }

    void wait() {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        while(!count_) // Handle spurious wake-ups.
            condition_.wait(lock);
        --count_;
    }

    bool try_wait() {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        if(count_) {
            --count_;
            return true;
        }
        return false;
    }
};

std::atomic<int> exec_count(0);

void chk_exec_count() {
    if(exec_count.load() == 7) {
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

void _async_target(struct coroutine *crt, void *user_data) {
    int *v = (int *) user_data;
    (*v)++;
    coroutine_async_exit(crt);
}

void async_calls(struct coroutine *crt) {
    int v = 0;
    for(int i = 0; i < 100000; i++) {
        coroutine_async_enter(crt, _async_target, (void *) &v);
    }
    printf("async_calls: v = %d\n", v);
    exec_count.fetch_add(1);
    chk_exec_count();
}

struct nested_context {
    std::atomic<int> count;
    semaphore notify;
    int target_value;
};

void _nested_co_inner(struct coroutine *crt) {
    nested_context *ctx = (nested_context *) crt -> user_data;
    int current = ctx -> count.fetch_add(1);
    if(current + 1 > ctx -> target_value) {
        abort();
    }
    if(current + 1 == ctx -> target_value) {
        ctx -> notify.notify();
    }
}

void nested_co(struct coroutine *crt) {
    nested_context ctx;
    ctx.target_value = 10000;

    for(int i = 0; i < ctx.target_value; i++) {
        start_coroutine(crt -> pool, 4096, _nested_co_inner, (void *) &ctx);
    }

    ctx.notify.wait();

    printf("nested_co OK\n");
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
    start_coroutine(&pool, 4096, async_calls, NULL);
    start_coroutine(&pool, 4096, nested_co, NULL);

    for(int i = 0; i < 4; i++) {
        std::thread([&pool]() {
            struct scheduler sch;
            scheduler_init(&sch, &pool);
            scheduler_run(&sch);
        }).detach();
    }

    while(1) {
        sleep(3600);
    }

    return 0;
}
