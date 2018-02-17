#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/timerfd.h>
#include "scheduler.h"

static struct task_pool global_pool;
static int epoll_fd;

static ssize_t (*realFwrite)(int fd, const void *buf, size_t nbytes);
static ssize_t (*realFread)(int fd, void *buf, size_t nbytes);
static int (*realFopen)(const char *file, int oflag);
static int (*realFclose)(int fd);
static int (*realFnanosleep)(
    const struct timespec *requested_time,
    struct timespec *remaining
);
static int (*realFpthread_cancel)(pthread_t);
static int (*realFpthread_create)(
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg
);
static int (*realFpthread_detach)(pthread_t thread);
static int (*realFpthread_equal)(pthread_t left, pthread_t right);
static int (*realFpthread_getschedparam)(pthread_t, int *, struct sched_param *);
static int (*realFpthread_join)(pthread_t, void **);
static pthread_t (*realFpthread_self)();
static int (*realFpthread_setschedparam)(pthread_t, int, const struct sched_param *);

static void * _run_scheduler(void *raw_sch) {
    struct scheduler *sch = (struct scheduler *) raw_sch;
    scheduler_run(sch);
    return NULL;
}

static void * _do_poll(void *unused) {
    int n_ready;
    struct epoll_event ev;

    while(1) {
        n_ready = epoll_wait(epoll_fd, &ev, 1, -1);
        if(n_ready == 0) continue;
        assert(n_ready == 1);

        struct coroutine *crt = (struct coroutine *) ev.data.ptr;
        coroutine_async_exit(crt);
    }
}

static void __attribute__((constructor)) __init() {
    int i;
    int num_cpus;
    struct scheduler *sch;
    pthread_t tinfo;

    realFwrite = dlsym(RTLD_NEXT, "write");
    realFread = dlsym(RTLD_NEXT, "read");
    realFopen = dlsym(RTLD_NEXT, "open");
    realFclose = dlsym(RTLD_NEXT, "close");
    realFnanosleep = dlsym(RTLD_NEXT, "nanosleep");
    realFpthread_cancel = dlsym(RTLD_NEXT, "pthread_cancel");
    realFpthread_create = dlsym(RTLD_NEXT, "pthread_create");
    realFpthread_detach = dlsym(RTLD_NEXT, "pthread_detach");
    realFpthread_equal = dlsym(RTLD_NEXT, "pthread_equal");
    realFpthread_getschedparam = dlsym(RTLD_NEXT, "pthread_getschedparam");
    realFpthread_join = dlsym(RTLD_NEXT, "pthread_join");
    realFpthread_self = dlsym(RTLD_NEXT, "pthread_self");
    realFpthread_setschedparam = dlsym(RTLD_NEXT, "pthread_setschedparam");

    task_pool_init(&global_pool, 1);
    num_cpus = get_nprocs();
    for(i = 0; i < num_cpus; i++) {
        sch = malloc(sizeof(struct scheduler));
        scheduler_init(sch, &global_pool);
        realFpthread_create(&tinfo, NULL, _run_scheduler, (void *) sch);
    }
    sch = NULL;

    epoll_fd = epoll_create(1);
    assert(epoll_fd >= 0);

    realFpthread_create(&tinfo, NULL, _do_poll, NULL);
}

static inline void try_yield() {
    return;
    printf("try_yield()\n");

    struct coroutine *co = current_coroutine();
    if(co) {
        coroutine_yield(co);
    }
}

ssize_t write(int fd, const void *buf, size_t nbytes) {
    try_yield();
    return realFwrite(fd, buf, nbytes);
}

ssize_t read(int fd, void *buf, size_t nbytes) {
    try_yield();
    return realFread(fd, buf, nbytes);
}

int open(const char *file, int oflag) {
    try_yield();
    return realFopen(file, oflag);
}

int close(int fd) {
    try_yield();
    return realFclose(fd);
}

struct nanosleep_context {
    int tfd; // out
    const struct timespec *requested_time; // in
};

static void _enter_nanosleep(struct coroutine *co, void *raw_context) {
    struct nanosleep_context *context = (struct nanosleep_context *) raw_context;

    const struct timespec *requested_time = context -> requested_time;
    struct itimerspec ts;
    struct epoll_event ev;

    context -> tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    assert(context -> tfd >= 0);

    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = requested_time -> tv_sec;
    ts.it_value.tv_nsec = requested_time -> tv_nsec;

    assert(
        timerfd_settime(context -> tfd, 0, &ts, NULL) >= 0
    );

    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = (void *) co;
    assert(epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        context -> tfd,
        &ev
    ) >= 0);
}

int nanosleep(
    const struct timespec *requested_time,
    struct timespec *remaining
) {
    struct coroutine *co;
    struct nanosleep_context context;

    co = current_coroutine();
    if(co == NULL) {
        return realFnanosleep(requested_time, remaining);
    }

    context.requested_time = requested_time;
    context.tfd = -1;

    coroutine_async_enter(co, _enter_nanosleep, (void *) &context);
    realFclose(context.tfd);
    return 0;
}

struct fake_thread_info {
    void *(*start_routine)(void *);
    void *arg;
};

void fake_thread_entry(struct coroutine *co) {
    struct fake_thread_info *info = (struct fake_thread_info *) co -> user_data;
    info -> start_routine(info -> arg);
    free(info);
}

int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg
) {
    struct fake_thread_info *info = (struct fake_thread_info *) malloc(sizeof(struct fake_thread_info));
    info -> start_routine = start_routine;
    info -> arg = arg;
    start_coroutine(&global_pool, 8192, fake_thread_entry, (void *) info);
    return 0;
}

int pthread_cancel(pthread_t thread) {
    printf("pthread_cancel\n");
    abort();
}

int pthread_detach(pthread_t thread) {
    /*printf("pthread_detach\n");
    abort();*/
    return 0;
}

int pthread_equal(pthread_t left, pthread_t right) {
    printf("pthread_equal\n");
    abort();
}

int pthread_getschedparam(pthread_t thread, int *p1, struct sched_param *p2) {
    printf("pthread_getschedparam\n");
    abort();
}

int pthread_join(pthread_t thread, void **p1) {
    printf("pthread_join\n");
    abort();
}

/*
pthread_t pthread_self() {
    //printf("pthread_self\n");
    abort();
}
*/

int pthread_setschedparam(pthread_t thread, int p1, const struct sched_param *p2) {
    //printf("pthread_setschedparam\n");
    abort();
}
