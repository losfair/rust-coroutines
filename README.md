# rust-coroutines

'Native' stackful coroutine library for Rust.

Replace `thread::spawn` with `coroutines::spawn` and that's all. Everything will just work!

## Features

- Most `libstd` functions that was blocking get patched and become asynchronous automatically
- Efficient asynchronous I/O and task scheduling
- Voluntary preemption and work stealing on entry to / exit from some system calls

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

## Bugs

- Patch for synchronization primitives is not yet implemented. As a result, any operations that involve locks etc. might result in a deadlock or even undefined behavior. This will hopefully get fixed soon :-)
- Thread local storage (TLS) is not yet patched. It needs some work to figure out how to do the hookings correctly.
- Only x86-64 Linux is supported at the moment. Porting to other Unix-like systems and i386 & ARM architectures is planned.
