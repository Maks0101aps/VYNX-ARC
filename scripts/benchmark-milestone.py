"""Reproducible archive performance fixtures and isolated Rust benchmark runner."""
import argparse,datetime,hashlib,json,os,pathlib,random,statistics,subprocess,time,zipfile
import psutil
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixtures',type=pathlib.Path,required=True)
parser.add_argument('--prepare',action='store_true')
parser.add_argument('--test-exe',type=pathlib.Path)
parser.add_argument('--output',type=pathlib.Path)
args=parser.parse_args();root=args.fixtures.resolve();root.mkdir(parents=True,exist_ok=True)
if args.prepare:
 import py7zr,tarfile
 from backports import zstd
 rng=random.Random(0x56594e58)
 cases=[]
 with zipfile.ZipFile(root/'cycles.zip','w',compression=zipfile.ZIP_STORED) as cycle:cycle.writestr('needle.txt','resource-cycle payload')
 def add(name,selected,encrypted=False):
  path=root/name
  cases.append({'name':name,'selected':selected,'encrypted':encrypted,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'physical_bytes':path.stat().st_size})
 with zipfile.ZipFile(root/'many.zip','w',compression=zipfile.ZIP_DEFLATED,compresslevel=1) as archive:
  for index in range(100000):archive.writestr(f'project/g{index//1000:03}/file-{index:06}.txt',f'VYNX entry {index}\n')
 add('many.zip','project/g000/file-000000.txt')
 noise=rng.randbytes(1<<20)
 with zipfile.ZipFile(root/'large.zip','w',compression=zipfile.ZIP_STORED) as archive:
  for index in range(2):
   with archive.open(f'project/large-{index}.bin','w') as target:
    for block in range(32):target.write(noise)
  archive.writestr('project/needle.txt','selected small file\n')
 add('large.zip','project/needle.txt')
 source=root/'mixed-source';source.mkdir(exist_ok=True)
 (source/'needle.txt').write_text('selected small file\n')
 for index in range(1000):(source/f'file-{index:04}.txt').write_bytes((f'file {index} data\n'*100).encode())
 for index in range(100):(source/f'binary-{index:03}.bin').write_bytes(rng.randbytes(64<<10))
 with zipfile.ZipFile(root/'mixed.zip','w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
  for file in sorted(source.iterdir()):archive.write(file,'project/'+file.name)
 add('mixed.zip','project/needle.txt')
 # A bounded solid corpus has many members and real binary/text data.
 solid=root/'solid-source';solid.mkdir(exist_ok=True)
 for file in sorted(source.iterdir())[:220]:(solid/file.name).write_bytes(file.read_bytes())
 (solid/'needle.txt').write_text('selected small file\n')
 for name,password in [('solid.7z',None),('encrypted.7z','benchmark-only')]:
  with py7zr.SevenZipFile(root/name,'w',password=password,header_encryption=bool(password)) as archive:archive.writeall(solid,'project')
  add(name,'project/needle.txt',bool(password))
 with tarfile.open(root/'mixed.tar','w',format=tarfile.USTAR_FORMAT) as archive:archive.add(source,arcname='project')
 (root/'mixed.tar.zst').write_bytes(zstd.compress((root/'mixed.tar').read_bytes()))
 add('mixed.tar.zst','project/needle.txt')
 (root/'cases.tsv').write_text(''.join(f"{row['name']}\t{int(row['encrypted'])}\t{row['selected']}\n" for row in cases))
 (root/'manifest.json').write_text(json.dumps({'seed':'0x56594e58','many_files':100000,'cases':cases},indent=2)+'\n')
 print('Prepared six benchmark corpora, including 100000-file ZIP and encrypted solid 7Z.',flush=True)
if args.test_exe:
 if not args.output:parser.error('--output is required with --test-exe')
 args.output.mkdir(parents=True,exist_ok=False)
 results=[]
 for case in json.loads((root/'manifest.json').read_text())['cases']:
  path=args.output/(case['name']+'.json');env=os.environ.copy();env.update(VYNX_BENCH_FIXTURES=str(root),VYNX_BENCH_CASE=case['name'],VYNX_BENCH_RESULT=str(path.resolve()))
  process=subprocess.Popen([str(args.test_exe.resolve()),'--ignored','--nocapture'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  sample=psutil.Process(process.pid);peak=0;cpu=0;start=time.perf_counter()
  while process.poll() is None:
   try:peak=max(peak,sample.memory_info().rss);times=sample.cpu_times();cpu=max(cpu,times.user+times.system)
   except psutil.NoSuchProcess:pass
   if time.perf_counter()-start>600:process.kill();raise RuntimeError('Benchmark case timeout: '+case['name'])
   time.sleep(.01)
  stdout,stderr=process.communicate()
  if process.returncode:raise RuntimeError((case['name'],stdout.decode(errors='replace'),stderr.decode(errors='replace')))
  result=json.loads(path.read_text());result.update(sampled_peak_rss_bytes=peak,sampled_cpu_seconds=cpu,corpus=case)
  results.append(result);print(case['name'],json.dumps(result['median_seconds']),flush=True)
 report={'timestamp_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'test_executable_sha256':hashlib.sha256(args.test_exe.read_bytes()).hexdigest(),'rss_scope':'10ms sampled process working set, not an exact peak','trials':3,'resource_cycle_scope':'100 open/preview/close operations on a fixed small ZIP after warmup; throughput cases remain separate','cases':results}
 (args.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
