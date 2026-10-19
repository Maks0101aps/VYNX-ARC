"""Read worker conversion results with independent AES-ZIP and 7Z implementations."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import py7zr
import pyzipper


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('probe', 'worker', 'rar-fixtures', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixtures = root / 'fixtures'
    run = subprocess.run([str(args.probe.resolve()), str(fixtures), str(args.worker.resolve()),
                          str(args.rar_fixtures.resolve())], capture_output=True, timeout=300)
    (root / 'probe.log').write_bytes(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError(f'Fixture producer failed; see {root / "probe.log"}')
    results = []
    for index, row in enumerate((fixtures / 'cases.tsv').read_text(encoding='utf-8').splitlines()):
        path, encrypted, expected_path, directories_path, source = row.split('\t')
        path = Path(path)
        password = 'conversion-acceptance-only' if encrypted == '1' else None
        expected = dict(line.rsplit('\t', 1) for line in Path(expected_path).read_text(encoding='utf-8').splitlines())
        expected_directories = set(Path(directories_path).read_text(encoding='utf-8').splitlines())
        if path.suffix == '.zip':
            with pyzipper.AESZipFile(path) as archive:
                if password:
                    archive.setpassword(password.encode())
                actual = {info.filename: digest(archive.read(info.filename))
                          for info in archive.infolist() if not info.is_dir()}
                directories = {info.filename.rstrip('/') for info in archive.infolist() if info.is_dir()}
        else:
            destination = root / f'decoded-{index}'
            with py7zr.SevenZipFile(path, 'r', password=password) as archive:
                archive.extractall(path=destination)
            actual = {file.relative_to(destination).as_posix(): digest(file.read_bytes())
                      for file in destination.rglob('*') if file.is_file()}
            directories = {file.relative_to(destination).as_posix()
                           for file in destination.rglob('*') if file.is_dir()}
        assert actual == expected, (path, actual, expected)
        assert directories == expected_directories, (path, directories, expected_directories)
        results.append(dict(output=path.name, source=Path(source).name, encrypted=bool(password),
                            files=len(actual), directories=len(directories), sha256=digest(path.read_bytes())))
        print(path.name + ': passed', flush=True)
    report = dict(status='passed', cases=results,
                  readers={name: importlib.metadata.version(name) for name in ('py7zr', 'pyzipper')},
                  worker_sha256=digest(args.worker.read_bytes()),
                  scope='Independent result decoding, exact file hashes and directory sets; expected source hashes produced by core. Fixed public fixture credentials, no user passwords.')
    (root / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
