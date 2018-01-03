#!/bin/bash

if [[ -e ThreadCommsConfig.cmake ]]; then
    echo "Building dependencies..."
else
    echo "This script should be run in the home directory of the project"
    exit 1
fi

if [[ -e deps/NSTimestamps ]]; then
    echo "Existing NSTimestamps directory, no need to clone"
else
    git clone https://github.com/Grauniad/NanoSecondTimestamps.git deps/NSTimestamps || exit 1
fi

DEPS_BUILD=$PWD/deps/build

mkdir -p deps/NSTimestamps/build
mkdir -p $DEPS_BUILD

pushd deps/NSTimestamps || exit 1
git pull
pushd build || exit 1

cmake "-DCMAKE_INSTALL_PREFIX:PATH=$DEPS_BUILD" ..
make -j 3 || exit 1
make install || exit 1

popd || exit 1
popd || exit 1
