#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != aarch64 ]]; then
    echo 'Build this portable package on native Linux ARM64.' >&2
    exit 1
fi

root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build-linux-arm64-portable"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$build" --parallel 2
(cd "$build" && ctest --output-on-failure)

file "$build/simplemap"
ldd "$build/simplemap"
result=0
(cd "$build" && timeout 3s env QT_QPA_PLATFORM=offscreen ./simplemap) || result=$?
if [[ "$result" != 124 ]]; then
    echo "Portable app failed startup check (exit $result)." >&2
    exit 1
fi

out="$build/dist"
mkdir -p "$out"
version=${ZHILI_RELEASE_VERSION:-$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")}
if [[ ! $version =~ ^v?[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
    echo 'Cannot determine a valid ZhiLi release version.' >&2
    exit 1
fi
archive="ZhiLi-v${version#v}-Linux-ARM64-Portable.tar.gz"
python3 "$root/packaging/assemble.py" kylin --app "$build/simplemap" --output "$out/$archive"
(cd "$out" && sha256sum "$archive" > SHA256SUMS.txt)
