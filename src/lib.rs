pub mod promise;

use std::cell::RefCell;
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

struct CoroutineEntry {
    entry: Box<Fn() + Send + 'static>
}

extern "C" fn _launch(co: *const CoroutineImpl) {
    let target = unsafe { Box::from_raw(
        extract_co_user_data(co) as *const CoroutineEntry as *mut CoroutineEntry
    ) };
    let entry = target.entry;
    (entry)();
}

pub fn spawn<F: FnOnce() + Send + 'static>(entry: F) {
    let entry = RefCell::new(Some(entry));
    let co = Box::new(CoroutineEntry {
        entry: Box::new(move || {
            (entry.borrow_mut().take().unwrap())();
        })
    });
    unsafe {
        launch_co(
            _launch,
            Box::into_raw(co) as *const AnyUserData
        );
    }
}

pub fn spawn_inherit<F: FnOnce() + Send + 'static>(entry: F) {
    if unsafe { current_coroutine() }.is_null() {
        ::std::thread::spawn(entry);
    } else {
        spawn(entry);
    }
}

pub fn global_event_count() -> usize {
    unsafe {
        co_get_global_event_count()
    }
}
