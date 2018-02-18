extern crate coroutines;

use std::net::{TcpListener, TcpStream, Shutdown};
use std::io::{Read, Write};

fn handle_client(mut stream: TcpStream) {
    let mut rb: Vec<u8> = vec![0; 4096];
    stream.read(&mut rb).unwrap();
    stream.write("HTTP/1.1 200 OK\r\n\r\n".as_bytes()).unwrap();
    stream.write("Hello world\n".as_bytes());
}

fn start() {
    let listener = TcpListener::bind("127.0.0.1:9781").unwrap();
    for stream in listener.incoming() {
        if let Ok(stream) = stream {
            coroutines::spawn_inherit(move || handle_client(stream));
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
