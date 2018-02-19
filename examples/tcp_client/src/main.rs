extern crate coroutines;

use std::net::TcpStream;
use std::io::{Read, Write};
use std::time::Duration;

fn run() {
    let mut stream = TcpStream::connect("127.0.0.1:9781").unwrap();
    let mut text = String::new();
    stream.read_to_string(&mut text).unwrap();
    println!("{}", text);
    std::process::exit(0);
}

fn main() {
    coroutines::spawn(run);
    loop {
        std::thread::sleep(Duration::from_millis(3600000));
    }
}
