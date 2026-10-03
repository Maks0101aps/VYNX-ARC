"""Run the final isolated packaged verifier repeatedly and retain every result."""
import argparse
import hashlib
import json
import pathlib
import os
import subprocess
import sys
import tempfile
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
try:
    previous = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
except (OSError, ValueError):
    previous = {}
series = previous.get('verification_series', []) if previous.get('portable_sha256') == expected_hash else []
default_scratch = os.environ.get('VYNX_VERIFY_SCRATCH', str(root / '.dev'))
locations = {
    'application': str(pathlib.Path(os.environ.get('VYNX_VERIFY_APP_SCRATCH', default_scratch)).resolve()),
    'archive_workspace': str(pathlib.Path(os.environ.get('VYNX_VERIFY_WORK_SCRATCH', default_scratch)).resolve()),
}
attempts = []
run_directory = pathlib.Path(tempfile.mkdtemp(prefix='verification-series-', dir=root / '.dev'))
for number in range(1, args.runs + 1):
    print(f'Packaged isolated verification {number}/{args.runs}', flush=True)
    run_report = run_directory / f'run-{number}.json'
    env = os.environ.copy()
    env['VYNX_VERIFY_REPORT'] = str(run_report)
    try:
        process = subprocess.run(
            [sys.executable, str(root / 'scripts/verify-portable.py')], env=env,
            cwd=root, capture_output=True, text=True, errors='replace', timeout=900)
        exit_code, stdout, stderr = process.returncode, process.stdout, process.stderr
    except subprocess.TimeoutExpired as error:
        exit_code, stdout, stderr = 124, '', str(error)
    (run_directory / f'run-{number}.stdout.txt').write_text(stdout, encoding='utf-8')
    (run_directory / f'run-{number}.stderr.txt').write_text(stderr, encoding='utf-8')
    try:
        current = json.loads(run_report.read_text(encoding='utf-8')) if run_report.exists() else {}
    except (OSError, ValueError) as error:
        current = {'status':'failed','error':str(error)}
    same_binary = current.get('portable_sha256') == expected_hash
    record = {
        'run': number,
        'timestamp_utc': datetime.now(timezone.utc).isoformat(),
        'exit_code': exit_code,
        'status': 'passed' if exit_code == 0 and same_binary and current.get('status') == 'passed' else 'failed',
        'same_portable_sha256': same_binary,
        'locations': locations,
        'completed_archive_checks': current.get('archive_verification',
                                                current.get('completed_archive_checks', {})),
        'error': current.get('error') or (stderr[-4000:] if exit_code else None),
        'evidence_report': str(run_report),
    }
    attempts.append(record)
    print(json.dumps(record, ensure_ascii=False), flush=True)

attempts_path.write_text(json.dumps({
    'portable_sha256': expected_hash,
    'requested_runs': args.runs,
    'locations': locations,
    'attempts': attempts,
}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
final = current
profile_passed = len(attempts) == args.runs and all(x['status'] == 'passed' for x in attempts)
series.append({'locations':locations, 'attempts':attempts, 'passed':profile_passed,
               'evidence_directory':str(run_directory)})
passed = profile_passed and all(row['passed'] for row in series)
final['status'] = 'passed' if passed else 'failed'
final['current_profile_status'] = 'passed' if profile_passed else 'failed'
final['verification_series'] = series
final['verification_locations'] = locations
final['repeated_packaged_runs'] = attempts
final['evidence_directory'] = str(run_directory)
final['portable_sha256'] = expected_hash
report.write_text(json.dumps(final, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if not passed:
    raise SystemExit(1)
