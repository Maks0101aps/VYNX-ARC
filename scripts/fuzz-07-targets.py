"""Bounded protocol mutations and real 0.7 GUI qualification; no optional skips."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import zipfile


def run(command, env):
    result = subprocess.run([str(x) for x in command], env=env, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError((result.returncode, (result.stdout + result.stderr).decode(errors='replace')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, default=Path('build-msvc'))
    parser.add_argument('--test-dir', type=Path)
    parser.add_argument('--output', type=Path, default=Path('.dev/qualification-0.7.json'))
    args = parser.parse_args()
    bins = {name: args.bin_dir.resolve() / name for name in ('VynxArc.exe', 'vynxarc-cli.exe', 'vynxarc-worker.exe')}
    bins['VynxPresentationTests.exe'] = (args.test_dir or args.bin_dir).resolve() / 'VynxPresentationTests.exe'
    for path in bins.values():
        if not path.is_file():
            raise FileNotFoundError(f'Required binary missing: {path}')
    import py7zr
    import pyzipper
    env = os.environ.copy()
    qt = env.get('VYNX_QT_ROOT') or str(Path(env.get('LOCALAPPDATA', '')) / 'VynxArcDev/QtMSVC')
    env['PATH'] = str(Path(qt) / 'bin') + os.pathsep + env.get('PATH', '')
    # Deployed Windows packages intentionally ship qwindows, not SDK-only test plugins.
    env['QT_QPA_PLATFORM'] = 'windows'
    env['QT_ASSUME_STDERR_HAS_CONSOLE'] = '1'
    report = {'status': 'running', 'binaries': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in bins.items()},
              'scope': 'Bounded regression suite, not coverage-guided fuzzing or security certification'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        rng = random.Random(0x070001)
        frames = [b'', b'VYNXWP03', b'VYNXWP04', b'VYNXWP04' + (16 * 1024 * 1024 + 1).to_bytes(4, 'little')]
        for _ in range(50):
            payload = rng.randbytes(rng.randint(0, 1024))
            magic = b'VYNXWP04' if rng.choice((True, False)) else rng.randbytes(8)
            frames.append(magic + len(payload).to_bytes(4, 'little') + payload)
        for frame in frames:
            result = subprocess.run([str(bins['vynxarc-worker.exe'])], input=frame, env=env, capture_output=True, timeout=5)
            if result.returncode not in (1, 2):
                raise RuntimeError(f'Malformed frame not rejected: {result.returncode}')
        report['malformed_frames'] = len(frames)
        with tempfile.TemporaryDirectory(prefix='vynx-07-') as temporary:
            root = Path(temporary) / 'product'
            run([bins['VynxArc.exe'], '--smoke-test', '--product-smoke', root], env)
            report['gui'] = json.loads((root / 'product.json').read_text(encoding='utf-8'))
            expected = {'note.txt': b'original', 'note (1).txt': b'reserved', 'note (2).txt': b'source bytes', 'folder/child.txt': b'folder bytes'}
            with zipfile.ZipFile(root / 'target.zip') as archive:
                for name, content in expected.items():
                    assert archive.read(name) == content, name
            factory = py7zr.io.BytesIOFactory(limit=1024 * 1024)
            with py7zr.SevenZipFile(root / 'target.7z') as archive:
                archive.extractall(factory=factory)
            for name, content in expected.items():
                assert factory.get(name).read() == content, name
            with pyzipper.AESZipFile(root / 'source.zip') as archive:
                archive.setpassword(b'test-secret')
                assert archive.read('note.txt') == b'source bytes'
                assert archive.read('folder/child.txt') == b'folder bytes'
            report['independent_readers'] = 'zipfile, py7zr, pyzipper: exact target and source bytes'
        tests = ['journalMigrationFrom06', 'workingCopiesPersistAndNeverJoinDisposableCleanup', 'journalSurvivesARealKilledProcess', 'extractionRecoveryReconciliation', 'archiveDragIncludesSourceIdentity']
        run([bins['VynxPresentationTests.exe'], *tests], env)
        report['cpp_regressions'] = tests
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        with args.output.open('w', encoding='utf-8', newline='\n') as output:
            json.dump(report, output, indent=2)
            output.write('\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
