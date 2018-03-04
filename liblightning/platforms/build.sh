#!/bin/bash

function build_for_target() {
    local TARGET=$1
    cd $TARGET || exit 1

    rm "../liblightning_platform.a" || true
    LL_PLATFORM_OUTPUT="../liblightning_platform.a" \
    ASFLAGS="-g" \
    make || exit 1

    cd ..
}

cd $(dirname $0)

./clean.sh

build_for_target "x86_64-linux"
