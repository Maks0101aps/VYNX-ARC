"""Bisect the packaged verifier lifecycle; retain every command and failure."""
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
SEQUENCES = ['create', 'list', 'test', 'extract', 'hash', 'smart', 'modify', 'rar', 'gui', 'full']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--location', choices=['project', 'temp'], required=True)
    parser.add_argument('--sequence', choices=SEQUENCES + ['all'], default='all')
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('--cli', type=pathlib.Path)
    parser.add_argument('--motw', choices=['absent', 'present', 'independent'], default='absent')
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--skip-zone', action='store_true', help='Diagnostic-feature build only; never use for acceptance')
    parser.add_argument('--full-prefix', choices=['zip', '7z', 'tar', 'tar.gz', 'rar'], default='rar',
                        help='Stop the full verifier prefix at this format before ZIP rename')
    parser.add_argument('--last-format-stage', choices=['create', 'test', 'list', 'extract', 'smart'], default='smart',
                        help='Stop the last non-ZIP format lifecycle at this stage')
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    if args.skip_zone and not args.diagnostics:
        parser.error('--skip-zone requires --diagnostics and a diagnostic-feature CLI')
    parent = ROOT / '.dev' if args.location == 'project' else pathlib.Path(tempfile.gettempdir())
    work = pathlib.Path(tempfile.mkdtemp(prefix='zip-publication-', dir=parent))
    app = work / 'app'
    with zipfile.ZipFile(ROOT / 'dist/VYNX-ARC-Portable-x64.zip') as package:
        package.extractall(app)
    cli = args.cli.resolve() if args.cli else app / 'vynxarc-cli.exe'
    env = os.environ.copy()
    env['PATH'] = os.environ['SystemRoot'] + ';' + str(pathlib.Path(os.environ['SystemRoot']) / 'System32')
    for name in ['QTDIR', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'VYNX_QT_ROOT']:
        env.pop(name, None)
    if args.diagnostics:
        env['VYNX_DIAGNOSTIC_PYTHON'] = sys.executable
        env['VYNX_DIAGNOSTIC_HELPER'] = str(ROOT / 'scripts/diagnose-publication.py')
        env['VYNX_DIAGNOSTIC_ON_FAILURE_ONLY'] = '1'
        if args.skip_zone:
            env['VYNX_DIAGNOSTIC_SKIP_ZONE'] = '1'
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    log = work / 'commands.jsonl'
    records = []

    def run(command):
        begin = datetime.datetime.now(datetime.timezone.utc).isoformat()
        p = subprocess.run([str(x) for x in command], env=env, capture_output=True, timeout=120, startupinfo=startup)
        row = {'started_utc': begin, 'finished_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'command': [str(x) for x in command], 'exit_code': p.returncode,
               'stdout': p.stdout.decode('utf-8', errors='replace'), 'stderr': p.stderr.decode('utf-8', errors='replace')}
        with log.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(row, ensure_ascii=False) + '\n')
        if p.returncode:
            raise RuntimeError(row)
        return row['stdout']

    sequences = SEQUENCES if args.sequence == 'all' else [args.sequence]
    for sequence in sequences:
        for iteration in range(args.runs):
            case = work / f'{sequence}-{iteration}'
            source = case / 'Project'
            (source / 'src').mkdir(parents=True)
            (source / 'README.md').write_text('# Archive verification\nReal test files for VYNX ARC.\n', encoding='utf-8')
            (source / 'src/дані.txt').write_text('Перевірка архівів — справжні байти.\n', encoding='utf-8')
            (source / 'empty.txt').write_bytes(b'')
            target = case / 'Project.zip'
            try:
                if sequence in ['gui', 'full']:
                    run([app / 'VynxArc.exe', '--smoke-test'])
                run([cli, 'create', target, source])
                if args.motw != 'absent':
                    zone = b'[ZoneTransfer]\r\nZoneId=3\r\n'
                    if args.motw == 'independent':
                        ads = case / 'zone.txt'
                        ads.write_bytes(zone)
                        zone = ads.read_bytes()
                    pathlib.Path(str(target) + ':Zone.Identifier').write_bytes(zone)
                if sequence in ['list', 'full']:
                    run([cli, 'list', target])
                if sequence in ['test', 'full']:
                    run([cli, 'test', target])
                if sequence == 'full':
                    with zipfile.ZipFile(target) as z:
                        assert z.read('Project/src/дані.txt') == (source / 'src/дані.txt').read_bytes()
                    run([cli, 'extract', target, '--output', case / 'extracted', '--replace'])
                    run([cli, 'extract', target, '--output', case / 'smart', '--smart'])
                    # Exact verifier prefix: complete all other writable formats
                    # and independent 7Z reads before RAR and ZIP modification.
                    import py7zr
                    preceding = ['7z', 'tar', 'tar.gz']
                    count = 0 if args.full_prefix == 'zip' else (
                        len(preceding) if args.full_prefix == 'rar' else preceding.index(args.full_prefix) + 1)
                    for ext in preceding[:count]:
                        other = case / f'Project.{ext}'
                        run([cli, 'create', other, source])
                        if ext == args.full_prefix and args.last_format_stage == 'create':
                            break
                        run([cli, 'test', other])
                        if ext == args.full_prefix and args.last_format_stage == 'test':
                            break
                        run([cli, 'list', other])
                        if ext == args.full_prefix and args.last_format_stage == 'list':
                            break
                        if ext == '7z':
                            with py7zr.SevenZipFile(other) as z:
                                z.extractall(case / 'python-7z')
                        run([cli, 'extract', other, '--output', case / f'extract-{ext}', '--replace'])
                        if ext == args.full_prefix and args.last_format_stage == 'extract':
                            break
                        run([cli, 'extract', other, '--output', case / f'smart-{ext}', '--smart'])
                if sequence == 'extract':
                    run([cli, 'extract', target, '--output', case / 'extracted', '--replace'])
                if sequence == 'smart':
                    run([cli, 'extract', target, '--output', case / 'smart', '--smart'])
                if sequence == 'hash':
                    run([cli, 'hash-entry', target, 'Project/src/дані.txt'])
                if sequence == 'modify':
                    extra = case / 'added.txt'
                    extra.write_bytes(b'added')
                    run([cli, 'add', target, extra])
                    run([cli, 'delete', target, 'added.txt'])
                if sequence == 'rar' or sequence == 'full' and args.full_prefix == 'rar':
                    for name in ['test_read_format_rar_binary_data.rar', 'test_read_format_rar5_stored.rar',
                                 'test_rar_multivolume_single_file.part1.rar', 'test_read_format_rar5_multiarchive_solid.part01.rar']:
                        for _ in range(10 if sequence == 'full' else 1):
                            run([cli, 'list', ROOT / 'tests/archives' / name])
                            run([cli, 'test', ROOT / 'tests/archives' / name])
                run([cli, 'rename', target, 'Project/src/дані.txt', 'Project/src/renamed.txt'])
                with zipfile.ZipFile(target) as z:
                    assert z.read('Project/src/renamed.txt') == (source / 'src/дані.txt').read_bytes()
                if args.motw != 'absent' and not args.skip_zone:
                    assert pathlib.Path(str(target) + ':Zone.Identifier').read_bytes() == zone
                if args.skip_zone:
                    assert not pathlib.Path(str(target) + ':Zone.Identifier').exists()
                records.append({'sequence': sequence, 'iteration': iteration, 'status': 'passed', 'archive': str(target)})
            except Exception as error:
                records.append({'sequence': sequence, 'iteration': iteration, 'status': 'failed', 'archive': str(target), 'error': str(error)})
            print(json.dumps(records[-1]), flush=True)
    report = {'cli': str(cli), 'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(),
              'location': args.location, 'motw': args.motw, 'diagnostic_skip_zone': args.skip_zone,
              'full_prefix': args.full_prefix,
              'last_format_stage': args.last_format_stage,
              'work': str(work), 'records': records}
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Evidence: {work}', flush=True)
    return int(any(r['status'] != 'passed' for r in records))


if __name__ == '__main__':
    sys.exit(main())
