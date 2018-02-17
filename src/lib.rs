#![feature(fnbox)]
#![feature(libc)]

extern crate libc;
use std::boxed::FnBox;
use std::ffi::CString;

#[repr(C)]
struct CoroutineImpl {
    _dummy: usize
}

#[repr(C)]
struct AnyUserData {
    _dummy: usize
}

#[allow(non_upper_case_globals)]
static mut launch_co: Option<extern "C" fn(
    entry: extern "C" fn (*const CoroutineImpl),
    user_data: *const AnyUserData
)> = None;

#[allow(non_upper_case_globals)]
static mut extract_co_user_data: Option<extern "C" fn(
    co: *const CoroutineImpl
) -> *const AnyUserData> = None;

static mut INITIALIZED: bool = false;

pub struct Coroutine {
    entry: Option<Box<FnBox()>>
}

impl Coroutine {
    extern "C" fn _launch(co: *const CoroutineImpl) {
        let mut target = unsafe { Box::from_raw(
            (extract_co_user_data.unwrap())(co) as *const Coroutine as *mut Coroutine
        ) };
        let entry = ::std::mem::replace(&mut target.entry, None).unwrap();
        (entry)();
    }

    pub fn spawn<F: FnOnce() + Send + 'static>(entry: F) {
        if !unsafe { INITIALIZED } {
            panic!("Calling spawn() without global initialization");
        }

        if unsafe { launch_co.is_none() } {
            ::std::thread::spawn(entry);
            return;
        }
        let co = Box::new(Coroutine {
            entry: Some(Box::new(entry))
        });
        unsafe {
            (launch_co.unwrap())(
                Coroutine::_launch,
                Box::into_raw(co) as *const AnyUserData
            );
        }
    }
}

pub unsafe fn init() {
    INITIALIZED = true;

    let handle = libc::dlopen(::std::ptr::null(), 1 /* RTLD_LAZY */);
    assert!(!handle.is_null());

    let launch_co_name = CString::new("launch_co").unwrap();
    let extract_co_user_data_name = CString::new("extract_co_user_data").unwrap();

    let launch_co_ptr = libc::dlsym(handle, launch_co_name.as_ptr());
    let extract_co_user_data_ptr = libc::dlsym(handle, extract_co_user_data_name.as_ptr());

    if launch_co_ptr.is_null() || extract_co_user_data_ptr.is_null() {
        eprintln!("Warning: Runtime patch is not found. spawn() will fallback to native threads.");
        return;
    }

    launch_co = Some(::std::mem::transmute(launch_co_ptr));
    extract_co_user_data = Some(::std::mem::transmute(extract_co_user_data_ptr));
}
