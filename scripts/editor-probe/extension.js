const vscode=require('vscode'),fs=require('fs'),path=require('path'),crypto=require('crypto');
exports.activate=async function(){
 const root=path.resolve(process.env.VYNX_EDITOR_PROBE_ROOT||'');
 const report={editor:'Visual Studio Code',version:vscode.version,cases:[]};
 try{
  for(const row of fs.readFileSync(path.join(root,'cases.tsv'),'utf8').trim().split(/\r?\n/)){
   const file=path.resolve(row.split('\t')[1]);
   if(!file.toLowerCase().startsWith(root.toLowerCase()+path.sep))throw Error('probe escaped private root');
   const before=fs.statSync(file),doc=await vscode.workspace.openTextDocument(vscode.Uri.file(file));
   await vscode.window.showTextDocument(doc,{preview:false,preserveFocus:true});
   const edit=new vscode.WorkspaceEdit();edit.insert(doc.uri,doc.positionAt(doc.getText().length),'\nVYNX ARC actual VS Code save\n');
   if(!await vscode.workspace.applyEdit(edit)||!await doc.save()||doc.isDirty)throw Error('editor save failed');
   const bytes=fs.readFileSync(file),after=fs.statSync(file);
   report.cases.push({file,saved:true,sha256:crypto.createHash('sha256').update(bytes).digest('hex'),file_id_changed:before.ino!==after.ino});
  }
  report.status='passed';fs.writeFileSync(path.join(root,'editor-result.json'),JSON.stringify(report,null,2));
  await vscode.commands.executeCommand('workbench.action.closeAllEditors');await vscode.commands.executeCommand('workbench.action.closeWindow');
 }catch(error){report.status='failed';report.error=String(error.stack||error);fs.writeFileSync(path.join(root,'editor-result.json'),JSON.stringify(report,null,2));await vscode.commands.executeCommand('workbench.action.closeWindow');}
};
