"""Restore ignored libarchive test data; verify decoded bytes before publication."""
import binascii
import hashlib
import json
import pathlib
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
REVISION = "d294297f9ecade3b2446b677bd087ad84fb7965a"


def main():
    directory = ROOT / "tests/archives"
    manifest = json.loads((directory / "rar-fixtures.json").read_text(encoding="utf-8"))
    for name, expected in manifest.items():
        target = directory / name
        if target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
                raise RuntimeError(f"Changed local fixture, refusing overwrite: {name}")
            continue
        url = f"https://raw.githubusercontent.com/libarchive/libarchive/{REVISION}/libarchive/test/{name}.uu"
        with urllib.request.urlopen(url, timeout=30) as response:
            encoded = response.read(4 * 1024 * 1024 + 1)
        if len(encoded) > 4 * 1024 * 1024:
            raise RuntimeError(f"Fixture exceeds download limit: {name}")
        lines = encoded.splitlines()
        start = next(i for i, line in enumerate(lines) if line.startswith(b"begin "))
        end = next(i for i in range(start + 1, len(lines)) if lines[i] == b"end")
        decoded = b"".join(binascii.a2b_uu(line) for line in lines[start + 1:end])
        if hashlib.sha256(decoded).hexdigest() != expected:
            raise RuntimeError(f"Upstream fixture SHA-256 mismatch: {name}")
        with tempfile.TemporaryFile(dir=directory) as staged:
            staged.write(decoded)
            staged.seek(0)
            with target.open("xb") as output:
                output.write(staged.read())
        print(f"Restored {name}")
    print(f"Verified {len(manifest)} RAR fixtures at {REVISION}")


if __name__ == "__main__":
    main()
