"""Real Writer UNO acceptance; private process/profile, no manual UI claim."""
import argparse,json,time
from pathlib import Path
import uno
p=argparse.ArgumentParser();p.add_argument('phase',choices=['create','edit']);p.add_argument('root',type=Path);p.add_argument('--port',type=int,default=20860);a=p.parse_args()
local=uno.getComponentContext();resolver=local.ServiceManager.createInstanceWithContext('com.sun.star.bridge.UnoUrlResolver',local)
ctx=None
for _ in range(100):
 try:ctx=resolver.resolve(f'uno:socket,host=127.0.0.1,port={a.port};urp;StarOffice.ComponentContext');break
 except Exception:time.sleep(.1)
if ctx is None:raise RuntimeError('private Writer unavailable')
desktop=ctx.ServiceManager.createInstanceWithContext('com.sun.star.frame.Desktop',ctx)
def prop(name,value):
 p=uno.createUnoStruct('com.sun.star.beans.PropertyValue');p.Name=name;p.Value=value;return p
report={'editor':'LibreOffice Writer','scope':'real headless Writer UNO load/edit/store, not manual UI testing','cases':[]}
try:
 if a.phase=='create':
  a.root.mkdir(parents=True,exist_ok=True);doc=desktop.loadComponentFromURL('private:factory/swriter','_blank',0,(prop('Hidden',True),))
  doc.getText().setString('VYNX ARC Writer source document')
  doc.storeAsURL((a.root/'document.docx').resolve().as_uri(),(prop('FilterName','Office Open XML Text'),prop('Overwrite',True)));doc.close(True)
 else:
  for row in (a.root/'cases.tsv').read_text(encoding='utf-8-sig').splitlines():
   file=Path(row.split('\t')[1]).resolve();file.relative_to(a.root.resolve());before=file.stat()
   doc=desktop.loadComponentFromURL(file.as_uri(),'_blank',0,(prop('Hidden',True),prop('ReadOnly',False),prop('UpdateDocMode',0)))
   if doc is None:raise RuntimeError('Writer cannot open copy')
   text=doc.getText();text.setString(text.getString()+'\nVYNX ARC actual Writer save');doc.store();doc.close(True);after=file.stat()
   report['cases'].append({'file':str(file),'saved':True,'file_id_changed':before.st_ino!=after.st_ino})
  report['status']='passed';(a.root/'editor-result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
finally:desktop.terminate()
