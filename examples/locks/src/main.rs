extern crate coroutines;

use std::time::Duration;
use std::sync::{Arc, Mutex, RwLock};

fn run_mutexes() {
    println!("[mutex] Outer enter");
    let m = Arc::new(Mutex::new(false));
    let m2 = m.clone();
    let _inner = m.lock().unwrap();
    coroutines::spawn(move || {
        println!("[mutex] Inner enter");
        let _inner = m2.lock().unwrap();
        println!("[mutex] Inner exit");
    });
    std::thread::sleep(Duration::from_millis(500));
    println!("[mutex] Outer exit");
}

fn run_rwlocks() {
    println!("[rwlock] Outer enter");
    let m = Arc::new(RwLock::new(false));
    let m2 = m.clone();
    let _inner = m.read().unwrap();
    coroutines::spawn(move || {
        println!("[rwlock] Inner enter");
        let _inner = m2.write().unwrap();
        println!("[rwlock] Inner exit");
    });
    std::thread::sleep(Duration::from_millis(500));
    println!("[rwlock] Outer exit");
}

fn main() {
    for _ in 0..10 {
        coroutines::spawn(run_mutexes);
        coroutines::spawn(run_rwlocks);
    }
    loop {
        std::thread::sleep(Duration::from_millis(1000));
    }
}
