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
    parser.add_argument('--workspace', type=pathlib.Path, help='Reuse an owned stress workspace for ON/OFF controls')
    parser.add_argument('--evidence', type=pathlib.Path, help='New directory for this run\'s immutable evidence')
    parser.add_argument('--continue-on-failure', action='store_true', help='Attempt all cycles; never retry a failed operation')
    args = parser.parse_args()
    if args.cycles < 100:
        parser.error('Acceptance requires at least 100 cycles')
    parent = ROOT / '.dev' if args.location == 'project' else pathlib.Path(tempfile.gettempdir())
    work = args.workspace.resolve() if args.workspace else pathlib.Path(tempfile.mkdtemp(prefix='zip-stress-', dir=parent))
    if args.workspace:
        if work.parent != parent.resolve() or not work.name.startswith('zip-stress-'):
            parser.error('Reusable workspace must be a direct child of the location root named zip-stress-*')
        marker = work / '.vynx-stress-owned'
        if work.exists() and not marker.is_file():
            parser.error('Refusing to reset a workspace without the stress ownership marker')
        work.mkdir(exist_ok=True)
        marker.write_text('VYNX publication stress fixtures\n')
    evidence = args.evidence.resolve() if args.evidence else work
    if args.evidence:
        evidence.mkdir(parents=True, exist_ok=False)
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
              'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'portable_sha256': hashlib.sha256((ROOT / 'dist/VYNX-ARC-Portable-x64.zip').read_bytes()).hexdigest(),
              'continue_on_failure': args.continue_on_failure,
              'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(), 'categories': []}
    log = evidence / 'commands.jsonl'
    checks = evidence / 'publication-checks.jsonl'

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
        case.mkdir(exist_ok=True)
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
            if args.workspace and archive.exists():
                archive.unlink()
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
            row = {'category': category, 'motw': motw, 'completed': 0, 'attempted_cycles': 0,
                   'publication_attempts': 0, 'publication_successes': 0, 'failures': [],
                   'original_preserved': True, 'status': 'running'}
            report['categories'].append(row)
            def publish(command, update):
                before = hashlib.sha256(archive.read_bytes()).hexdigest()
                row['publication_attempts'] += 1
                try:
                    run(*command)
                except Exception as error:
                    after = hashlib.sha256(archive.read_bytes()).hexdigest()
                    preserved = before == after
                    try:
                        verify()
                    except Exception:
                        preserved = False
                    row['original_preserved'] &= preserved
                    row['failures'].append({'cycle': i, 'error': str(error), 'before_sha256': before,
                                            'after_sha256': after, 'original_preserved': preserved})
                    with checks.open('a', encoding='utf-8') as stream:
                        stream.write(json.dumps({'category': category, 'motw': motw, 'cycle': i,
                                                'command': list(map(str, command)), 'success': False,
                                                'before_sha256': before, 'after_sha256': after,
                                                'contents_verified': preserved}) + '\n')
                    raise
                update()
                row['publication_successes'] += 1
                verify()
                after = hashlib.sha256(archive.read_bytes()).hexdigest()
                with checks.open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps({'category': category, 'motw': motw, 'cycle': i,
                                            'command': list(map(str, command)), 'success': True,
                                            'before_sha256': before, 'after_sha256': after,
                                            'contents_verified': True}) + '\n')
            try:
                for i in range(args.cycles):
                    row['attempted_cycles'] += 1
                    added = case / f'added-{i}.txt'
                    name = added.name
                    data = f'content-{i}'.encode()
                    failures_before = len(row['failures'])
                    try:
                        if category in ['add', 'mixed']:
                            added.write_bytes(data)
                            publish(('add', archive, added), lambda: expected.update({name: data}))
                        if category in ['rename', 'mixed']:
                            old = original.name if original.name in expected else 'renamed.txt'
                            new = 'renamed.txt' if old == original.name else original.name
                            publish(('rename', archive, old, new), lambda: expected.update({new: expected.pop(old)}))
                        if category in ['delete', 'mixed']:
                            publish(('delete', archive, name), lambda: expected.pop(name))
                        if category == 'mixed':
                            publish(('rename', archive, new, old), lambda: expected.update({old: expected.pop(new)}))
                        row['completed'] += 1
                    except Exception:
                        if (not args.continue_on_failure or not row['original_preserved']
                                or len(row['failures']) == failures_before):
                            raise
                run('test', archive)
                row['status'] = 'failed' if row['failures'] else 'passed'
            except Exception as error:
                row.update(status='failed', error=str(error))
                try:
                    verify()
                    row['original_preserved'] &= True
                except Exception as preservation_error:
                    row['original_preserved'] = False
                    row['preservation_error'] = str(preservation_error)
            print(json.dumps({k: v for k, v in row.items() if k not in ['error', 'failures']}, ensure_ascii=False), flush=True)
            (evidence / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (evidence / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Evidence: {evidence}', flush=True)
    return int(any(row['status'] != 'passed' for row in report['categories']))


if __name__ == '__main__':
    sys.exit(main())
