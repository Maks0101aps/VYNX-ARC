"""Controlled cross-process locks: one failed update, byte preservation, explicit retry."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def hold(path):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        print('LOCKED', flush=True)
        sys.stdin.buffer.read(1)
    finally:
        kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--hold', type=Path)
    args = parser.parse_args()
    if args.hold:
        hold(args.hold)
        return
    if not args.cli or not args.output or os.name != 'nt':
        parser.error('Windows, --cli and a new --output directory are required')
    args.output.mkdir(parents=True, exist_ok=False)
    cli = args.cli.resolve()
    rows = []
    try:
        with tempfile.TemporaryDirectory(prefix='lock-check-', dir=args.output) as scratch:
            work = Path(scratch)
            original = work / 'original.txt'
            original.write_bytes(b'original')
            added = work / 'added.txt'
            added.write_bytes(b'addition')

            def run(*command):
                return subprocess.run([str(cli), *map(str, command)], capture_output=True, timeout=30)

            for ext in ['zip', '7z']:
                for kind in ['add', 'delete', 'rename']:
                    archive = work / f'{ext}-{kind}.{ext}'
                    assert run('create', archive, original).returncode == 0
                    zone = Path(str(archive) + ':Zone.Identifier')
                    zone.write_bytes(b'[ZoneTransfer]\r\nZoneId=3\r\n')
                    before = hashlib.sha256(archive.read_bytes()).hexdigest()
                    command = {'add': ['add', archive, added],
                               'delete': ['delete', archive, 'original.txt'],
                               'rename': ['rename', archive, 'original.txt', 'renamed.txt']}[kind]
                    holder = subprocess.Popen([sys.executable, __file__, '--hold', str(archive)],
                                              stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE)
                    try:
                        assert holder.stdout.readline().strip() == b'LOCKED'
                        failed = run(*command)
                        error = failed.stderr.decode('utf-8', errors='replace')
                        assert failed.returncode == 1, (failed.returncode, error)
                        assert 'ARCHIVE_BUSY' in error or 'ACCESS_DENIED' in error, error
                        assert 'was not replaced' in error, error
                        assert hashlib.sha256(archive.read_bytes()).hexdigest() == before
                        assert zone.read_bytes() == b'[ZoneTransfer]\r\nZoneId=3\r\n'
                        assert not list(work.glob('.tmp*')), 'Staging file leaked'
                    finally:
                        holder.stdin.write(b'X')
                        holder.stdin.flush()
                        holder.communicate(timeout=5)
                    # New CLI invocation reopens metadata. It never reuses a stale
                    # failed transaction or silently retries the failed publication.
                    retried = run(*command)
                    assert retried.returncode == 0, retried.stderr
                    assert run('test', archive).returncode == 0
                    assert zone.read_bytes() == b'[ZoneTransfer]\r\nZoneId=3\r\n'
                    rows.append({'format': ext, 'operation': kind, 'status': 'passed',
                                 'failed_stderr': error, 'original_sha256': before,
                                 'explicit_retry': 'passed'})
    finally:
        (args.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(f'{len(rows)}/6 cross-process lock/preservation/retry checks passed')


if __name__ == '__main__':
    main()
