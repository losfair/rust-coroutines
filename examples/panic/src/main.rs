extern crate coroutines;
use coroutines::JoinHandle;

use std::panic::catch_unwind;

fn panic_thread(id: usize) {
    fn inner_fn() {
        panic!("Oops!");
    }

    let ret = catch_unwind(|| inner_fn());
    assert!(ret.is_err());

    println!("OK {}", id);
}

fn main() {
    std::panic::set_hook(Box::new(|_| {}));
    let handles: Vec<JoinHandle<()>> = (0..200000)
        .map(|i| coroutines::spawn(move || panic_thread(i)))
        .collect();
    for h in handles {
        h.join().unwrap();
    }
}
