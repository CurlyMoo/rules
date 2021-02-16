#!/bin/bash -e

if [[ ${PLATFORM} == "Unix" ]]; then
    mkdir -p build
    cd build || exit 1

    cmake -DCOVERALLS=ON \
        -DCMAKE_BUILD_TYPE=Release ..
    make

    ./start && exit 0
fi
