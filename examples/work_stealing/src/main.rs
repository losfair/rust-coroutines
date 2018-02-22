extern crate coroutines;

use coroutines::JoinHandle;

fn run_inner() {
    let join_handles: Vec<JoinHandle<()>> = (0..10000).map(|_| {
        coroutines::spawn(|| {
            let join_handles: Vec<JoinHandle<()>> = (0..10)
                .map(|_| coroutines::spawn(|| {}))
                .collect();
            for h in join_handles {
                h.join().unwrap();
            }
        })
    }).collect();
    for h in join_handles {
        h.join().unwrap();
    }
}

fn main() {
    run_inner();
    println!("Migration count before enabling work stealing: {}", coroutines::migration_count());
    unsafe {
        coroutines::set_work_stealing(true);
    }
    run_inner();
    println!("Migration count after enabling work stealing: {}", coroutines::migration_count());
}
