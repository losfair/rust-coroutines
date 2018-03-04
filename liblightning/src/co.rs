use platform;

use std::os::raw;
use std::any::Any;
use std::panic::{catch_unwind, resume_unwind, AssertUnwindSafe};

pub type StackInitializer = extern "C" fn (user_data: *mut raw::c_void);

#[link(name = "lightning_platform", kind = "static")]
extern "C" {
    fn __ll_co_yield_now(rsp_save_target: *mut usize, new_rsp: usize);
    fn __ll_init_co_stack(
        rsp_save_target: *mut usize,
        new_rsp: usize,
        initializer: StackInitializer,
        user_data: *mut raw::c_void
    );
}

#[derive(Eq, PartialEq)]
enum RunningState {
    NotStarted,
    Running,
    Terminated
}

pub struct CoState<F: FnOnce(&mut Yieldable)> {
    stack: *mut [u8],
    rsp: usize,
    inside: bool,
    yield_val: Option<*const Any>,
    error_val: Option<Box<Any + Send>>,
    running_state: RunningState,
    f: Option<F>
}

pub trait Yieldable {
    fn yield_now(&mut self, val: &Any);
}

impl<F: FnOnce(&mut Yieldable)> Yieldable for CoState<F> {
    fn yield_now(&mut self, val: &Any) {
        if !self.inside {
            panic!("Yielding out from a coroutine outside itself");
        }
        self.inside = false;
        unsafe {
            self.yield_val = Some(val as *const Any);

            let new_rsp = self.rsp;
            __ll_co_yield_now(&mut self.rsp, new_rsp);
        }
    }
}

impl<F: FnOnce(&mut Yieldable)> CoState<F> {
    pub fn new(stack_size: usize, f: F) -> CoState<F> {
        let stack = platform::setup_stack(stack_size + *platform::PAGE_SIZE);
        unsafe {
            platform::setup_stack_guard_page(stack);
        }

        // The actual stack size may be greater than the requested size
        // So we use the actual size here.
        let rsp: usize = {
            let stack = unsafe { &mut *stack };
            &mut stack[0] as *mut u8 as usize + stack.len()
        };

        CoState {
            stack: stack,
            rsp: rsp,
            inside: false,
            yield_val: None,
            error_val: None,
            running_state: RunningState::NotStarted,
            f: Some(f)
        }
    }

    extern "C" fn co_initializer(user_data: *mut raw::c_void) {
        let this: &mut Self = unsafe { &mut *(user_data as *mut Self) };
        {
            let f = this.f.take().unwrap();
            if let Err(e) = catch_unwind(AssertUnwindSafe(|| f(this))) {
                this.error_val = Some(e);
            }
        }

        // No droppable objects should remain at this point.
        // Otherwise there will be a resource leak.
        this.terminate();
    }

    pub fn resume<'a>(&'a mut self) -> Option<&'a Any> {
        if self.inside {
            panic!("Entering a CoState within itself");
        }
        unsafe {
            let new_rsp = self.rsp;

            match self.running_state {
                RunningState::NotStarted => {
                    self.inside = true;
                    self.running_state = RunningState::Running;
                    let rsp = &mut self.rsp as *mut usize;
                    let self_raw = self as *mut Self as *mut raw::c_void;
                    __ll_init_co_stack(rsp, new_rsp, Self::co_initializer, self_raw);

                    if let Some(e) = self.error_val.take() {
                        resume_unwind(e);
                    }

                    self.yield_val.take().map(|v| &*v)
                },
                RunningState::Running => {
                    self.inside = true;
                    __ll_co_yield_now(&mut self.rsp, new_rsp);

                    if let Some(e) = self.error_val.take() {
                        resume_unwind(e);
                    }

                    self.yield_val.take().map(|v| &*v)
                },
                RunningState::Terminated => None
            }
        }
    }

    fn terminate(&mut self) -> ! {
        if !self.inside {
            panic!("[yield_terminate] Yielding out from a coroutine outside itself");
        }
        self.inside = false;

        self.running_state = RunningState::Terminated;

        unsafe {
            self.yield_val = None;

            let new_rsp = self.rsp;
            __ll_co_yield_now(&mut self.rsp, new_rsp);
        }

        eprintln!("Coroutine termination failed");
        ::std::process::abort();
    }

    
}

impl<F: FnOnce(&mut Yieldable)> Drop for CoState<F> {
    fn drop(&mut self) {
        if self.inside {
            eprintln!("CoState dropped before leaving");
            ::std::process::abort();
        }
        // This will only cause a resource leak, which is considered "safe"
        if self.running_state != RunningState::Terminated {
            panic!("Coroutine dropped before termination");
        }
        unsafe {
            platform::free_stack(self.stack);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn yield_should_work() {
        let mut co = CoState::new(4096, |c| {
            c.yield_now(&42i32 as &Any);
        });

        assert!(*co.resume().unwrap().downcast_ref::<i32>().unwrap() == 42);
        assert!(co.resume().is_none());
    }

    #[test]
    fn nested_should_work() {
        let mut co = CoState::new(4096, |c| {
            let mut co = CoState::new(4096, |c| {
                c.yield_now(&42i32 as &Any);
            });
            let v = *co.resume().unwrap().downcast_ref::<i32>().unwrap() + 1;
            co.resume();
            c.yield_now(&v as &Any);
        });

        assert!(*co.resume().unwrap().downcast_ref::<i32>().unwrap() == 43);
        assert!(co.resume().is_none());
    }

    #[test]
    fn panics_should_propagate() {
        // Use a larger stack size here to make backtrace work
        let mut co = CoState::new(16384, |_| {
            panic!("Test panic");
        });
        let e = catch_unwind(AssertUnwindSafe(|| {
            co.resume();
        })).err().unwrap();
        let v: &&'static str = e.downcast_ref().unwrap();
        assert_eq!(*v, "Test panic");
    }

    #[test]
    fn instant_termination_should_work() {
        let mut co = CoState::new(4096, |_| {});
        assert!(co.resume().is_none());
    }

    #[test]
    fn resume_terminated_should_return_none() {
        let mut co = CoState::new(4096, |_| {});
        assert!(co.resume().is_none());
        assert!(co.resume().is_none());
    }

    // The correct behavior for these two tests is to segfault with
    // a bad permissions error.
    /*#[test]
    fn stack_overflow() {
        fn inner(i: i32) -> i32 {
            if i == 42 {
                inner(42)
            } else {
                0
            }
        }

        let mut co = CoState::new(4096, |_| {
            inner(42);
        });
        co.resume();
    }

    #[test]
    fn really_big_stack_overflow() {
        fn inner(v: i32) -> i32 {
            let mut arr: [i32; 8192] = [0; 8192];
            arr[0] = v;
            for i in 1..8192 {
                arr[i] = arr[i - 1] + arr[8192 - i - 1] + 1;
            }
            arr[1000]
        }
        let mut co = CoState::new(4096, |_| {
            inner(42);
        });
        co.resume();
    }*/
}
