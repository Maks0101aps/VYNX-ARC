"""Measure real packaged create/extract runs on bounded, reproducible datasets."""
import hashlib
import json
import os
import pathlib
import platform
import random
import subprocess
import tempfile
import time
import zipfile

import psutil

ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = pathlib.Path(os.environ.get('VYNX_BENCH_STAGE', ROOT / 'dist/stage-906053756d6c449ebbe6ac20830ecbb6'))
CLI = APP / 'vynxarc-cli.exe'
OUT = ROOT / 'docs/RC_PERFORMANCE.json'
SCRATCH = pathlib.Path(tempfile.mkdtemp(prefix='rc-benchmark-', dir=ROOT / '.dev'))
STARTUP = subprocess.STARTUPINFO()
STARTUP.dwFlags = subprocess.STARTF_USESHOWWINDOW
STARTUP.wShowWindow = 0
ENV = os.environ.copy()
ENV['PATH'] = str(pathlib.Path(os.environ['SystemRoot']) / 'System32') + ';' + os.environ['SystemRoot']
for key in ['QTDIR', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'VYNX_QT_ROOT']:
    ENV.pop(key, None)


def tree_hash(directory):
    result = {}
    for file in sorted(pathlib.Path(directory).rglob('*')):
        if file.is_file():
            result[file.relative_to(directory).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
    return result


def measure(command):
    process = subprocess.Popen([str(x) for x in command], env=ENV, cwd=ROOT,
                               startupinfo=STARTUP, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    observed = psutil.Process(process.pid)
    peak = 0
    cpu_start = observed.cpu_times()
    cpu_seconds = 0.0
    started = time.perf_counter()
    while process.poll() is None:
        try:
            peak = max(peak, observed.memory_info().rss)
            cpu = observed.cpu_times()
            cpu_seconds = max(cpu_seconds, (cpu.user + cpu.system) -
                              (cpu_start.user + cpu_start.system))
        except psutil.Error:
            pass
        time.sleep(0.01)
    stdout, stderr = process.communicate(timeout=30)
    elapsed = time.perf_counter() - started
    if process.returncode:
        raise RuntimeError((process.returncode, stderr.decode(errors='replace')[-2000:]))
    return {'wall_seconds': round(elapsed, 4), 'cpu_seconds': round(cpu_seconds, 4) if cpu_seconds is not None else None,
            'peak_rss_bytes_observed_10ms': peak, 'stdout': stdout.decode(errors='replace').strip()[-500:]}


def write_dataset(name, recipe):
    root = SCRATCH / name
    root.mkdir()
    recipe(root)
    return root, sum(p.stat().st_size for p in root.rglob('*') if p.is_file())


def small(root):
    for i in range(1000):
        row = f'fixture row {i:04}\n'.encode()
        (root / f'file-{i:04}.txt').write_bytes((row * (1024 // len(row) + 1))[:1024])


def large(root):
    rng = random.Random(81027)
    for i in range(3):
        (root / f'large-{i}.bin').write_bytes(rng.randbytes(16 * 1024 * 1024))


def mixed(root):
    rng = random.Random(81028)
    for i in range(80):
        size = (i % 20 + 1) * 32768
        content = (b'VYNX ARC mixed dataset\n' * (size // 23 + 1))[:size] if i % 2 else rng.randbytes(size)
        (root / f'mixed-{i:03}.bin').write_bytes(content)


def compressible(root):
    (root / 'compressible.bin').write_bytes(b'VYNX ARC benchmark data\n' * (16 * 1024 * 1024 // 24))


def incompressible(root):
    (root / 'incompressible.bin').write_bytes(random.Random(81029).randbytes(16 * 1024 * 1024))


datasets = {'many_small': small, 'few_large': large, 'mixed': mixed,
            'compressible': compressible, 'incompressible': incompressible}
records = []
for name, recipe in datasets.items():
    source, source_bytes = write_dataset(name, recipe)
    expected = tree_hash(source)
    for fmt in ('zip', '7z'):
        archive = SCRATCH / f'{name}.{fmt}'
        create = measure([CLI, 'create', archive, source])
        archive_bytes = archive.stat().st_size
        output = SCRATCH / f'extract-{name}-{fmt}'
        extract = measure([CLI, 'extract', archive, '--output', output, '--replace'])
        extracted = output / name
        if tree_hash(extracted) != expected:
            raise RuntimeError(f'Extracted bytes differ: {name} {fmt}')
        for op_name, result in [('create', create), ('extract', extract)]:
            records.append({'dataset': name, 'format': fmt.upper(), 'operation': op_name,
                            'source_bytes': source_bytes, 'archive_bytes': archive_bytes,
                            'compression_setting': 'actual format default', **result})
    print(f'Measured {name}', flush=True)

# Real small RAR fixtures exercise native RAR4/RAR5 decoding; the CLI intentionally
# has no password argument, so encrypted-RAR performance is measured in core tests.
rar_fixtures = ['test_read_format_rar_binary_data.rar', 'test_read_format_rar5_compressed.rar',
                'test_rar_multivolume_single_file.part1.rar',
                'test_read_format_rar5_multiarchive_solid.part01.rar']
for fixture_name in rar_fixtures:
    archive = ROOT / 'tests/archives' / fixture_name
    listing = measure([CLI, 'list', archive])
    output = SCRATCH / f'rar-{fixture_name.replace(".rar", "")}'
    extraction = measure([CLI, 'extract', archive, '--output', output])
    records.append({'dataset': fixture_name, 'format': 'RAR', 'operation': 'list',
                    'source_bytes': archive.stat().st_size, **listing})
    records.append({'dataset': fixture_name, 'format': 'RAR', 'operation': 'extract',
                    'source_bytes': archive.stat().st_size, **extraction})
    print(f'Measured {fixture_name}', flush=True)

# Process-cold launches (Windows file cache remains managed by the OS).
gui = APP / 'VynxArc.exe'
for attempt in range(3):
    capture = SCRATCH / f'cold-start-{attempt}.png'
    row = measure([gui, '--capture', capture])
    records.append({'dataset': 'empty Home window', 'format': 'GUI',
                    'operation': 'fresh-process startup and capture', **row})
    print(f'Measured GUI startup {attempt + 1}/3', flush=True)

# Construct a real ZIP directory table with 100k entries, then measure the real
# packaged GUI's parse/model/sort/layout and screenshot exit path.
hundred_k = SCRATCH / 'hundred-k.zip'
with zipfile.ZipFile(hundred_k, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for index in range(100_000):
        archive.writestr(f'entry-{index:06}.txt', b'x')
capture = SCRATCH / 'hundred-k-browser.png'
row = measure([gui, hundred_k, '--capture', capture])
records.append({'dataset': '100000 one-byte entries', 'format': 'ZIP',
                'operation': 'GUI open, model/sort/layout and capture',
                'archive_bytes': hundred_k.stat().st_size, **row})
print('Measured 100k-entry GUI open', flush=True)

result = {
    'windows': platform.platform(), 'cpu': platform.processor(), 'logical_cpus': psutil.cpu_count(),
    'ram_bytes': psutil.virtual_memory().total,
    'windows_caption': 'Windows 11 Pro', 'windows_version': '10.0.26200',
    'storage': {'filesystem': 'NTFS', 'drive_type': 'fixed', 'media': 'NVMe'},
    'portable_sha256': hashlib.sha256((ROOT / 'dist/VYNX-ARC-Portable-x64.zip').read_bytes()).hexdigest(),
    'method': 'Packaged CLI; PATH restricted to Windows/System32; no developer Qt variables; real round-trip bytes compared; 10ms RSS polling.',
    'settings': 'ZIP and 7Z use the application format defaults; no simulated compression preset.',
    'dataset_recipes': {'many_small': '1000 x 1024-byte text files', 'few_large': '3 x 16 MiB deterministic random files',
                        'mixed': '80 files, alternating deterministic compressible/random data, 32 KiB to 640 KiB',
                        'compressible': 'approximately 16 MiB repeated text',
                        'incompressible': '16 MiB deterministic random data'},
    'records': records,
    'limitations': ['Wall and CPU operation measurements include process startup.',
                    'RSS peak is a 10ms observed peak, not an exact maximum.',
                    'RAR measurements use small real fixtures, not matched-size datasets.',
                    'The 7-Zip CLI has 10ms polling granularity; CPU samples can quantize to zero on short work.',
                    'No WinRAR/7-Zip comparison, clean VM, disk-cache control, or repeated trials.']
}
OUT.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f'Wrote {len(records)} actual measurements to {OUT}', flush=True)
