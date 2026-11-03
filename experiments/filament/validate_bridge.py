"""Validate serialized glTF buffers against the production game's pose export."""
import argparse,json,math,struct
from pathlib import Path

def validate(folder):
    g=json.loads((folder/'character.gltf').read_text());source=json.loads((folder/'source.json').read_text())
    blob=(folder/'character.bin').read_bytes();width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
    def read(index):
        a=g['accessors'][index];v=g['bufferViews'][a['bufferView']];n=width[a['type']]
        code={5126:'f',5123:'H',5125:'I'}[a['componentType']];size=struct.calcsize(code)*n
        start=v.get('byteOffset',0)+a.get('byteOffset',0)
        if a['count']*size>v['byteLength'] or start+a['count']*size>len(blob):raise ValueError('Accessor outside buffer')
        return [struct.unpack_from('<'+code*n,blob,start+i*size) for i in range(a['count'])]
    for i in range(len(g['accessors'])):read(i)
    for image in g['images']:
        if not (folder/image['uri']).is_file():raise ValueError('Missing texture')
    if len(g['skins'][0]['joints'])!=61:raise ValueError('Lost original bones')
    for m in read(g['skins'][0]['inverseBindMatrices']):
        if list(m)!=[1. if i%5==0 else 0. for i in range(16)]:raise ValueError('Unexpected inverse bind')
    animation=g['animations'][0];channels={}
    for c in animation['channels']:
        sampler=animation['samplers'][c['sampler']]
        channels[(c['target']['node'],c['target']['path'])]=read(sampler['output'])
    def mat(t,q,s):
        x,y,z,w=q
        a=[[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],
           [2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],
           [2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]]
        return [a[r][c]*s[c] if r<3 and c<3 else t[r] if c==3 and r<3 else 1. if r==c==3 else 0.
                for c in range(4) for r in range(4)]
    def pose(p,joints,weights,bones):
        total=sum(weights);answer=[0.,0.,0.]
        for j,w in zip(joints,weights):
            for r in range(3):answer[r]+=(sum(bones[j][c*4+r]*p[c] for c in range(3))+bones[j][12+r])*w/total
        return answer
    frames=[json.loads(p.read_text()) for p in sorted(folder.glob('pose-??.json'))]
    triangles=0;worst=0.;checks=0
    for mesh,part in zip(g['meshes'],source['parts']):
        primitive=mesh['primitives'][0];a=primitive['attributes']
        positions=read(a['POSITION']);joints=read(a['JOINTS_0']);weights=read(a['WEIGHTS_0'])
        indices=read(primitive['indices']);triangles+=len(indices)//3
        if any(i[0]>=len(positions) for i in indices):raise ValueError('Invalid triangle index')
        if any(abs(sum(w)-1)>1e-5 for w in weights):raise ValueError('Unnormalized weights')
        if any(j>60 for row in joints for j in row):raise ValueError('Invalid game bone')
        unique=list(dict.fromkeys(tuple(row) for row in part['vertices']))
        if len(unique)!=len(positions):raise ValueError('Vertex identity changed')
        for f,frame in enumerate(frames):
            bones=[mat(channels[(node,'translation')][f],channels[(node,'rotation')][f],channels[(node,'scale')][f])
                   for node in g['skins'][0]['joints']]
            for i in range(0,len(positions),97):
                row=unique[i]
                expected=pose(row[:3],[int(j) for j in row[8:16:2]],row[9:16:2],frame['bones'])
                actual=pose(positions[i],joints[i],weights[i],bones)
                worst=max(worst,max(abs(a-b) for a,b in zip(expected,actual)));checks+=1
    if triangles!=source['triangles'] or worst>1e-4:raise ValueError(f'Geometry/skinning mismatch {triangles}, {worst}')
    report={'serializedVertexPoseChecks':checks,'maxPositionErrorFeet':worst,'triangles':triangles,'bones':61,'frames':len(frames)}
    (folder/'validation.json').write_text(json.dumps(report,indent=2));return report

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);args=parser.parse_args()
    print(json.dumps(validate(args.folder),indent=2))
