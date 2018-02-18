#![feature(fnbox)]
use std::boxed::FnBox;

#[repr(C)]
struct CoroutineImpl {
    _dummy: usize
}

#[repr(C)]
struct AnyUserData {
    _dummy: usize
}

#[link(name = "unblock_hook", kind = "dylib")]
extern "C" {
    fn current_coroutine() -> *const CoroutineImpl;
    fn launch_co(
        entry: extern "C" fn (*const CoroutineImpl),
        user_data: *const AnyUserData
    );
    fn extract_co_user_data(
        co: *const CoroutineImpl
    ) -> *const AnyUserData;
    fn co_get_global_event_count() -> usize;
}

struct CoroutineEntry {
    entry: Box<FnBox() + Send + 'static>
}

extern "C" fn _launch(co: *const CoroutineImpl) {
    let mut target = unsafe { Box::from_raw(
        extract_co_user_data(co) as *const CoroutineEntry as *mut CoroutineEntry
    ) };
    let entry = target.entry;
    (entry)();
}

pub fn spawn<F: FnOnce() + Send + 'static>(entry: F) {
    let co = Box::new(CoroutineEntry {
        entry: Box::new(entry)
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
