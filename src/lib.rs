extern crate spin;

pub mod promise;

use std::any::Any;
use std::sync::Arc;
use std::panic::{catch_unwind, AssertUnwindSafe};
pub use promise::Promise;

#[repr(C)]
pub(crate) struct CoroutineImpl {
    _dummy: usize
}

#[repr(C)]
pub(crate) struct AnyUserData {
    _dummy: usize
}

pub(crate) type AsyncEntry = extern "C" fn (co: *const CoroutineImpl, data: *const AnyUserData);

#[link(name = "unblock_hook", kind = "dylib")]
extern "C" {
    pub(crate) fn current_coroutine() -> *const CoroutineImpl;
    fn launch_co(
        entry: extern "C" fn (*const CoroutineImpl),
        user_data: *const AnyUserData
    );
    fn extract_co_user_data(
        co: *const CoroutineImpl
    ) -> *const AnyUserData;
    fn co_get_global_event_count() -> usize;

    fn coroutine_yield(
        co: *const CoroutineImpl
    );

    fn gtp_enable_work_stealing();
    fn gtp_disable_work_stealing();
    fn gtp_get_migration_count() -> i32;

    pub(crate) fn coroutine_async_enter(
        co: *const CoroutineImpl,
        entry: AsyncEntry,
        data: *const AnyUserData
    ) -> *const AnyUserData;
    
    pub(crate) fn coroutine_async_exit(
        co: *const CoroutineImpl,
        data: *const AnyUserData
    );
}

struct CoroutineEntry<
    T: Send + 'static,
    F: FnOnce() -> T + Send + 'static,
    E: FnOnce(Result<T, Box<Any + Send>>) + Send + 'static
> {
    entry: Option<F>,
    on_exit: Option<E>
}

extern "C" fn _launch<
    T: Send + 'static,
    F: FnOnce() -> T + Send + 'static,
    E: FnOnce(Result<T, Box<Any + Send>>) + Send + 'static
>(co: *const CoroutineImpl) {
    let mut target = unsafe { Box::from_raw(
        extract_co_user_data(co) as *const CoroutineEntry<T, F, E> as *mut CoroutineEntry<T, F, E>
    ) };
    let entry = target.entry.take().unwrap();
    let ret = catch_unwind(AssertUnwindSafe(move || (entry)()));
    (target.on_exit.take().unwrap())(ret);
}

fn spawn_with_callback<
    T: Send + 'static,
    F: FnOnce() -> T + Send + 'static,
    E: FnOnce(Result<T, Box<Any + Send>>) + Send + 'static
>(entry: F, cb: E) {
    let co = Box::new(CoroutineEntry {
        entry: Some(entry),
        on_exit: Some(cb)
    });
    unsafe {
        launch_co(
            _launch::<T, F, E>,
            Box::into_raw(co) as *const AnyUserData
        );
    }
}

/// A handle used to wait on a coroutine's termination.
pub struct JoinHandle<T: Send + 'static> {
    state: Arc<spin::Mutex<JoinHandleState<T>>>
}

impl<T: Send + 'static> JoinHandle<T> {
    fn priv_clone(&self) -> JoinHandle<T> {
        JoinHandle {
            state: self.state.clone()
        }
    }

    /// Waits for the associated coroutine to finish.
    ///
    /// If the associated coroutine has already terminated,
    /// `join` returns instantly with the result.
    /// Otherwise, `join` waits until the coroutine terminates.
    ///
    /// If the child coroutine panics, `Err` is returned with the
    /// boxed value passed to `panic`. Otherwise, `Ok` is returned
    /// with the return value of the closure executed in the coroutine.
    pub fn join(self) -> Result<T, Box<Any + Send>> {
        Promise::await(move |p| {
            let mut state = self.state.lock();
            let result = match ::std::mem::replace(&mut *state, JoinHandleState::Empty) {
                JoinHandleState::Empty => None,
                JoinHandleState::Done(v) => Some(v),
                JoinHandleState::Pending(_) => unreachable!()
            };
            if let Some(result) = result {
                drop(state);
                p.resolve(result);
            } else {
                *state = JoinHandleState::Pending(p);
            }
        })
    }
}

enum JoinHandleState<T: Send + 'static> {
    Empty,
    Done(Result<T, Box<Any + Send>>),
    Pending(Promise<Result<T, Box<Any + Send>>>)
}

/// Spawns a coroutine without building a `JoinHandle`.
///
/// This may be faster than `spawn` in some cases.
pub fn fast_spawn<T: Send + 'static, F: FnOnce() -> T + Send + 'static>(entry: F) {
    spawn_with_callback(entry, |_| {});
}

/// Spawns a coroutine and returns its `JoinHandle`.
pub fn spawn<T: Send + 'static, F: FnOnce() -> T + Send + 'static>(entry: F) -> JoinHandle<T> {
    let handle = JoinHandle {
        state: Arc::new(spin::Mutex::new(JoinHandleState::Empty as JoinHandleState<T>))
    };
    let handle2 = handle.priv_clone();
    spawn_with_callback(entry, move |ret| {
        let mut ret = Some(ret);
        let mut resolve_target: Option<Promise<Result<T, Box<Any + Send>>>> = None;

        let mut state = handle2.state.lock();
        let new_state = match ::std::mem::replace(&mut *state, JoinHandleState::Empty) {
            JoinHandleState::Empty => JoinHandleState::Done(ret.take().unwrap()),
            JoinHandleState::Pending(p) => {
                resolve_target = Some(p);
                JoinHandleState::Empty
            },
            JoinHandleState::Done(_) => unreachable!()
        };
        *state = new_state;
        drop(state);

        if let Some(p) = resolve_target {
            p.resolve(ret.take().unwrap());
        }
    });
    handle
}

/// Deprecated.
///
/// Spawns another coroutine if called inside a coroutine,
/// or an OS thread otherwise.
pub fn spawn_inherit<T: Send + 'static, F: FnOnce() -> T + Send + 'static>(entry: F) {
    if unsafe { current_coroutine() }.is_null() {
        ::std::thread::spawn(entry);
    } else {
        spawn(entry);
    }
}

/// Yields out of the current coroutine and allows the scheduler
/// to execute other coroutines.
pub fn yield_now() {
    let co = unsafe { current_coroutine() };
    if !co.is_null() {
        unsafe {
            coroutine_yield(co);
        }
    }
}

/// Returns the global event count.
pub fn global_event_count() -> usize {
    unsafe {
        co_get_global_event_count()
    }
}

/// Enable / disable coroutine migration ("work stealing")
/// globally. (disabled by default)
///
/// This should be used with care because migrating values of
/// non-Send types might break Rust's safety guarantee.
pub unsafe fn set_work_stealing(enabled: bool) {
    if enabled {
        gtp_enable_work_stealing();
    } else {
        gtp_disable_work_stealing();
    }
}

/// Returns the current global migration count.
pub fn migration_count() -> usize {
    unsafe {
        gtp_get_migration_count() as usize
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    #[test]
    fn test_spawn_join_instant() {
        super::spawn(move || {
            let handle = super::spawn(|| {
                42
            });
            ::std::thread::sleep(Duration::from_millis(50));
            let v: i32 = handle.join().unwrap();
            assert!(v == 42);
        }).join().unwrap();
    }

    #[test]
    fn test_spawn_join_deferred() {
        super::spawn(move || {
            let handle = super::spawn(|| {
                ::std::thread::sleep(Duration::from_millis(50));
                42
            });
            let v: i32 = handle.join().unwrap();
            assert!(v == 42);
        }).join().unwrap();
    }
}
