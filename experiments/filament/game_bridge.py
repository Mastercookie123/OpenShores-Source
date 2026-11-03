"""Convert actual DpBody render vertices + absolute bone poses into skinned glTF.

No mesh fitting, replacement skeleton or assumed inverse-bind pose. The legacy
shader uses absolute bone matrices; identity inverse binds reproduce that rule.
"""
import argparse,hashlib,json,math,struct,subprocess,shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--variant',choices=['human','multi','quadruped'],default='human')
parser.add_argument('--pose',choices=['stand','walk','strike'],default='walk')
parser.add_argument('--samples',type=int,default=13)
parser.add_argument('--dna',help='Actual 63-character game DNA, if provided')
parser.add_argument('--exporter',type=Path,default=Path('/opt/hazeron/Source/build/linux-debug/bin/ExportRendererScene'))
parser.add_argument('--pack',type=Path,default=ROOT.parent/'Client/body-parts-modular')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
if not 2<=args.samples<=241:raise SystemExit('Samples must be between 2 and 241')
if not (args.pack/'manifest.json').is_file():raise SystemExit('Candidate pack manifest is missing')
def export(path,seconds,pose,bones_only=False):
    command=[str(args.exporter),'-o',str(path),'-p',str(args.pack),'--textures',str(ROOT.parent/'Client'),
             '--variant',args.variant,'--pose',pose,'--seconds',str(seconds)]
    if bones_only:command+=['--bones-only']
    if args.dna:command+=['--dna',args.dna]
    log_path=path.with_suffix('.log')
    with log_path.open('w') as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
    diagnostics=log_path.read_text()
    if 'Modular body part rejected' in diagnostics:raise RuntimeError('Component checksum rejected; inspect '+str(log_path))
    if not bones_only and 'VISUAL_ASSET' not in diagnostics:raise RuntimeError('No enhanced game components were loaded')
    return json.loads(path.read_text())
source=export(out/'source.json',0,'stand')
duration=source['cycleSeconds'];times=[duration*i/(args.samples-1) for i in range(args.samples)]
frames=[export(out/f'pose-{i:02}.json',t,args.pose,True) for i,t in enumerate(times)]
offset=source['skinOffset']
if offset[:2]!=offset[2:]:raise RuntimeError('Dual-palette blending needs a custom Filament material; not silently approximated')
def xyz(m,p):return [sum(m[c*4+r]*p[c] for c in range(3))+m[12+r] for r in range(3)]
def posed(row,bones):
    weights=row[9:16:2];total=sum(weights)
    if total<=0:raise ValueError('Zero skin weight')
    p=[0.,0.,0.]
    for joint,weight in zip(row[8:16:2],weights):
        q=xyz(bones[int(joint)],row[:3])
        for k in range(3):p[k]+=q[k]*weight/total
    return p
def trs(m):
    scale=[math.sqrt(sum(m[c*4+r]**2 for r in range(3))) for c in range(3)]
    a=[[m[c*4+r]/scale[c] for c in range(3)] for r in range(3)]
    trace=sum(a[i][i] for i in range(3))
    if trace>0:
        s=math.sqrt(trace+1)*2;q=[(a[2][1]-a[1][2])/s,(a[0][2]-a[2][0])/s,(a[1][0]-a[0][1])/s,s/4]
    else:
        i=max(range(3),key=lambda j:a[j][j]);j=(i+1)%3;k=(i+2)%3
        s=math.sqrt(max(0,1+a[i][i]-a[j][j]-a[k][k]))*2
        q=[0.,0.,0.,0.];q[i]=s/4;q[j]=(a[j][i]+a[i][j])/s;q[k]=(a[k][i]+a[i][k])/s;q[3]=(a[k][j]-a[j][k])/s
    return m[12:15],q,scale
def matrix(t,q,s):
    x,y,z,w=q
    a=[[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],
       [2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],
       [2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]]
    return [a[r][c]*s[c] if r<3 and c<3 else t[r] if c==3 and r<3 else 1. if r==c==3 else 0.
            for c in range(4) for r in range(4)]
rows=[row for part in source['parts'] for row in part['vertices']]
standing=[posed(row,source['bones']) for row in rows]
floor=-min(p[2] for p in standing)*.3048
height=(max(p[2] for p in standing)-min(p[2] for p in standing))*.3048
camera_points=standing+[posed(row,frame['bones']) for frame in frames[::3] for row in rows[::37]]
minimum=[min(p[i] for p in camera_points) for i in range(3)]
maximum=[max(p[i] for p in camera_points) for i in range(3)]
extent=max((maximum[i]-minimum[i])*.3048 for i in range(3))*1.12
target=[(minimum[0]+maximum[0])*.1524,(minimum[2]+maximum[2])*.1524+floor,-(minimum[1]+maximum[1])*.1524]
g={'asset':{'version':'2.0','generator':'OpenShores DpBody to Filament bridge'},'scene':0,
   'scenes':[{'nodes':[0]}],'nodes':[{'name':'Feet-to-meters Z-up conversion','rotation':[-math.sqrt(.5),0,0,math.sqrt(.5)],
    'scale':[.3048]*3,'translation':[0,floor,0],'children':[]}],
   'buffers':[],'bufferViews':[],'accessors':[],'meshes':[],'skins':[],'animations':[],
   'images':[{'uri':'skin.png'}],'samplers':[{'magFilter':9729,'minFilter':9987,'wrapS':10497,'wrapT':10497}],
   'textures':[{'sampler':0,'source':0}],
   'materials':[{'name':'Legacy skin artwork, dielectric PBR','alphaMode':'MASK','alphaCutoff':.15,'doubleSided':False,
       'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':.66}}]}
data=bytearray()
def accessor(values,width,kind=5126,target=None):
    while len(data)%4:data.append(0)
    start=len(data);flat=[x for row in values for x in row] if width>1 else values
    code={5126:'f',5123:'H',5125:'I'}[kind];data.extend(struct.pack('<'+code*len(flat),*flat))
    view={'buffer':0,'byteOffset':start,'byteLength':len(data)-start}
    if target:view['target']=target
    g['bufferViews'].append(view)
    a={'bufferView':len(g['bufferViews'])-1,'componentType':kind,'count':len(values),
       'type':{1:'SCALAR',2:'VEC2',3:'VEC3',4:'VEC4',16:'MAT4'}[width]}
    if width in (1,3):
        a['min']=[min(values)] if width==1 else [min(v[i] for v in values) for i in range(width)]
        a['max']=[max(values)] if width==1 else [max(v[i] for v in values) for i in range(width)]
    g['accessors'].append(a);return len(g['accessors'])-1
joint_nodes=[]
for b,m in enumerate(frames[0]['bones']):
    t,q,s=trs(m);joint_nodes.append(len(g['nodes']))
    g['nodes'].append({'name':f'Actual game bone {b}','translation':t,'rotation':q,'scale':s})
g['nodes'][0]['children']+=joint_nodes
identity=[1. if i%5==0 else 0. for i in range(16)]
g['skins'].append({'name':'Original 61 absolute game bones','joints':joint_nodes,
                   'inverseBindMatrices':accessor([identity]*61,16)})
for part in source['parts']:
    vertices=[];lookup={};indices=[]
    for row in part['vertices']:
        key=tuple(row)
        if key not in lookup:lookup[key]=len(vertices);vertices.append(row)
        indices.append(lookup[key])
    # The legacy client front face is clockwise; glTF front face is CCW.
    for i in range(0,len(indices),3):indices[i+1],indices[i+2]=indices[i+2],indices[i+1]
    weights=[]
    for row in vertices:
        w=row[9:16:2];total=sum(w);weights.append([x/total for x in w])
    attributes={'POSITION':accessor([v[:3] for v in vertices],3,target=34962),
        'NORMAL':accessor([v[3:6] for v in vertices],3,target=34962),
        'TEXCOORD_0':accessor([[v[6]+offset[0],1-v[7]-offset[1]] for v in vertices],2,target=34962),
        'JOINTS_0':accessor([[int(v[i]) for i in (8,10,12,14)] for v in vertices],4,5123,34962),
        'WEIGHTS_0':accessor(weights,4,target=34962)}
    mesh=len(g['meshes']);g['meshes'].append({'name':part['name'],'primitives':[{'attributes':attributes,
        'indices':accessor(indices,1,5125,34963),'material':0}]})
    g['nodes'][0]['children'].append(len(g['nodes']));g['nodes'].append({'name':part['name'],'mesh':mesh,'skin':0})
animation={'name':f'Actual game {args.pose} cycle','samplers':[],'channels':[]};time_accessor=accessor(times,1)
worst=0.;vertex_error=0.;float_error=0.
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
for b,node in enumerate(joint_nodes):
    decomposed=[trs(frame['bones'][b]) for frame in frames]
    for i in range(1,len(decomposed)):
        if sum(a*b for a,b in zip(decomposed[i-1][1],decomposed[i][1]))<0:
            decomposed[i]=(decomposed[i][0],[-q for q in decomposed[i][1]],decomposed[i][2])
    for f,(t,q,s) in zip(frames,decomposed):
        rebuilt=matrix(t,q,s);worst=max(worst,max(abs(x-y) for x,y in zip(rebuilt,f['bones'][b])))
    for component,path in enumerate(('translation','rotation','scale')):
        sampler=len(animation['samplers']);animation['samplers'].append({'input':time_accessor,
            'output':accessor([v[component] for v in decomposed],4 if component==1 else 3),'interpolation':'LINEAR'})
        animation['channels'].append({'sampler':sampler,'target':{'node':node,'path':path}})
g['animations'].append(animation)
for frame in frames:
    rebuilt=[matrix(*trs(m)) for m in frame['bones']]
    quantized=[matrix(*[[f32(x) for x in values] for values in trs(m)]) for m in frame['bones']]
    for row in rows[::97]:
        original=posed(row,frame['bones']);converted=posed(row,rebuilt)
        vertex_error=max(vertex_error,max(abs(a-b) for a,b in zip(original,converted)))
        quantized_row=[f32(x) for x in row]
        converted=posed(quantized_row,quantized)
        float_error=max(float_error,max(abs(a-b) for a,b in zip(original,converted)))
if worst>1e-4 or vertex_error>1e-4 or float_error>1e-4:raise RuntimeError(f'Skinning conversion drift: {worst}, {vertex_error}, {float_error}')
# A neutral reference floor, intentionally separate from exported game geometry.
g['materials'].append({'name':'Neutral reference deck','pbrMetallicRoughness':{'baseColorFactor':[.16,.18,.20,1],
    'metallicFactor':0,'roughnessFactor':.85}})
floor_mesh=len(g['meshes']);g['meshes'].append({'name':'Reference floor','primitives':[{
    'attributes':{'POSITION':accessor([[-4,0,-4],[-4,0,4],[4,0,4],[4,0,-4]],3, target=34962),
                  'NORMAL':accessor([[0,1,0]]*4,3,target=34962)},'indices':accessor([0,1,2,0,2,3],1,5125,34963),'material':1}]})
g['scenes'][0]['nodes'].append(len(g['nodes']));g['nodes'].append({'name':'Neutral deck','mesh':floor_mesh})
g['buffers']=[{'uri':'character.bin','byteLength':len(data)}];(out/'character.bin').write_bytes(data)
texture=['Exo.png','Scales.png','Skin.png','FurFeathers.png','FurFeathers.png','Exo.png','Scales.png','Skin.png'][source['skinType']]
shutil.copy2(ROOT.parent/'Client/tex'/texture,out/'skin.png')
(out/'character.gltf').write_text(json.dumps(g,separators=(',',':')))
settings={'camera':{'enabled':True,'center':[target[0]+extent*.9,target[1]+extent*.18,target[2]-extent*2.1],
    'lookAt':target,'up':[0,1,0],'focalLength':45,'aperture':16,'shutterSpeed':125,'sensitivity':100},
    'viewer':{'autoScaleEnabled':False,'skyboxEnabled':False,'backgroundColor':[.025,.035,.05]},
    'lighting':{'enableShadows':True,'enableSunlight':True,'iblIntensity':14000,
        'sunlight':{'direction':[-.4,-.8,.5],'intensity':80000,'castShadows':True}},
    'view':{'ssao':{'enabled':True}}}
(out/'settings.json').write_text(json.dumps(settings,indent=2))
report={'source':'Production DpBody DNA assembly, not a fixed replacement human','variant':args.variant,'pose':args.pose,
    'triangles':source['triangles'],'bones':61,'samples':args.samples,'cycleSeconds':duration,
    'maxBoneTRSError':worst,'maxSkinnedPositionErrorFeet':vertex_error,
    'maxFloat32SkinnedPositionErrorFeet':float_error,'dna':source.get('dna'),
    'componentManifestSha256':hashlib.sha256((args.pack/'manifest.json').read_bytes()).hexdigest(),
    'skinTextureSha256':hashlib.sha256((out/'skin.png').read_bytes()).hexdigest(),
    'units':'Legacy feet converted to meters, Z-up to Y-up at a common root',
    'limits':['Current bridge exports skin only, not ordinary clothing layers or procedural fur shells',
              'Two different top/bottom palettes require a custom material; rejected instead of approximated',
              'Neutral floor is synthetic; this is not the live world renderer or a server-connected client']}
(out/'bridge-report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
