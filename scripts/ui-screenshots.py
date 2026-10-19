"""Capture real deployed Widgets at the requested theme/language/DPI matrix."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile
import time
import psutil
import struct
import zlib
import hashlib

parser = argparse.ArgumentParser()
parser.add_argument('--app', required=True, type=pathlib.Path)
parser.add_argument('--output', required=True, type=pathlib.Path)
parser.add_argument('--quick', action='store_true')
parser.add_argument('--scale', action='append', choices=['1','1.25','1.5','2'])
parser.add_argument('--window-size')
parser.add_argument('--details-only',action='store_true')
parser.add_argument('--workspace-content',action='store_true')
parser.add_argument('--language', choices=['en', 'uk', 'ru'])
parser.add_argument('--recents', action='store_true')
parser.add_argument('--state', action='append', choices=['convert','tabs','working-copies','temporary-files','review','home','archive','create','create-stream','extract','conflict','settings','settings-resources','batch','about','operation','queue','breadcrumb','search','preview','preview-image','preview-hex','external'])
args = parser.parse_args()
app = args.app.resolve()
args.output.mkdir(parents=True, exist_ok=True)
scratch = pathlib.Path(tempfile.mkdtemp(prefix='ui-capture-'))
source = scratch / 'Project'
(source / 'src/components').mkdir(parents=True)
(source / 'README.md').write_text('VYNX ARC visual QA\n', encoding='utf-8')
(source / 'src/components/main.cpp').write_text('int main() { return 0; }\n')
# Deterministic raster/binary fixtures go through the real archive and preview pipeline.
def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
width, height = 480, 240
raster = b''.join(b'\x00' + bytes(v for x in range(width) for v in (32 + x * 190 // width, 75 + y * 125 // height, 220)) for y in range(height))
png = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
png += png_chunk(b'IDAT', zlib.compress(raster)) + png_chunk(b'IEND', b'')
(source / 'cover.png').write_bytes(png)
(source / 'sample.bin').write_bytes(bytes(range(256)) * 4)
archive = scratch / 'Project.zip'
subprocess.run([str(app / 'vynxarc-cli.exe'), 'create', str(archive), str(source)], check=True, capture_output=True)
configuration = app / 'data/settings.ini'
configuration.parent.mkdir(exist_ok=True)
original = configuration.read_bytes() if configuration.exists() else None
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
samples = []
try:
    for theme in ['light', 'dark']:
        for language in ([args.language] if args.language else ['en'] if args.quick else ['en', 'uk', 'ru']):
            recent = f'recent={archive.as_posix()}\n' if args.recents else ''
            configuration.write_text(f'[General]\ntheme={theme}\nlanguage={language}\nhistoryEnabled=false\n{recent}', encoding='utf-8')
            for scale in (args.scale or (['1'] if args.quick or args.details_only or args.state else ['1', '1.25', '1.5', '2'])):
                for state in (args.state or (['create','create-stream','extract','conflict','settings','settings-resources','batch','about','operation','queue','breadcrumb','search','preview','preview-image','preview-hex','external'] if args.details_only else ['home', 'archive'])):
                    output = args.output / f'{state}-{theme}-{language}-{scale}.png'
                    env = os.environ.copy()
                    env['QT_SCALE_FACTOR'] = scale
                    windows = next(value for key, value in env.items() if key.lower() == 'systemroot')
                    env['PATH'] = windows + ';' + windows + '/System32'
                    for key in ['QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'VYNX_QT_ROOT']:
                        env.pop(key, None)
                    command = [str(app / 'VynxArc.exe')]
                    if state != 'home':
                        command.append(str(archive))
                    command += ['--capture', str(output.resolve())]
                    if args.details_only or args.state or (args.workspace_content and state=='archive'):
                        command += ['--capture-ui',state]
                    if args.window_size:
                        command += ['--capture-size', args.window_size]
                    process = subprocess.Popen(command, env=env, startupinfo=startup, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        time.sleep(1)
                        working_set = psutil.Process(process.pid).memory_info().rss
                        stdout, stderr = process.communicate(timeout=30)
                    except BaseException:
                        process.kill()
                        process.communicate()
                        raise
                    if process.returncode or not output.exists():
                        raise RuntimeError((command, stdout, stderr))
                    samples.append(dict(state=state, theme=theme, language=language, scale=scale, working_set_bytes=working_set))
                    print(output.name, flush=True)
finally:
    if original is None:
        configuration.unlink(missing_ok=True)
    else:
        configuration.write_bytes(original)
(args.output / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n', encoding='utf-8')
(args.output / 'manifest.json').write_text(json.dumps({
    'application_sha256': hashlib.sha256((app/'VynxArc.exe').read_bytes()).hexdigest(),
    'screenshot_count': len(samples), 'requested_window_size': args.window_size,
    'states': sorted({sample['state'] for sample in samples})}, indent=2) + '\n', encoding='utf-8')
