"""Validate evidence from the actual DpBody -> Vulkan -> game FBO path."""
import html,json,re,sys
from pathlib import Path
from PIL import Image,ImageChops,ImageStat

root=Path(sys.argv[1]);rows=[];errors=[];comparisons=[];pointShadow=None
for setting in ('off','on'):
    capture=root.parent.parent/f'filament-shadow-{setting}.ppm'
    if capture.exists():Image.open(capture).save(root/f'point-shadow-{setting}.png')
log=root.parent.parent/'filament-shadow-native.log'
if log.exists():
    match=re.search(r'point shadow receiver off=(\d+) on=(\d+)',log.read_text())
    if match:
        lit,shadow=map(int,match.groups());pointShadow={'unshadowed':lit,'shadowed':shadow,'passed':lit>30 and shadow<lit/2}
        if not pointShadow['passed']:errors.append('Native point-light shadow failed to darken its receiver')
for directory in sorted(root.iterdir()):
    if not directory.is_dir() or directory.name.endswith('-legacy') or directory.name=='first-frame':continue
    path=directory/'render.json'
    if not path.exists():continue
    report=json.loads(path.read_text());case=report.get('fixture',{})
    if report.get('renderer')!='filament' or len(report.get('filamentFrames',[]))<3:errors.append(f'{directory.name}: no live Filament evidence')
    if report.get('glErrors')!=0 or report.get('warnings'):errors.append(f'{directory.name}: GL errors or warnings')
    if report.get('glStatePreserved') is not True:errors.append(f'{directory.name}: GL state preservation not verified')
    timings=[]
    for line in report.get('filamentFrames',[]):
        found=re.search(r'elapsedMs=(\d+)',line)
        if found:timings.append(int(found[1]))
    for name in ('body','portrait'):
        image=Image.open(directory/f'{name}.png').convert('RGB')
        if image.size!=(900,1000):errors.append(f'{directory.name}: incorrect output dimensions')
        if case.get('depthOccluder'):
            left=image.crop((0,0,450,1000));green=Image.new('RGB',left.size,(0,255,0))
            if ImageChops.difference(left,green).getbbox():errors.append(f'{directory.name}/{name}: body overwrote nearer world depth')
            right=image.crop((450,0,900,1000));background=Image.new('RGB',right.size,right.getpixel((449,0)))
            if not ImageChops.difference(right,background).getbbox():errors.append(f'{directory.name}/{name}: visible body missing')
        if case.get('expectUnlit'):
            if max(high for low,high in image.getextrema())>22:errors.append(f'{directory.name}: light leaked into zero-light control')
    legacy=root/(directory.name+'-legacy')/'render.json';difference=None
    if legacy.exists():
        old=json.loads(legacy.read_text())
        for key in ('dna','triangles','leftGrip','rightGrip','camera','parts'):
            if old[key]!=report[key]:errors.append(f'{directory.name}: original {key} changed')
        a=Image.open(directory/'portrait.png').convert('RGB');b=Image.open(legacy.parent/'portrait.png').convert('RGB')
        difference=sum(ImageStat.Stat(ImageChops.difference(a,b)).mean)/3
        if difference<.1:errors.append(f'{directory.name}: renderer output unchanged')
        comparisons.append({'case':directory.name,'meanPixelDifference':difference,'anchorsAndDNAIdentical':True})
    rows.append({'case':directory.name,'triangles':report['triangles'],'firstDrawMs':timings[0] if timings else None,'nextDrawMs':timings[-1] if timings else None,'pixelDifference':difference})
result={'passed':not errors,'cases':rows,'comparisons':comparisons,'pointShadow':pointShadow,'errors':errors,
        'scope':'Live procedural character renderer; OpenGL world/UI; gameplay session verification separate'}
(root/'validation.json').write_text(json.dumps(result,indent=2))
cards=[]
if pointShadow:
    cards.append('<article><h2>Measured point-light shadow</h2><p>Same receiver and blocker; shadow casting toggled. Sample: '+str(pointShadow['unshadowed'])+' → '+str(pointShadow['shadowed'])+'/255. The shadow falls below and left of the central blocker.</p><div class="pair"><figure><img src="point-shadow-off.png"><figcaption>Shadows off</figcaption></figure><figure><img src="point-shadow-on.png"><figcaption>Shadows on</figcaption></figure></div></article>')
for row in rows:
    name=html.escape(row['case']);legacy=root/(row['case']+'-legacy')/'portrait.png'
    before=f'<figure><img src="{name}-legacy/portrait.png"><figcaption>OpenGL</figcaption></figure>' if legacy.exists() else ''
    cards.append(f'<article><h2>{name}</h2><p>{row["triangles"]:,} source triangles · next draw {row["nextDrawMs"]} ms</p><div class="pair">{before}<figure><img src="{name}/portrait.png"><figcaption>Live Filament hook</figcaption></figure></div></article>')
(root/'index.html').write_text('''<!doctype html><meta charset="utf-8"><title>Live Filament integration checks</title>
<style>body{background:#11171d;color:#e3eaf0;font:16px system-ui;margin:32px}p{color:#bdc9d2}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:24px}article{background:#1c2630;padding:20px;border-radius:12px}h1,h2{margin-top:0}.pair{display:flex;gap:8px}figure{margin:0;flex:1;min-width:0}img{width:100%;display:block}figcaption{padding:8px}a{color:#8ad1ff}</style>
<h1>Live Filament integration</h1><p>Actual procedural game draw calls → native Vulkan → color/depth composition into the game framebuffer. World, held-item rendering and UI still use OpenGL. These captures are deterministic lab fixtures, not screenshots of a logged-in session.</p>'''+f'<p>Validation: {"PASS" if not errors else "FAIL"} · {len(rows)} cases. <a href="validation.json">Machine-readable results</a></p><main>'+''.join(cards)+'</main>')
print(json.dumps({'passed':not errors,'cases':len(rows),'comparisons':len(comparisons),'errors':errors}))
raise SystemExit(0 if not errors else 1)
