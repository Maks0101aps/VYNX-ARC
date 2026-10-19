"""Exercise opt-in diagnostic worker faults through the actual Qt queue."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "gui", "cli", "worker", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--offscreen-plugin", required=True, type=Path)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    bundle = root / "bundle"
    shutil.copytree(args.runtime, bundle, ignore=shutil.ignore_patterns("data", "logs"))
    for source, name in ((args.gui, "VynxArc.exe"), (args.cli, "vynxarc-cli.exe"),
                         (args.worker, "vynxarc-worker.exe")):
        shutil.copy2(source, bundle / name)
    (bundle / "portable.flag").touch()
    shutil.copy2(args.offscreen_plugin, bundle / "platforms" / "qoffscreen.dll")
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("QT_") or key in ("QML2_IMPORT_PATH", "QML_IMPORT_PATH"):
            env.pop(key)
    env.update(QT_QPA_PLATFORM="offscreen", QT_ASSUME_STDERR_HAS_CONSOLE="1")
    windows = next(value for key, value in env.items() if key.lower() == "systemroot")
    env["PATH"] = os.pathsep.join((windows, str(Path(windows) / "System32")))
    results = []
    for label in ("crash", "hang", "memory", "before", "after", "after-dirty"):
        mode = "after" if label == "after-dirty" else label
        case = root / ("case-" + label)
        env["VYNX_WORKER_FAULT"] = {"before": "before-publish", "after": "after-publish"}.get(mode, mode)
        env["VYNX_WORKER_FAULT_TARGET"] = str(case / "fault.zip")
        phases = (mode, "recover-" + label) if mode in ("before", "after") else (mode,)
        for phase in phases:
            if phase == "recover-after-dirty":
                files = list((case / "WorkingCopies").glob("edit-*/note.txt"))
                assert len(files) == 1
                files[0].write_bytes(b"newer unsaved working copy")
            run = subprocess.run([str(bundle / "VynxArc.exe"), "--smoke-test",
                                  "--worker-fault-smoke", phase, str(case)],
                                 env=env, capture_output=True, timeout=45)
            (root / (label + "-" + phase + ".log")).write_bytes(run.stdout + run.stderr)
            if run.returncode:
                raise RuntimeError(f"{label}/{phase}: exit {run.returncode}; see {root}")
        result = json.loads((case / "worker-result.json").read_text(encoding="utf-8"))
        if len(phases) == 2:
            result["recovery"] = json.loads((case / "recovery-result.json").read_text(encoding="utf-8"))
        results.append(result)
        result["case"] = label
        print(label + ": passed", flush=True)
    report = {"status": "passed", "cases": results,
              "scope": "Actual GUI queue and new-process recovery with opt-in fault hooks; diagnostic hooks excluded from normal builds.",
              "binaries": {name: hashlib.sha256((bundle / name).read_bytes()).hexdigest()
                           for name in ("VynxArc.exe", "vynxarc-worker.exe")}}
    (root / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
