"""Portable archives expose one folder and one entry, or the build fails."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / 'packaging' / 'portable_layout.py'
ASSEMBLE = ROOT / 'packaging' / 'assemble.py'
ENTRY = ROOT / 'packaging' / 'zhili-entry.sh'
PNG = ROOT / 'assets' / 'branding' / 'zhili.png'


def run_check(args):
    return subprocess.run([sys.executable, str(CHECK), *args], capture_output=True, text=True)


class LayoutTests(unittest.TestCase):
    def test_missing_icon_extra_name_and_symlink_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp) / 'stage'
            folder = stage / 'zhili'
            folder.mkdir(parents=True)
            (folder / 'ZhiLi').write_bytes(b'app')
            (folder / 'zhili.png').write_bytes(b'png')
            entry = stage / 'zhili.sh'
            entry.write_text('#!/bin/sh\n', encoding='utf-8')
            entry.chmod(0o755)
            base = ['tree', str(stage), '--spec', 'kylin-arm64']
            good = run_check(base)
            self.assertEqual(good.returncode, 0, good.stderr)

            (folder / 'zhili.png').unlink()
            missing = run_check(base)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn('zhili.png', missing.stderr)
            (folder / 'zhili.png').write_bytes(b'png')

            (stage / 'README.txt').write_text('extra\n', encoding='utf-8')
            extra = run_check(base)
            self.assertNotEqual(extra.returncode, 0)
            self.assertIn('top level', extra.stderr)
            (stage / 'README.txt').unlink()

            link = folder / 'link'
            link.symlink_to('ZhiLi')
            linked = run_check(base)
            self.assertNotEqual(linked.returncode, 0)
            self.assertIn('symlink', linked.stderr)

    def test_windows_spec_rejects_a_missing_runtime_file(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            (stage / 'ZhiLi.exe').write_bytes(b'MZ')
            folder = stage / 'ZhiLi'
            for rel in (
                'simplemap.exe', 'qt.conf', 'zhili.png', 'Qt5Core.dll', 'Qt5Gui.dll',
                'Qt5Widgets.dll', 'Qt5Svg.dll', 'platforms/qwindows.dll',
                'iconengines/qsvgicon.dll',
            ):
                path = folder / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'x')
            result = run_check(['tree', str(stage), '--spec', 'windows-x64'])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('qsvg.dll', result.stderr)

    def test_script_is_not_an_icon_exe_and_a_resource_entry_is(self):
        note = run_check(['exe', str(ENTRY)])
        self.assertNotEqual(note.returncode, 0)
        self.assertIn('no icon', note.stderr)
        with tempfile.TemporaryDirectory() as temp:
            pe = Path(temp) / 'app.exe'
            pe.write_bytes(_pe_with_group_icon())
            good = run_check(['exe', str(pe)])
            self.assertEqual(good.returncode, 0, good.stderr)
            pe.write_bytes(_pe_with_group_icon(icon_type=1))
            bad = run_check(['exe', str(pe)])
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('no icon', bad.stderr)

    def test_kylin_archive_is_one_folder_and_one_executable_entry(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'app'
            app.write_text('#!/bin/sh\nprintf ok\n', encoding='utf-8')
            app.chmod(0o755)
            output = Path(temp) / 'ZhiLi-v1.0.6-Linux-ARM64-Portable.tar.gz'
            result = subprocess.run(
                [sys.executable, str(ASSEMBLE), 'kylin', '--app', str(app), '--output', str(output)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with tarfile.open(output) as archive:
                names = [member.name for member in archive.getmembers()]
                tops = {name.split('/')[0] for name in names if name}
                self.assertEqual(tops, {'zhili', 'zhili.sh'})
                entry = archive.getmember('zhili.sh')
                self.assertEqual(entry.mode & 0o777, 0o755)
                self.assertIn('zhili/examples/研发项目交付.smap', names)
                self.assertGreater(archive.getmember('zhili/zhili.png').size, 0)

    def test_empty_runtime_file_fails_the_package(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'empty'
            app.write_bytes(b'')
            output = Path(temp) / 'out.tar.gz'
            result = subprocess.run(
                [sys.executable, str(ASSEMBLE), 'kylin', '--app', str(app), '--output', str(output)],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing runtime file', result.stderr)

    def test_entry_writes_desktop_file_once_and_updates_after_a_move(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp) / 'home'
            home.mkdir()
            ran = Path(temp) / 'ran'
            stage = _entry_tree(Path(temp) / 'place')
            env = _home_env(home, ran)
            first = subprocess.run(['sh', str(stage / 'zhili.sh')], env=env, cwd=temp, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(ran.read_text(encoding='utf-8'), 'ok')
            desktop = home / 'Desktop' / '枝理.desktop'
            apps = home / '.local' / 'share' / 'applications' / 'org.zhili.simplemap.desktop'
            text = desktop.read_text(encoding='utf-8')
            self.assertIn('StartupWMClass=ZhiLi', text)
            self.assertIn('Icon=%s' % (stage / 'zhili' / 'zhili.png'), text)
            self.assertIn('Exec=%s' % (stage / 'zhili.sh'), text)
            self.assertEqual(apps.read_text(encoding='utf-8'), text)
            desktop.unlink()
            apps.unlink()
            ran.unlink()
            second = subprocess.run(['sh', str(stage / 'zhili.sh')], env=env, cwd=temp, capture_output=True, text=True)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertFalse(desktop.exists())
            self.assertTrue(ran.is_file())
            moved = Path(temp) / 'moved'
            shutil.copytree(stage, moved)
            os.chmod(moved / 'zhili.sh', 0o755)
            os.chmod(moved / 'zhili' / 'ZhiLi', 0o755)
            third = subprocess.run(['sh', str(moved / 'zhili.sh')], env=env, cwd=temp, capture_output=True, text=True)
            self.assertEqual(third.returncode, 0, third.stderr)
            rewritten = desktop.read_text(encoding='utf-8')
            self.assertIn('Exec=%s' % (moved / 'zhili.sh'), rewritten)
            self.assertIn('Icon=%s' % (moved / 'zhili' / 'zhili.png'), rewritten)
            self.assertIn('StartupWMClass=ZhiLi', rewritten)


def _entry_tree(stage):
    folder = stage / 'zhili'
    folder.mkdir(parents=True)
    app = folder / 'ZhiLi'
    app.write_text('#!/bin/sh\nprintf ok > "$ZHILI_TEST_RAN"\n', encoding='utf-8')
    app.chmod(0o755)
    shutil.copyfile(PNG, folder / 'zhili.png')
    shutil.copyfile(ENTRY, stage / 'zhili.sh')
    os.chmod(stage / 'zhili.sh', 0o755)
    return stage


def _home_env(home, ran):
    stub = home / 'bin'
    stub.mkdir()
    xdg = stub / 'xdg-user-dir'
    xdg.write_text('#!/bin/sh\nprintf "%s\\n" "$HOME/Desktop"\n', encoding='utf-8')
    xdg.chmod(0o755)
    env = os.environ.copy()
    env['HOME'] = str(home)
    env['XDG_CONFIG_HOME'] = str(home / '.config')
    env['PATH'] = str(stub) + os.pathsep + env.get('PATH', '')
    env['ZHILI_TEST_RAN'] = str(ran)
    return env


def _pe_with_group_icon(icon_type=14):
    data = bytearray(0x400)
    data[0:2] = b'MZ'
    pe = 0x80
    data[0x3C:0x40] = pe.to_bytes(4, 'little')
    data[pe:pe + 4] = b'PE\0\0'
    data[pe + 6:pe + 8] = (1).to_bytes(2, 'little')
    optsz = 240
    data[pe + 20:pe + 22] = optsz.to_bytes(2, 'little')
    data[pe + 24:pe + 26] = (0x20B).to_bytes(2, 'little')
    dd = pe + 24 + 112
    rva = 0x1000
    data[dd + 16:dd + 20] = rva.to_bytes(4, 'little')
    data[dd + 20:dd + 24] = (0x100).to_bytes(4, 'little')
    sec = pe + 24 + optsz
    data[sec + 8:sec + 12] = (0x200).to_bytes(4, 'little')
    data[sec + 12:sec + 16] = rva.to_bytes(4, 'little')
    data[sec + 16:sec + 20] = (0x200).to_bytes(4, 'little')
    raw = 0x200
    data[sec + 20:sec + 24] = raw.to_bytes(4, 'little')
    data[raw + 14:raw + 16] = (1).to_bytes(2, 'little')
    data[raw + 16:raw + 20] = icon_type.to_bytes(4, 'little')
    return bytes(data)


if __name__ == '__main__':
    unittest.main()
