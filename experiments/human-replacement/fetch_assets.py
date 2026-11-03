"""Fetch selected CC0 system assets using HTTP byte ranges, without unpacking the full pack."""
import io, json, zipfile, urllib.request, hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parent
URL='https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip'
class RemoteFile(io.RawIOBase):
    def __init__(self):
        self.position=0
        request=urllib.request.Request(URL,method='HEAD')
        with urllib.request.urlopen(request,timeout=60) as response:self.length=int(response.headers['Content-Length'])
    def seekable(self):return True
    def readable(self):return True
    def tell(self):return self.position
    def seek(self,offset,whence=0):
        self.position=offset if whence==0 else (self.position if whence==1 else self.length)+offset
        return self.position
    def read(self,size=-1):
        size=min(size if size>=0 else self.length-self.position,self.length-self.position)
        if size<=0:return b''
        request=urllib.request.Request(URL,headers={'Range':f'bytes={self.position}-{self.position+size-1}'})
        with urllib.request.urlopen(request,timeout=120) as response:
            if response.status!=206:raise RuntimeError('Asset server did not honor byte range')
            data=response.read()
        self.position+=len(data)
        return data
with zipfile.ZipFile(RemoteFile()) as archive:
    names=archive.namelist()
    (ROOT/'asset-pack-index.txt').write_text('\n'.join(names))
    selected=[n for n in names if ('young_caucasian_male/' in n and 'diffuse' in n and n.endswith('.png'))]
    selected=selected[:1]+[n for n in names if 'eyes' in n and 'brown' in n and n.endswith('.png')][:1]
    selected += [n for n in names if n.startswith(('clothes/male_worksuit01/','clothes/shoes01/','hair/short01/')) and not n.endswith('/')]
    selected += [n for n in names if n.lower().endswith(('license.txt','license.md'))][:2]
    if not selected:raise RuntimeError('No matching skin assets')
    manifest=[]
    for name in selected:
        print('Fetching',name,flush=True)
        target=ROOT/'assets'/name
        if target.is_file():
            data=target.read_bytes()
        else:data=archive.read(name)
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        manifest.append({'path':name,'sha256':hashlib.sha256(data).hexdigest()})
    (ROOT/'asset-manifest.json').write_text(json.dumps({'source':URL,'files':manifest},indent=2))
