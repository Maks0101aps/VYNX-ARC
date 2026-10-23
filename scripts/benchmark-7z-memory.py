"""Compare real 7Z resource modes, with independent round-trip verification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import tempfile
import time

import psutil
import py7zr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--trials', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.trials <= 10:
        parser.error('--trials must be between 1 and 10')
    if args.output.exists():
        parser.error('Output exists; choose a new evidence path')
    cli = args.cli.resolve()
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join((os.environ['SystemRoot'],
                                   str(Path(os.environ['SystemRoot']) / 'System32')))
    samples = []
    # Retain fixtures beside the ignored report so a failure stays inspectable.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='7z-memory-', dir=args.output.parent))
    source = root / 'random16.bin'
    source.write_bytes(random.Random(81029).randbytes(16 * 1024 * 1024))
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    for mode in ('eco', 'balanced', 'maximum'):
        for trial in range(args.trials):
            archive = root / f'{mode}-{trial}.7z'
            started = time.perf_counter()
            process = subprocess.Popen(
                [str(cli), 'create', str(archive.resolve()), str(source.resolve()),
                 '--resource', mode], env=env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
            observed = psutil.Process(process.pid)
            peak = 0
            while process.poll() is None:
                try:
                    peak = max(peak, observed.memory_info().rss)
                except psutil.NoSuchProcess:
                    pass
                if time.perf_counter() - started > 120:
                    process.kill()
                    process.communicate()
                    raise TimeoutError(f'7Z {mode} exceeded 120 seconds')
                time.sleep(0.01)
            stdout, stderr = process.communicate()
            elapsed = time.perf_counter() - started
            if process.returncode:
                raise RuntimeError(stderr.decode(errors='replace'))
            destination = root / f'{mode}-{trial}-out'
            with py7zr.SevenZipFile(archive) as reader:
                reader.extractall(destination)
            restored = list(destination.rglob('random16.bin'))
            if len(restored) != 1 or hashlib.sha256(restored[0].read_bytes()).hexdigest() != source_hash:
                raise RuntimeError('Independent 7Z round-trip differs')
            samples.append(dict(resource=mode, trial=trial + 1, rss_bytes=peak,
                                seconds=elapsed, settings=stdout.decode().strip(),
                                independent_roundtrip=True))
    report = dict(cli_sha256=hashlib.sha256(cli.read_bytes()).hexdigest(),
                  source_sha256=source_hash, samples=samples,
                  method='10 ms sampled CLI RSS, 16 MiB deterministic random input; warm filesystem cache; not a hard memory bound')
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    for mode in ('eco', 'balanced', 'maximum'):
        rows = [row for row in samples if row['resource'] == mode]
        print(mode, round(statistics.median(row['rss_bytes'] for row in rows) / 2**20, 1),
              'MiB', round(statistics.median(row['seconds'] for row in rows), 3), 's')


if __name__ == '__main__':
    main()
