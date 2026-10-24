"""Test-only native RAR qualification. External decoders never ship with VYNX ARC."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import threading
import time

import psutil


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def tree(path):
    return {p.relative_to(path).as_posix(): sha(p) for p in path.rglob('*') if p.is_file()}


def decoder_sections(path):
    """mt.exe may change resources/PE headers, never independent decoder code/data."""
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    table = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    result = {}
    for index in range(count):
        section = table + index * 40
        name = data[section:section + 8].rstrip(b'\0').decode('ascii')
        size, offset = struct.unpack_from('<II', data, section + 16)
        if name != '.rsrc':
            result[name] = hashlib.sha256(data[offset:offset + size]).hexdigest()
    if '.text' not in result:
        raise RuntimeError('Independent decoder has no PE .text section')
    return result


def prepare_tar_oracle(root, env, startup):
    """Use the OS decoder with an explicit UTF-8 process ACP, only in QA output.

    MSVC setlocale(LC_ALL, "") uses OS ACP, not LC_ALL/LANG environment variables.
    A Windows-1252 ACP cannot represent the required Cyrillic fixture filename.
    https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page
    """
    original = Path(os.environ['SystemRoot']) / 'System32/tar.exe'
    sdk = Path(os.environ['ProgramFiles(x86)']) / 'Windows Kits/10/bin'
    manifests = sorted(sdk.glob('*/x64/mt.exe'), key=lambda p: tuple(int(v) for v in p.parent.parent.name.split('.')))
    if not manifests:
        raise RuntimeError('Windows SDK mt.exe is required for the test-only UTF-8 tar oracle')
    manifest_tool = manifests[-1]
    oracle = root / 'tar-oracle'
    oracle.mkdir()
    prepared = oracle / 'tar-utf8.exe'
    shutil.copy2(original, prepared)
    manifest = oracle / 'utf8.manifest'
    manifest.write_text('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="VynxArc.TestOnly.TarOracle" version="1.0.0.0"/>
  <application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings>
    <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
  </windowsSettings></application>
</assembly>
''', encoding='utf-8')
    source_sections = decoder_sections(original)
    command = [str(manifest_tool), '-nologo', '-manifest', str(manifest), f'-outputresource:{prepared};#1']
    compiled = subprocess.run(command, env=env, startupinfo=startup, capture_output=True, timeout=30)
    if compiled.returncode:
        raise RuntimeError(f'mt.exe failed: {compiled.stdout!r} {compiled.stderr!r}')
    if decoder_sections(prepared) != source_sections:
        raise RuntimeError('Manifest preparation changed independent decoder code or non-resource data')
    version = subprocess.run([str(original), '--version'], env=env, startupinfo=startup,
                             capture_output=True, check=True, timeout=30).stdout.decode(errors='replace').strip()
    return original, prepared, {
        'source_path': str(original), 'source_sha256': sha(original), 'version': version,
        'prepared_path': str(prepared), 'prepared_sha256': sha(prepared),
        'process_active_code_page': 'UTF-8', 'manifest_tool': str(manifest_tool),
        'non_resource_sections_unchanged': source_sections,
        'scope': 'Isolated test-only OS tar copy; manifest resource changed, OS original untouched; never shipped.',
        'LC_ALL': env.get('LC_ALL'), 'LANG': env.get('LANG'),
        'windows_process_acp': ctypes.windll.kernel32.GetACP(),
        'windows_oem_code_page': ctypes.windll.kernel32.GetOEMCP(),
        'python_version': sys.version,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--winrar', type=Path, default=Path('C:/Program Files/WinRAR/WinRAR.exe'))
    parser.add_argument('--many-files', type=int, default=10000)
    args = parser.parse_args()
    source_cli = args.cli.resolve()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    binary = root / 'bin'
    binary.mkdir()
    cli = binary / source_cli.name
    shutil.copy2(source_cli, cli)
    shutil.copy2(source_cli.with_name('vynxarc-worker.exe'), binary / 'vynxarc-worker.exe')
    report = {'status': 'running', 'cli_sha256': sha(cli), 'cases': [], 'external_tools_are_test_only': True,
              'environment': 'isolated host test directories; not a clean Windows VM',
              'worker_sha256': sha(binary / 'vynxarc-worker.exe'),
              'winrar': str(args.winrar) if args.winrar.is_file() else None}
    if args.winrar.is_file():
        report['winrar_sha256'] = sha(args.winrar)
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32') + ';' + os.environ['SystemRoot']
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0

    def save():
        (root / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

    def run(command, password=None, ok=True, timeout=180):
        proc = subprocess.Popen([str(v) for v in command], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env, cwd=root, startupinfo=startup)
        done = threading.Event()
        peak = [0]

        def sample():
            while not done.wait(.01):
                try:
                    processes = [psutil.Process(proc.pid)]
                    processes += processes[0].children(recursive=True)
                    peak[0] = max(peak[0], sum(p.memory_info().rss for p in processes if p.is_running()))
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass

        observer = threading.Thread(target=sample)
        observer.start()
        start = time.perf_counter()
        try:
            out, err = proc.communicate(None if password is None else (password + '\n').encode(), timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
            raise
        finally:
            done.set()
            observer.join()
        row = {'command': [str(v) for v in command], 'exit': proc.returncode,
               'wall_seconds': time.perf_counter() - start, 'sampled_tree_rss_bytes': peak[0]}
        if Path(command[0]).name == 'tar-utf8.exe':
            row.update(stdout=out.decode(errors='replace'), stderr=err.decode(errors='replace'))
        # Passwords appear neither in command arguments nor retained process output.
        if (proc.returncode == 0) != ok:
            raise RuntimeError(f'{row}: {out.decode(errors="replace")} {err.decode(errors="replace")}')
        return row

    corpus = root / 'corpus'
    (corpus / 'empty-dir').mkdir(parents=True)
    (corpus / 'nested').mkdir()
    (corpus / 'empty.txt').write_bytes(b'')
    (corpus / 'nested' / '\u0434\u0430\u043d\u0456.txt').write_bytes(b'VYNX native RAR5 test data.\n' * 20000)
    (corpus / 'random.bin').write_bytes(random.Random(90).randbytes(512 * 1024))
    expected = tree(corpus)
    report['input_file_count'] = len(expected)
    report['input_bytes'] = sum(p.stat().st_size for p in corpus.rglob('*') if p.is_file())
    save()
    try:
        if not args.winrar.is_file():
            original_tar, tar_oracle, report['libarchive_oracle'] = prepare_tar_oracle(root, env, startup)
            save()
        cases = [(p, r, False, False, False, 0, False) for r in ('eco', 'balanced', 'maximum')
                 for p in ('store', 'fast', 'balanced', 'maximum')]
        cases += [('fast', 'balanced', s, True, h, rr, volume)
                  for s, h, rr, volume in [(False, False, 0, False), (True, False, 0, False),
                                            (False, True, 0, False), (True, True, 3, False),
                                            (False, False, 0, True), (True, True, 0, True),
                                            (False, False, 3, True)]]
        cases += [('store', 'eco', False, False, False, 3, False),
                  ('fast', 'eco', False, False, False, 0, True)]
        stored_size = None
        for number, (preset, resource, solid, encrypted, headers, recovery, volume) in enumerate(cases):
            archive = root / f'case-{number}.rar'
            command = [cli, 'create', archive, corpus, '--preset', preset, '--resource', resource]
            if solid:
                command += ['--solid']
            if headers:
                command += ['--encrypt-headers']
            if encrypted:
                command += ['--password-stdin']
            if recovery:
                command += ['--recovery-percent', str(recovery)]
            if volume:
                command += ['--split-bytes', '131072']
            password = 'rar-test-only-secret' if encrypted else None
            row = run(command, password)
            path = root / f'case-{number}.part1.rar' if volume else archive
            parts = list(root.glob(f'case-{number}.part*.rar')) if volume else [archive]
            if volume and len(parts) < 2:
                raise AssertionError('Volume test did not produce multiple physical RAR volumes')
            row.update({'preset': preset, 'resource': resource, 'solid': solid, 'encrypted': encrypted,
                        'header_encryption': headers, 'recovery_percent': recovery,
                        'parts': len(parts), 'archive_bytes': sum(p.stat().st_size for p in parts)})
            if preset == 'store' and not recovery:
                stored_size = row['archive_bytes']
            if preset != 'store' and row['archive_bytes'] >= stored_size * .8:
                raise AssertionError('Requested compression did not materially compress test corpus')
            destination = root / f'vynx-{number}'
            read = [cli, 'isolated-extract', path, '--output', destination]
            if encrypted:
                read += ['--password-stdin']
            row['extraction'] = run(read, password)
            assert tree(destination / 'corpus') == expected
            assert (destination / 'corpus/empty-dir').is_dir()
            row['vynx_hashes'] = 'passed'
            if args.winrar.is_file():
                independent = root / f'winrar-{number}'
                independent.mkdir()
                # WinRAR is an independently installed TEST oracle, never discovered by application code.
                # Test fixture password is disposable and appears only in this test tool's arguments.
                pw = '-p' + password if password else '-p-'
                for action in ('t', 'x'):
                    cmd = [args.winrar, action, '-ibck', '-inul', '-y', '-o-', pw, path]
                    if action == 'x':
                        cmd += [str(independent) + os.sep]
                    run(cmd)
                assert tree(independent / 'corpus') == expected
                assert (independent / 'corpus/empty-dir').is_dir()
                row['winrar_test_and_hashes'] = 'passed'
            elif not encrypted and not volume:
                independent = root / f'libarchive-{number}'
                independent.mkdir()
                if number == 0:
                    # Retain the host-default decoder result, including a locale failure.
                    # This diagnostic cannot replace the required UTF-8 decoder/hash gate.
                    default_output = root / 'libarchive-default-acp-control'
                    default_output.mkdir()
                    control_command = [str(original_tar), '-xf', str(path), '-C', str(default_output)]
                    control = subprocess.run(control_command, env=env, cwd=root, startupinfo=startup,
                                             capture_output=True, timeout=180)
                    report['libarchive_oracle']['default_acp_control'] = {
                        'command': control_command, 'exit': control.returncode,
                        'stdout': control.stdout.decode(errors='replace'),
                        'stderr': control.stderr.decode(errors='replace'),
                        'hashes': tree(default_output / 'corpus'),
                        'expected_hashes': expected,
                        'unicode_tree_matches': tree(default_output / 'corpus') == expected,
                    }
                    save()
                row['libarchive_extraction'] = run([tar_oracle, '-xf', path, '-C', independent])
                assert tree(independent / 'corpus') == expected
                assert (independent / 'corpus/empty-dir').is_dir()
                row['libarchive_hashes'] = 'passed'
                if number == 0:
                    # Independent CRC negative control, with decoder code untouched.
                    corrupt = bytearray(path.read_bytes())
                    offset = corrupt.find((corpus / 'random.bin').read_bytes()[:64])
                    assert offset > 0
                    corrupt[offset + 100] ^= 0x55
                    corrupted = root / 'libarchive-corrupt-payload.rar'
                    corrupted.write_bytes(corrupt)
                    rejected_output = root / 'libarchive-corrupt-payload-output'
                    rejected_output.mkdir()
                    corrupt_command = [str(tar_oracle), '-xf', str(corrupted), '-C', str(rejected_output)]
                    control = subprocess.run(corrupt_command, env=env, cwd=root, startupinfo=startup,
                                             capture_output=True, timeout=180)
                    corrupt_hashes = tree(rejected_output / 'corpus')
                    # Some OS libarchive builds do not reject a payload CRC mismatch.
                    # Our independent whole-tree SHA256 gate must still reject it.
                    assert control.returncode != 0 or corrupt_hashes != expected
                    row['libarchive_corrupt_payload_control'] = {
                        'command': corrupt_command, 'exit': control.returncode,
                        'stdout': control.stdout.decode(errors='replace'),
                        'stderr': control.stderr.decode(errors='replace'),
                        'hashes': corrupt_hashes, 'expected_hashes': expected,
                        'payload_crc_rejected_by_decoder': control.returncode != 0,
                        'independent_hash_gate_rejected': corrupt_hashes != expected,
                    }
                    row['native_corrupt_payload_rejected'] = run([cli, 'isolated-test', corrupted], ok=False)
            else:
                row['independent_encryption_or_volumes'] = 'not available'
            # Wrong-password, damaged and incomplete-volume checks never extract to original sources.
            if encrypted:
                run([cli, 'isolated-test', path, '--password-stdin'], 'wrong-password', ok=False)
            damaged = root / f'damaged-{number}.rar'
            data = bytearray(path.read_bytes())
            data[12] ^= 0x80  # corrupt main header CRC/fields
            damaged.write_bytes(data)
            run([cli, 'isolated-test', damaged], ok=False)
            row['damaged_header_rejected'] = True
            if not args.winrar.is_file() and not encrypted and not volume:
                row['libarchive_damaged_header_rejected'] = run([tar_oracle, '-tf', damaged], ok=False)
            if volume:
                last = max(parts, key=lambda p: int(p.stem.rsplit('part', 1)[1]))
                held = last.with_suffix('.held')
                last.rename(held)
                try:
                    cmd = [cli, 'isolated-test', path] + (['--password-stdin'] if encrypted else [])
                    run(cmd, password, ok=False)
                finally:
                    held.rename(last)
                row['missing_volume_rejected'] = True
            assert tree(corpus) == expected
            report['cases'].append(row)
            save()
            print(f'RAR case {number + 1}/{len(cases)} passed', flush=True)

        # Exercise the shared publication/rebuild path under a real RAR decoder.
        archive = root / 'case-1.rar'
        extra = root / 'extra.txt'
        extra.write_bytes(b'rebuild contents')
        for command in [('add', archive, extra), ('rename', archive, 'extra.txt', 'renamed.txt'),
                        ('delete', archive, 'renamed.txt')]:
            run([cli, *command])
        run([cli, 'isolated-test', archive])
        report['rebuild_add_rename_delete'] = 'passed'
        if args.winrar.is_file():
            # Prove the emitted recovery data repairs actual payload damage.
            work = root / 'recovery-oracle'
            work.mkdir()
            stored = root / 'case-19.rar'
            raw = bytearray(stored.read_bytes())
            sample = (corpus / 'random.bin').read_bytes()
            offset = raw.find(sample[:64])
            assert offset > 0
            raw[offset + 100] ^= 0x55
            damaged = work / 'damaged.rar'
            damaged.write_bytes(raw)
            run([args.winrar, 'r', '-ibck', '-inul', '-y', damaged, str(work) + os.sep])
            repaired = list(work.glob('fixed.*.rar')) + list(work.glob('rebuilt.*.rar'))
            assert len(repaired) == 1
            decoded = work / 'decoded'
            decoded.mkdir()
            run([args.winrar, 'x', '-ibck', '-inul', '-y', '-o-', repaired[0], str(decoded) + os.sep])
            assert tree(decoded / 'corpus') == expected
            report['independent_recovery_repair'] = {'status': 'passed', 'mutated_offset': offset + 100,
                                                     'repaired_sha256': sha(repaired[0])}
        if args.many_files:
            many = root / 'many'
            many.mkdir()
            for index in range(args.many_files):
                (many / f'{index:05}.txt').write_bytes(f'native RAR member {index}\n'.encode())
            output = root / 'many.rar'
            row = run([cli, 'create', output, many, '--preset', 'fast', '--resource', 'eco'], timeout=600)
            decoded = root / 'many-decoded'
            extraction = run([cli, 'isolated-extract', output, '--output', decoded], timeout=600)
            assert tree(decoded / 'many') == tree(many)
            row.update({'files': args.many_files, 'archive_bytes': output.stat().st_size})
            row['extraction'] = extraction
            report['many_files'] = row
        report['status'] = 'passed'
    except Exception as exc:
        report['status'] = 'failed'
        report['error'] = repr(exc)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
