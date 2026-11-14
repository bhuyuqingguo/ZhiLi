#!/usr/bin/env bash
# Build the Kylin V10 ARM64 portable archive against an Ubuntu 20.04 arm64 root.
# The root supplies glibc 2.31 and Qt 5.12. Set ZHILI_SYSROOT if it is not /opt/focal-arm64.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
sysroot=${ZHILI_SYSROOT:-/opt/focal-arm64}
if [[ ! -x "$sysroot/usr/bin/c++" ]]; then
    echo "ARM64 root has no compiler: $sysroot" >&2
    exit 1
fi

sudo mkdir -p "$sysroot/src"
if ! mountpoint -q "$sysroot/src"; then
    sudo mount --bind "$root" "$sysroot/src"
fi
sudo chroot "$sysroot" bash -lc 'cd /src && cmake -S . -B build-kylin-arm64 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && cmake --build build-kylin-arm64 --parallel 2'

sudo chown -R "$(id -u):$(id -g)" "$root/build-kylin-arm64"
app="$root/build-kylin-arm64/simplemap"
aarch64-linux-gnu-strip "$app"
if ! aarch64-linux-gnu-readelf -h "$app" | grep -q 'AArch64'; then
    echo 'Kylin binary is not AArch64.' >&2
    exit 1
fi
max=$(aarch64-linux-gnu-objdump -T "$app" | sed -n 's/.*GLIBC_\([0-9.]*\).*/\1/p' | sort -Vu | tail -1)
if [[ -z "$max" ]]; then
    echo 'Could not read the glibc version of the Kylin binary.' >&2
    exit 1
fi
newest=$(printf '%s\n' "$max" 2.31 | sort -V | tail -1)
if [[ "$newest" != 2.31 ]]; then
    echo "Kylin binary needs glibc $max, which is newer than 2.31." >&2
    exit 1
fi

version=${ZHILI_RELEASE_VERSION:-$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$root/build-kylin-arm64/CMakeCache.txt")}
if [[ ! $version =~ ^v?[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
    echo 'Cannot determine a valid ZhiLi release version.' >&2
    exit 1
fi
out="$root/build-kylin-arm64/dist"
mkdir -p "$out"
archive="ZhiLi-v${version#v}-Linux-ARM64-Portable.tar.gz"
python3 "$root/packaging/assemble.py" kylin --app "$app" --output "$out/$archive"
(cd "$out" && sha256sum "$archive" > SHA256SUMS.txt)
echo "glibc $max"
