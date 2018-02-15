#ifndef _SCHED_HELPER_H_
#define _SCHED_HELPER_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*co_stack_initializer)(void *);

void yield_now(long *rsp_save_target, long *new_rsp);
void init_co_stack(long *rsp_save_target, long *new_rsp, co_stack_initializer initializer, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
