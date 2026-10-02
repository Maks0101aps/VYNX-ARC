"""Capture real deployed Widgets at the requested theme/language/DPI matrix."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile
import time
import psutil

parser = argparse.ArgumentParser()
parser.add_argument('--app', required=True, type=pathlib.Path)
parser.add_argument('--output', required=True, type=pathlib.Path)
args = parser.parse_args()
app = args.app.resolve()
args.output.mkdir(parents=True, exist_ok=True)
scratch = pathlib.Path(tempfile.mkdtemp(prefix='ui-capture-'))
source = scratch / 'Project'
(source / 'src/components').mkdir(parents=True)
(source / 'README.md').write_text('VYNX ARC visual QA\n', encoding='utf-8')
(source / 'src/components/main.cpp').write_text('int main() { return 0; }\n')
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
        for language in ['en', 'uk', 'ru']:
            configuration.write_text(f'[General]\ntheme={theme}\nlanguage={language}\nhistoryEnabled=false\n', encoding='utf-8')
            for scale in ['1', '1.25', '1.5', '2']:
                for state in ['home', 'archive']:
                    output = args.output / f'{state}-{theme}-{language}-{scale}.png'
                    env = os.environ.copy()
                    env['QT_SCALE_FACTOR'] = scale
                    env['PATH'] = os.environ['SystemRoot'] + ';' + os.environ['SystemRoot'] + '/System32'
                    for key in ['QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'VYNX_QT_ROOT']:
                        env.pop(key, None)
                    command = [str(app / 'VynxArc.exe')]
                    if state == 'archive':
                        command.append(str(archive))
                    command += ['--capture', str(output.resolve())]
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
