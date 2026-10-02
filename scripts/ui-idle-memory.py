"""Compare settled GUI working sets with the same fixture, settings and runtime."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import statistics
import subprocess
import tempfile
import time
import psutil

parser=argparse.ArgumentParser()
parser.add_argument('--before',required=True,type=pathlib.Path)
parser.add_argument('--after',required=True,type=pathlib.Path)
parser.add_argument('--output',required=True,type=pathlib.Path)
args=parser.parse_args()
scratch=pathlib.Path(tempfile.mkdtemp(prefix='ui-memory-'))
source=scratch/'Project';(source/'src').mkdir(parents=True)
(source/'README.md').write_text('UI working-set comparison\n')
(source/'src/main.cpp').write_text('int main() { return 0; }\n')
archive=scratch/'Project.zip'
subprocess.run([str(args.before.resolve()/'vynxarc-cli.exe'),'create',str(archive),str(source)],capture_output=True,check=True)
startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
samples=[];binaries={}
for revision,directory in [('before',args.before.resolve()),('after',args.after.resolve())]:
    binary=directory/'VynxArc.exe';binaries[revision]=hashlib.sha256(binary.read_bytes()).hexdigest()
    configuration=directory/'data/settings.ini';configuration.parent.mkdir(exist_ok=True)
    original=configuration.read_bytes() if configuration.exists() else None
    try:
        for theme in ['light','dark']:
            configuration.write_text(f'[General]\ntheme={theme}\nlanguage=en\nhistoryEnabled=false\n',encoding='utf-8')
            for state in ['home','archive']:
                for repetition in range(3):
                    env=os.environ.copy();env['QT_SCALE_FACTOR']='1';env['PATH']=os.environ['SystemRoot']+';'+os.environ['SystemRoot']+'/System32'
                    for key in ['QT_PLUGIN_PATH','QML2_IMPORT_PATH','VYNX_QT_ROOT']:env.pop(key,None)
                    command=[str(binary)]+([str(archive)] if state=='archive' else [])
                    process=subprocess.Popen(command,env=env,startupinfo=startup,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                    peak=0
                    try:
                        measured=psutil.Process(process.pid)
                        for _ in range(50):
                            time.sleep(0.1);peak=max(peak,measured.memory_info().rss)
                        cpu=measured.cpu_percent(interval=1)
                        settled=measured.memory_info().rss
                    finally:
                        process.terminate();process.communicate(timeout=10)
                    samples.append(dict(revision=revision,theme=theme,state=state,repetition=repetition+1,settled_working_set_bytes=settled,observed_startup_peak_bytes=peak,idle_cpu_percent=cpu))
                    print(revision,theme,state,repetition+1,settled,flush=True)
    finally:
        if original is None:configuration.unlink(missing_ok=True)
        else:configuration.write_bytes(original)
summary=[]
for theme in ['light','dark']:
    for state in ['home','archive']:
        medians={revision:statistics.median(s['settled_working_set_bytes'] for s in samples if s['revision']==revision and s['theme']==theme and s['state']==state) for revision in ['before','after']}
        summary.append(dict(theme=theme,state=state,**medians,delta_bytes=medians['after']-medians['before']))
result=dict(method='Three fresh launches per state/theme/revision; RSS after 5 seconds settling plus 1 second CPU sampling. Peak is 100ms polling, not an exact maximum. Same small ZIP, English, 100% DPI, restricted PATH. Read-only processes terminated after sampling.',windows=platform.platform(),logical_cpus=psutil.cpu_count(),ram_bytes=psutil.virtual_memory().total,binary_sha256=binaries,samples=samples,median_comparison=summary)
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
