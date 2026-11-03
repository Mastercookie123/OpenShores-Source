"""Render real assembled game geometry/UVs, comparing the same pose and light."""
import bpy,json
from pathlib import Path
from mathutils import Matrix,Vector
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'experiments/visual-overhaul';OUT.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32
scene.world.color=(.12,.12,.12)
scene.render.resolution_x=900;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
def aim(obj,point):obj.rotation_euler=(Vector(point)-obj.location).to_track_quat('-Z','Y').to_euler()
for location,power,size in [((4,6,7),1000,5),((-4,2,4),500,4),((0,-4,5),700,3)]:
    bpy.ops.object.light_add(type='AREA',location=location);light=bpy.context.object;light.data.energy=power;light.data.size=size;aim(light,(0,0,1))
bpy.ops.object.camera_add(location=(3.5,9,3));camera=bpy.context.object;aim(camera,(0,.05,1.5));camera.data.type='ORTHO';camera.data.ortho_scale=3.3;scene.camera=camera
for label,geometry,atlas in [
    ('baseline','baseline.json',ROOT/'resources/textures/runtime/Skin.png'),
    ('geometry','enhanced.json',ROOT/'resources/textures/runtime/Skin.png'),
    ('atlas-candidate','enhanced.json',OUT/'candidates/Skin-repaint-v1.png')]:
    reference=json.loads((OUT/geometry).read_text());matrices=[]
    for basis in reference['bones']:
        origin=Vector(basis[0]);matrix=Matrix.Identity(4)
        for col in range(3):
            delta=Vector(basis[col+1])-origin
            for row in range(3):matrix[row][col]=delta[row]
        for row in range(3):matrix[row][3]=origin[row]
        matrices.append(matrix)
    points=[];normals=[];uv=[]
    for part in reference['parts']:
        for row in part['vertices']:
            blend=sum((matrices[int(row[8+i*2])]*row[9+i*2] for i in range(4)),Matrix(((0,)*4,)*4))
            point=blend@Vector((*row[:3],1));normal=(blend.to_3x3()@Vector(row[3:6])).normalized()
            points.append(point[:3]);normals.append(normal);uv.append(row[6:8])
    mesh=bpy.data.meshes.new(label);mesh.from_pydata(points,[],[list(range(i,i+3)) for i in range(0,len(points),3)]);mesh.update()
    obj=bpy.data.objects.new(label,mesh);bpy.context.collection.objects.link(obj)
    layer=mesh.uv_layers.new()
    for poly in mesh.polygons:
        poly.use_smooth=True
        for index in poly.loop_indices:layer.data[index].uv=uv[mesh.loops[index].vertex_index]
    mesh.normals_split_custom_set(normals)
    material=bpy.data.materials.new(label);material.use_nodes=True
    nodes=material.node_tree.nodes;shader=nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=.65
    texture=nodes.new('ShaderNodeTexImage');texture.image=bpy.data.images.load(str(atlas))
    material.node_tree.links.new(texture.outputs['Color'],shader.inputs['Base Color'])
    # An unlit view checks UV placement independently of differences between
    # Blender and the game's clockwise-face lighting conventions.
    emission=nodes.new('ShaderNodeEmission');material.node_tree.links.new(texture.outputs['Color'],emission.inputs['Color'])
    material.node_tree.links.new(emission.outputs[0],nodes.get('Material Output').inputs['Surface']);mesh.materials.append(material)
    scene.render.filepath=str(OUT/(label+'-portrait.png'));bpy.ops.render.render(write_still=True)
    camera.data.ortho_scale=7;aim(camera,(0,0,0));scene.render.filepath=str(OUT/(label+'-body.png'));bpy.ops.render.render(write_still=True)
    camera.data.ortho_scale=3.3;aim(camera,(0,.05,1.5));bpy.data.objects.remove(obj,do_unlink=True)
