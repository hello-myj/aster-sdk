"""Names remain UTF-8 manifest text; current launcher renders native fonts."""
import json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from pack_ast import main
class NameMetadata(unittest.TestCase):
 def test_non_ascii_name_roundtrip_without_bitmap_asset(self):
  import struct
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);manifest=root/'manifest.json';wasm=root/'app.wasm';output=root/'test.ast'
   manifest.write_text(json.dumps({'id':'test.name','name':'阅读','description':'翻开一页','version':'1.0.0','abi':{'min':'3.2','target':'3.7.0'}},ensure_ascii=False))
   wasm.write_bytes(b'\0asm\1\0\0\0')
   self.assertEqual(main(['--manifest',str(manifest),'--wasm',str(wasm),'--out',str(output)]),0)
   raw=output.read_bytes();offset,length=struct.unpack_from('<II',raw,12);data=json.loads(raw[offset:offset+length])
   self.assertEqual(data['name'],'阅读');self.assertEqual(data['description'],'翻开一页')
   self.assertNotIn(b'name.a8',raw)
