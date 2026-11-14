"""Stage one portable archive: a folder of runtime files and one entry file."""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from portable_layout import SPECS, check_archive, check_exe, check_tree, pe_import_dlls


ROOT = Path(__file__).resolve().parents[1]
PNG = ROOT / 'assets' / 'branding' / 'zhili.png'
ENTRY_SH = Path(__file__).resolve().parent / 'zhili-entry.sh'
QT_CONF = (
    "[Paths]\nPrefix=.\nPlugins=.\n[Platforms]\nWindowsArguments=fontengine=freetype\n"
)
PLUGINS = (
    'platforms/qwindows.dll',
    'iconengines/qsvgicon.dll',
    'imageformats/qsvg.dll',
)
SYSTEM_DLLS = {
    'advapi32.dll', 'bcrypt.dll', 'comctl32.dll', 'comdlg32.dll', 'crypt32.dll',
    'd3d11.dll', 'dwmapi.dll', 'dxgi.dll', 'gdi32.dll', 'glu32.dll', 'imm32.dll',
    'iphlpapi.dll', 'kernel32.dll', 'kernelbase.dll', 'mpr.dll', 'msimg32.dll',
    'msvcrt.dll', 'netapi32.dll', 'normaliz.dll', 'ntdll.dll', 'ole32.dll',
    'oleaut32.dll', 'opengl32.dll', 'powrprof.dll', 'propsys.dll', 'psapi.dll',
    'rpcrt4.dll', 'setupapi.dll', 'shcore.dll', 'shell32.dll', 'shlwapi.dll',
    'ucrtbase.dll', 'user32.dll', 'userenv.dll', 'uxtheme.dll', 'version.dll',
    'winmm.dll', 'winspool.drv', 'ws2_32.dll', 'wtsapi32.dll',
}


def fail(msg):
    raise SystemExit('assemble: ' + msg)


def copy_file(src, dest, mode):
    src = Path(src)
    if src.is_symlink() or not src.is_file() or src.stat().st_size == 0:
        fail('missing runtime file: ' + str(src))
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest)
    os.chmod(dest, mode)


def copy_tree(src, dest):
    src = Path(src)
    dest = Path(dest)
    for dirpath, dirnames, filenames in os.walk(src, followlinks=False):
        current = Path(dirpath)
        for name in dirnames + filenames:
            path = current / name
            if path.is_symlink():
                fail('symlink: ' + str(path))
        for name in filenames:
            path = current / name
            rel = path.relative_to(src)
            copy_file(path, dest / rel, 0o644)


def shared_files(folder, app, app_name, app_mode):
    copy_file(app, folder / app_name, app_mode)
    copy_file(PNG, folder / 'zhili.png', 0o644)
    copy_file(ROOT / 'README.md', folder / 'README.md', 0o644)
    licenses = folder / 'licenses'
    for notice in sorted((ROOT / 'assets' / 'fonts').glob('OFL-*.txt')):
        copy_file(notice, licenses / notice.name, 0o644)
    copy_file(ROOT / 'assets' / 'symbols' / 'LICENSE.txt', licenses / 'SYMBOLS-CC0.txt', 0o644)
    copy_file(ROOT / 'src' / 'vendor' / 'miniz' / 'LICENSE', licenses / 'MINIZ-MIT.txt', 0o644)
    copy_tree(ROOT / 'examples', folder / 'examples')


def system_dll(name):
    key = name.lower()
    return key in SYSTEM_DLLS or key.startswith('api-ms-win-')


def find_dll(name, search_dirs):
    for directory in search_dirs:
        direct = directory / name
        if direct.is_file() and not direct.is_symlink():
            return direct
        if not directory.is_dir():
            continue
        for candidate in directory.iterdir():
            if candidate.is_file() and not candidate.is_symlink() and candidate.name.lower() == name.lower():
                return candidate
    return None


def bundle_imports(binaries, search_dirs, dest):
    pending = [Path(binary) for binary in binaries]
    seen = set()
    while pending:
        current = pending.pop()
        for dll in pe_import_dlls(current):
            key = dll.lower()
            if key in seen or system_dll(key):
                continue
            seen.add(key)
            src = find_dll(dll, search_dirs)
            if src is None:
                fail('missing runtime file: ' + dll)
            target = dest / src.name
            if not target.exists():
                copy_file(src, target, 0o644)
            pending.append(target)


def assert_no_symlinks(stage):
    for dirpath, dirnames, filenames in os.walk(stage, followlinks=False):
        for name in dirnames + filenames:
            path = Path(dirpath) / name
            if path.is_symlink():
                fail('symlink: ' + str(path))


def write_zip(stage, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for dirpath, dirnames, filenames in os.walk(stage, followlinks=False):
            for name in filenames:
                path = Path(dirpath) / name
                rel = path.relative_to(stage).as_posix()
                info = zipfile.ZipInfo(rel)
                info.flag_bits |= 0x800
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (path.stat().st_mode & 0o777) << 16
                with path.open('rb') as source:
                    archive.writestr(info, source.read())


def write_tar(stage, output, executables):
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    with tarfile.open(output, 'w:gz', format=tarfile.GNU_FORMAT) as archive:
        for dirpath, dirnames, filenames in os.walk(stage, followlinks=False):
            current = Path(dirpath)
            rel_dir = current.relative_to(stage).as_posix()
            if rel_dir != '.':
                info = tarfile.TarInfo(rel_dir)
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                archive.addfile(info)
            for name in filenames:
                path = current / name
                rel = path.relative_to(stage).as_posix()
                info = tarfile.TarInfo(rel)
                info.size = path.stat().st_size
                info.mode = 0o755 if rel in executables else 0o644
                info.type = tarfile.REGTYPE
                with path.open('rb') as source:
                    archive.addfile(info, source)


def check_spec(stage, spec, archive):
    chosen = SPECS[spec]
    check_tree(str(stage), chosen['folder'], chosen['entry'], chosen['needs'], chosen.get('mode'))
    check_archive(str(archive), chosen['folder'], chosen['entry'], chosen['needs'], chosen.get('mode'))


def assemble_kylin(app, output):
    with tempfile.TemporaryDirectory() as temp:
        stage = Path(temp)
        folder = stage / 'zhili'
        shared_files(folder, app, 'ZhiLi', 0o755)
        copy_file(ENTRY_SH, stage / 'zhili.sh', 0o755)
        assert_no_symlinks(stage)
        write_tar(stage, output, {'zhili.sh', 'zhili/ZhiLi'})
        check_spec(stage, 'kylin-arm64', output)
    print(output)


def assemble_windows(app, entry, qt_prefix, notices, output, search):
    qt_prefix = Path(qt_prefix)
    qt_bin = qt_prefix / 'bin'
    plugins = qt_prefix / 'plugins'
    if not qt_bin.is_dir() or not plugins.is_dir():
        fail('Qt prefix is missing bin or plugins: ' + str(qt_prefix))
    check_exe(str(entry))
    with tempfile.TemporaryDirectory() as temp:
        stage = Path(temp)
        folder = stage / 'ZhiLi'
        shared_files(folder, app, 'simplemap.exe', 0o644)
        (folder / 'qt.conf').write_text(QT_CONF, encoding='ascii')
        binaries = [folder / 'simplemap.exe']
        for rel in PLUGINS:
            src = plugins / rel
            dest = folder / rel
            copy_file(src, dest, 0o644)
            binaries.append(dest)
        bundle_imports(binaries, [qt_bin, *[Path(item) for item in search]], folder)
        license_dir = folder / 'licenses'
        for name in ('LICENSE.LGPL3', 'LICENSE.GPL3'):
            copy_file(Path(notices) / name, license_dir / name, 0o644)
        copy_file(ROOT / 'packaging' / 'QT-NOTICE.txt', license_dir / 'QT-NOTICE.txt', 0o644)
        copy_file(entry, stage / 'ZhiLi.exe', 0o644)
        assert_no_symlinks(stage)
        write_zip(stage, output)
        check_spec(stage, 'windows-x64', output)
        check_exe(str(stage / 'ZhiLi.exe'))
    print(output)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    kylin = sub.add_parser('kylin')
    kylin.add_argument('--app', type=Path, required=True)
    kylin.add_argument('--output', type=Path, required=True)
    windows = sub.add_parser('windows')
    windows.add_argument('--app', type=Path, required=True)
    windows.add_argument('--entry', type=Path, required=True)
    windows.add_argument('--qt-prefix', type=Path, required=True)
    windows.add_argument('--qt-notices', type=Path, required=True)
    windows.add_argument('--search', type=Path, action='append', default=[])
    windows.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv[1:])
    if not PNG.is_file() or PNG.stat().st_size == 0:
        fail('missing icon: assets/branding/zhili.png')
    if args.command == 'kylin':
        assemble_kylin(args.app, args.output)
    else:
        assemble_windows(args.app, args.entry, args.qt_prefix, args.qt_notices, args.output, args.search)


if __name__ == '__main__':
    main(sys.argv)
