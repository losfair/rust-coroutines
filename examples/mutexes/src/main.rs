extern crate coroutines;

use std::time::Duration;
use std::sync::{Arc, Mutex};

fn main() {
    for _ in 0..10 {
        coroutines::spawn(move || {
            println!("Outer enter");
            let m = Arc::new(Mutex::new(false));
            let m2 = m.clone();
            let _inner = m.lock().unwrap();
            coroutines::spawn(move || {
                println!("Inner enter");
                let _inner = m2.lock().unwrap();
                println!("Inner exit");
            });
            std::thread::sleep(Duration::from_millis(500));
            println!("Outer exit");
        });
    }
    loop {
        std::thread::sleep(Duration::from_millis(1000));
    }
}
