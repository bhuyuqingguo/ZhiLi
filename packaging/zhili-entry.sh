#!/bin/sh
# Top-level entry for the Kylin portable archive.
# The archive root is this script plus the sibling zhili/ folder.
# No root and no installer. The first launch writes a desktop shortcut.
# A later launch from a moved copy rewrites the shortcut. Deleting the
# shortcut leaves the marker, so it stays deleted.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=$HERE/zhili
ENTRY=$HERE/zhili.sh
if [ ! -f "$APP/ZhiLi" ] || [ ! -f "$APP/zhili.png" ]; then
  printf '%s\n' '请把 zhili.sh 和旁边的 zhili 文件夹放在一起，不要只拷走脚本。' >&2
  exit 1
fi

desktop_field() {
  case "$1" in
    *[!A-Za-z0-9/._+-]*)
      printf '"%s"' "$(printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"
      ;;
    *) printf '%s' "$1" ;;
  esac
}

write_shortcuts() {
  ICON=$APP/zhili.png
  DESKTOP=$(xdg-user-dir DESKTOP 2>/dev/null || true)
  if [ -z "$DESKTOP" ] || [ ! -d "$DESKTOP" ]; then
    if [ -d "$HOME/桌面" ]; then
      DESKTOP=$HOME/桌面
    else
      DESKTOP=$HOME/Desktop
    fi
  fi
  [ -n "$DESKTOP" ] || return 0
  CONF=${XDG_CONFIG_HOME:-$HOME/.config}/zhili
  MARK=$CONF/entry-path
  mkdir -p "$DESKTOP" "$HOME/.local/share/applications" "$CONF" || return 0
  cur=$(cat "$MARK" 2>/dev/null || true)
  if [ "$cur" = "$ENTRY" ]; then
    return 0
  fi
  exec_field=$(desktop_field "$ENTRY")
  icon_field=$(desktop_field "$ICON")
  path_field=$(desktop_field "$APP")
  apps=$HOME/.local/share/applications/org.zhili.simplemap.desktop
  desk=$DESKTOP/枝理.desktop
  for dest in "$apps" "$desk"; do
    tmp=$dest.tmp.$$
    cat > "$tmp" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=枝理
GenericName=思维导图
Comment=枝理离线思维导图
Exec=$exec_field
Path=$path_field
Icon=$icon_field
StartupWMClass=ZhiLi
Terminal=false
Categories=Office;
EOF
    chmod 755 "$tmp"
    mv -f "$tmp" "$dest"
    if command -v gio >/dev/null 2>&1; then
      gio set "$dest" metadata::trusted true >/dev/null 2>&1 || true
    fi
  done
  tmpm=$MARK.tmp.$$
  printf '%s\n' "$ENTRY" > "$tmpm"
  mv -f "$tmpm" "$MARK"
}

write_shortcuts || true
cd "$APP"
exec "$APP/ZhiLi" "$@"
