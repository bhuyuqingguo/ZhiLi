#!/usr/bin/env bash
# Cross-build the Windows x64 portable zip with the official Qt 5.15.2 MinGW kit.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
qt=${ZHILI_QT_WIN:-/opt/qt-win/5.15.2/mingw81_64}
build="$root/build-windows-mingw"
if [[ ! -d "$qt/bin" ]]; then
    echo "Qt MinGW prefix is missing: $qt" >&2
    exit 1
fi

cmake -S "$root" -B "$build" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=AMD64 \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DCMAKE_FIND_ROOT_PATH="$qt" \
    -DCMAKE_PREFIX_PATH="$qt" \
    -DZHILI_HOST_RCC=/usr/bin/rcc \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "$build" --parallel

notices="$build/qt-license-notices"
mkdir -p "$notices"
for notice in LICENSE.LGPL3 LICENSE.GPL3; do
    if [[ ! -s "$notices/$notice" ]]; then
        curl -fLsS --retry 2 --max-time 45 \
            "https://raw.githubusercontent.com/qt/qtbase/v5.15.2/$notice" -o "$notices/$notice"
    fi
done

version=${ZHILI_RELEASE_VERSION:-$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")}
if [[ ! $version =~ ^v?[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
    echo 'Cannot determine a valid ZhiLi release version.' >&2
    exit 1
fi
out="$build/dist"
mkdir -p "$out"
archive="ZhiLi-v${version#v}-Windows-x64.zip"
runtime=$(dirname "$(x86_64-w64-mingw32-g++ -print-file-name=libstdc++-6.dll)")
pthread=$(dirname "$(x86_64-w64-mingw32-g++ -print-file-name=libwinpthread-1.dll)")
python3 "$root/packaging/assemble.py" windows \
    --app "$build/simplemap.exe" \
    --entry "$build/ZhiLi.exe" \
    --qt-prefix "$qt" \
    --qt-notices "$notices" \
    --search "$runtime" \
    --search "$pthread" \
    --output "$out/$archive"
(cd "$out" && sha256sum "$archive" > SHA256SUMS.txt)
