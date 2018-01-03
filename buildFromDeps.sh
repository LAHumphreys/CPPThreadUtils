#!/bin/bash

DEPS_LOCATION="$PWD/deps/build/lib/cmake"
if [[ -e $DEPS_LOCATION ]]; then
    echo "Building project..."
else
    echo "Dependencies must be built first"
    exit 1
fi

mkdir -p Build
cd Build
cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH:PATH=$DEPS_LOCATION" .. || exit 1
make || exit 1
cd ..
