# rust-coroutines

'Native' stackful coroutine library for Rust.

Replace `std::thread::spawn` with `coroutines::spawn` and that's all. Everything will just work!

[![Crates.io](https://img.shields.io/crates/v/coroutines.svg)](https://crates.io/crates/coroutines)
[![Build Status](https://travis-ci.org/losfair/rust-coroutines.svg?branch=master)](https://travis-ci.org/losfair/rust-coroutines)

## Features

- Most `libstd` functions that was blocking get patched and become asynchronous automatically
- Efficient asynchronous I/O and task scheduling
- Voluntary preemption and work stealing

## How does it work?

rust-coroutines includes a separate runtime library which provides coroutine scheduling and patches the runtime environment, hooking the entries to blocking system calls in libc.

## Build and use

The runtime library includes a lot of platform-specific code and writing it in Rust won't provide much benefit. Therefore, I decide to write it in C. Before using the library, you need to build (and optionally 'install') `core_impl/unblock_hook` first (clang required):

```
cd core_impl
make
sudo cp libunblock_hook.so /usr/lib/ # Optional (see below)
```

If you can't or don't want to install `libunblock_hook` as a system-wide library, you could specify `RUSTFLAGS="-L/path/to/core_impl/"` for cargo commands and specify `LD_LIBRARY_PATH=/path/to/core_impl/` when running the built binaries.

After setting up the runtime library, you will be able to use the `coroutines` crate in your application.

For example:

`sleep.rs`

```rust
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
```

`tcp_listener.rs`

```rust
extern crate coroutines;

use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write};

fn start() {
    let listener = TcpListener::bind("127.0.0.1:9781").unwrap();
    for stream in listener.incoming() {
        if let Ok(stream) = stream {
            coroutines::spawn(move || stream.write("Hello world\n".as_bytes()));
        } else {
            println!("{:?}", stream.unwrap_err());
        }
    }
}

fn main() {
    coroutines::spawn(start);
    loop {
        std::thread::sleep_ms(3600000);
    }
}
```

More examples can be found in `examples/`.

It should be noted that **all data structures related to blocking `libstd` API calls should be constructed in a coroutine** to inform the runtime library to apply necessary patches. Typically, this can be done by spawning a coroutine from `main` and do everything there. Not doing so may lead to unexpected blocking behaviors (e.g. `TcpListener::accept` blocks instead of yielding out if the `TcpListener` object is constructed outside a coroutine and does not have the `nonblocking` flag bit set. )

## Performance

The performance of rust-coroutines is similar to most other (stackful/stackless) coroutine libraries in Rust, including [may](https://github.com/Xudong-Huang/may) and [futures](https://github.com/rust-lang-nursery/futures-rs) (with tokio).

With a [modified version](https://github.com/losfair/corona) of [corona](https://github.com/vorner/corona) benchmarks:

```
test async                        ... bench:  31,028,198 ns/iter (+/- 2,738,679)
test async_cpupool                ... bench:  29,813,426 ns/iter (+/- 14,394,817)
test async_cpupool_cpus           ... bench:  31,497,284 ns/iter (+/- 7,074,686)
test async_cpupool_many           ... bench:  30,785,178 ns/iter (+/- 7,982,811)
test async_cpus                   ... bench:  25,660,063 ns/iter (+/- 204,811,182)
test async_many                   ... bench:  24,347,067 ns/iter (+/- 1,980,457)
test corona                       ... bench:  38,565,408 ns/iter (+/- 1,717,367)
test corona_blocking_wrapper      ... bench:  38,856,394 ns/iter (+/- 2,242,614)
test corona_blocking_wrapper_cpus ... bench:  29,147,673 ns/iter (+/- 89,727,410)
test corona_blocking_wrapper_many ... bench:  28,384,512 ns/iter (+/- 2,480,628)
test corona_cpus                  ... bench:  28,862,550 ns/iter (+/- 2,197,395)
test corona_many                  ... bench:  28,509,142 ns/iter (+/- 2,767,814)
test coroutines                   ... bench:  26,673,276 ns/iter (+/- 3,232,604)
test coroutines_cpus              ... bench:  27,194,849 ns/iter (+/- 2,787,879)
test coroutines_many              ... bench:  26,744,986 ns/iter (+/- 3,161,595)
test futures                      ... bench:  30,695,434 ns/iter (+/- 2,447,777)
test futures_cpupool              ... bench:  29,626,141 ns/iter (+/- 3,090,787)
test futures_cpupool_cpus         ... bench:  30,573,408 ns/iter (+/- 3,549,979)
test futures_cpupool_many         ... bench:  30,276,154 ns/iter (+/- 4,121,736)
test futures_cpus                 ... bench:  24,814,705 ns/iter (+/- 2,107,849)
test futures_many                 ... bench:  24,750,719 ns/iter (+/- 1,875,699)
test may                          ... bench:  27,553,510 ns/iter (+/- 3,164,095)
test may_cpus                     ... bench:  28,315,233 ns/iter (+/- 2,892,440)
test may_many                     ... bench:  27,812,071 ns/iter (+/- 2,814,072)
test threads                      ... bench:  42,440,270 ns/iter (+/- 4,181,660)
test threads_cpus                 ... bench:  40,202,798 ns/iter (+/- 6,837,590)
test threads_many                 ... bench:  40,259,324 ns/iter (+/- 864,916,488)
```

## Known issues

- Entering asynchronous operations while holding a Mutex or RwLock on the current coroutine is currently a very expensive operation because the current coroutine has to be pinned to an execution unit and cannot yield out while waiting for the lock to be available. However, doing I/O while holding a lock is a slow operation itself so performance penalty with coroutine pinning / blocking wait will not be noticed most of the time.
- Thread local storage (TLS) is not yet patched and might be dangerous to use across an async_enter/exit boundary. It needs some work to figure out how to do the hookings correctly.
- Only x86-64 Linux is supported at the moment. Porting to other Unix-like systems and i386 & ARM architectures is planned.
