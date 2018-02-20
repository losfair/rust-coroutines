#!/bin/bash

export CC=/usr/local/clang-5.0.0/bin/clang-5.0
export CXX=/usr/local/clang-5.0.0/bin/clang++-5.0

cd core_impl
make unblock_hook || exit 1
cd ..

export LD_LIBRARY_PATH="./core_impl/"
export RUSTFLAGS="-L./core_impl/"

cargo test --release || exit 1
