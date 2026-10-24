"""Deterministic mutation fuzz smoke; bounded child job, timeouts and retained failures."""
import argparse
import binascii
import datetime
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cases', type=int, default=256)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--operation', default='list')
    args = parser.parse_args()
    cli = args.cli.resolve()
    if args.worker:
        from windows_test_limits import install
        install()
        result = subprocess.run([str(cli), args.operation, str(args.worker)], capture_output=True, timeout=4)
        # CLI rejects bad input with 1 or 2. Native exceptions/abort/OOM are failures.
        if result.returncode not in [0, 1, 2]:
            sys.stderr.buffer.write(result.stderr)
            raise RuntimeError(f'CLI crash: {result.returncode:#x}')
        return
    if not args.output or args.cases < 1:
        parser.error('--output must name a new evidence directory; --cases must be positive')
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.output / 'source.txt'
    source.write_text('UTF-8 test текст\n' * 100, encoding='utf-8')
    seeds = []
    for ext in ['rar', 'zip', '7z', 'tar', 'tar.gz', 'gz', 'xz', 'bz2', 'zst', 'lzma', 'tar.xz', 'tar.bz2', 'tar.zst']:
        path = args.output / f'seed.{ext}'
        subprocess.run([str(cli), 'create', str(path), str(source)], check=True, capture_output=True, timeout=15)
        seeds.append(path.read_bytes())
    rng = random.Random(0x56594E58)
    rows = []
    failed = False
    for index in range(args.cases):
        data = bytearray(seeds[index % len(seeds)])
        mode = index % 7
        if mode == 0:
            del data[rng.randrange(len(data)):]
        elif mode == 1:
            data.extend(rng.randbytes(rng.randrange(1, 100)))
        else:
            for _ in range(rng.randrange(1, 12)):
                data[rng.randrange(len(data))] = rng.randrange(256)
            # Keep 7Z CRCs valid in half the mutations to exercise deeper parsing.
            if index % 2 == 1 and len(data) > 32 and data.startswith(b'7z\xbc\xaf\x27\x1c'):
                offset=int.from_bytes(data[12:20],'little')+32
                size=int.from_bytes(data[20:28],'little')
                if offset+size <= len(data):
                    data[28:32] = binascii.crc32(data[offset:offset+size]).to_bytes(4, 'little')
                data[8:12] = binascii.crc32(data[12:32]).to_bytes(4, 'little')
        path = args.output / f'case-{index:04}.bin'
        path.write_bytes(data)
        try:
            result = subprocess.run([sys.executable, __file__, '--cli', str(cli), '--worker', str(path),
                                     '--operation', 'test' if index % 5 == 0 else 'list'],
                                    capture_output=True, timeout=7)
            ok = result.returncode == 0
            error = result.stderr.decode('utf-8', errors='replace') if not ok else ''
        except subprocess.TimeoutExpired:
            ok, error = False, 'timeout'
        row = {'case': index, 'sha256': hashlib.sha256(data).hexdigest(),
               'status': 'passed' if ok else 'failed', 'error': error}
        rows.append(row)
        failed |= not ok
        if ok:
            path.unlink()
    report = {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(), 'seed': '0x56594E58',
              'job_memory_mib': 512, 'status': 'failed' if failed else 'passed', 'cases': rows,
              'scope': 'Mutation smoke, not coverage-guided fuzzing or a security certification'}
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{sum(r["status"] == "passed" for r in rows)}/{len(rows)} parser mutations passed')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
