extern crate coroutines;
use std::panic::catch_unwind;

fn panic_thread() {
    fn inner_fn() {
        panic!("Oops!");
    }

    let ret = catch_unwind(|| inner_fn());
    assert!(ret.is_err());
}

fn main() {
    for _ in 0..200000 {
        coroutines::spawn(panic_thread);
    }

    std::thread::sleep(std::time::Duration::from_secs(100));
}
