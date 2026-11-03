"""Lossless container reader for the game's custom .3dsN body-part format.

Unknown chunks are retained. Normals are stored in each 44-byte face record.
This deliberately rejects unsupported layouts rather than guessing at them.
"""
import struct
from dataclasses import dataclass

@dataclass
class Chunk:
    tag:int
    data:bytes

def chunks(data):
    result=[];offset=0
    while offset<len(data):
        if offset+6>len(data):raise ValueError('Truncated chunk header')
        tag,size=struct.unpack_from('<HI',data,offset)
        if size<6 or offset+size>len(data):raise ValueError('Invalid chunk size')
        result.append(Chunk(tag,data[offset+6:offset+size]));offset+=size
    return result

def encode(nodes):
    return b''.join(struct.pack('<HI',n.tag,len(n.data)+6)+n.data for n in nodes)

def cstring(data):
    end=data.index(b'\0');return data[:end].decode('ascii'),data[end+1:]

def objects(data,transform=None):
    result=[]
    def visit(nodes):
        output=[]
        for node in nodes:
            if node.tag in [0x4d4d,0x3d3d]:node=Chunk(node.tag,encode(visit(chunks(node.data))))
            elif node.tag==0x4000:
                name,tail=cstring(node.data);children=chunks(tail)
                for i,child in enumerate(children):
                    if child.tag==0x4100:
                        mesh=Mesh(name,chunks(child.data));result.append(mesh)
                        if transform:transform(mesh)
                        children[i]=Chunk(child.tag,encode(mesh.nodes))
                node=Chunk(node.tag,name.encode()+b'\0'+encode(children))
            output.append(node)
        return output
    rewritten=encode(visit(chunks(data)))
    return result,rewritten

class Mesh:
    def __init__(self,name,nodes):
        self.name=name;self.nodes=nodes
        node=self.one(0x4110);count=struct.unpack_from('<H',node.data)[0]
        if len(node.data)!=2+count*12:raise ValueError('Unsupported vertex layout')
        self.vertices=[struct.unpack_from('<3f',node.data,2+i*12) for i in range(count)]
        node=self.one(0x4120);count=struct.unpack_from('<H',node.data)[0]
        end=2+count*44
        if end>len(node.data):raise ValueError('Unsupported normal layout')
        self.faces=[struct.unpack_from('<4H9f',node.data,2+i*44) for i in range(count)]
        self.face_chunks=chunks(node.data[end:])
        uv=self.one(0x4140);count=struct.unpack_from('<H',uv.data)[0]
        self.uv=[struct.unpack_from('<2f',uv.data,2+i*8) for i in range(count)]
        if len(self.uv)!=len(self.vertices):raise ValueError('UV count mismatch')
        self.groups=[]
        for node in nodes:
            if node.tag!=0x4112:continue
            children=chunks(node.data);name=cstring(next(n for n in children if n.tag==0x4113).data)[0]
            values=next(n for n in children if n.tag==0x4114).data
            count=struct.unpack_from('<H',values)[0]
            rows=[struct.unpack_from('<4f',values,2+i*16) for i in range(count)]
            # The loader treats the final row as a scale marker for these groups.
            marker=len(rows)==1 or any(s in name for s in ['ScalePt','ConnectPt','MidPt','HandWingPt','EyePosition','Torso','Hand','Foot','Neck','Tail','Head'])
            self.groups.append({'name':name,'rows':rows,'marker':rows[-1] if marker and rows else None,'node':node})
    def one(self,tag):return next(n for n in self.nodes if n.tag==tag)
    def replace(self,tag,data):
        index=next(i for i,n in enumerate(self.nodes) if n.tag==tag);self.nodes[index]=Chunk(tag,data)

def boundary_indices(mesh):
    counts={}
    for face in mesh.faces:
        for a,b in [(face[0],face[1]),(face[1],face[2]),(face[2],face[0])]:
            edge=tuple(sorted((mesh.vertices[a],mesh.vertices[b])))
            counts[edge]=counts.get(edge,0)+1
    boundary={p for edge,count in counts.items() if count==1 for p in edge}
    # Texture islands duplicate indices, but do not create anatomical openings.
    return {i for i,p in enumerate(mesh.vertices) if p in boundary}

def validate_contract(original,edited):
    """Reject changed component identities, anchors, or existing seam positions."""
    if len(original)!=len(edited):raise ValueError('Object count changed')
    for before,after in zip(original,edited):
        if before.name!=after.name:raise ValueError('Object identity changed')
        names=lambda m:[c.data.split(b'\0',1)[0] for c in m.face_chunks if c.tag==0x4130]
        if names(before)!=names(after):raise ValueError('Material identity changed')
        boundary={(before.vertices[i],before.uv[i]) for i in boundary_indices(before)}
        # Blender welds almost coincident UV coordinates at internal splits.
        # Keep positions exact; permit <0.21 texel at the 2048px palette size.
        exported={}
        for point,uv in zip(after.vertices,after.uv):exported.setdefault(point,[]).append(uv)
        missing=[(p,uv) for p,uv in boundary if not any(max(abs(t[j]-uv[j]) for j in range(2))<=1e-4 for t in exported.get(p,[]))]
        if missing:raise ValueError('Boundary seam coordinates or UVs changed: '+repr(missing[:1]))
        if [g['name'] for g in before.groups]!=[g['name'] for g in after.groups]:raise ValueError('Procedural groups changed')
        for a,b in zip(before.groups,after.groups):
            if a['marker']!=b['marker']:raise ValueError('Scale/attachment marker changed: '+a['name'])
            if a['name'] in ['TopConnect','BottomConnect']:
                if not set(a['rows']).issubset(set(b['rows'])):raise ValueError('Seam coordinates changed')
