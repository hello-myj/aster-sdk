"""Offline sampling must keep pixel art exact and avoid alpha contamination."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from prepare_image import prepare,fit_size

@unittest.skipUnless(importlib.util.find_spec('PIL'), 'optional Pillow assets dependency unavailable')
class ImagePreparation(unittest.TestCase):
 @staticmethod
 def pixels(image):
  raw=image.tobytes();return zip(raw[0::3],raw[1::3],raw[2::3])
 def test_pixel_palette_photo_filter_and_actual_fit(self):
  from PIL import Image
  with tempfile.TemporaryDirectory() as d:
   src=Path(d)/'source.png';dst=Path(d)/'prepared.bmp'
   im=Image.new('RGB',(8,4));im.putdata([(255,0,0) if x%2 else (0,255,0) for y in range(4) for x in range(8)]);im.save(src)
   self.assertEqual(prepare(src,dst,4,4,'pixel'),(4,2))
   with Image.open(dst) as p:self.assertTrue(set(self.pixels(p)) <= {(255,0,0),(0,255,0)})
   prepare(src,dst,4,4,'photo')
   with Image.open(dst) as p:self.assertTrue(any(0<r<255 and 0<g<255 for r,g,b in self.pixels(p)))
   with self.assertRaisesRegex(ValueError,'upscaling'):prepare(src,dst,16,16,'photo')
   with self.assertRaisesRegex(ValueError,'differ'):prepare(src,src,4,4,'pixel')
 def test_transparent_edges_require_explicit_matte(self):
  from PIL import Image
  with tempfile.TemporaryDirectory() as d:
   src=Path(d)/'source.png';dst=Path(d)/'prepared.png'
   im=Image.new('RGBA',(4,2),(255,0,0,0));im.putpixel((1,0),(0,255,0,255));im.putpixel((1,1),(0,255,0,255));im.save(src)
   with self.assertRaisesRegex(ValueError,'background'):prepare(src,dst,2,1,'photo')
   prepare(src,dst,2,1,'photo','#000000')
   with Image.open(dst) as p:
    self.assertEqual(p.mode,'RGB');self.assertTrue(all(r==0 and b==0 for r,g,b in self.pixels(p)))
 def test_fit_rounding_preserves_nonzero_axes(self):
  self.assertEqual(fit_size(13,7,8,8),(8,4))
  self.assertEqual(fit_size(1000,1,1,1),(1,1))
  with self.assertRaises(ValueError):fit_size(2,2,0,1)
