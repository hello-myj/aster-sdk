import hashlib,json,sys,tempfile,unittest,zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from development_kit import stage

class DevelopmentKit(unittest.TestCase):
    def fixture(self, root, members=None):
        (root/'contract').mkdir()
        (root/'contract/lock.json').write_text('{"abi":"3.7.0"}')
        out=root/'out';out.mkdir();(out/'release.json').write_text('{"abi":"3.7.0"}')
        bundles=root/'bundles';bundles.mkdir();archive=bundles/'kit.zip'
        with zipfile.ZipFile(archive,'w') as z:
            for name,data in (members or {'firmware/board.bin':b'firmware','simulator/aster-sim.exe':b'simulator'}).items():z.writestr(name,data)
        family={'schema':1,'family':'test','sdk_tag':'sdk-test','abi':'3.7.0','components':{'firmware':{'abi':'3.7.0','revision':'a'*40,'images':{'board':{'path':'firmware/board.bin','sha256':hashlib.sha256(b'firmware').hexdigest()}}},'simulator':{'abi':'3.7.0','revision':'b'*40}},'payloads':{'windows-x64':{'file':'kit.zip','format':'zip','sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}}}
        (root/'contract/release-family.json').write_text(json.dumps(family))
        return out,bundles,archive
    def test_pinned_payload_and_component_versions_are_carried(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);out,bundles,_=self.fixture(root);stage(root,out,'windows-x64',root/'cache',bundles)
            self.assertEqual((out/'firmware/board.bin').read_bytes(),b'firmware')
            info=json.loads((out/'release.json').read_text());self.assertEqual(info['development_kit']['components']['firmware']['revision'],'a'*40)
    def test_stale_payload_is_rejected_before_extracting(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);out,bundles,archive=self.fixture(root);archive.write_bytes(archive.read_bytes()+b'changed')
            with self.assertRaisesRegex(ValueError,'checksum mismatch'):stage(root,out,'windows-x64',root/'cache',bundles)
            self.assertFalse((out/'firmware').exists())
    def test_archive_cannot_overwrite_sdk_tools_or_escape_root(self):
        for name in ['tools/aster.py','firmware/../../escape','simulator/..\\escape']:
            with self.subTest(name=name),tempfile.TemporaryDirectory() as d:
                root=Path(d);out,bundles,_=self.fixture(root,{name:b'bad'})
                with self.assertRaisesRegex(ValueError,'Unsafe development kit member'):stage(root,out,'windows-x64',root/'cache',bundles)
    def test_wrong_board_image_is_rejected_even_in_valid_outer_archive(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);out,bundles,_=self.fixture(root,{'firmware/board.bin':b'wrong board','simulator/aster-sim.exe':b'simulator'})
            with self.assertRaisesRegex(ValueError,'firmware image checksum mismatch'):stage(root,out,'windows-x64',root/'cache',bundles)
