"""Install generated prototype assets beside a client executable."""
import argparse, json, shutil
from pathlib import Path

root=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client-dir',type=Path,required=True)
args=parser.parse_args()
files=[root/'output'/'human-prototype.json',root/'output'/'human-diffuse.png',
       root/'LICENSE.ASSETS.md',root/'MAKEHUMAN-LICENSE.md',root/'asset-manifest.json']
for path in files:
    if not path.is_file():raise SystemExit('Missing asset: '+str(path))
mesh=json.loads(files[0].read_text())
if mesh.get('version')!=2 or len(mesh.get('inverseBind',[]))!=61:
    raise SystemExit('Unsupported prototype mesh format')
destination=args.client_dir.resolve()/'human-prototype'
destination.mkdir(parents=True,exist_ok=True)
for path in files:shutil.copy2(path,destination/path.name)
print('Installed human prototype in',destination)
