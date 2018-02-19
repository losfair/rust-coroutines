extern crate coroutines;
use std::sync::atomic::{Ordering, AtomicUsize, ATOMIC_USIZE_INIT};
use std::panic::catch_unwind;

static PANIC_COUNT: AtomicUsize = ATOMIC_USIZE_INIT;

fn panic_thread() {
    fn inner_fn() {
        panic!("Oops!");
    }

    let ret = catch_unwind(|| inner_fn());
    assert!(ret.is_err());

    let id = PANIC_COUNT.fetch_add(1, Ordering::SeqCst);
    println!("OK {}", id);

    let id = PANIC_COUNT.load(Ordering::SeqCst);
    if id == 200000 {
        std::process::exit(0);
    }
}

fn main() {
    std::panic::set_hook(Box::new(|_| {}));
    for _ in 0..200000 {
        coroutines::spawn(panic_thread);
    }

    std::thread::sleep(std::time::Duration::from_secs(100));
}
