#!/bin/bash

export CC=/usr/local/clang-5.0.0/bin/clang-5.0
export CXX=/usr/local/clang-5.0.0/bin/clang++-5.0

cd core_impl
make || exit 1
cp libunblock_hook.so /usr/lib/ || exit 1
cd ..

cargo test --release || exit 1
