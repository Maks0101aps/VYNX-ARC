import pathlib,zipfile,subprocess,os,time,json,psutil,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
import tempfile
scratch=root/'.dev';scratch.mkdir(exist_ok=True)
app=pathlib.Path(tempfile.mkdtemp(prefix='portable-check-',dir=scratch))
with zipfile.ZipFile(root/'dist/VYNX-ARC-Portable-x64.zip') as z:z.extractall(app)
env=os.environ.copy();env['PATH']=str(pathlib.Path(os.environ['SystemRoot'])/'System32')+';'+os.environ['SystemRoot'];env['QT_QPA_PLATFORM']='windows';env['QT_ASSUME_STDERR_HAS_CONSOLE']='1'
for key in ['QTDIR','QT_PLUGIN_PATH','QML2_IMPORT_PATH','VYNX_QT_ROOT']:env.pop(key,None)
startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
exe=app/'VynxArc.exe';cli=app/'vynxarc-cli.exe'
def run(command):
    r=subprocess.run([str(x) for x in command],env=env,capture_output=True,timeout=30,startupinfo=startup)
    if r.returncode:raise RuntimeError((r.returncode,r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace')))
    return r.stdout.decode(errors='replace')
run([exe,'--smoke-test']);print('Isolated deployed GUI smoke passed',flush=True)
run([cli,'--help']);print('Isolated deployed CLI passed',flush=True)
work=pathlib.Path(tempfile.mkdtemp(prefix='independent-check-',dir=scratch));source=work/'Project';(source/'src').mkdir(parents=True,exist_ok=True)
(source/'README.md').write_text('# Archive verification\nReal test files for VYNX ARC.\n',encoding='utf-8');(source/'src/дані.txt').write_text('Перевірка архівів — справжні байти.\n',encoding='utf-8');(source/'empty.txt').write_bytes(b'')
import py7zr
evidence={}
for ext in ['zip','7z','tar','tar.gz']:
    output=work/f'Project.{ext}'
    if output.exists():output.unlink()
    run([cli,'create',output,source]);run([cli,'test',output]);run([cli,'list',output])
    if ext=='zip':
        with zipfile.ZipFile(output) as z:assert z.read('Project/src/дані.txt')==(source/'src/дані.txt').read_bytes()
    if ext=='7z':
        import tempfile
        independent=pathlib.Path(tempfile.mkdtemp(prefix='python-7z-',dir=work))
        with py7zr.SevenZipFile(output) as z:z.extractall(independent)
        assert (independent/'Project/src/дані.txt').read_bytes()==(source/'src/дані.txt').read_bytes()
    dest=work/('extracted-'+ext);dest.mkdir(exist_ok=True)
    result=subprocess.run([str(cli),'extract',str(output),'--output',str(dest),'--replace'],env=env,capture_output=True,startupinfo=startup,timeout=30)
    assert result.returncode==0,(ext,result.stderr)
    assert (dest/'Project/src/дані.txt').read_bytes()==(source/'src/дані.txt').read_bytes()
    evidence[ext]='CLI roundtrip matches' + ('; opened by an independent Python reader' if ext in ['zip','7z'] else '')
for name in ['test_read_format_rar5_stored.rar','test_read_format_rar_binary_data.rar']:
    run([cli,'test',root/'tests/archives'/name])
print('Independent ZIP/7Z read and packaged format tests passed',flush=True)
screens=root/'docs/screenshots';screens.mkdir(parents=True,exist_ok=True);(app/'data').mkdir(exist_ok=True)
measure=[]
for theme,lang,scale in [('light','en','1'),('dark','en','1'),('dark','uk','1.25'),('light','ru','2')]:
    (app/'data/settings.ini').write_text(f'[General]\ntheme={theme}\nlanguage={lang}\nhistoryEnabled=false\n',encoding='utf-8')
    sample_env=env.copy();sample_env['QT_SCALE_FACTOR']=scale
    screenshot=screens/f'browser-{theme}-{lang}-{scale}.png'
    process=subprocess.Popen([str(exe),str(work/'Project.zip'),'--capture',str(screenshot)],env=sample_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,startupinfo=startup)
    time.sleep(1)
    measure.append({'theme':theme,'language':lang,'scale':scale,'working_set_bytes':psutil.Process(process.pid).memory_info().rss})
    stdout,stderr=process.communicate(timeout=20)
    assert process.returncode==0,(stdout,stderr)
    assert screenshot.exists()
result={'isolated_environment':'PATH only Windows and System32; all developer Qt variables removed','archive_verification':evidence,'memory_samples':measure,'remaining_processes':[p.pid for p in psutil.process_iter(['name']) if p.info['name']=='VynxArc.exe']}
(root/'docs/VERIFICATION.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2),flush=True)
