import hashlib,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from aot_format import normalize
class AotFormat(unittest.TestCase):
    def test_alias_preserves_entire_payload_and_requires_pinned_compiler(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);compiler=root/'compiler';compiler.write_bytes(b'pinned');image=root/'app.aot';original=b'\0aot'+struct.pack('<I',5)+bytes(range(256));image.write_bytes(original)
            host={'emitted_aot_format':5,'wamrc_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest()}
            normalize(image,compiler,host,6);self.assertEqual(image.read_bytes()[8:],original[8:]);self.assertEqual(struct.unpack_from('<I',image.read_bytes(),4)[0],6)
            image.write_bytes(original);compiler.write_bytes(b'other')
            with self.assertRaisesRegex(ValueError,'Unpinned'):normalize(image,compiler,host,6)
            self.assertEqual(image.read_bytes(),original)
            with self.assertRaisesRegex(ValueError,'profile'):normalize(image,compiler,{},7)
