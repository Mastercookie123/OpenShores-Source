"""Run real production renderer fixtures and publish an inspectable local report.

On WSL, Windows executables run through native Windows interop, not Wine/Mesa.
No server/login/database needed. Image differences measure change, not quality.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import shutil
from PIL import Image, ImageChops, ImageStat

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace',type=Path,default=ROOT.parent)
parser.add_argument('--backend',choices=['windows','linux'],default='windows')
parser.add_argument('--output',type=Path)
parser.add_argument('--pack',type=Path,help='Candidate pack to test without installing it over the live client')
parser.add_argument('--only',help='Comma-separated fixture IDs for a quick check')
parser.add_argument('--self-check',action='store_true',help='Prove empty packs and bad checksums are rejected')
args=parser.parse_args()
workspace=args.workspace.resolve()
output=(args.output or workspace/'Artifacts'/'visual-tests'/'latest').resolve()
output.mkdir(parents=True,exist_ok=True)
cases=json.loads((Path(__file__).parent/'cases.json').read_text())['cases']
if args.only:
    requested=set(args.only.split(','))
    cases=[c for c in cases if c['id'] in requested]
    if {c['id'] for c in cases}!=requested:raise SystemExit('Unknown fixture ID')
exe=(workspace/'Client/RenderVisualTest.exe' if args.backend=='windows' else Path('/opt/hazeron/Source/build/linux-debug/bin/RenderProceduralAvatar'))
textures=workspace/'Client'
pack=(args.pack or workspace/'Client/body-parts-modular').resolve()
if not exe.is_file() or not (pack/'manifest.json').is_file():raise SystemExit('Build harness and install modular pack first')
manifest=json.loads((pack/'manifest.json').read_text())
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()
def native(path):
    if args.backend=='windows':return subprocess.check_output(['wslpath','-m',str(path)],text=True).strip()
    return str(path)
def pixels(path):return Image.open(path).convert('RGB')
def metrics(left,right):
    a,b=pixels(left),pixels(right)
    if a.size!=b.size:raise ValueError('Image sizes differ')
    difference=ImageChops.difference(a,b)
    red,green,blue=difference.split()
    largest=ImageChops.lighter(ImageChops.lighter(red,green),blue)
    changed=sum(largest.histogram()[3:])
    return {'meanAbsoluteError':sum(ImageStat.Stat(difference).mean)/3,
            'changedPercent':100*changed/(a.width*a.height)}
def capture(case,label,pack_path):
    folder=output/case['id']/label;folder.mkdir(parents=True,exist_ok=True)
    config=output/case['id']/'case.json';config.write_text(json.dumps(case,indent=2))
    command=[str(exe),native(pack_path) if pack_path else 'none',native(folder),native(textures),'--case',native(config)]
    started=time.monotonic()
    with (folder/'process.log').open('w') as log:
        result=subprocess.run(command,cwd=textures,stdout=log,stderr=subprocess.STDOUT,timeout=120)
    if result.returncode:raise RuntimeError(f'{label} exit {result.returncode}; see {folder}/process.log')
    report=json.loads((folder/'render.json').read_text())
    if report['glErrors'] or not report['bodyShaderLinked']:raise RuntimeError('Renderer/shader failure')
    if not report['shaderArchiveSha256']:raise RuntimeError('Missing embedded shader fingerprint')
    if args.backend=='windows' and any(s in report['renderer'].lower() for s in ('llvmpipe','softpipe','software','gdi generic')):
        raise RuntimeError('Windows suite unexpectedly used software OpenGL')
    if report['selfShadows']!=case.get('shadows',False):raise RuntimeError('Shadow mode mismatch')
    if any(a.get('status')=='rejected' for a in report['assets']):raise RuntimeError('Replacement rejected')
    if pack_path:
        if not report['assets']:raise RuntimeError('No replacements loaded: original assets were silently used')
        if not report['detailUniformPresent'] or not report['materials']:raise RuntimeError('Enhanced material shader not active')
        for event in report['assets']:
            entry=manifest['files'].get(event['component'])
            if not entry or entry['assetSha256']!=event['sha256']:raise RuntimeError('Loaded asset differs from installed manifest')
        if not all(m['detailTexture']>0 and m['detailUniform']>=0 for m in report['materials']):raise RuntimeError('Detail texture/uniform failed')
    elif report['assets'] or report['materials']:raise RuntimeError('Baseline accidentally loaded enhanced assets')
    for image in ('body.png','portrait.png'):
        im=pixels(folder/image)
        background=Image.new('RGB',im.size,im.getpixel((0,0)))
        if ImageChops.difference(im,background).getbbox() is None:raise RuntimeError('Blank framebuffer')
    if case.get('expectUnlit'):
        portrait=pixels(folder/'portrait.png')
        if max(portrait.getpixel((portrait.width//2,portrait.height*3//5)))>1:
            raise RuntimeError('Character emits light with all lights and ambient disabled')
    report['elapsedSeconds']=round(time.monotonic()-started,3)
    return report

summary={'version':1,'backend':args.backend,'executableSha256':digest(exe),
         'manifestSha256':digest(pack/'manifest.json'),'cases':[],
         'qualityVerdict':'Visual review required; image difference is not proof of improvement.',
         'scope':'Production character renderer; controlled fixtures, not a live-world/server replay. Frozen animation phase; no held tool mesh.'}
try:
    if args.self_check:
        controls={}
        for name,expected in [('empty-pack','No replacements loaded'),('bad-checksum','Replacement rejected')]:
            bad=output/'controls'/name;bad.mkdir(parents=True,exist_ok=True)
            bad_manifest={'version':1,'files':{}}
            if name=='bad-checksum':
                component='2Skull.3dsN';entry=dict(manifest['files'][component]);entry['assetSha256']='0'*64
                bad_manifest['files'][component]=entry;shutil.copy2(pack/component,bad/component)
            (bad/'manifest.json').write_text(json.dumps(bad_manifest))
            try:capture(cases[0],'control-'+name,bad)
            except RuntimeError as error:
                if expected not in str(error):raise
                controls[name]='correctly rejected'
            else:raise RuntimeError('Harness accepted invalid control: '+name)
        summary['negativeControls']=controls
    for case in cases:
        print('Testing '+case['id'],flush=True)
        item={'id':case['id'],'failures':[]}
        try:
            original=capture(case,'original',None)
            candidate=capture(case,'candidate',pack)
            anchor_delta=max(abs(a-b) for key in ('leftGrip','rightGrip','camera') for a,b in zip(original[key],candidate[key]))
            if anchor_delta>1e-6:item['failures'].append(f'Grip/camera moved by {anchor_delta}')
            if original['shaderArchiveSha256']!=candidate['shaderArchiveSha256']:item['failures'].append('A/B uses different shader binaries')
            differences={name:metrics(output/case['id']/'original'/f'{name}.png',output/case['id']/'candidate'/f'{name}.png') for name in ('body','portrait')}
            if case.get('minimumChestLift'):
                def chest_luminance(label):
                    im=pixels(output/case['id']/label/'portrait.png')
                    r,g,b=im.getpixel((im.width//2,im.height*3//5))
                    return .2126*r+.7152*g+.0722*b
                old_light,new_light=chest_luminance('original'),chest_luminance('candidate')
                item['backlitChestLuminance']={'original':old_light,'candidate':new_light}
                if new_light-old_light<case['minimumChestLift']:item['failures'].append('Backlit chest readability did not improve enough')
            if not case.get('expectUnlit') and max(d['changedPercent'] for d in differences.values())<.01:item['failures'].append('Candidate appears identical to original')
            if case==cases[0] or case.get('pose') in ('walk','strike'):
                capture(case,'repeat',pack)
                for name in ('body','portrait'):
                    difference=metrics(output/case['id']/'candidate'/f'{name}.png',output/case['id']/'repeat'/f'{name}.png')
                    if difference['meanAbsoluteError']>.01:item['failures'].append('Fixture is not deterministic')
            item.update({'renderer':candidate['renderer'],'replacements':len(candidate['assets']),
                'originalTriangles':original['triangles'],'candidateTriangles':candidate['triangles'],
                'anchorDelta':anchor_delta,'differences':differences,'warnings':candidate['warnings']})
        except Exception as error:item['failures'].append(str(error))
        item['status']='failed' if item['failures'] else 'mechanical-pass'
        summary['cases'].append(item)
    failed=[c['id'] for c in summary['cases'] if c['failures']]
    if failed:summary['failure']='Failed cases: '+', '.join(failed)
except Exception as error:
    summary['failure']=str(error)
finally:
    (output/'summary.json').write_text(json.dumps(summary,indent=2))
    template=(Path(__file__).parent/'report.html').read_text()
    (output/'index.html').write_text(template.replace('/*REPORT_DATA*/',json.dumps(summary).replace('<','\\u003c')))
print(json.dumps({'cases':len(summary['cases']),'failure':summary.get('failure'),'report':str(output/'index.html')},indent=2))
sys.exit(1 if 'failure' in summary else 0)
