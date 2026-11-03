"""Check component round trips and rejection of edited procedural anchors."""
import copy,unittest,json
from pathlib import Path
from component import objects,validate_contract,boundary_indices

ROOT=Path(__file__).resolve().parents[2]

class ComponentContract(unittest.TestCase):
    def test_pack_contracts_and_lossless_reader(self):
        pack=ROOT/'experiments/procedural-parts/pack'
        names=json.loads((pack/'manifest.json').read_text())['files']
        for replacement in [pack/name for name in sorted(names)]:
            source=next((ROOT/'resources/bodies/shards').glob('*/data/'+replacement.name))
            original,roundtrip=objects(source.read_bytes())
            self.assertEqual(roundtrip,source.read_bytes())
            edited,_=objects(replacement.read_bytes())
            validate_contract(original,edited)
            if replacement.name.startswith('2') and any(part in replacement.name for part in ('Hand','Foot')):
                for before,after in zip(original,edited):
                    for axis in range(3):
                        self.assertEqual(min(v[axis] for v in before.vertices),min(v[axis] for v in after.vertices))
                        self.assertEqual(max(v[axis] for v in before.vertices),max(v[axis] for v in after.vertices))
            self.assertGreater(sum(len(m.faces) for m in edited),sum(len(m.faces) for m in original))

    def test_reject_moved_marker(self):
        path=ROOT/'resources/bodies/shards/00/data/0ArmLeft0.3dsN'
        original,_=objects(path.read_bytes());edited=copy.deepcopy(original)
        group=next(g for g in edited[0].groups if g['marker'])
        marker=list(group['marker']);marker[0]+=.1;group['marker']=tuple(marker)
        with self.assertRaisesRegex(ValueError,'marker changed'):validate_contract(original,edited)

    def test_reject_moved_connection_seam(self):
        path=ROOT/'resources/bodies/shards/02/data/2Skull.3dsN'
        original,_=objects(path.read_bytes());edited=copy.deepcopy(original)
        index=next(iter(boundary_indices(original[0])));point=original[0].vertices[index]
        edited[0].vertices=[(p[0]+.1,p[1],p[2]) if p==point else p for p in edited[0].vertices]
        with self.assertRaisesRegex(ValueError,'Boundary seam'):validate_contract(original,edited)

    def test_uv_duplicates_are_not_anatomical_openings(self):
        path=ROOT/'resources/bodies/shards/02/data/2ArmRight0.3dsN'
        original,_=objects(path.read_bytes())
        self.assertEqual(boundary_indices(original[0]),set())

if __name__=='__main__':unittest.main()
