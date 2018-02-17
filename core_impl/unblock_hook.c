#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <errno.h>
#include "scheduler.h"

#define MAX_N_EPOLL_EVENTS 16

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
static int (*realFpthread_key_create)(pthread_key_t *__key, void (*__destructor) (void *));
static int (*realFpthread_key_delete)(pthread_key_t __key);
static void * (*realFpthread_getspecific)(pthread_key_t __key);
static int (*realFpthread_setspecific)(pthread_key_t __key, const void *__value);
static int (*realFsocket)(int domain, int type, int protocol);
static int (*realFbind)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
static int (*realFlisten)(int sockfd, int backlog);
static int (*realFaccept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static int (*realFaccept4)(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);

static void * _run_scheduler(void *raw_sch) {
    struct scheduler *sch = (struct scheduler *) raw_sch;
    scheduler_run(sch);
    return NULL;
}

struct co_poll_context {
    struct coroutine *co;
    struct epoll_event ev;
};

static void * _do_poll(void *unused) {
    int i;
    int n_ready;
    struct epoll_event ev[MAX_N_EPOLL_EVENTS];
    struct co_poll_context *req;
    struct coroutine *co;

    while(1) {
        n_ready = epoll_wait(epoll_fd, ev, MAX_N_EPOLL_EVENTS, -1);
        for(i = 0; i < n_ready; i++) {
            req = ev[i].data.ptr;
            if(!req -> co) {
                continue;
            }
            co = req -> co;
            req -> co = NULL; // `req` should be released by the requesting coroutine
            memcpy(&req -> ev, &ev[i], sizeof(struct epoll_event));
            coroutine_async_exit(co, req);
        }
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
    realFpthread_key_create = dlsym(RTLD_NEXT, "pthread_key_create");
    realFpthread_key_delete = dlsym(RTLD_NEXT, "pthread_key_delete");
    realFpthread_getspecific = dlsym(RTLD_NEXT, "pthread_getspecific");
    realFpthread_setspecific = dlsym(RTLD_NEXT, "pthread_setspecific");
    realFsocket = dlsym(RTLD_NEXT, "socket");
    realFbind = dlsym(RTLD_NEXT, "bind");
    realFlisten = dlsym(RTLD_NEXT, "listen");
    realFaccept = dlsym(RTLD_NEXT, "accept");
    realFaccept4 = dlsym(RTLD_NEXT, "accept4");

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

    struct co_poll_context *pc = malloc(sizeof(struct co_poll_context));
    pc -> co = co;

    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = pc;

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
    struct co_poll_context *ctx;

    co = current_coroutine();
    if(co == NULL) {
        return realFnanosleep(requested_time, remaining);
    }

    context.requested_time = requested_time;
    context.tfd = -1;

    ctx = coroutine_async_enter(co, _enter_nanosleep, (void *) &context);
    free(ctx);

    assert(epoll_ctl(
        epoll_fd,
        EPOLL_CTL_DEL,
        context.tfd,
        NULL
    ) >= 0);
    realFclose(context.tfd);
    return 0;
}

int socket(int domain, int type, int protocol) {
    struct coroutine *co;
    int fd;

    co = current_coroutine();
    if(co == NULL) {
        return realFsocket(domain, type, protocol);
    }

    type |= SOCK_NONBLOCK;
    fd = realFsocket(domain, type, protocol);
    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return realFbind(sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog) {
    return realFlisten(sockfd, backlog);
}

struct accept4_context {
    int sockfd;
    struct sockaddr *addr;
    socklen_t *addrlen;
    int flags;
};

static void _enter_accept4(struct coroutine *co, void *raw_context) {
    int status;
    struct epoll_event ev;
    struct accept4_context *context = raw_context;

    struct co_poll_context *pc = malloc(sizeof(struct co_poll_context));
    pc -> co = co;

    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = pc;

    status = epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        context -> sockfd,
        &ev
    );
    assert(status >= 0);
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    struct coroutine *co;
    int status;
    int connfd;
    struct accept4_context context;
    struct co_poll_context *pc;
    struct epoll_event ev_builder;

    co = current_coroutine();
    if(co == NULL) {
        return realFaccept(sockfd, addr, addrlen);
    }

    context.sockfd = sockfd;
    context.addr = addr;
    context.addrlen = addrlen;
    context.flags = flags | SOCK_NONBLOCK;

    status = realFaccept4(context.sockfd, context.addr, context.addrlen, context.flags);
    while(status < 0) {
        if(errno != EAGAIN && errno != EWOULDBLOCK) {
            return status;
        }

        pc = coroutine_async_enter(co, _enter_accept4, &context);
        free(pc);

        assert(epoll_ctl(
            epoll_fd,
            EPOLL_CTL_DEL,
            sockfd,
            NULL
        ) >= 0);

        status = realFaccept4(context.sockfd, context.addr, context.addrlen, context.flags);
    }

    return status;
}

void launch_co(
    coroutine_entry entry,
    void *user_data
) {
    start_coroutine(&global_pool, 8192, entry, user_data);
}

void * extract_co_user_data(
    struct coroutine *co
) {
    return co -> user_data;
}
