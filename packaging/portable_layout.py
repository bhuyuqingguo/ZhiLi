"""Check a portable package before it is shipped.

A portable archive has exactly two top-level names: one folder and one entry
file. Required runtime files and the icon must be present and non-empty.
Symlinks fail the build. Example files may use Chinese names. The top level
is still only the folder and the entry.
"""
import os
import stat
import sys
import tarfile
import zipfile


SPECS = {
    'windows-x64': {
        'folder': 'ZhiLi',
        'entry': 'ZhiLi.exe',
        'needs': (
            'ZhiLi/simplemap.exe',
            'ZhiLi/qt.conf',
            'ZhiLi/zhili.png',
            'ZhiLi/Qt5Core.dll',
            'ZhiLi/Qt5Gui.dll',
            'ZhiLi/Qt5Widgets.dll',
            'ZhiLi/Qt5Svg.dll',
            'ZhiLi/platforms/qwindows.dll',
            'ZhiLi/iconengines/qsvgicon.dll',
            'ZhiLi/imageformats/qsvg.dll',
        ),
    },
    'kylin-arm64': {
        'folder': 'zhili',
        'entry': 'zhili.sh',
        'mode': 0o755,
        'needs': (
            'zhili/ZhiLi',
            'zhili/zhili.png',
        ),
    },
}


def _die(msg):
    raise SystemExit('portable layout: ' + msg)


def _tops(names):
    found = set()
    for name in names:
        name = name.replace('\\', '/').strip('/')
        if not name:
            continue
        found.add(name.split('/')[0])
    return found


def _require_pair(found, folder, entry):
    if found != {folder, entry}:
        _die('top level is %s, want %s and %s' % (', '.join(sorted(found)) or '(empty)', folder, entry))


def _mode(value):
    return value & 0o777


def check_tree(stage, folder, entry, needs, mode=None):
    stage = os.path.abspath(stage)
    if not os.path.isdir(stage):
        _die('stage is not a directory: ' + stage)
    found = set()
    for name in os.listdir(stage):
        path = os.path.join(stage, name)
        if os.path.islink(path):
            _die('symlink: ' + name)
        found.add(name)
    _require_pair(found, folder, entry)
    entry_path = os.path.join(stage, entry)
    folder_path = os.path.join(stage, folder)
    if not os.path.isfile(entry_path) or os.path.getsize(entry_path) == 0:
        _die('entry is missing or empty: ' + entry)
    if mode is not None and _mode(os.stat(entry_path).st_mode) != mode:
        _die('entry mode is %o, want %o' % (_mode(os.stat(entry_path).st_mode), mode))
    if not os.path.isdir(folder_path) or os.path.islink(folder_path):
        _die('folder is missing: ' + folder)
    for rel in needs:
        path = os.path.join(stage, *rel.split('/'))
        if os.path.islink(path) or not os.path.isfile(path) or os.path.getsize(path) == 0:
            _die('missing or empty: ' + rel)
    for dirpath, dirnames, filenames in os.walk(stage, followlinks=False):
        for name in dirnames + filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, stage).replace(os.sep, '/')
            if os.path.islink(path):
                _die('symlink: ' + rel)


def _zip_symlink(info):
    return stat.S_ISLNK(info.external_attr >> 16)


def check_zip(path, folder, entry, needs, mode=None):
    try:
        zf = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        _die('bad zip %s: %s' % (path, exc))
    try:
        infos = zf.infolist()
        names = []
        for info in infos:
            if _zip_symlink(info):
                _die('symlink in zip: ' + info.filename)
            names.append(info.filename)
        _require_pair(_tops(names), folder, entry)
        have = {n.replace('\\', '/').rstrip('/') for n in names}
        for rel in needs:
            if rel not in have:
                _die('zip missing ' + rel)
            info = zf.getinfo(rel)
            if info.file_size == 0:
                _die('zip empty ' + rel)
        if mode is not None:
            info = zf.getinfo(entry)
            got = _mode(info.external_attr >> 16)
            if got != mode:
                _die('entry mode is %o, want %o' % (got, mode))
    finally:
        zf.close()


def check_tar(path, folder, entry, needs, mode=None):
    try:
        tf = tarfile.open(path, 'r:*')
    except (OSError, tarfile.TarError) as exc:
        _die('bad tar %s: %s' % (path, exc))
    try:
        names = []
        by_name = {}
        for info in tf.getmembers():
            if info.issym() or info.islnk():
                _die('symlink in tar: ' + info.name)
            names.append(info.name)
            by_name[info.name.rstrip('/')] = info
        _require_pair(_tops(names), folder, entry)
        for rel in needs:
            info = by_name.get(rel)
            if info is None or not info.isfile() or info.size == 0:
                _die('tar missing or empty: ' + rel)
        if mode is not None:
            info = by_name.get(entry)
            if info is None or _mode(info.mode) != mode:
                got = _mode(info.mode) if info is not None else 0
                _die('entry mode is %o, want %o' % (got, mode))
    finally:
        tf.close()


def check_archive(path, folder, entry, needs, mode=None):
    if path.endswith('.zip'):
        check_zip(path, folder, entry, needs, mode)
    elif path.endswith('.tar.gz') or path.endswith('.tgz'):
        check_tar(path, folder, entry, needs, mode)
    else:
        _die('unknown archive: ' + path)


def pe_has_icon(path):
    """True when the PE has an RT_GROUP_ICON (14) or RT_ICON (3) resource."""
    with open(path, 'rb') as fh:
        data = fh.read()
    if len(data) < 0x40 or data[:2] != b'MZ':
        return False
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    if pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        return False
    nsec = int.from_bytes(data[pe + 6:pe + 8], 'little')
    optsz = int.from_bytes(data[pe + 20:pe + 22], 'little')
    magic = int.from_bytes(data[pe + 24:pe + 26], 'little')
    if magic == 0x10B:
        dd = pe + 24 + 96
    elif magic == 0x20B:
        dd = pe + 24 + 112
    else:
        return False
    if dd + 24 > len(data):
        return False
    rva = int.from_bytes(data[dd + 16:dd + 20], 'little')
    size = int.from_bytes(data[dd + 20:dd + 24], 'little')
    if rva == 0 or size == 0:
        return False
    sec = pe + 24 + optsz
    offset = None
    for i in range(nsec):
        s = sec + i * 40
        if s + 24 > len(data):
            return False
        vsz = int.from_bytes(data[s + 8:s + 12], 'little')
        va = int.from_bytes(data[s + 12:s + 16], 'little')
        rawsz = int.from_bytes(data[s + 16:s + 20], 'little')
        raw = int.from_bytes(data[s + 20:s + 24], 'little')
        span = max(vsz, rawsz)
        if va <= rva < va + span:
            offset = raw + (rva - va)
            break
    if offset is None or offset + 16 > len(data):
        return False
    named = int.from_bytes(data[offset + 12:offset + 14], 'little')
    ident = int.from_bytes(data[offset + 14:offset + 16], 'little')
    entry = offset + 16
    for _i in range(named + ident):
        if entry + 4 > len(data):
            return False
        name = int.from_bytes(data[entry:entry + 4], 'little')
        if name in (3, 14):
            return True
        entry += 8
    return False


def check_exe(path):
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        _die('exe missing: ' + path)
    if not pe_has_icon(path):
        _die('exe has no icon resource: ' + path)


def _u16(data, offset):
    return int.from_bytes(data[offset:offset + 2], 'little')


def _u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], 'little')


def pe_import_dlls(path):
    """DLL names recorded in a PE import table. Empty when the file is not a PE."""
    with open(path, 'rb') as fh:
        data = fh.read()
    if len(data) < 0x40 or data[:2] != b'MZ':
        return []
    pe = _u32(data, 0x3C)
    if pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        return []
    nsec = _u16(data, pe + 6)
    optsz = _u16(data, pe + 20)
    magic = _u16(data, pe + 24)
    if magic == 0x10B:
        dd = pe + 24 + 96
    elif magic == 0x20B:
        dd = pe + 24 + 112
    else:
        return []
    if dd + 16 > len(data):
        return []
    rva = _u32(data, dd + 8)
    if rva == 0:
        return []
    sec = pe + 24 + optsz
    sections = []
    for i in range(nsec):
        start = sec + i * 40
        if start + 24 > len(data):
            return []
        sections.append((
            _u32(data, start + 8),
            _u32(data, start + 12),
            _u32(data, start + 16),
            _u32(data, start + 20),
        ))

    def rva_off(value):
        for vsz, va, rawsz, raw in sections:
            span = max(vsz, rawsz)
            if va <= value < va + span:
                return raw + (value - va)
        return None

    off = rva_off(rva)
    if off is None:
        return []
    names = []
    while off + 20 <= len(data):
        name_rva = _u32(data, off + 12)
        if name_rva == 0:
            break
        name_off = rva_off(name_rva)
        if name_off is None or name_off >= len(data):
            break
        end = data.find(b'\0', name_off)
        if end < 0:
            break
        names.append(data[name_off:end].decode('ascii', 'replace'))
        off += 20
    return names


def _parse(argv):
    if len(argv) < 3:
        _die('usage: portable_layout.py tree|archive|exe PATH [--spec NAME | --folder F --entry E --need REL ...]')
    cmd, path = argv[1], argv[2]
    folder = entry = spec = None
    mode = None
    needs = []
    i = 3
    while i < len(argv):
        flag = argv[i]
        if flag in ('--folder', '--entry', '--need', '--spec', '--mode'):
            if i + 1 >= len(argv):
                _die('missing value for ' + flag)
            value = argv[i + 1]
            i += 2
            if flag == '--folder':
                folder = value
            elif flag == '--entry':
                entry = value
            elif flag == '--spec':
                spec = value
            elif flag == '--mode':
                mode = int(value, 8)
            else:
                needs.append(value)
        else:
            _die('unknown argument ' + flag)
    if spec is not None:
        if spec not in SPECS:
            _die('unknown spec ' + spec)
        chosen = SPECS[spec]
        folder = chosen['folder']
        entry = chosen['entry']
        needs = list(chosen['needs']) + needs
        if 'mode' in chosen:
            mode = chosen['mode']
    return cmd, path, folder, entry, needs, mode


def main(argv):
    cmd, path, folder, entry, needs, mode = _parse(argv)
    if cmd == 'exe':
        check_exe(path)
        print('icon ok', path)
        return
    if not folder or not entry:
        _die('tree and archive checks need --folder and --entry, or --spec')
    if cmd == 'tree':
        check_tree(path, folder, entry, needs, mode)
    elif cmd == 'archive':
        check_archive(path, folder, entry, needs, mode)
    else:
        _die('unknown command ' + cmd)
    print('layout ok', path)


if __name__ == '__main__':
    main(sys.argv)
