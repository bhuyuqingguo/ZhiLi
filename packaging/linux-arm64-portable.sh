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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage="$work/ZhiLi"
mkdir -p "$stage/licenses" "$stage/examples"
install -m 755 "$build/simplemap" "$stage/ZhiLi"
cp "$root/README.md" "$stage/"
cp "$root"/assets/fonts/OFL-*.txt "$stage/licenses/"
cp "$root"/examples/*.smap "$stage/examples/"

file "$stage/ZhiLi"
ldd "$stage/ZhiLi"
result=0
(cd "$stage" && timeout 3s env QT_QPA_PLATFORM=offscreen ./ZhiLi) || result=$?
if [[ "$result" != 124 ]]; then
    echo "Portable app failed startup check (exit $result)." >&2
    exit 1
fi

out="$build/dist"
mkdir -p "$out"
archive=ZhiLi-Linux-ARM64-Portable.tar.gz
tar -C "$work" -czf "$out/$archive" ZhiLi
(cd "$out" && sha256sum "$archive" > "$archive.sha256")
