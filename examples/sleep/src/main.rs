extern crate coroutines;
use coroutines::Coroutine;

fn sleep_thread() {
    println!("sleep_thread enter");
    std::thread::sleep(std::time::Duration::from_millis(500));
    println!("sleep_thread exit");
}

fn main() {
    unsafe { coroutines::init(); }

    for _ in 0..5 {
        Coroutine::spawn(sleep_thread);
    }

    std::thread::sleep(std::time::Duration::from_secs(1));
    println!("main exit");
}
