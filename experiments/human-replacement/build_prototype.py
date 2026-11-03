"""Blender batch: fit CC0 human to the game's standing reference; bake and export.

Run with blender -b --python build_prototype.py. Source OBJ remains untouched.
"""
import bpy, gzip, json, math
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.kdtree import KDTree

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'output'
OUT.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

def read_obj(path):
    vertices, uvs, groups = [], [], {}
    group = 'default'
    for line in path.read_text().splitlines():
        fields=line.split()
        if not fields: continue
        if fields[0]=='v': vertices.append(Vector(tuple(map(float,fields[1:4]))))
        elif fields[0]=='vt': uvs.append(tuple(map(float,fields[1:3])))
        elif fields[0]=='g': group=fields[1]
        elif fields[0]=='f':
            face=[]
            for corner in fields[1:]:
                indices=corner.split('/')
                face.append((int(indices[0])-1,int(indices[1])-1 if len(indices)>1 and indices[1] else 0))
            groups.setdefault(group,[]).append(face)
    return vertices,uvs,groups

source,uvs,groups=read_obj(ROOT/'base.obj')
for line in gzip.open(ROOT/'male-young.target.gz','rt'):
    fields=line.split()
    if len(fields)==4 and not line.startswith('#'):
        source[int(fields[0])] += Vector(tuple(map(float,fields[1:])))

def joint(name):
    ids={v for f in groups['joint-'+name] for v,uv in f}
    return sum((source[i] for i in ids),Vector())/len(ids)

print('SOURCE JOINTS', {n:tuple(joint(n)) for n in ['head','neck','l-shoulder','l-elbow','l-ankle','pelvis']})
reference=json.loads((ROOT/'legacy-reference.json').read_text())
matrices=[]
for basis in reference['bones']:
    origin=Vector(basis[0]); m=Matrix.Identity(4)
    for col in range(3):
        delta=Vector(basis[col+1])-origin
        for row in range(3):m[row][col]=delta[row]
    for row in range(3):m[row][3]=origin[row]
    matrices.append(m)
refrows=[]
for part in reference['parts']:
    for row in part['vertices']:
        blend=sum((matrices[int(row[8+i*2])]*row[9+i*2] for i in range(4)),Matrix(((0,)*4,)*4))
        posed=blend@Vector((*row[:3],1))
        refrows.append((Vector(posed[:3]),row))
tree=KDTree(len(refrows))
for i,(pos,row) in enumerate(refrows):tree.insert(pos,i)
tree.balance()

# MakeHuman is Y-up / +Z-forward; the game standing pose is Z-up / +Y-forward.
body_faces=groups['body']
body_ids={v for face in body_faces for v,uv in face}
low=min(source[i].y for i in body_ids); high=max(source[i].y for i in body_ids)
target_low=min(p.z for p,row in refrows); target_high=max(p.z for p,row in refrows)
scale=(target_high-target_low)/(high-low)
shoulder=joint('l-shoulder')
pelvis=joint('pelvis')

def fitted(p):
    p=p.copy()
    # Lower the source A-pose arms into the game's relaxed standing pose.
    side=1 if p.x>0 else -1
    pivot=Vector((side*abs(shoulder.x),shoulder.y,shoulder.z))
    if abs(p.x)>abs(shoulder.x)*.88 and p.y<shoulder.y+.5 and (p.y>pelvis.y+.5 or abs(p.x)>3.1):
        weight=min(1,max(0,(abs(p.x)-abs(shoulder.x)*.88)/.8))
        angle=math.radians(-side*47)*weight
        delta=p-pivot
        p=pivot+Vector((math.cos(angle)*delta.x-math.sin(angle)*delta.y,
                       math.sin(angle)*delta.x+math.cos(angle)*delta.y,delta.z))
    return Vector((p.x*scale,p.z*scale,(p.y-low)*scale+target_low))

def smoothstep(a,b,x):
    t=min(1,max(0,(x-a)/(b-a)))
    return t*t*(3-2*t)

def anatomical_weights(p):
    # The legacy rig has no finger bones. Constrain each anatomical chain so
    # neighboring hands, torso and thighs cannot steal each other's weights.
    side='l' if p.x>0 else 'r'
    arm=abs(p.x)>abs(shoulder.x)*.88 and (p.y>pelvis.y+.5 or abs(p.x)>3.1)
    if p.y>joint('neck').y-.45:
        head=smoothstep(joint('neck').y-.45,joint('neck').y+.1,p.y)
        return {7:head,8:1-head}
    if arm:
        upper,fore,hand=(28,27,26) if p.x>0 else (25,24,23)
        elbow=joint(side+'-elbow');wrist=joint(side+'-hand')
        fore_weight=1-smoothstep(elbow.y-.3,elbow.y+.3,p.y)
        hand_weight=1-smoothstep(wrist.y-.25,wrist.y+.25,p.y)
        return {upper:1-fore_weight,fore:fore_weight*(1-hand_weight),hand:fore_weight*hand_weight}
    if p.y<pelvis.y+.4:
        thigh,calf,foot=(46,45,44) if p.x>0 else (43,42,41)
        knee=joint(side+'-knee');ankle=joint(side+'-ankle')
        calf_weight=1-smoothstep(knee.y-.4,knee.y+.4,p.y)
        foot_weight=1-smoothstep(ankle.y-.35,ankle.y+.35,p.y)
        hip=1-smoothstep(pelvis.y-.7,pelvis.y+.4,p.y)
        return {1:1-hip,thigh:hip*(1-calf_weight),calf:hip*calf_weight*(1-foot_weight),foot:hip*calf_weight*foot_weight}
    upper=smoothstep(pelvis.y+1,pelvis.y+3,p.y)
    return {0:upper,1:1-upper}

authored=json.loads((ROOT/'default_weights.mhw').read_text())['weights']
source_weights={i:{} for i in body_ids}
def game_bone(name):
    positive=name.endswith('.L')
    if name.startswith('upperarm'):return 28 if positive else 25
    if name.startswith('lowerarm'):return 27 if positive else 24
    if name.startswith(('finger','metacarpal','wrist')):return 26 if positive else 23
    if name.startswith('upperleg'):return 46 if positive else 43
    if name.startswith('lowerleg'):return 45 if positive else 42
    if name.startswith(('foot','toe')):return 44 if positive else 41
    if name.startswith(('clavicle','shoulder','breast')) or name in ['spine01','spine02']:return 0
    if name.startswith('neck'):return 8
    if name.startswith('pelvis') or name in ['root','spine03','spine04','spine05']:return 1
    return 7
for name,values in authored.items():
    bone=game_bone(name)
    for i,weight in values:
        if i in source_weights:source_weights[i][bone]=source_weights[i].get(bone,0)+weight
weight_tree=KDTree(len(body_ids))
for j,i in enumerate(body_ids):
    weight_tree.insert(fitted(source[i]),i)
    if not source_weights[i]:source_weights[i]=anatomical_weights(source[i])
weight_tree.balance()

def material(name,color,roughness=.65,image=None):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    bsdf=m.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value=(*color,1)
    bsdf.inputs['Roughness'].default_value=roughness
    if image:
        tex=m.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(image))
        uv=m.node_tree.nodes.new('ShaderNodeUVMap');uv.uv_map='SourceUV'
        m.node_tree.links.new(uv.outputs['UV'],tex.inputs['Vector'])
        m.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
        if name=='short01':
            m.node_tree.links.new(tex.outputs['Alpha'],bsdf.inputs['Alpha'])
            m['alpha_cutout']=True
    return m

skin_paths=list((ROOT/'assets'/'skins'/'young_caucasian_male').glob('*diffuse*.png'))
if not skin_paths:skin_paths=list((ROOT/'assets').rglob('*male*diffuse*.png'))
if not skin_paths:raise RuntimeError('Official male diffuse skin texture not found in assets/')
print('SKIN',skin_paths[0])
skin=material('CC0 skin',(.55,.34,.23),image=skin_paths[0])
shirt=material('Midnight blue undershirt',(.055,.11,.145),.85)
pants=material('Charcoal trousers',(.045,.055,.065),.9)
boots=material('Brown boots',(.045,.027,.018),.8)
hairmat=material('Short brown hair',(.045,.021,.012),.85)
eye_white=material('Eye sclera',(.75,.72,.65),.35)
iris=material('Iris',(.07,.17,.12),.4)
pupil=material('Pupil',(.004,.004,.004),.3)

def mesh_object(name,points,faces,source_uvs,materials,weights=None):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(points,[],[[v for v,uv in f] for f in faces]);mesh.update()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
    layer=mesh.uv_layers.new(name='SourceUV')
    for poly,face in zip(mesh.polygons,faces):
        for loop,corner in zip(poly.loop_indices,face):layer.data[loop].uv=source_uvs[corner[1]] if source_uvs else (0,0)
        poly.use_smooth=True
    for m in materials:mesh.materials.append(m)
    if weights is None:
        weights=[source_weights.get(i,{7:1}) for i in range(len(points))]
    vertex_groups={bone:obj.vertex_groups.new(name='GameBone_'+str(bone)) for bone in range(61)}
    for i,influences in enumerate(weights):
        total=sum(max(0,w) for w in influences.values())
        if not total:raise RuntimeError('Missing vertex skin weights: '+name)
        for bone,weight in influences.items():
            if weight>0:vertex_groups[bone].add([i],weight/total,'REPLACE')
    return obj

def proxy(name,family='clothes'):
    directory=ROOT/'assets'/family/name
    points,proxy_uvs,proxy_groups=read_obj(directory/(name+'.obj'))
    lines=(directory/(name+'.mhclo')).read_text().splitlines()
    axis_scale=[1.,1.,1.];mapped=[];mapped_weights=[];deleted=set();section=''
    for line in lines:
        fields=line.split()
        if not fields or fields[0].startswith('#'):continue
        if fields[0] in ['x_scale','y_scale','z_scale']:
            axis='xyz'.index(fields[0][0]);a,b=int(fields[1]),int(fields[2])
            axis_scale[axis]=abs(source[a][axis]-source[b][axis])/float(fields[3])
        elif fields[0]=='verts':section='verts'
        elif fields[0]=='delete_verts':section='delete'
        elif fields[0].isdecimal() and section=='verts':
            if len(fields)==1:
                index=int(fields[0]);mapped.append(source[index].copy());mapped_weights.append(source_weights.get(index,{7:1}))
            elif len(fields)==9:
                indices=list(map(int,fields[:3]));weights=list(map(float,fields[3:6]))
                offset=Vector(tuple(float(x)*s for x,s in zip(fields[6:],axis_scale)))
                mapped.append(sum((source[i]*w for i,w in zip(indices,weights)),Vector())+offset)
                influence={}
                for index,weight in zip(indices,weights):
                    for bone,bw in source_weights.get(index,anatomical_weights(source[index])).items():
                        influence[bone]=influence.get(bone,0)+max(0,weight)*bw
                mapped_weights.append(influence)
        elif fields[0].isdecimal() and section=='delete':
            i=0
            while i<len(fields):
                if i+2<len(fields) and fields[i+1]=='-':
                    deleted.update(range(int(fields[i]),int(fields[i+2])+1));i+=3
                else:deleted.add(int(fields[i]));i+=1
        elif section:section=''
    if len(mapped)!=len(points):raise RuntimeError('Proxy vertex count mismatch: '+name)
    faces=[face for group in proxy_groups.values() for face in group]
    diffuse=directory/(name+'_diffuse.png')
    mat=material(name,(.1,.1,.1),.8,diffuse)
    return mesh_object(name,[fitted(p) for p in mapped],faces,proxy_uvs,[mat],mapped_weights),deleted

garment,hidden_shirt=proxy('male_worksuit01')
shoes,hidden_shoes=proxy('shoes01')
hidden=hidden_shirt|hidden_shoes
visible_faces=[face for face in body_faces if not any(v in hidden for v,uv in face)]
body=mesh_object('Human', [fitted(p) for p in source],visible_faces,uvs,[skin])

# Use authored hair geometry and its cutout texture rather than a scalp cap.
hair,_=proxy('short01','hair')

eye_points,eye_uvs,eye_groups=read_obj(ROOT/'eyes.obj')
eye_faces=[face for faces in eye_groups.values() for face in faces]
# Eye geometry shares the base OBJ's original coordinates. Apply nearby shape deltas.
source_base,_,_=read_obj(ROOT/'base.obj')
mapping=(ROOT/'eyes.mhclo').read_text().split('verts 0',1)[1].splitlines()
mapped=[]
for line in mapping:
    fields=line.split()
    if len(fields)!=9:continue
    indices=list(map(int,fields[:3]));weights=list(map(float,fields[3:6]));offset=Vector(tuple(map(float,fields[6:])))
    mapped.append(sum((source[i]*w for i,w in zip(indices,weights)),Vector())+offset)
if len(mapped)!=len(eye_points):raise RuntimeError('Eye fitting metadata does not match eye OBJ')
eye_points=mapped
eyes=mesh_object('Eyes',[fitted(p) for p in eye_points],eye_faces,eye_uvs,[eye_white,iris,pupil],[{7:1} for p in eye_points])
for poly in eyes.data.polygons:
    uv=sum((eyes.data.uv_layers[0].data[i].uv for i in poly.loop_indices),Vector((0,0)))/len(poly.loop_indices)
    # Material regions are supplied by the eye OBJ's UV layout; use its texture when available.
eye_images=list((ROOT/'assets').rglob('*brown*.png'))
eye_images=[p for p in eye_images if 'eye' in str(p).lower()]
if eye_images:
    eyes.data.materials[0]=material('CC0 eyes',(.7,.7,.7),.4,eye_images[0])

# Remove unused helper vertices before unwrapping; keep all visible mesh topology.
for obj in [body,hair,eyes,garment,shoes]:
    bpy.context.view_layer.objects.active=obj;obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.delete_loose();bpy.ops.object.mode_set(mode='OBJECT');obj.select_set(False)
bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=body;bpy.ops.object.join()
body.data.uv_layers.new(name='GameAtlas');body.data.uv_layers.active_index=1
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(island_margin=.006);bpy.ops.object.mode_set(mode='OBJECT')
atlas=bpy.data.images.new('Human diffuse atlas',width=2048,height=2048)
for m in body.data.materials:
    node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;m.node_tree.nodes.active=node
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=1
scene.render.bake.use_pass_direct=False;scene.render.bake.use_pass_indirect=False;scene.render.bake.use_pass_color=True
scene.render.bake.margin=8
bpy.ops.object.bake(type='DIFFUSE')
# Bake alpha separately so the game can discard empty hair-card texels.
alpha_image=bpy.data.images.new('Cutout mask',width=2048,height=2048)
restore=[]
for m in body.data.materials:
    nodes=m.node_tree.nodes;links=m.node_tree.links
    output=next(n for n in nodes if n.type=='OUTPUT_MATERIAL')
    original=output.inputs['Surface'].links[0].from_socket
    emission=nodes.new('ShaderNodeEmission');emission.inputs['Color'].default_value=(1,1,1,1)
    if m.get('alpha_cutout'):
        source_tex=next(n for n in nodes if n.type=='TEX_IMAGE' and n.image!=atlas)
        links.new(source_tex.outputs['Alpha'],emission.inputs['Color'])
    links.new(emission.outputs[0],output.inputs['Surface'])
    target=nodes.new('ShaderNodeTexImage');target.image=alpha_image;nodes.active=target
    restore.append((m,output,original,emission,target))
bpy.ops.object.bake(type='EMIT')
from array import array
color=array('f',[0.0])*(2048*2048*4);mask=array('f',[0.0])*(2048*2048*4)
atlas.pixels.foreach_get(color);alpha_image.pixels.foreach_get(mask)
color[3::4]=mask[0::4];atlas.pixels.foreach_set(color)
for m,output,original,emission,target in restore:
    m.node_tree.links.new(original,output.inputs['Surface'])
    m.node_tree.nodes.remove(emission);m.node_tree.nodes.remove(target)
atlas.filepath_raw=str(OUT/'human-diffuse.png');atlas.file_format='PNG';atlas.save()

# Export triangle corners in the game's existing four-influence body layout.
body.data.calc_loop_triangles();rows=[];headrows=[];max_error=0
for tri in body.data.loop_triangles:
    destination=headrows if sum(body.data.vertices[i].co.z for i in tri.vertices)/3>2.0 else rows
    for loop_index,vertex_index in zip(tri.loops,tri.vertices):
        vertex=body.data.vertices[vertex_index];point=vertex.co
        influences=[(int(body.vertex_groups[g.group].name.split('_')[1]),g.weight) for g in vertex.groups if g.weight>1e-8]
        influences=sorted(influences,key=lambda pair:pair[1],reverse=True)[:4]
        influences += [(0,0)]*(4-len(influences))
        indices=[i for i,w in influences];weights=[w for i,w in influences]
        total=sum(weights);weights=[w/total for w in weights]
        blend=sum((matrices[index]*weight for index,weight in zip(indices,weights)),Matrix(((0,)*4,)*4))
        position=point.to_4d()
        normal=vertex.normal.normalized()
        restored=sum((matrices[index]@matrices[index].inverted()@position*weight for index,weight in zip(indices,weights)),Vector((0,0,0,0)))
        max_error=max(max_error,(restored.xyz-point).length)
        uv=body.data.uv_layers['GameAtlas'].data[loop_index].uv
        destination.append([*position[:3],*normal,float(uv.x),float(uv.y),*sum(([i,w] for i,w in zip(indices,weights)),[])])
inverse_bind=[[float(m.inverted()[r][c]) for c in range(4) for r in range(4)] for m in matrices]
(OUT/'human-prototype.json').write_text(json.dumps({'version':2,'inverseBind':inverse_bind,'body':rows,'head':headrows},separators=(',',':')))
print('EXPORT',len(rows)//3,len(headrows)//3,'triangles; bind reconstruction max error',max_error)

# Save the editable source with its original skin texture and baked atlas.
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'human-prototype.blend'))
scene.render.engine='CYCLES';scene.cycles.samples=24
scene.world.color=(.12,.12,.12)
def aim(obj,point):obj.rotation_euler=(Vector(point)-obj.location).to_track_quat('-Z','Y').to_euler()
for location,power,size in [((4,5,7),1100,5),((-4,1,3),600,4),((1,-4,5),900,3)]:
    bpy.ops.object.light_add(type='AREA',location=location);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size;aim(light,(0,0,.6))
bpy.ops.object.camera_add(location=(6,10,3));camera=bpy.context.object;aim(camera,(0,0,0));camera.data.type='ORTHO';camera.data.ortho_scale=7.1;scene.camera=camera
scene.render.resolution_x=900;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'human-preview.png');bpy.ops.render.render(write_still=True)
camera.location=(3.3,6,2.8);aim(camera,(0,.1,2.2));camera.data.ortho_scale=2.5
scene.render.filepath=str(OUT/'face-preview.png');bpy.ops.render.render(write_still=True)
