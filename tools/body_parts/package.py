"""Copy a verified modular component pack beside a client executable."""
import argparse,json,hashlib,shutil
from pathlib import Path
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client-dir',type=Path,required=True)
parser.add_argument('--folder',choices=['body-parts','body-parts-visual','body-parts-modular'],default='body-parts')
args=parser.parse_args()
pack=root/'experiments/procedural-parts/pack'
manifest=json.loads((pack/'manifest.json').read_text())
if manifest.get('version')!=1:raise SystemExit('Unsupported pack manifest')
for name,entry in manifest['files'].items():
    if Path(name).name!=name or not name.endswith('.3dsN'):raise SystemExit('Invalid component filename')
    if hashlib.sha256((pack/name).read_bytes()).hexdigest()!=entry['assetSha256']:raise SystemExit('Component checksum mismatch: '+name)
destination=args.client_dir.resolve()/args.folder;destination.mkdir(parents=True,exist_ok=True)
for name in [*manifest['files'],'manifest.json']:shutil.copy2(pack/name,destination/name)
for name,entry in manifest.get('materials',{}).items():
    if Path(name).is_absolute() or '..' in Path(name).parts:raise SystemExit('Invalid material path')
    if hashlib.sha256((pack/name).read_bytes()).hexdigest()!=entry['assetSha256']:raise SystemExit('Material checksum mismatch')
    (destination/name).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(pack/name,destination/name)
print('Installed',len(manifest['files']),'procedural components in',destination)
