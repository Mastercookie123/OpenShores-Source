"""Blender batch authoring for compatible procedural limb components.

Preserves original UVs, group names, scale markers and connection seams.
Produces ordinary .3dsN components, not a new fixed skeleton or body.
"""
import bpy,bmesh,struct,json,hashlib,sys,re,math
from mathutils.kdtree import KDTree
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from component import Chunk,chunks,encode,objects,validate_contract,boundary_indices
from human_shapes import author

ROOT=Path(__file__).resolve().parents[2]
OUTPUT=ROOT/'experiments'/'procedural-parts'
PACK=OUTPUT/'pack';PACK.mkdir(parents=True,exist_ok=True)
manifest={'version':1,'files':{}}
report=[]
skipped=[]
EXPORT_CURRENT='--export-current' in sys.argv
HUMAN_SET='--human-set' in sys.argv
current_component=''
current_source_hash=''

def sculpt_component(obj,protected):
    """Small anatomy-aware contour edits, faded to zero at every interface."""
    data=obj.data;bm=bmesh.new();bm.from_mesh(data)
    boundary={v for edge in bm.edges if edge.is_boundary for v in edge.verts}
    boundary|={v for v in bm.verts if tuple(round(x,6) for x in v.co) in protected}
    deform=bm.verts.layers.deform.verify()
    seam_groups={g.index for g in obj.vertex_groups if g.name in ['TopConnect','BottomConnect']}
    boundary|={v for v in bm.verts if any(v[deform].get(i,0)>0 for i in seam_groups)}
    minimum=[min(v.co[i] for v in bm.verts) for i in range(3)]
    size=[max(v.co[i] for v in bm.verts)-minimum[i] for i in range(3)]
    if min(size)<1e-8:bm.free();return
    tree=KDTree(len(boundary))
    for i,v in enumerate(boundary):tree.insert(v.co,i)
    if boundary:tree.balance()
    def bump(x,y,cx,cy,sx,sy):return math.exp(-((x-cx)/sx)**2-((y-cy)/sy)**2)
    name=current_component[1:]
    for v in bm.verts:
        if v in boundary:continue
        x=(v.co.x-minimum[0])/size[0]*2-1;y=(v.co.y-minimum[1])/size[1]*2-1
        front=max(0,min(1,((v.co.z-minimum[2])/size[2]-.42)*2))
        fade=min(1,tree.find(v.co)[2]/(max(size)*.12)) if boundary else 1
        delta=0
        if name.startswith('Skull'):
            delta=.035*(bump(x,y,-.42,-.05,.3,.4)+bump(x,y,.42,-.05,.3,.4))-.012*bump(x,y,0,-.55,.25,.22)
        elif name.startswith('Nose6'):
            delta=.07*bump(x,y,0,-.25,.45,.4)+.025*bump(x,y,0,.4,.2,.5)-.025*(bump(x,y,-.5,-.45,.2,.2)+bump(x,y,.5,-.45,.2,.2))
        elif name.startswith('Mouth2'):
            delta=.045*bump(x,y,0,-.2,.65,.25)+.03*(bump(x,y,-.2,.2,.3,.2)+bump(x,y,.2,.2,.3,.2))
        elif name.startswith('Ear1'):delta=.025*bump(x,y,0,.2,.65,.65)
        elif name.startswith(('ArmTo','LegTo','NoLimb','TopLimb')):
            delta=.025*(bump(x,y,-.35,.4,.35,.35)+bump(x,y,.35,.4,.35,.35))-.01*bump(x,y,0,.2,.15,.5)
        elif name.startswith('Leg'):delta=.018*bump(x,y,0,.4,.7,.5)
        elif name.startswith('HeadCrest01'):delta=.012*math.sin(x*65+y*7)
        v.co.z+=size[2]*delta*fade*front
    bm.normal_update();bm.to_mesh(data);bm.free();data.update()

def update(mesh):
    locked=set()
    if EXPORT_CURRENT:
        candidates=[o for o in bpy.data.objects if o.get('component_file')==current_component and o.get('component_object')==mesh.name]
        if len(candidates)!=1:raise ValueError('Expected one editable component: '+current_component)
        obj=candidates[0]
        if obj.get('source_sha256')!=current_source_hash:raise ValueError('Source component revision changed')
        if set(g.name for g in obj.vertex_groups)!=set(g['name'] for g in mesh.groups):raise ValueError('Procedural vertex groups changed')
        data=obj.data;data.calc_loop_triangles()
    else:
        data=bpy.data.meshes.new(mesh.name)
        data.from_pydata(mesh.vertices,[],[f[:3] for f in mesh.faces]);data.update()
        for slot,chunk in enumerate(c for c in mesh.face_chunks if c.tag==0x4130):
            end=chunk.data.index(b'\0');label=chunk.data[:end].decode('ascii')
            data.materials.append(bpy.data.materials.get(label) or bpy.data.materials.new(label))
            count=struct.unpack_from('<H',chunk.data,end+1)[0]
            for index in struct.unpack_from('<'+'H'*count,chunk.data,end+3):data.polygons[index].material_index=slot
        obj=bpy.data.objects.new(mesh.name,data);bpy.context.collection.objects.link(obj)
        bpy.context.view_layer.objects.active=obj;obj.select_set(True)
        layer=data.uv_layers.new(name='LegacyPaletteUV')
        legacy_normals=data.attributes.new('LegacyNormal','FLOAT_VECTOR','CORNER')
        seam_weight=data.attributes.new('SeamWeight','FLOAT','POINT')
        protected_indices=boundary_indices(mesh)
        for i,item in enumerate(seam_weight.data):item.value=1. if i in protected_indices else 0.
        for polygon,face in zip(data.polygons,mesh.faces):
            for corner,(loop,index) in enumerate(zip(polygon.loop_indices,face[:3])):
                data.uv_layers['LegacyPaletteUV'].data[loop].uv=mesh.uv[index]
                data.attributes['LegacyNormal'].data[loop].vector=face[4+corner*3:7+corner*3]
            polygon.use_smooth=True
        for group in mesh.groups:
            vg=obj.vertex_groups.new(name=group['name'])
            rows=group['rows'][:-1] if group['marker'] else group['rows']
            weights={tuple(row[:3]):row[3] for row in rows}
            for index,point in enumerate(mesh.vertices):
                if tuple(point) in weights:vg.add([index],weights[tuple(point)],'REPLACE')
        if HUMAN_SET and current_component.startswith('2'):
            protected={tuple(round(x,6) for x in mesh.vertices[i]) for i in boundary_indices(mesh)}
            author(data,obj,current_component,protected)
        else:
            bm=bmesh.new();bm.from_mesh(data)
            # UV islands may duplicate positions. Weld topology while retaining
            # per-corner UVs so shading is continuous across those internal splits.
            bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.0)
            protected={tuple(round(x,6) for x in mesh.vertices[i]) for i in boundary_indices(mesh)}
            locked=set()
            locked|={v for v in bm.verts if tuple(round(x,6) for x in v.co) in protected}
            for edge in bm.edges:
                if edge.is_boundary:locked.update(edge.verts)
            # Connection groups remain exact, including vertices introduced along seams.
            deform=bm.verts.layers.deform.verify()
            seam_groups={g.index for g in obj.vertex_groups if g.name in ['TopConnect','BottomConnect']}
            for v in bm.verts:
                if any(v[deform].get(i,0)>0 for i in seam_groups):locked.add(v)
            # Keep the source cage and its full procedural groups in the editable file.
            for _ in range(4):
                proposals={}
                for v in bm.verts:
                    if v in locked or not v.link_edges:continue
                    average=sum((edge.other_vert(v).co for edge in v.link_edges),v.co*0)/len(v.link_edges)
                    proposals[v]=v.co.lerp(average,.18)
                for v,point in proposals.items():v.co=point
            bm.to_mesh(data);bm.free();data.update()
            # Simple edge subdivision interpolates UVs and all source deform groups.
            modifier=obj.modifiers.new('Editable component detail','SUBSURF');modifier.subdivision_type='SIMPLE';modifier.levels=1;modifier.uv_smooth='NONE'
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            # Midpoints of protected edges must also stay on the original seam.
            for face in mesh.faces:
                for a,b in [(face[0],face[1]),(face[1],face[2]),(face[2],face[0])]:
                    if tuple(round(x,6) for x in mesh.vertices[a]) in protected and tuple(round(x,6) for x in mesh.vertices[b]) in protected:
                        protected.add(tuple(round((mesh.vertices[a][j]+mesh.vertices[b][j])*.5,6) for j in range(3)))
            sculpt_component(obj,protected)
        data=obj.data;data.calc_loop_triangles()
        obj["component_file"]=current_component
        obj["component_object"]=mesh.name
        obj["source_sha256"]=current_source_hash
    points=[];uvs=[];faces=[];group_rows={g['name']:{} for g in mesh.groups};vertex_indices={}
    material_faces={}
    for tri in data.loop_triangles:
        ids=[];normals=[]
        for loop,index in zip(tri.loops,tri.vertices):
            vertex=data.vertices[index]
            point=struct.unpack('<3f',struct.pack('<3f',*vertex.co))
            uv=tuple(data.uv_layers[0].data[loop].uv)
            weights=tuple((g.group,g.weight) for g in vertex.groups if g.weight>1e-8)
            key=(point,uv,weights)
            if key not in vertex_indices:
                vertex_indices[key]=len(points);points.append(point);uvs.append(uv)
            ids.append(vertex_indices[key])
            seam=data.attributes['SeamWeight'].data[index].value
            authored=data.attributes['LegacyNormal'].data[loop].vector
            normal=vertex.normal.copy() if HUMAN_SET and current_component.startswith('2') else authored.copy()
            if normal.dot(authored)<0:normal.negate()
            if normal.length:normal.normalize()
            normals.extend(normal)
            for membership in vertex.groups:
                name=obj.vertex_groups[membership.group].name
                if membership.weight>1e-8:group_rows[name][point]=membership.weight
        if len(points)>=65535:raise ValueError('Component exceeds 16-bit vertex limit')
        material_faces.setdefault(tri.material_index,[]).append(len(faces))
        faces.append(struct.pack('<4H9f',*ids,7,*normals))
    mesh.replace(0x4110,struct.pack('<H',len(points))+b''.join(struct.pack('<3f',*p) for p in points))
    mesh.replace(0x4140,struct.pack('<H',len(uvs))+b''.join(struct.pack('<2f',*uv) for uv in uvs))
    # Carry each original material assignment through subdivision.
    materials=[c for c in mesh.face_chunks if c.tag==0x4130]
    extra=[]
    for slot,material in enumerate(materials):
        name=material.data.split(b'\0',1)[0]+b'\0';indices=material_faces.get(slot,[])
        extra.append(Chunk(0x4130,name+struct.pack('<H',len(indices))+struct.pack('<'+'H'*len(indices),*indices)))
    mesh.replace(0x4120,struct.pack('<H',len(faces))+b''.join(faces)+encode(extra))
    for group in mesh.groups:
        # Pure marker records stay byte-for-byte identical.
        if len(group['rows'])==1:continue
        rows=[(*point,weight) for point,weight in group_rows[group['name']].items()]
        if group['marker']:rows.append(group['marker'])
        children=chunks(group['node'].data)
        for i,child in enumerate(children):
            if child.tag==0x4114:children[i]=Chunk(child.tag,struct.pack('<H',len(rows))+b''.join(struct.pack('<4f',*r) for r in rows))
        group['node'].data=encode(children)
    report.append({'object':mesh.name,'oldTriangles':len(mesh.faces),'newTriangles':len(faces),'protectedBoundaryVertices':len(boundary_indices(mesh))})

if not EXPORT_CURRENT:
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
sources=sorted((ROOT/'resources'/'bodies'/'shards').glob('*/data/*.3dsN'))
pattern=re.compile(r'^[0-7](Skull|Nose[26][1-4]|Mouth[26][1-4]|Eye2[1-4]|Hand(Left|Right)[03]|Foot(Left|Right)[02]|Ear1[1-4]|HeadCrest01|Neck[1-4][1-2]|Arm(Left|Right)0|Leg(Left|Right)[01]|(ArmTo|LegTo|LegClosedTo|NoLimb|TopLimb).*)\.3dsN$')
for source_path in sources:
    name=source_path.name
    if pattern.match(name) and (not re.match(r'^[0-7](Skull|Nose|Mouth|Eye)',name) or HUMAN_SET and name.startswith('2')):
        original=source_path.read_bytes()
        try:before,_=objects(original)
        except (ValueError,StopIteration,struct.error) as error:
            skipped.append({'file':name,'reason':str(error) or 'unsupported layout'});continue
        current_component=name;current_source_hash=hashlib.sha256(original).hexdigest()
        existing=set(bpy.data.objects);report_start=len(report)
        try:
            _,rewritten=objects(original,update);after,_=objects(rewritten)
            validate_contract(before,after)
            if sum(len(m.faces) for m in after)<=sum(len(m.faces) for m in before):
                raise ValueError('No editable interior detail; retain original component')
        except (ValueError,StopIteration,struct.error) as error:
            skipped.append({'file':name,'reason':str(error) or 'unsupported layout'})
            del report[report_start:]
            for obj in set(bpy.data.objects)-existing:bpy.data.objects.remove(obj,do_unlink=True)
            print('Original retained:',name,str(error),flush=True)
            continue
        (PACK/name).write_bytes(rewritten)
        manifest['files'][name]={'sourceSha256':hashlib.sha256(original).hexdigest(),'assetSha256':hashlib.sha256(rewritten).hexdigest()}
        print('Compatible component:',name,flush=True)
(PACK/'manifest.json').write_text(json.dumps(manifest,indent=2))
manifest['materials']={str(p.relative_to(PACK)).replace('\\','/'):{'assetSha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in (PACK/'materials').glob('*.png')}
(PACK/'manifest.json').write_text(json.dumps(manifest,indent=2))
(OUTPUT/'report.json').write_text(json.dumps(report,indent=2))
(OUTPUT/'unsupported-components.json').write_text(json.dumps(skipped,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT/'limb-components.blend'))
print('PASS: procedural markers and existing connection seams preserved in',len(manifest['files']),'limb assets')
