"""Build-only license collection. Never included as an application runtime dependency."""
import json, pathlib, shutil, sys
root=pathlib.Path(sys.argv[1]); qt=pathlib.Path(sys.argv[2])
metadata=json.loads((root/'CARGO_LICENSES.json').read_text(encoding='utf-8-sig'))
resolved={node['id'] for node in metadata['resolve']['nodes']}
metadata['packages']=[package for package in metadata['packages'] if package['id'] in resolved]
for package in metadata['packages']:
    source=pathlib.Path(package['manifest_path']).parent
    target=root/'rust'/f"{package['name']}-{package['version']}"
    target.mkdir(parents=True,exist_ok=True)
    found=False
    for file in source.rglob('*'):
        if file.is_file() and any(file.name.upper().startswith(n) for n in ('LICENSE','COPYING','NOTICE')):
            relative=file.relative_to(source);(target/relative).parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(file,target/relative);found=True
    if not found and package['source'] is not None:
        if package['name'].startswith('winapi-'):
            owner=next(p for p in metadata['packages'] if p['name']=='winapi')
            owner_dir=pathlib.Path(owner['manifest_path']).parent
            for license_file in owner_dir.glob('LICENSE*'):shutil.copy2(license_file,target/license_file.name)
            (target/'ORIGIN.txt').write_text('These generated WinAPI import libraries are part of the winapi-rs project and use its MIT OR Apache-2.0 license.',encoding='utf-8')
        else:raise RuntimeError(f"License text missing for {package['name']}; resolve before release")
for source in (pathlib.Path(__file__).resolve().parent.parent/'third_party/notices').iterdir():
    shutil.copy2(source,root/source.name)
if not (root/'LGPL-3.0.txt').exists():raise RuntimeError('Checked-in Qt notices missing')
for source in (qt/'sbom').glob('qtbase*'):
    shutil.copy2(source,root/source.name)
compiler=qt/'Tools/mingw1310_64'
for file in compiler.rglob('*'):
    if file.is_file() and file.name.lower() in ['copying','copying3','copying.runtime','copying.lib','copyright','license.txt']:
        target=root/'mingw'/file.relative_to(compiler);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
rust_doc=pathlib.Path.home()/'.rustup/toolchains/1.99.0-x86_64-pc-windows-gnu/share/doc/rust'
for name in ['COPYRIGHT-library.html']:
    if not (rust_doc/name).exists():raise RuntimeError('Rust standard library notice missing: '+name)
    shutil.copy2(rust_doc/name,root/('Rust-'+name))
summary=[{key:package.get(key) for key in ('name','version','license','repository','source')} for package in metadata['packages']]
(root/'CARGO_LICENSES.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
