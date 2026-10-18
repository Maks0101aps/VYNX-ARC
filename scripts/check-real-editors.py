"""Opt-in actual VS Code/Writer saves in private profiles and owned test copies."""
import argparse,hashlib,json,os,socket,subprocess,time
from pathlib import Path
import psutil
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--probe',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--code',type=Path,required=True);p.add_argument('--libreoffice',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=a.output.resolve();root.mkdir(parents=True,exist_ok=False)
probe=a.probe.resolve();worker=a.worker.resolve();office=a.libreoffice.resolve();code=a.code.resolve();support=Path(__file__).resolve().parent/'editor-probe'
def call(args,timeout=60,env=None):
 result=subprocess.run([str(x) for x in args],capture_output=True,timeout=timeout,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
 if result.returncode:raise RuntimeError(result.stderr.decode('utf-8',errors='replace')[-12000:])
 return result.stdout.decode('utf-8',errors='replace').strip()
def stop(process):
 try:
  parent=psutil.Process(process.pid)
  descendants=parent.children(recursive=True)
  for child in reversed(descendants):
   try:child.kill()
   except psutil.Error:pass
  if process.poll() is None:process.kill()
 except psutil.Error:pass
 try:process.wait(timeout=10)
 except subprocess.TimeoutExpired:raise RuntimeError('private editor did not stop')
def writer(phase,directory):
 with socket.socket() as channel:channel.bind(('127.0.0.1',0));port=channel.getsockname()[1]
 profile=root/f'writer-profile-{phase}';profile.mkdir()
 command=[str(office/'soffice.exe'),'-env:UserInstallation='+profile.as_uri(),'--headless','--norestore','--nodefault','--nofirststartwizard',f'--accept=socket,host=127.0.0.1,port={port};urp;StarOffice.ServiceManager']
 with (root/f'writer-{phase}.log').open('wb') as log:
  process=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
  try:call([office/'python.exe',support/'writer.py',phase,directory,'--port',port],90)
  finally:
   try:process.wait(timeout=10)
   except subprocess.TimeoutExpired:stop(process)
report={'scope':'actual editor APIs and worker saveback across separate client processes, not manual UI testing','status':'running','cases':[],'worker_sha256':hashlib.sha256(worker.read_bytes()).hexdigest()}
try:
 text=root/'text-input.txt';text.write_text('VYNX ARC source text\n',encoding='utf-8');cases=root/'vscode-cases'
 call([probe,'prepare',cases,worker,text])
 profile=root/'vscode-profile';(profile/'User').mkdir(parents=True)
 (profile/'User'/'settings.json').write_text(json.dumps({'security.workspace.trust.enabled':False,'files.autoSave':'off','files.hotExit':'off','window.restoreWindows':'none','workbench.startupEditor':'none','update.mode':'none','telemetry.telemetryLevel':'off','extensions.autoUpdate':False}),encoding='utf-8')
 env=os.environ.copy();env['VYNX_EDITOR_PROBE_ROOT']=str(cases)
 for key in ['VSCODE_IPC_HOOK_CLI','VSCODE_PID','ELECTRON_RUN_AS_NODE']:env.pop(key,None)
 with (root/'vscode.log').open('wb') as log:
  process=subprocess.Popen([str(code),'--user-data-dir',str(profile),'--extensions-dir',str(root/'empty-extensions'),'--extensionDevelopmentPath='+str(support),'--disable-extensions','--disable-gpu','--skip-welcome','--skip-release-notes','--new-window'],env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
  start=time.monotonic()
  try:
   while not (cases/'editor-result.json').exists():
    if process.poll() is not None:raise RuntimeError('private VS Code exited before probe completion')
    if time.monotonic()-start>90:raise RuntimeError('VS Code acceptance timed out')
    time.sleep(.2)
   result=json.loads((cases/'editor-result.json').read_text(encoding='utf-8'));assert result['status']=='passed',result
   try:process.wait(timeout=10)
   except subprocess.TimeoutExpired:stop(process)
  finally:
   if process.poll() is None:stop(process)
 report['cases'].append({'editor':result['editor'],'version':result['version'],'saves':result['cases'],'saveback':call([probe,'finish',cases,worker])})
 writer_input=root/'writer-input';writer('create',writer_input)
 cases=root/'writer-cases';call([probe,'prepare',cases,worker,writer_input/'document.docx']);writer('edit',cases)
 result=json.loads((cases/'editor-result.json').read_text(encoding='utf-8'))
 report['cases'].append({'editor':'LibreOffice Writer','version':call([office/'soffice.com','--version']),'saves':result['cases'],'saveback':call([probe,'finish',cases,worker])})
 report['status']='passed'
finally:(root/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
