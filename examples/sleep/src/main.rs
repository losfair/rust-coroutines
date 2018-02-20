extern crate coroutines;

use coroutines::JoinHandle;

fn sleep_thread() {
    println!("sleep_thread enter");
    std::thread::sleep(std::time::Duration::from_millis(500));
    println!("sleep_thread exit");
}

fn main() {
    let handles: Vec<JoinHandle<()>> = (0..5)
        .map(|_| coroutines::spawn(sleep_thread))
        .collect();
    for h in handles {
        h.join().unwrap();
    }
}
