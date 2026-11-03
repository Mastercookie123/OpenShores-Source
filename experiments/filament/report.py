"""Verify native Filament captures and write the renderer prototype gallery."""
import argparse,html,json,re
from pathlib import Path
from PIL import Image,ImageChops
from validate_bridge import validate

parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);args=parser.parse_args()
root=args.output.resolve();results=[];cards=[]
for name in ('human','multi','quadruped'):
    folder=root/name;proof=validate(folder);images=[];device=''
    for frame in (240,270):
        log=(folder/f'filament-{frame}.log').read_text()
        found=re.search("Selected physical device '([^']+)'",log)
        if not found or 'NVIDIA' not in found.group(1):raise RuntimeError('Native NVIDIA Vulkan device not confirmed')
        device=found.group(1)
        im=Image.open(folder/f'filament-{frame}.tif').convert('RGB');im.save(folder/f'filament-{frame}.png');images.append(im)
        if im.size!=(1000,1000):raise RuntimeError('Unexpected 3D viewport size')
    if ImageChops.difference(*images).getbbox() is None:raise RuntimeError('Animation images did not change')
    entry={'variant':name,'device':device,'backend':'native Windows Vulkan','captureSize':images[0].size,
           'imagesDiffer':True,**proof};results.append(entry)
    cards.append(f'''<article><h2>{name}</h2><img id="{name}" src="{name}/filament-240.png">
<button onclick="document.getElementById('{name}').src='{name}/filament-240.png'">Frame 240</button>
<button onclick="document.getElementById('{name}').src='{name}/filament-270.png'">Frame 270</button>
<p>{proof['triangles']:,} actual game triangles · 61 bones</p>
<a href="{name}/bridge-report.json">Bridge metadata</a> · <a href="{name}/validation.json">Buffer checks</a> ·
<a href="{name}/filament-240.log">Native GPU log</a></article>''')
summary={'renderer':'Filament v1.79.0','status':'Working procedural-character renderer prototype',
         'gameIntegration':'Not yet a playable game renderer; official SDK viewer hosts the new backend.',
         'results':results,'limitations':['No live world, login, terrain, Qt UI composition or held tools',
            'Skin only; ordinary clothing and dual-palette materials require further integration',
            'Sampled animation loops; no live server/entity updates',
            'Different lighting from the legacy client; not a matched renderer performance comparison']}
(root/'validation-report.json').write_text(json.dumps(summary,indent=2))
page='''<!doctype html><html lang="en"><meta charset="utf-8"><title>OpenShores Filament renderer prototype</title>
<style>body{font:16px system-ui;background:#111820;color:#e5ebf4;margin:28px}h1{margin-bottom:8px}p{max-width:1050px;line-height:1.5}a{color:#9acbff}.gallery{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}article{background:#1b2633;padding:16px;border-radius:12px}img{width:100%;display:block}button{padding:8px;margin:8px 6px 0 0;font:inherit}pre{white-space:pre-wrap}</style>
<h1>Filament / Vulkan — real procedural game characters</h1>
<p>Native RTX 5080 rendering using the game's generated meshes and original 61 bone transforms.
Physically based materials, environment lighting, shadows and tone mapping are supplied by Filament.</p>
<p><strong>Development preview, not the playable client.</strong> The SDK viewer hosts this backend.
The floor is synthetic and lighting differs from the live world. Original character artwork remains.</p>
<p>Open <strong>Try-Filament.cmd</strong> in the workspace for the interactive human preview.
Other anatomies: <code>Filament-Preview.ps1 -Variant multi</code> or <code>-Variant quadruped</code>.</p>
<div class="gallery">'''+''.join(cards)+'''</div><h2>Verification and limits</h2><pre>'''+html.escape(json.dumps(summary,indent=2))+'''</pre>
<p><a href="validation-report.json">Machine-readable results</a></p></html>'''
(root/'index.html').write_text(page)
print(json.dumps(summary,indent=2))
