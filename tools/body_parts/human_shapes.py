"""Anatomical cage edits in each original component's authoring coordinates.

Keeps the procedural group graph, original connection rings and UV islands.
Limb Y is longitudinal; head Z is vertical and +Y is facial forward.
"""
import math
import bmesh
from mathutils.kdtree import KDTree


def gaussian(x, center, width):
    return math.exp(-((x-center)/width)**2)


def loft(t, knots):
    """Smooth radial silhouette specified by anatomical cross sections."""
    for (a,ra),(b,rb) in zip(knots,knots[1:]):
        if t<=b:
            u=max(0,min(1,(t-a)/(b-a)));u=u*u*(3-2*u)
            return ra+(rb-ra)*u
    return knots[-1][1]


def author(mesh, obj, filename, protected):
    bm=bmesh.new();bm.from_mesh(mesh)
    # Position-identical UV copies must move together during cage editing.
    # Their UVs remain separate per face corner after welding topology.
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.)
    deform=bm.verts.layers.deform.verify()
    groups={g.name:g.index for g in obj.vertex_groups}
    lock={v for v in bm.verts if tuple(round(x,6) for x in v.co) in protected}
    for v in bm.verts:
        if any(v[deform].get(groups[n],0)>0 for n in ('TopConnect','BottomConnect') if n in groups):lock.add(v)
    anchors={v:v.co.copy() for v in lock}
    # The material selector is binary in the legacy vertex ABI. Never insert
    # vertices on mixed-palette edges: that changes the original linear blend.
    bottom=groups.get('BottomSkin')
    def is_bottom(v):return bottom is not None and v[deform].get(bottom,0)>0
    palette_edges={e for e in bm.edges if is_bottom(e.verts[0])!=is_bottom(e.verts[1])}
    fixed_edges={e for e in bm.edges if e.is_boundary or all(v in lock for v in e.verts)}
    lo=[min(v.co[i] for v in bm.verts) for i in range(3)]
    size=[max(v.co[i] for v in bm.verts)-lo[i] for i in range(3)]
    if min(size)<1e-7:bm.free();return
    name=filename[1:]
    # Gameplay derives hand/foot measurements from mesh extents, not just
    # explicit markers. Preserve the extrema as part of that contract.
    measured=name.startswith(('Hand','Foot'))
    if measured:
        lock|={v for v in bm.verts if any(abs(v.co[i]-lo[i])<1e-7 or abs(v.co[i]-lo[i]-size[i])<1e-7 for i in range(3))}
        anchors={v:v.co.copy() for v in lock}
    # A volume-preserving pair removes angular cage ridges rather than merely
    # adding polygons to their existing planes.
    for factor in (.48,-.5,.48,-.5,.48,-.5):
        proposals={}
        for v in bm.verts:
            if v in lock or not v.link_edges:continue
            average=sum((e.other_vert(v).co for e in v.link_edges),v.co*0)/len(v.link_edges)
            proposals[v]=v.co.lerp(average,factor)
        for v,p in proposals.items():v.co=p
    tree=KDTree(len(lock))
    for i,v in enumerate(lock):tree.insert(v.co,i)
    if lock:tree.balance()
    for v in bm.verts:
        if v in lock:continue
        x=(v.co.x-lo[0])/size[0]*2-1
        y=(v.co.y-lo[1])/size[1]
        z=(v.co.z-lo[2])/size[2]*2-1
        fade=min(1,tree.find(v.co)[2]/(max(size)*.1)) if lock else 1
        if name.startswith('Arm') and 'To' not in name:
            # Author a continuous deltoid/biceps/elbow/forearm silhouette,
            # rather than preserving the source's stacked spherical volumes.
            radius=loft(y,[(0,.22),(.09,.83),(.20,.88),(.36,.70),(.52,.49),(.65,.61),(.82,.43),(1,.27)])
            cx=lo[0]+size[0]*.5;cz=lo[2]+size[2]*.5
            dx=(v.co.x-cx)/(size[0]*.5);dz=(v.co.z-cz)/(size[2]*.5)
            radial=math.hypot(dx,dz)
            if radial>.025:
                blend=.72*fade*min(1,radial/.25)
                target=radial+(radius-radial)*blend
                v.co.x=cx+dx/radial*target*size[0]*.5
                v.co.z=cz+dz/radial*target*size[2]*.5
        elif name.startswith('Leg') and 'To' not in name:
            radius=loft(y,[(0,.52),(.12,.89),(.28,.85),(.47,.53),(.55,.46),(.69,.62),(.83,.44),(1,.25)])
            cx=lo[0]+size[0]*.5;cz=lo[2]+size[2]*.5
            dx=(v.co.x-cx)/(size[0]*.5);dz=(v.co.z-cz)/(size[2]*.5)
            radial=math.hypot(dx,dz)
            if radial>.025:
                blend=.65*fade*min(1,radial/.25)
                target=radial+(radius-radial)*blend
                v.co.x=cx+dx/radial*target*size[0]*.5
                v.co.z=cz+dz/radial*target*size[2]*.5
        elif name.startswith('Skull'):
            height=(z+1)*.5;front=max(0,min(1,(y-.45)*3))
            # Cheek planes, tapered jaw, chin pad and brow. Facial openings
            # remain exact so independently selected eyes/nose/mouth still fit.
            jaw=1+.055*gaussian(height,.14,.16)
            v.co.x=(lo[0]+size[0]*.5)+(v.co.x-(lo[0]+size[0]*.5))*(1+(jaw-1)*fade)
            relief=.045*gaussian(abs(x),.48,.23)*gaussian(height,.42,.18)
            relief+=.025*gaussian(height,.69,.07)-.016*gaussian(abs(x),.8,.17)
            v.co.y+=size[1]*relief*front*fade
        elif name.startswith('Nose'):
            v.co.y+=size[1]*.12*gaussian(x,0,.42)*gaussian((z+1)*.5,.45,.22)*fade
        elif name.startswith('Mouth'):
            v.co.y+=size[1]*.12*gaussian(x,0,.6)*gaussian(z,0,.35)*fade
        elif name.startswith('Hand'):
            v.co.z+=size[2]*.06*gaussian(y,.3,.22)*gaussian(x,0,.65)*fade
        elif name.startswith('Foot'):
            # A continuous instep instead of flat wedge planes.
            v.co.z+=size[2]*.12*gaussian(y,.48,.19)*gaussian(x,0,.65)*fade
        elif name.startswith(('ArmTo','LegTo','NoLimb','TopLimb')):
            # Less barrel-shaped waist and shallow pectoral/sternum definition.
            v.co.x-=size[0]*.045*x*gaussian(y,.45,.27)*fade
            front=max(0,z)
            v.co.z+=size[2]*(.035*gaussian(abs(x),.36,.23)*gaussian(y,.72,.23)-.018*gaussian(x,0,.12))*front*fade
    bm.normal_update()
    original=set(bm.verts)
    # Two cuts give rounded PN-like edge samples while retaining source corners.
    eligible=[e for e in bm.edges if e not in palette_edges and e not in fixed_edges]
    result=bmesh.ops.subdivide_edges(bm,edges=eligible,cuts=2,use_grid_fill=True,smooth=.35)
    for v,position in anchors.items():
        if v.is_valid:v.co=position
    # Blender may recreate a corner during subdivision and round it by one ULP.
    # Snap only those near-identical connection samples back to source floats.
    anchor_tree=KDTree(len(anchors))
    for i,position in enumerate(anchors.values()):anchor_tree.insert(position,i)
    if anchors:
        anchor_tree.balance()
        for v in bm.verts:
            position,index,distance=anchor_tree.find(v.co)
            if distance<1e-5:v.co=position
    if measured:
        for v in bm.verts:
            for i in range(3):v.co[i]=max(lo[i],min(lo[i]+size[i],v.co[i]))
    # Split regions with different palette flags are not welded across the edge.
    # Interpolation on same-region edges preserves their categorical membership.
    bm.normal_update();bm.to_mesh(mesh);bm.free();mesh.update()
