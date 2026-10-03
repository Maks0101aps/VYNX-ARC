"""Controlled native CLI create/extract preset/resource matrix; no competitor claims."""
import argparse
import csv
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import json
import pathlib
import random
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


class MemoryCounters(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in ['PeakWorkingSetSize', 'WorkingSetSize',
        'QuotaPeakPagedPoolUsage', 'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage',
        'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage', 'PrivateUsage']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mib', type=int, default=32)
    parser.add_argument('--cli', type=pathlib.Path, default=ROOT / 'target/release/vynxarc-cli.exe')
    args = parser.parse_args()
    cli = args.cli.resolve()
    if not 1 <= args.mib <= 256:
        parser.error('Use a bounded 1–256 MiB dataset')
    work = pathlib.Path(tempfile.mkdtemp(prefix='vynx-dev-benchmark-'))
    report = ROOT / '.dev' / ('benchmark-development-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    report.mkdir()
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    datasets = {}
    generator = random.Random(7042)
    for kind in ['random', 'compressible', 'small-files', 'mixed']:
        directory = work / kind
        directory.mkdir()
        if kind in ['random', 'compressible']:
            data = generator.randbytes(args.mib << 20) if kind == 'random' else (b'archive benchmark\n' * ((args.mib << 20) // 18 + 1))[:args.mib << 20]
            (directory / 'large.bin').write_bytes(data)
        elif kind == 'small-files':
            for i in range(1000):
                (directory / f'{i:04}.txt').write_bytes((f'record {i}\n' * 200).encode())
        else:
            (directory / 'random.bin').write_bytes(generator.randbytes((args.mib << 20) // 2))
            (directory / 'compressible.bin').write_bytes(b'abcde' * ((args.mib << 20) // 10))
            for i in range(128):
                (directory / f'{i:04}.txt').write_bytes(f'mixed record {i}\n'.encode() * 100)
        datasets[kind] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir()}
    identity = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip(),
                'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(), 'cli': str(cli),
                'workspace': str(work), 'mib': args.mib, 'repeats': 1,
                'dataset_sha256': datasets, 'metric_scope': 'whole native process, including create verification; cold/warm cache uncontrolled'}
    (report / 'identity.json').write_text(json.dumps(identity, indent=2))
    rows = []

    def measure(command, kind, fmt, preset, resource, operation, source_bytes, output):
        started = time.perf_counter()
        p = subprocess.Popen([str(cli), *map(str, command)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        peak = 0
        while p.poll() is None:
            counters = MemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            if psapi.GetProcessMemoryInfo(int(p._handle), ctypes.byref(counters), counters.cb):
                peak = max(peak, counters.PeakWorkingSetSize)
            time.sleep(0.01)
        stdout, stderr = p.communicate()
        wall = time.perf_counter() - started
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(int(p._handle), *map(ctypes.byref, times)):
            raise ctypes.WinError(ctypes.get_last_error())
        cpu = sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 10_000_000
        row = {'dataset': kind, 'format': fmt, 'preset': preset, 'resource': resource, 'operation': operation,
               'wall_seconds': wall, 'cpu_seconds': cpu, 'peak_rss_bytes': peak,
               'input_bytes': source_bytes, 'archive_bytes': output.stat().st_size if output.exists() else 0,
               'throughput_mib_s': source_bytes / wall / (1 << 20), 'exit_code': p.returncode,
               'effective_settings': stdout.decode(errors='replace').strip() if operation == 'create' else ''}
        rows.append(row)
        with (report / 'results.csv').open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=row.keys())
            writer.writeheader()
            writer.writerows(rows)
        if p.returncode:
            (report / 'failure.json').write_text(json.dumps({'row': row, 'command': list(map(str, command)), 'stderr': stderr.decode(errors='replace')}, indent=2))
            raise RuntimeError(stderr.decode(errors='replace'))

    for kind, expected in datasets.items():
        source = work / kind
        size = sum(p.stat().st_size for p in source.iterdir())
        for fmt in ['zip', '7z']:
            for preset in ['store', 'fast', 'balanced', 'maximum']:
                for resource in ['eco', 'balanced', 'maximum']:
                    name = f'{kind}-{fmt}-{preset}-{resource}'
                    output = work / (name + '.' + fmt)
                    dest = work / (name + '-out')
                    measure(['create', output, source, '--preset', preset, '--resource', resource], kind, fmt, preset, resource, 'create', size, output)
                    measure(['extract', output, '--output', dest, '--resource', resource], kind, fmt, preset, resource, 'extract', size, output)
                    actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (dest / kind).iterdir()}
                    if actual != expected:
                        raise RuntimeError('Independent extracted hashes differ')
                    # Remove only verified generated extraction files to bound disk use.
                    for p in (dest / kind).iterdir():
                        p.unlink()
                    (dest / kind).rmdir()
                    dest.rmdir()
            print(f'{kind} {fmt}: 24 process measurements verified', flush=True)
    (report / 'complete.json').write_text(json.dumps({'measurements': len(rows), 'all_extracted_hashes_verified': True}, indent=2))
    print(f'Evidence: {report}', flush=True)


if __name__ == '__main__':
    main()
