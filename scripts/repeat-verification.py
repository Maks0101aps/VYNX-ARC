"""Run the final isolated packaged verifier repeatedly and retain every result."""
import argparse
import hashlib
import json
import pathlib
import os
import subprocess
import sys
from datetime import datetime, timezone

root = pathlib.Path(__file__).resolve().parents[1]
report = root / 'docs/VERIFICATION.json'
attempts_path = root / 'docs/VERIFICATION_ATTEMPTS.json'
parser = argparse.ArgumentParser()
parser.add_argument('--runs', type=int, default=3)
args = parser.parse_args()
if not 1 <= args.runs <= 20:
    raise SystemExit('--runs must be between 1 and 20')
portable = root / 'dist/VYNX-ARC-Portable-x64.zip'
expected_hash = hashlib.sha256(portable.read_bytes()).hexdigest()
default_scratch = os.environ.get('VYNX_VERIFY_SCRATCH', str(root / '.dev'))
locations = {
    'application': str(pathlib.Path(os.environ.get('VYNX_VERIFY_APP_SCRATCH', default_scratch)).resolve()),
    'archive_workspace': str(pathlib.Path(os.environ.get('VYNX_VERIFY_WORK_SCRATCH', default_scratch)).resolve()),
}
attempts = []
for number in range(1, args.runs + 1):
    print(f'Packaged isolated verification {number}/{args.runs}', flush=True)
    process = subprocess.run(
        [sys.executable, str(root / 'scripts/verify-portable.py')],
        cwd=root, capture_output=True, text=True, errors='replace', timeout=900)
    current = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    same_binary = current.get('portable_sha256') == expected_hash
    record = {
        'run': number,
        'timestamp_utc': datetime.now(timezone.utc).isoformat(),
        'exit_code': process.returncode,
        'status': current.get('status', 'failed'),
        'same_portable_sha256': same_binary,
        'locations': locations,
        'completed_archive_checks': current.get('archive_verification',
                                                current.get('completed_archive_checks', {})),
        'error': current.get('error') or (process.stderr[-4000:] if process.returncode else None),
    }
    attempts.append(record)
    print(json.dumps(record, ensure_ascii=False), flush=True)

attempts_path.write_text(json.dumps({
    'portable_sha256': expected_hash,
    'requested_runs': args.runs,
    'locations': locations,
    'attempts': attempts,
}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
final = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
passed = len(attempts) == args.runs and all(x['status'] == 'passed' for x in attempts)
final['status'] = 'passed' if passed else 'failed'
final['verification_locations'] = locations
final['repeated_packaged_runs'] = attempts
report.write_text(json.dumps(final, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if not passed:
    raise SystemExit(1)
