// https://avacariu.me/articles/2015/rust-echo-server-example

extern crate coroutines;

use std::net::{TcpListener, TcpStream};

// traits
use std::io::Read;
use std::io::Write;

fn handle_client(mut stream: TcpStream) {
    let mut buf;
    loop {
        // clear out the buffer so we don't send garbage
        buf = [0; 512];
        let _ = match stream.read(&mut buf) {
            Err(e) => panic!("Got an error: {}", e),
            Ok(m) => {
                if m == 0 {
                    // we've got an EOF
                    break;
                }
                m
            },
        };

        match stream.write(&buf) {
            Err(_) => break,
            Ok(_) => continue,
        }
    }
}

fn run() {
    let listener = TcpListener::bind("127.0.0.1:8888").unwrap();
    for stream in listener.incoming() {
        match stream {
            Err(e) => { println!("failed: {}", e) }
            Ok(stream) => {
                coroutines::spawn_inherit(move || {
                    handle_client(stream)
                });
            }
        }
    }
}

fn main() {
    coroutines::spawn(run).join().unwrap();
}
