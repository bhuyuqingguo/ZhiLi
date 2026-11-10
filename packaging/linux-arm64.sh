#!/usr/bin/env bash
set -euo pipefail

# Run on the oldest supported ARM64 distribution, using its system Qt.
version=${1:?Usage: bash packaging/linux-arm64.sh DEB_VERSION}
if [[ $(uname -s) != Linux || $(dpkg --print-architecture) != arm64 ]]; then
    echo 'This packaging script requires native ARM64 Debian-family Linux.' >&2
    exit 1
fi
dpkg --validate-version "$version"

source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$source_dir/build-linux-arm64"
cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel 2
(cd "$build_dir" && ctest --output-on-failure)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage="$work/package"
install -D -m 755 "$build_dir/simplemap" "$stage/usr/lib/zhili/simplemap"
install -D -m 644 "$source_dir/packaging/org.zhili.simplemap.desktop" \
    "$stage/usr/share/applications/org.zhili.simplemap.desktop"
sed -i 's|^Exec=simplemap$|Exec=/usr/lib/zhili/simplemap|' \
    "$stage/usr/share/applications/org.zhili.simplemap.desktop"
install -D -m 644 "$source_dir/assets/branding/zhili.svg" \
    "$stage/usr/share/icons/hicolor/scalable/apps/org.zhili.simplemap.svg"

for notice in "$source_dir"/assets/fonts/OFL-*.txt; do
    install -D -m 644 "$notice" "$stage/usr/share/doc/zhili/${notice##*/}"
done
install -D -m 644 "$source_dir/assets/symbols/LICENSE.txt" \
    "$stage/usr/share/doc/zhili/SYMBOLS-CC0.txt"
install -D -m 644 "$source_dir/src/vendor/miniz/LICENSE" \
    "$stage/usr/share/doc/zhili/MINIZ-MIT.txt"

# Derive minimum library versions from the actual native executable.
mkdir -p "$work/debian" "$stage/DEBIAN"
cat > "$work/debian/control" <<'CONTROL'
Source: zhili
Section: editors
Priority: optional
Maintainer: ZhiLi Release Team

Package: zhili
Architecture: arm64
Description: Offline mind-map editor
CONTROL
deps=$(cd "$work" && dpkg-shlibdeps -O -e"$stage/usr/lib/zhili/simplemap")
case "$deps" in
    shlibs:Depends=*) deps=${deps#shlibs:Depends=} ;;
    *) echo "Dependency scan failed: $deps" >&2; exit 1 ;;
esac
test -n "$deps"

cat > "$stage/DEBIAN/control" <<CONTROL
Package: zhili
Version: $version
Architecture: arm64
Section: editors
Priority: optional
Maintainer: ZhiLi Release Team
Depends: $deps
Description: ZhiLi offline mind-map editor
 An offline editor for classic mind maps and top-down structure charts.
CONTROL

out="$build_dir/dist"
mkdir -p "$out"
package="zhili_${version}_arm64.deb"
dpkg-deb --build --root-owner-group "$stage" "$out/$package"
(cd "$out" && sha256sum "$package" > "$package.sha256")
dpkg-deb --info "$out/$package"
