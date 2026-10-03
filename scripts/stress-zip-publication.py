"""Single-attempt packaged ZIP publication matrix; no retries or sleeps."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--location', choices=['project', 'temp'], required=True)
    parser.add_argument('--cycles', type=int, default=100)
    parser.add_argument('--cli', type=pathlib.Path)
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--category', choices=['all', 'add', 'delete', 'rename', 'mixed'], default='all')
    args = parser.parse_args()
    if args.cycles < 100:
        parser.error('Acceptance requires at least 100 cycles')
    parent = ROOT / '.dev' if args.location == 'project' else pathlib.Path(tempfile.gettempdir())
    work = pathlib.Path(tempfile.mkdtemp(prefix='zip-stress-', dir=parent))
    with zipfile.ZipFile(ROOT / 'dist/VYNX-ARC-Portable-x64.zip') as z:
        z.extractall(work / 'app')
    cli = args.cli.resolve() if args.cli else work / 'app/vynxarc-cli.exe'
    env = os.environ.copy()
    env['PATH'] = os.environ['SystemRoot'] + ';' + str(pathlib.Path(os.environ['SystemRoot']) / 'System32')
    for name in ['QTDIR', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'VYNX_QT_ROOT',
                 'VYNX_DIAGNOSTIC_PYTHON', 'VYNX_DIAGNOSTIC_HELPER']:
        env.pop(name, None)
    if args.diagnostics:
        env['VYNX_DIAGNOSTIC_PYTHON'] = sys.executable
        env['VYNX_DIAGNOSTIC_HELPER'] = str(ROOT / 'scripts/diagnose-publication.py')
        env['VYNX_DIAGNOSTIC_ON_FAILURE_ONLY'] = '1'
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    report = {'location': args.location, 'work': str(work), 'cycles': args.cycles,
              'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(), 'categories': []}
    log = work / 'commands.jsonl'

    def run(*command):
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        p = subprocess.run([str(cli), *map(str, command)], env=env, capture_output=True, timeout=30, startupinfo=startup)
        row = {'timestamp_utc': started, 'command': list(map(str, command)), 'exit_code': p.returncode,
               'stderr': p.stderr.decode('utf-8', errors='replace')}
        row['stdout'] = p.stdout.decode('utf-8', errors='replace')
        with log.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(row, ensure_ascii=False) + '\n')
        if p.returncode:
            raise RuntimeError(row)

    for motw in [False, True]:
        case = work / ('motw' if motw else 'plain')
        case.mkdir()
        original = case / 'дані.txt'
        original.write_bytes(b'original content')
        archive = case / 'archive.zip'
        zone = b'[ZoneTransfer]\r\nZoneId=3\r\n'
        expected = {original.name: original.read_bytes()}

        def verify():
            with zipfile.ZipFile(archive) as z:
                actual = {name: z.read(name) for name in z.namelist()}
            assert actual == expected, 'Published bytes/manifest differ'
            if motw:
                assert pathlib.Path(str(archive) + ':Zone.Identifier').read_bytes() == zone

        categories = ['add', 'delete', 'rename', 'mixed'] if args.category == 'all' else [args.category]
        for category in categories:
            archive = case / f'{category}.zip'
            run('create', archive, original)
            expected = {original.name: original.read_bytes()}
            if category == 'delete':
                # Independent seed avoids requiring successful add publication
                # before testing deletion. Each category has its own archive.
                with zipfile.ZipFile(archive, 'a', compression=zipfile.ZIP_DEFLATED) as z:
                    for i in range(args.cycles):
                        name, data = f'added-{i}.txt', f'content-{i}'.encode()
                        z.writestr(name, data)
                        expected[name] = data
            if motw:
                pathlib.Path(str(archive) + ':Zone.Identifier').write_bytes(zone)
            row = {'category': category, 'motw': motw, 'completed': 0, 'status': 'running'}
            report['categories'].append(row)
            try:
                for i in range(args.cycles):
                    added = case / f'added-{i}.txt'
                    name = added.name
                    data = f'content-{i}'.encode()
                    if category in ['add', 'mixed']:
                        added.write_bytes(data)
                        run('add', archive, added)
                        expected[name] = data
                    if category in ['rename', 'mixed']:
                        old = original.name if category == 'mixed' or i % 2 == 0 else 'renamed.txt'
                        new = 'renamed.txt' if old == original.name else original.name
                        run('rename', archive, old, new)
                        expected[new] = expected.pop(old)
                    if category in ['delete', 'mixed']:
                        run('delete', archive, name)
                        del expected[name]
                    if category == 'mixed':
                        run('rename', archive, 'renamed.txt', original.name)
                        expected[original.name] = expected.pop('renamed.txt')
                    verify()
                    row['completed'] += 1
                run('test', archive)
                row['status'] = 'passed'
            except Exception as error:
                row.update(status='failed', error=str(error))
                try:
                    verify()
                    row['original_preserved'] = True
                except Exception as preservation_error:
                    row['original_preserved'] = False
                    row['preservation_error'] = str(preservation_error)
            print(json.dumps({k: v for k, v in row.items() if k != 'error'}), flush=True)
            (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Evidence: {work}', flush=True)
    return int(any(row['status'] != 'passed' for row in report['categories']))


if __name__ == '__main__':
    sys.exit(main())
