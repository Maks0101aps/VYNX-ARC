"""Kill a real CLI rebuild and verify the source container and edited input survive.
This checks an observed pre-publication boundary, not physical power-loss durability.
"""
import argparse,hashlib,json,os,pathlib,random,subprocess,time,zipfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cli',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);root=a.output.resolve();cli=a.cli.resolve()
report={'status':'running','cli_sha256':hashlib.sha256(cli.read_bytes()).hexdigest(),'scope':'forced process termination during observed rebuild staging; source publication remains atomic; not a power-loss test','cases':[]}
try:
 rng=random.Random(5050);source=root/'source.zip'
 with zipfile.ZipFile(source,'w',compression=zipfile.ZIP_STORED) as z:
  z.writestr('note.txt','original entry')
  for i in range(16):z.writestr(f'data-{i}.bin',rng.randbytes(1<<20))
 working=root/'working-note.txt';working.write_text('unsaved edited content',encoding='utf-8')
 original=hashlib.sha256(source.read_bytes()).hexdigest()
 for label,threshold in [('rebuild',64<<10),('late-staging',12<<20)]:
  target=root/(label+'.zip');target.write_bytes(source.read_bytes())
  process=subprocess.Popen([str(cli),'rename',str(target),'note.txt','renamed.txt'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
  start=time.perf_counter();observed=None
  while process.poll() is None and time.perf_counter()-start<30:
   candidates=[x for x in root.glob('.tmp*') if x.is_file() and x.stat().st_size>=threshold]
   if candidates:observed=candidates[0];process.kill();break
   time.sleep(.001)
  out,err=process.communicate(timeout=10)
  assert observed is not None,('No staging boundary observed',label,out,err)
  preserved=hashlib.sha256(target.read_bytes()).hexdigest()==original
  assert preserved,('Source changed before observed termination',label)
  assert working.read_text(encoding='utf-8')=='unsaved edited content'
  with zipfile.ZipFile(target) as z:assert z.testzip() is None
  result=subprocess.run([str(cli),'test',str(target)],capture_output=True,timeout=30);assert result.returncode==0,result.stderr
  report['cases'].append({'boundary':label,'termination_exit':process.returncode,'original_preserved':preserved,'working_copy_preserved':True,'orphan_stage_retained':observed.name})
  # Do not confuse a prior run's staging file with the next observed boundary.
  if observed.parent!=root:raise RuntimeError('Ownership mismatch')
  observed.rename(root/(label+'.interrupted-stage'))
 report['status']='passed'
finally:
 (root/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
