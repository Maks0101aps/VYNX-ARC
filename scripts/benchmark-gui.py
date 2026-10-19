"""Actual Qt/worker large-ZIP and 100-cycle acceptance, with sampled process metrics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import threading
import time
import psutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    binary = args.app.resolve() / 'VynxArc.exe'
    env = os.environ.copy()
    windows = next(v for k, v in env.items() if k.lower() == 'systemroot')
    env['PATH'] = os.pathsep.join((windows, str(Path(windows) / 'System32')))
    for key in list(env):
        if key.startswith('QT_') or key in ('QML2_IMPORT_PATH', 'QML_IMPORT_PATH'):
            env.pop(key)
    env.update(QT_QPA_PLATFORM='offscreen', QT_ASSUME_STDERR_HAS_CONSOLE='1')
    run = subprocess.Popen([str(binary), '--smoke-test', '--performance-smoke',
                            str(args.archive.resolve()), str(root / 'case')],
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    process = psutil.Process(run.pid)
    checkpoints, samples, lines = [], [], []

    def metrics():
        rss = private = handles = children = 0
        try:
            members = [process] + process.children(recursive=True)
        except psutil.NoSuchProcess:
            return None
        for member in members:
            try:
                memory = member.memory_info()
                rss += memory.rss
                private += memory.private
                handles += member.num_handles()
                children += member.pid != process.pid
            except psutil.NoSuchProcess:
                pass
        return dict(rss_bytes=rss, private_bytes=private, handles=handles, children=children)

    def read():
        for raw in run.stdout:
            line = raw.decode('utf-8', errors='replace')
            lines.append(line)
            if 'GUI_RESOURCE_CHECKPOINT ' in line:
                checkpoint = metrics()
                if checkpoint:
                    checkpoint['cycle'] = int(line.rsplit(' ', 1)[1])
                    checkpoints.append(checkpoint)

    reader = threading.Thread(target=read)
    reader.start()
    start = time.monotonic()
    while run.poll() is None:
        sample = metrics()
        if sample:
            sample['seconds'] = time.monotonic() - start
            samples.append(sample)
        if time.monotonic() - start > 600:
            run.kill()
            raise RuntimeError('GUI benchmark exceeded 600 seconds')
        time.sleep(.01)
    reader.join()
    (root / 'gui.log').write_text(''.join(lines), encoding='utf-8')
    if run.returncode:
        raise RuntimeError(f'GUI benchmark exit {run.returncode}; see {root / "gui.log"}')
    result = json.loads((root / 'case/results.json').read_text(encoding='utf-8'))
    searches = sorted(result['search_milliseconds'])
    result.update(gui_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  worker_sha256=hashlib.sha256((binary.parent / 'vynxarc-worker.exe').read_bytes()).hexdigest(),
                  corpus_sha256=hashlib.sha256(args.archive.read_bytes()).hexdigest(),
                  median_open_seconds=statistics.median(result['open_seconds']),
                  median_selected_extract_seconds=statistics.median(result['selected_extract_seconds']),
                  search_p95_milliseconds=searches[int(.95 * (len(searches) - 1))],
                  sampled_peak={key: max(s[key] for s in samples)
                                for key in ('rss_bytes', 'private_bytes', 'handles', 'children')},
                  resource_checkpoints=checkpoints,
                  metrics_scope='10 ms samples of GUI plus live descendant processes; combined RSS includes shared pages, private bytes is committed private memory; not an exact peak')
    (root / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    (root / 'samples.json').write_text(json.dumps(samples) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('median_open_seconds', 'median_selected_extract_seconds',
                                            'search_p95_milliseconds', 'sampled_peak', 'resource_checkpoints')}, indent=2))


if __name__ == '__main__':
    main()
