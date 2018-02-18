extern crate coroutines;

fn sleep_thread() {
    println!("sleep_thread enter");
    std::thread::sleep(std::time::Duration::from_millis(500));
    println!("sleep_thread exit");
}

fn main() {
    for _ in 0..5 {
        coroutines::spawn(sleep_thread);
    }

    std::thread::sleep(std::time::Duration::from_secs(1));
    println!("main exit");
}
