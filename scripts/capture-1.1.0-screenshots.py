import os
import pathlib
import subprocess
import tempfile
import time
import shutil

repo_root = pathlib.Path(__file__).resolve().parent.parent
vynx_cli = repo_root / "target" / "release" / "vynxarc-cli.exe"
vynx_gui = repo_root / "build-msvc" / "VynxArc.exe"
qt_root = os.environ.get("VYNX_QT_ROOT", r"C:\Users\Maksi\AppData\Local\VynxArcDev\QtMSVC")

out_dir = repo_root / "docs" / "screenshots" / "1.1"
out_dir.mkdir(parents=True, exist_ok=True)

scratch = pathlib.Path(tempfile.mkdtemp(prefix="vynx-1.1-shot-"))
archive_dir = scratch / "Project"
archive_dir.mkdir(parents=True)

(archive_dir / "Backup").mkdir()
(archive_dir / "Documents").mkdir()
(archive_dir / "Setup.exe").write_bytes(b"MZ" + b"\x00" * 37175532)
(archive_dir / "ReadMe.txt").write_text("VYNX ARC 1.1.0 - Modern Neo-Classic Archive Manager\nWinRAR-inspired interface with high-DPI vector SVG icons and instant rubberband marquee selection.\n", encoding="utf-8")
(archive_dir / "Changelog.md").write_text("# Changelog 1.1.0\n- Modern WinRAR Toolbar with large 32x32 vector icons\n- Sub-navigation bar with Breadcrumbs and Quick Search\n- High-performance Rubberband Marquee selection\n", encoding="utf-8")
(archive_dir / "Release-v1.0.9.zip").write_bytes(b"PK\x03\x04" + b"\x00" * 1258291)
(archive_dir / "Architecture.pdf").write_bytes(b"%PDF-1.7" + b"\x00" * 45892)
(archive_dir / "Specification.docx").write_bytes(b"PK\x03\x04" + b"\x00" * 15360)
(archive_dir / "SystemRecovery.iso").write_bytes(b"\x00" * 65536)
(archive_dir / "MainCore.cpp").write_text("#include <iostream>\n\nint main() {\n    std::cout << \"VYNX ARC Modern WinRAR UI 1.1.0\" << std::endl;\n    return 0;\n}\n", encoding="utf-8")
(archive_dir / "Soundtrack.mp3").write_bytes(b"ID3" + b"\x00" * 32410)
(archive_dir / "Trailer.mp4").write_bytes(b"\x00\x00\x00 ftypisom" + b"\x00" * 1048576)
(archive_dir / "Banner.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00" * 2048)

archive_file = scratch / "VYNX-DemoArchive.zip"
subprocess.run([str(vynx_cli), "create", str(archive_file), str(archive_dir)], check=True)

portable_flag = vynx_gui.parent / "portable.flag"
settings_file = vynx_gui.parent / "data" / "settings.ini"
settings_file.parent.mkdir(parents=True, exist_ok=True)

portable_created = False
if not portable_flag.exists():
    portable_flag.write_text("")
    portable_created = True

original_settings = settings_file.read_bytes() if settings_file.exists() else None

env = os.environ.copy()
env["PATH"] = f"{qt_root}\\bin;{env['PATH']}"
env["QT_QPA_PLATFORM"] = "windows"
env["QT_SCALE_FACTOR"] = "1"

targets = [
    ("winrar-light-en.png", "light", "en", "archive", "1060x680"),
    ("winrar-dark-en.png", "dark", "en", "archive", "1060x680"),
    ("winrar-light-ru.png", "light", "ru", "archive", "1060x680"),
    ("winrar-dark-ru.png", "dark", "ru", "archive", "1060x680"),
    ("winrar-selection-light.png", "light", "en", "archive-selected", "1060x680"),
    ("winrar-selection-dark.png", "dark", "en", "archive-selected", "1060x680"),
]

try:
    for filename, theme, lang, ui_mode, size in targets:
        settings_content = f"[General]\ntheme={theme}\nlanguage={lang}\nhistoryEnabled=false\n"
        settings_file.write_text(settings_content, encoding="utf-8")
        out_path = out_dir / filename
        cmd = [
            str(vynx_gui),
            str(archive_file),
            "--capture", str(out_path),
            "--capture-ui", ui_mode,
            "--capture-size", size
        ]
        print(f"Capturing {filename} ({theme}, {lang}, mode={ui_mode})...")
        p = subprocess.run(cmd, env=env, timeout=15)
        if p.returncode != 0 or not out_path.exists():
            print(f"Error capturing {filename}: code {p.returncode}")
        else:
            print(f"Successfully captured {out_path} ({out_path.stat().st_size} bytes)")
finally:
    if original_settings is not None:
        settings_file.write_bytes(original_settings)
    else:
        settings_file.unlink(missing_ok=True)
    if portable_created:
        portable_flag.unlink(missing_ok=True)
    shutil.rmtree(scratch, ignore_errors=True)

print("Done capturing 1.1.0 screenshots!")
