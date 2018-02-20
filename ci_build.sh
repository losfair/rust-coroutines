#!/bin/bash

rm /usr/bin/clang || true
rm /usr/bin/clang++ || true
ln -s $(which clang-5.0) /usr/bin/clang
ln -s $(which clang++-5.0) /usr/bin/clang++
ls -l /usr/bin/

cd core_impl
make || exit 1
cp libunblock_hook.so /usr/lib/ || exit 1
cd ..

cargo test --release || exit 1
