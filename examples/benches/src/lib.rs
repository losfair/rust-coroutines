#![feature(test)]
extern crate test;
extern crate coroutines;

use test::Bencher;

#[test]
fn test_mpsc() {
    let (tx, rx) = ::std::sync::mpsc::channel();
    coroutines::spawn(move || {
        tx.send(42).unwrap();
    });
    let v: i32 = rx.recv().unwrap();
    assert!(v == 42);
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
