"""Real per-user installer upgrade in a new QA directory, with guarded host cleanup."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
import winreg

UNINSTALL = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\{957C2CE0-1072-43F7-98E5-4930AF16C309}_is1'
EXTENSIONS = ('.zip', '.7z', '.tar', '.tgz', '.gz', '.xz', '.bz2', '.zst', '.lzma')


def value(path, name):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            return winreg.QueryValueEx(key, name)[0]
    except FileNotFoundError:
        return None


def environment(name):
    return next(v for k, v in os.environ.items() if k.lower() == name.lower())


def has_value(path, name):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            winreg.QueryValueEx(key, name)
        return True
    except FileNotFoundError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('old', 'new', 'gui', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--old-version', default='0.6.0')
    parser.add_argument('--new-version', default='0.8.0')
    args = parser.parse_args()
    if value(UNINSTALL, 'UninstallString') is not None:
        raise RuntimeError('An existing installation is registered; refusing to replace it for QA')
    if value(r'Software\Classes\VynxArc.Archive', '') is not None:
        raise RuntimeError('A pre-existing Open With registration must be preserved')
    for ext in EXTENSIONS:
        if has_value(rf'Software\Classes\{ext}\OpenWithProgids', 'VynxArc.Archive'):
            raise RuntimeError('A pre-existing Open With mapping must be preserved')
    identity = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command',
                               'Get-AppxPackage -Name VYNX.ARC.Explorer | Select-Object -ExpandProperty PackageFullName'],
                              capture_output=True, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if identity.stdout.strip():
        raise RuntimeError('A pre-existing Explorer identity must be preserved')
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    app = root / 'installed'
    settings = Path(environment('APPDATA')) / 'VYNX/VYNX ARC.ini'
    original = settings.read_bytes() if settings.exists() else None
    if original is not None:
        (root / 'original-settings.ini').write_bytes(original)
    expected = b'[General]\ntheme=dark\nlanguage=uk\nhistoryEnabled=false\nqueuePaused=true\n'
    defaults = {ext: value(rf'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\{ext}\UserChoice', 'ProgId')
                for ext in EXTENSIONS}
    copy_id = str(uuid.uuid4())
    working = Path(environment('LOCALAPPDATA')) / 'VYNX/VYNX ARC/WorkingCopies' / ('edit-' + copy_id)
    report = dict(status='running', settings_preexisting=original is not None,
                  scope=f'Actual per-user {args.old_version}-to-{args.new_version} installer upgrade, Open With registry/command, installed GUI smoke, settings and schema-1 working-copy fixture preservation; not a clean VM or unsigned Explorer identity registration')
    env = os.environ.copy()
    windows = environment('SystemRoot')
    env['PATH'] = windows + ';' + windows + '/System32'
    env['QT_ASSUME_STDERR_HAS_CONSOLE'] = '1'
    for key in ('QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'VYNX_QT_ROOT'):
        env.pop(key, None)

    def run(command, timeout=120):
        result = subprocess.run(list(map(str, command)), env=env, capture_output=True,
                                timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise RuntimeError((result.returncode, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace')))
        return result

    def uninstall():
        registered = value(UNINSTALL, 'UninstallString')
        if registered is None:
            return
        executable = Path(registered.strip('"'))
        if executable.resolve().parent != app.resolve():
            raise RuntimeError('Refusing to uninstall a directory outside this QA installation')
        run([executable, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', f'/LOG={root / "uninstall.log"}'])
        deadline = time.monotonic() + 60
        while (app / 'VynxArc.exe').exists() and time.monotonic() < deadline:
            time.sleep(.1)
        assert not (app / 'VynxArc.exe').exists()

    try:
        run([args.old.resolve(), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/TASKS=openwith',
             f'/DIR={app}', f'/LOG={root / "install-old.log"}'])
        assert args.old_version.encode() in run([app / 'vynxarc-cli.exe', '--help']).stdout
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_bytes(expected)
        note = root / 'note.txt'
        note.write_bytes(b'original archive entry')
        archive = root / 'source.zip'
        run([app / 'vynxarc-cli.exe', 'create', archive, note])
        working.mkdir(parents=True, exist_ok=False)
        payload = working / 'note.txt'
        payload.write_bytes(b'unsaved upgrade acceptance edit')
        manifest = dict(schema=1, id=copy_id, archive=str(archive), entry='note.txt',
                        file=str(payload), state='changed', fingerprint='original')
        (working / '.edit.json').write_text(json.dumps(manifest), encoding='utf-8')
        preserved = {p.name: p.read_bytes() for p in working.iterdir()}
        run([args.new.resolve(), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/TASKS=openwith',
             f'/DIR={app}', f'/LOG={root / "upgrade-new.log"}'])
        assert settings.read_bytes() == expected
        assert value(UNINSTALL, 'DisplayVersion') == args.new_version
        assert args.new_version.encode() in run([app / 'vynxarc-cli.exe', '--help']).stdout
        assert (app / 'vynxarc-worker.exe').is_file()
        assert hashlib.sha256((app / 'VynxArc.exe').read_bytes()).digest() == hashlib.sha256(args.gui.read_bytes()).digest()
        assert {p.name: p.read_bytes() for p in working.iterdir()} == preserved
        command = value(r'Software\Classes\VynxArc.Archive\shell\open\command', '')
        assert command == f'"{app}\\VynxArc.exe" "%1"'
        for ext in EXTENSIONS:
            assert has_value(rf'Software\Classes\{ext}\OpenWithProgids', 'VynxArc.Archive')
        run([app / 'VynxArc.exe', archive, '--capture', root / 'installed-archive.png'], 45)
        run([app / 'VynxArc.exe', '--smoke-test'], 60)
        for ext, default in defaults.items():
            assert value(rf'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\{ext}\UserChoice', 'ProgId') == default
        uninstall()
        assert value(UNINSTALL, 'UninstallString') is None
        assert value(r'Software\Classes\VynxArc.Archive', '') is None
        assert settings.exists() and {p.name: p.read_bytes() for p in working.iterdir()} == preserved
        for ext, default in defaults.items():
            assert not has_value(rf'Software\Classes\{ext}\OpenWithProgids', 'VynxArc.Archive')
            assert value(rf'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\{ext}\UserChoice', 'ProgId') == default
        report.update(status='passed', settings_preserved=True, working_copy_preserved=True,
                      defaults_unchanged=True, open_with_registered_and_removed=True,
                      installed_gui_smoke='passed', uninstall='passed',
                      old_setup_sha256=hashlib.sha256(args.old.read_bytes()).hexdigest(),
                      new_setup_sha256=hashlib.sha256(args.new.read_bytes()).hexdigest())
    except Exception as error:
        report.update(status='failed', error=repr(error))
        raise
    finally:
        uninstall()
        if original is None:
            settings.unlink(missing_ok=True)
        else:
            settings.write_bytes(original)
        # Only the two named files inside this newly allocated UUID are ours.
        if working.is_dir():
            for name in ('note.txt', '.edit.json'):
                file = working / name
                if file.is_file():
                    (root / ('retained-' + name.lstrip('.'))).write_bytes(file.read_bytes())
                    file.unlink()
            working.rmdir()
        (root / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Installer {args.old_version} -> {args.new_version}, Open With, preserved data and uninstall: passed')


if __name__ == '__main__':
    main()
