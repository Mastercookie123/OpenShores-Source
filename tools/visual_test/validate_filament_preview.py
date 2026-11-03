"""Compare production-renderer reference fixture with the original SDK preview."""
import argparse,json
from pathlib import Path
from PIL import Image, ImageChops, ImageFilter, ImageStat
p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('actual',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
ref=Image.open(a.reference).convert('RGB');actual=Image.open(a.actual).convert('RGB')
assert ref.size==actual.size==(1000,1000),'Unexpected comparison dimensions'
def skin(im):
    out=Image.new('L',im.size);out.putdata([255 if r>65 and r>g*1.12 and g>b*1.08 else 0 for r,g,b in im.getdata()]);return out
rm,am=skin(ref),skin(actual);intersection=ImageChops.darker(rm,am);union=ImageChops.lighter(rm,am)
count=lambda m:sum(m.histogram()[1:])
mask=intersection.filter(ImageFilter.MinFilter(5));mae=sum(ImageStat.Stat(ImageChops.difference(ref,actual),mask).mean)/3
result={'reference':str(a.reference),'actual':str(a.actual),'referenceSkinPixels':count(rm),'actualSkinPixels':count(am),'skinSilhouetteIoU':count(intersection)/max(1,count(union)),'skinMeanAbsoluteRgbError255':mae}
result['passed']=result['actualSkinPixels']>50000 and result['skinSilhouetteIoU']>.95 and mae<12
if a.output:a.output.write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
