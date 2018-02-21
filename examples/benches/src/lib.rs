#![feature(test)]
extern crate test;
extern crate coroutines;

use test::Bencher;
use coroutines::Promise;

#[bench]
fn async_enter(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| {
            let ret = Promise::await(|p| p.resolve(42));
            assert!(ret == 42);
        })
    }).join().unwrap();
}

#[bench]
fn async_enter_holding_mutex(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| {
            let m = ::std::sync::Mutex::new(());
            let _handle = m.lock().unwrap();
            let ret = Promise::await(|p| p.resolve(42));
            assert!(ret == 42);
        })
    }).join().unwrap();
}

#[bench]
fn async_enter_dropped_mutex(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| {
            let m = ::std::sync::Mutex::new(());
            let _handle = m.lock().unwrap();
            drop(_handle);
            let ret = Promise::await(|p| p.resolve(42));
            assert!(ret == 42);
        })
    }).join().unwrap();
}

#[bench]
fn mpsc_co_native(b: &mut Bencher) {
    b.iter(|| {
        let (tx, rx) = ::std::sync::mpsc::channel();
        coroutines::spawn(move || {
            tx.send(42).unwrap();
        });
        let v: i32 = rx.recv().unwrap();
        assert!(v == 42);
    });
}

#[bench]
fn mpsc_native_thread(b: &mut Bencher) {
    b.iter(|| {
        let (tx, rx) = ::std::sync::mpsc::channel();
        std::thread::spawn(move || {
            tx.send(42).unwrap();
        });
        let v: i32 = rx.recv().unwrap();
        assert!(v == 42);
    });
}

#[bench]
fn simple_yield(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| coroutines::yield_now());
    }).join().unwrap();
}

#[bench]
fn native_thread_spawn(b: &mut Bencher) {
    b.iter(|| std::thread::spawn(|| {}));
}

#[bench]
fn fast_spawn(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| coroutines::fast_spawn(|| {}));
    }).join().unwrap();
}

#[bench]
fn spawn(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| coroutines::spawn(|| {}));
    }).join().unwrap();
}

#[bench]
fn spawn_join(b: &mut Bencher) {
    let b = unsafe {
        std::mem::transmute::<&mut Bencher, &'static mut Bencher>(b)
    };
    coroutines::spawn(move || {
        b.iter(|| coroutines::spawn(|| {}).join().unwrap());
    }).join().unwrap();
}
