#!/usr/bin/env python3
"""Build an isolated Filament test scene; does not modify the game or its assets."""
import argparse
import base64
import copy
import json
import math
from pathlib import Path
import struct
import subprocess


def build(repo, output):
    output.mkdir(parents=True, exist_ok=True)
    imported = output / 'asteroid.gltf'
    subprocess.run(['assimp', 'export', str(repo / 'resources/models/shards/03/data/Asteroid1.3ds'),
                    str(imported), '-fgltf2', '-ptv'], check=True)
    scene = json.loads(imported.read_text())
    # Make the exported scene portable and remove legacy material extensions.
    for buffer in scene['buffers']:
        buffer['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(
            (output / buffer['uri']).read_bytes()).decode()
    scene.pop('extensionsUsed', None)
    scene.pop('extensionsRequired', None)
    scene.pop('images', None)
    scene.pop('textures', None)
    scene.pop('samplers', None)
    scene['materials'] = []

    def material(name, color, roughness, metal=0, emission=None):
        item = {'name': name, 'pbrMetallicRoughness': {
            'baseColorFactor': [*color, 1], 'roughnessFactor': roughness, 'metallicFactor': metal}}
        if emission:
            item['emissiveFactor'] = emission
        scene['materials'].append(item)
        return len(scene['materials']) - 1

    rock = material('Original asteroid / rough stone', [.25, .20, .15], .92)
    ground = material('Ground / matte regolith', [.30, .26, .19], .95)
    hull = material('Hull / brushed alloy', [.48, .55, .61], .32, .75)
    paint = material('Hull / teal paint', [.035, .28, .32], .42, .1)
    dark = material('Interior / dark rubber', [.04, .055, .065], .8)
    light = material('Cabin / emissive panels', [.8, .68, .4], .5, emission=[1, .7, .3])
    for mesh in scene['meshes']:
        for primitive in mesh['primitives']:
            primitive['material'] = rock
    roots = scene['scenes'][scene.get('scene', 0)]['nodes']
    scene['nodes'].append({'name': 'Original game asteroid', 'children': roots,
                           'translation': [-6, 1.25, -1], 'scale': [1.3, 1.3, 1.3]})
    scene['scenes'] = [{'name': 'Renderer evaluation courtyard', 'nodes': [len(scene['nodes']) - 1]}]
    scene['scene'] = 0
    data = bytearray()
    buffer_index = len(scene['buffers'])

    def accessor(values, components, component_type=5126, target=None, bounds=False):
        while len(data) % 4:
            data.append(0)
        offset = len(data)
        flattened = [n for v in values for n in v] if components > 1 else values
        data.extend(struct.pack('<' + ('f' if component_type == 5126 else 'I') * len(flattened), *flattened))
        view = {'buffer': buffer_index, 'byteOffset': offset, 'byteLength': len(data) - offset}
        if target:
            view['target'] = target
        scene['bufferViews'].append(view)
        item = {'bufferView': len(scene['bufferViews']) - 1, 'componentType': component_type,
                'count': len(values), 'type': {1: 'SCALAR', 3: 'VEC3', 4: 'VEC4'}[components]}
        if bounds:
            item['min'] = [min(v[k] for v in values) for k in range(components)] if components > 1 else [min(values)]
            item['max'] = [max(v[k] for v in values) for k in range(components)] if components > 1 else [max(values)]
        scene['accessors'].append(item)
        return len(scene['accessors']) - 1

    positions, normals, indices = [], [], []
    # Each face has its own vertices so edges have flat, correct normals.
    faces = [([1, 0, 0], [[.5, -.5, -.5], [.5, .5, -.5], [.5, .5, .5], [.5, -.5, .5]]),
             ([-1, 0, 0], [[-.5, -.5, .5], [-.5, .5, .5], [-.5, .5, -.5], [-.5, -.5, -.5]]),
             ([0, 1, 0], [[-.5, .5, -.5], [-.5, .5, .5], [.5, .5, .5], [.5, .5, -.5]]),
             ([0, -1, 0], [[-.5, -.5, .5], [-.5, -.5, -.5], [.5, -.5, -.5], [.5, -.5, .5]]),
             ([0, 0, 1], [[.5, -.5, .5], [.5, .5, .5], [-.5, .5, .5], [-.5, -.5, .5]]),
             ([0, 0, -1], [[-.5, -.5, -.5], [-.5, .5, -.5], [.5, .5, -.5], [.5, -.5, -.5]])]
    for normal, corners in faces:
        start = len(positions)
        positions.extend(corners)
        normals.extend([normal] * 4)
        indices.extend([start, start + 1, start + 2, start, start + 2, start + 3])
    primitive = {'attributes': {'POSITION': accessor(positions, 3, target=34962, bounds=True),
                                'NORMAL': accessor(normals, 3, target=34962)},
                 'indices': accessor(indices, 1, 5125, 34963)}
    box_meshes = {}

    def node(item, parent=None):
        scene['nodes'].append(item)
        index = len(scene['nodes']) - 1
        if parent is None:
            scene['scenes'][0]['nodes'].append(index)
        else:
            scene['nodes'][parent].setdefault('children', []).append(index)
        return index

    def box(name, position, size, mat, parent=None):
        if mat not in box_meshes:
            p = copy.deepcopy(primitive)
            p['material'] = mat
            scene['meshes'].append({'name': 'Procedural box', 'primitives': [p]})
            box_meshes[mat] = len(scene['meshes']) - 1
        return node({'name': name, 'mesh': box_meshes[mat], 'translation': position, 'scale': size}, parent)

    box('Ground', [0, -.18, 0], [18, .3, 16], ground)
    box('Ship deck', [2, .15, 0], [6, .3, 8], hull)
    box('Deck inset', [2, .32, 0], [5.5, .04, 7.5], dark)
    box('Rear hull', [2, 1.7, -4], [6, 3.1, .18], paint)
    box('Port hull', [-1, 1.7, 0], [.18, 3.1, 8], paint)
    box('Front left bulkhead', [.1, 1.7, 4], [2.2, 3.1, .18], hull)
    box('Front right bulkhead', [3.9, 1.7, 4], [2.2, 3.1, .18], hull)
    box('Door lintel', [2, 3, 4], [1.6, .5, .2], paint)
    for z in [-3, 0, 3]:
        box('Exposed support', [4.9, 1.7, z], [.18, 3.1, .18], hull)
        box('Ceiling beam', [2, 3.25, z], [6, .18, .18], hull)
        box('Warm light panel', [-.86, 2.4, z], [.07, .18, .8], light)
    box('Console', [2, .85, -2.8], [1.8, 1.0, .8], hull)
    box('Console display', [2, 1.37, -2.8], [1.5, .05, .6], light)

    actor = node({'name': 'Procedural articulated test actor', 'translation': [-3, 0, 5.5]})
    box('Torso', [0, 1.3, 0], [.6, .9, .35], paint, actor)
    box('Head', [0, 2, 0], [.4, .4, .4], hull, actor)
    for x in [-.2, .2]:
        box('Leg', [x, .5, 0], [.23, 1, .25], dark, actor)
    joints = []
    for x in [-.48, .48]:
        joint = node({'name': 'Animated shoulder', 'translation': [x, 1.7, 0]}, actor)
        box('Arm', [0, -.4, 0], [.18, .85, .2], hull, joint)
        joints.append(joint)
    times = accessor([0, .5, 1, 1.5, 2], 1, bounds=True)
    animation = {'name': 'Shoulder articulation smoke test', 'samplers': [], 'channels': []}
    for side, joint in enumerate(joints):
        angles = [0, .65, 0, -.65, 0]
        rotations = [[math.sin(a / 2) * (1 if side else -1), 0, 0, math.cos(a / 2)] for a in angles]
        animation['samplers'].append({'input': times, 'output': accessor(rotations, 4), 'interpolation': 'LINEAR'})
        animation['channels'].append({'sampler': side, 'target': {'node': joint, 'path': 'rotation'}})
    scene['animations'] = [animation]
    scene['buffers'].append({'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()})
    scene['asset']['generator'] = 'Community fork Filament evaluation: original asteroid + synthetic deck/actor'
    result = output / 'courtyard.gltf'
    result.write_text(json.dumps(scene, separators=(',', ':')))
    print(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.repo, args.output)
