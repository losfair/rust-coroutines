extern crate coroutines;

use std::net::TcpStream;
use std::io::Read;

fn run() {
    let mut stream = TcpStream::connect("127.0.0.1:9781").unwrap();
    let mut text = String::new();
    stream.read_to_string(&mut text).unwrap();
    println!("{}", text);
}

fn main() {
    coroutines::spawn(run).join().unwrap();
}
