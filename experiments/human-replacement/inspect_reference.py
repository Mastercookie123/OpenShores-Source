import json
from pathlib import Path
from mathutils import Vector,Matrix
p=Path('/mnt/c/Users/Mason/Documents/ChatGPT/Hazeron/Source/experiments/human-replacement')
d=json.loads((p/'legacy-reference.json').read_text()); mats=[]
for basis in d['bones']:
 o=Vector(basis[0]); cols=[Vector(x)-o for x in basis[1:]]
 m=Matrix.Identity(4)
 for j in range(3):
  for i in range(3):m[i][j]=cols[j][i]
 for i in range(3):m[i][3]=o[i]
 mats.append(m)
for part in d['parts']:
 pts=[]
 for row in part['vertices']:
  v=Vector((*row[:3],1)); q=Vector((0,0,0,0))
  for i in range(4):q+=mats[int(row[8+2*i])]@v*row[9+2*i]
  pts.append(q)
 print(part['name'],[(round(min(v[i] for v in pts),3),round(max(v[i] for v in pts),3)) for i in range(3)])
