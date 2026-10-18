"""Time-budgeted deterministic mutation campaign; retained inputs and honest scope."""
import argparse, binascii, concurrent.futures, ctypes, datetime, hashlib, json
import os, random, shutil, subprocess, sys, threading, time
from pathlib import Path

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write_json(path, value):
    tmp=path.with_suffix('.pending')
    tmp.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    tmp.replace(path)

def child(args):
    from windows_test_limits import install
    install()
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    result=subprocess.run([str(args.cli),args.operation,str(args.input)],capture_output=True,timeout=4)
    if result.returncode not in (0,1,2):
        sys.stderr.buffer.write(result.stderr[-16384:])
        return 1
    return 0

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cli',type=Path,required=True)
    p.add_argument('--output',type=Path)
    p.add_argument('--seconds',type=float,default=3600)
    p.add_argument('--workers',type=int,default=4)
    p.add_argument('--seed',type=lambda v:int(v,0),default=0x060000)
    p.add_argument('--child',action='store_true')
    p.add_argument('--input',type=Path)
    p.add_argument('--operation',default='list')
    args=p.parse_args();args.cli=args.cli.resolve()
    if args.child:return child(args)
    if args.seconds<=0 or not 1<=args.workers<=4 or args.output is None:p.error('positive duration, output and 1..4 workers required')
    root=args.output.resolve();root.mkdir(parents=True,exist_ok=False)
    # Freeze the actual binaries under test. Ongoing development cannot change them.
    binary=root/'bin';binary.mkdir();shutil.copy2(args.cli,binary/args.cli.name);cli=binary/args.cli.name
    sibling=args.cli.with_name('vynxarc-worker.exe')
    if sibling.exists():shutil.copy2(sibling,binary/sibling.name)
    source=root/'source.txt';source.write_text('bounded mutation corpus\n'*120,encoding='utf-8')
    seed_dir=root/'seeds';seed_dir.mkdir();seeds=[]
    for ext in ['zip','7z','tar','tar.gz','gz','xz','bz2','zst','lzma','tar.xz','tar.bz2','tar.zst']:
        path=seed_dir/f'valid.{ext}'
        subprocess.run([str(cli),'create',str(path),str(source)],check=True,capture_output=True,timeout=30)
        seeds.append((ext,path.read_bytes()))
    rar_dir=Path(__file__).resolve().parent.parent/'tests'/'archives'
    for path in sorted(rar_dir.glob('*.rar')):
        if 'part' in path.name.lower() or path.stat().st_size>2<<20:continue
        shutil.copy2(path,seed_dir/path.name);seeds.append(('RAR:'+path.name,path.read_bytes()))
    stop=threading.Event();began=datetime.datetime.now(datetime.timezone.utc).isoformat()
    def worker(number):
        work=root/f'worker-{number}';work.mkdir();rng=random.Random(args.seed+number)
        start=time.monotonic();counts={};cases=0;errors=[];last_checkpoint=0
        with (work/'cases.jsonl').open('w',encoding='utf-8') as journal:
            while time.monotonic()-start<args.seconds and not stop.is_set():
                label,seed=seeds[cases%len(seeds)];data=bytearray(seed);mode=cases%9
                if mode==0:del data[rng.randrange(len(data)):]
                elif mode==1:data.extend(rng.randbytes(rng.randrange(1,257)))
                else:
                    for _ in range(rng.randrange(1,17)):data[rng.randrange(len(data))]=rng.randrange(256)
                    if mode in (3,5,7) and len(data)>32 and data.startswith(b'7z\xbc\xaf\x27\x1c'):
                        offset=int.from_bytes(data[12:20],'little')+32;size=int.from_bytes(data[20:28],'little')
                        if offset+size<=len(data):data[28:32]=binascii.crc32(data[offset:offset+size]).to_bytes(4,'little')
                        data[8:12]=binascii.crc32(data[12:32]).to_bytes(4,'little')
                path=work/'current.bin';path.write_bytes(data);operation='test' if cases%3==0 else 'list'
                reason='';case_start=time.monotonic()
                try:
                    result=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--cli',str(cli),'--child','--input',str(path),'--operation',operation],capture_output=True,timeout=7,creationflags=subprocess.CREATE_NO_WINDOW)
                    ok=result.returncode==0
                    if not ok:reason=result.stderr.decode('utf-8',errors='replace')[-16384:]
                except subprocess.TimeoutExpired:ok=False;reason='campaign child exceeded 7 seconds'
                row={'case':cases,'seed_format':label,'operation':operation,'sha256':hashlib.sha256(data).hexdigest(),'seconds':time.monotonic()-case_start,'passed':ok}
                if not ok:
                    retained=work/f'failure-{cases}.bin';path.rename(retained)
                    row.update(error=reason,input=str(retained));errors.append(row);stop.set()
                journal.write(json.dumps(row)+'\n');cases+=1;counts[label]=counts.get(label,0)+1
                now=time.monotonic()
                if now-last_checkpoint>=30 or not ok:
                    journal.flush();write_json(work/'status.json',{'cases':cases,'seconds':now-start,'formats':counts,'errors':errors});last_checkpoint=now
            journal.flush()
        report={'cases':cases,'seconds':time.monotonic()-start,'formats':counts,'errors':errors}
        write_json(work/'status.json',report);return report
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        rows=list(pool.map(worker,range(args.workers)))
    passed=all(not row['errors'] and row['seconds']>=args.seconds for row in rows)
    report={'status':'passed' if passed else 'failed','started_utc':began,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'cli_sha256':digest(cli),'seed':args.seed,'workers':rows,'aggregate_campaign_seconds':sum(r['seconds'] for r in rows),
            'cases':sum(r['cases'] for r in rows),'job_memory_mib':512,'case_timeout_seconds':4,
            'scope':'Native core list/test deterministic mutation campaigns in independent bounded child jobs; aggregate active campaign wall time includes process startup. Not coverage-guided fuzzing, sanitizer coverage or security certification.'}
    write_json(root/'results.json',report);print(json.dumps({k:v for k,v in report.items() if k!='workers'},indent=2));return 0 if passed else 1
if __name__=='__main__':sys.exit(main())
