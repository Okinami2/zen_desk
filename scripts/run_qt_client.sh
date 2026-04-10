#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."
mkdir -p build_qt_client
cd build_qt_client

/usr/bin/qmake ../qt_client/qt_client.pro
make -j"$(nproc)"

./qt_client
