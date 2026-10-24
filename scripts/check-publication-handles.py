"""Qualify native archive replacement with controlled Windows handle sharing.

Uses owned fixtures only; never closes another process handle or changes policy.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import pathlib
import subprocess
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    work = pathlib.Path(tempfile.mkdtemp(prefix='vynx-share-probe-'))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--baseline', action='store_true', help='Measure without expecting compatible handles to succeed')
    args = parser.parse_args()
    cli = args.cli.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    results = []
    for label, access, share in [('pylance_attributes_none', 0x100080, 0),
                                  ('generic_read_all_share', 0x80000000, 7),
                                  ('generic_read_no_delete_share', 0x80000000, 3)]:
        archive = work / (label + '.zip')
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('old.txt', b'original content')
        before = hashlib.sha256(archive.read_bytes()).hexdigest()
        control = work / (label + '.control.zip')
        control.write_bytes(archive.read_bytes())
        baseline = subprocess.run([str(cli), 'rename', str(control), 'old.txt', 'new.txt'],
                                  capture_output=True, timeout=30)
        handle = kernel.CreateFileW(str(archive), access, share, None, 3, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            p = subprocess.run([str(cli), 'rename', str(archive), 'old.txt', 'new.txt'],
                               capture_output=True, timeout=30)
        finally:
            kernel.CloseHandle(handle)
        after = hashlib.sha256(archive.read_bytes()).hexdigest()
        with zipfile.ZipFile(archive) as z:
            content = {name: z.read(name).decode() for name in z.namelist()}
        results.append({'label': label, 'desired_access': hex(access), 'share_mode': share,
                        'independent_unheld_control_exit': baseline.returncode,
                        'independent_unheld_control_stderr': baseline.stderr.decode(errors='replace'),
                        'exit_code': p.returncode, 'stderr': p.stderr.decode(errors='replace'),
                        'before_sha256': before, 'after_sha256': after,
                        'unchanged_on_failure': p.returncode != 0 and before == after,
                        'content': content})
    if not args.baseline:
        assert all(r['independent_unheld_control_exit'] == 0 for r in results)
        assert all(r['exit_code'] == 0 and r['content'] == {'new.txt': 'original content'} for r in results[:2])
        assert results[2]['unchanged_on_failure'] and results[2]['content'] == {'old.txt': 'original content'}
    report = {'status': 'measured baseline' if args.baseline else 'passed',
              'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(),
              'worker_sha256': hashlib.sha256(cli.with_name('vynxarc-worker.exe').read_bytes()).hexdigest(),
              'workspace': str(work), 'results': results}
    (work / 'results.json').write_text(json.dumps(report, indent=2))
    (args.output / 'results.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
