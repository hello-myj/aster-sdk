import hashlib
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request,urlopen

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import setup_server as server

def font(path):
    head=struct.pack('<8sHHB19s',b'CPFONT\0\0',4,0,1,bytes(19))
    toc=struct.pack('<B3xIIBhhHHBBBI4x',0,1,1,20,16,-4,0,0,0,0,0,64)
    path.write_bytes(head+toc+struct.pack('<III',65,65,0)+struct.pack('<BBHhhH2xI',8,8,128,0,8,32,0)+b'\xff'*32)

class SetupServerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.fonts=self.root/'fonts';self.fonts.mkdir();self.data=self.root/'data'
        for px in server.SIZES:font(self.fonts/f'ui_{px}.cpfont')
        self.package=self.root/'store.ast';self.package.write_bytes(b'fixture AOT payload')
        manifest={'version':'0.1.8','abi':{'min':'4.3'},'requires':['display','touch','filesystem','http'],'memory':{'initial':1572864}}
        item={'kind':'store','path':'store/package.ast','bytes':self.package.stat().st_size,'sha256':server.digest_file(self.package)}
        self.verifier=patch.object(server,'store_metadata',return_value=(manifest,item));self.verifier.start()
        self.key=server.publish(self.data,self.fonts,self.package,'osptek-p4-st7102','riscv','1.0.0','http://127.0.0.1:9080',[[480,480]])
        self.resources=server.Resources(self.data)
        self.http=ThreadingHTTPServer(('127.0.0.1',0),server.handler(self.resources));self.http.daemon_threads=True
        self.thread=threading.Thread(target=self.http.serve_forever,daemon=True);self.thread.start()
        self.base=f'http://127.0.0.1:{self.http.server_port}'
        self.dev={'board':'osptek-p4-st7102','target':'riscv','abi':0x40300,'width':480,'height':480,'features':(1<<0)|(1<<1)|(1<<11)|(1<<16)}
    def tearDown(self):
        self.http.shutdown();self.http.server_close();self.thread.join();self.verifier.stop();self.tmp.cleanup()
    def get(self,path,headers=None):return urlopen(Request(self.base+path,headers=headers or {}),timeout=3)
    def test_resolve_metadata_and_payload(self):
        with self.get('/v1/setup/resolve?'+urlencode(self.dev)) as r:plan=json.load(r)
        self.assertEqual(plan['files'],11);self.assertEqual(plan['release'],self.key)
        self.assertEqual(plan['store_version'],'0.1.8')
        with self.get(f'/v1/setup/releases/{self.key}/files/10') as r:item=json.load(r)
        self.assertEqual(item['kind'],'store')
        with self.get(item['url']) as r:body=r.read();self.assertEqual(r.headers['ETag'],'"'+item['sha256']+'"')
        self.assertEqual(hashlib.sha256(body).hexdigest(),item['sha256'])
    def test_hardware_abi_and_features_rejected(self):
        for changed,status in [({'target':'xtensa'},409),({'abi':0x40200},409),({'abi':0x50300},409),({'width':320},409),({'features':1},409),({'board':'missing-board'},404)]:
            with self.subTest(changed=changed),self.assertRaises(HTTPError) as caught:
                self.get('/v1/setup/resolve?'+urlencode(self.dev|changed))
            self.assertEqual(caught.exception.code,status)
    def test_resume_and_if_range(self):
        path=f'/v1/setup/releases/{self.key}/payload/10';body=self.package.read_bytes()
        with self.get(path,{'Range':'bytes=4-','If-Range':'"'+server.digest_file(self.package)+'"'}) as r:
            self.assertEqual(r.status,206);self.assertEqual(r.read(),body[4:]);self.assertEqual(r.headers['Content-Range'],f'bytes 4-{len(body)-1}/{len(body)}')
        with self.get(path,{'Range':'bytes=4-','If-Range':'"old"'}) as r:self.assertEqual(r.status,200);self.assertEqual(r.read(),body)
        with self.assertRaises(HTTPError) as caught:self.get(path,{'Range':'bytes=9999-'})
        self.assertEqual(caught.exception.code,416)
    def test_traversal_unknown_routes_and_duplicates(self):
        for path in ['/v1/setup/releases/../payload/0','/v1/setup/releases/'+self.key+'/payload/999','/etc/passwd']:
            with self.assertRaises(HTTPError) as caught:self.get(path)
            self.assertEqual(caught.exception.code,404)
        with self.assertRaises(HTTPError) as caught:self.get('/v1/setup/resolve?'+urlencode(self.dev)+'&board=other')
        self.assertEqual(caught.exception.code,400)
    def test_publication_is_immutable_and_interruption_preserves_profile(self):
        profile=self.data/'profiles/osptek-p4-st7102.json';before=profile.read_bytes()
        with patch.object(server.shutil,'copyfile',side_effect=OSError('interrupted')):
            with self.assertRaises(OSError):server.publish(self.data,self.fonts,self.package,'osptek-p4-st7102','riscv','1.0.1','http://localhost:9080',[[480,480]])
        self.assertEqual(profile.read_bytes(),before)
        info=self.resources.release(self.key)
        self.assertEqual(len(info['files']),11)
    def test_corrupt_font_is_rejected(self):
        path=self.fonts/'ui_16.cpfont';path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaises(ValueError):server.font_metadata(path,16)
    def test_http_surface_cannot_publish(self):
        with self.assertRaises(HTTPError) as caught:urlopen(Request(self.base+'/publish',data=b'{}'),timeout=3)
        self.assertEqual(caught.exception.code,501)

if __name__=='__main__':unittest.main()
