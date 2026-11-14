import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import setup_server as setup
import static_distribution as static
from test_setup_server import font


class StaticDistribution(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.fonts=self.root/'fonts';self.fonts.mkdir()
        for px in setup.SIZES:font(self.fonts/f'ui_{px}.cpfont')
        package=self.root/'store.ast';package.write_bytes(b'fixture store AOT')
        item={'kind':'store','path':'store/package.ast','bytes':package.stat().st_size,'sha256':setup.digest_file(package)}
        manifest={'version':'0.2.0','abi':{'min':'4.3'},'requires':['display','touch','filesystem','http']}
        self.setup=self.root/'setup';self.store=self.root/'store';self.output=self.root/'output'
        with patch.object(setup,'store_metadata',return_value=(manifest,item)):
            for board,target,screen in [('osptek-p4-st7102','riscv',[480,480]),('aster.esp32s3.lckfb','xtensa',[240,320])]:
                setup.publish(self.setup,self.fonts,package,board,target,'1.1.0','http://localhost:9080',[screen])
        self.ids=[]
        for board,target,screen in [('osptek-p4-st7102','riscv',[480,480]),('aster.esp32s3.lckfb','xtensa',[240,320])]:
            data=b'fixture '+target.encode()
            record={'schema':1,'id':'aster.touchtest','name':'TouchTest','version':'1.0.1',
                    'target':target,'boards':[board],'screens':[screen],'abi_min':[4,1,0],
                    'requires':['display','touch'],'permissions':[],'description':'Test',
                    'files':[{'index':0,'role':'package','path':'package.ast','bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}],
                    'total_bytes':len(data)}
            key=hashlib.sha256(setup.encode(record)).hexdigest();self.ids.append(key)
            release=self.store/'releases'/key;(release/'payload').mkdir(parents=True)
            (release/'release.json').write_bytes(setup.encode(record));(release/'payload/0').write_bytes(data)
        self.validate=patch.object(static,'validate_package');self.validator=self.validate.start()
    def tearDown(self):
        self.validate.stop();self.temp.cleanup()
    def export(self,**kw):
        return static.export(self.setup,self.store,self.output,kw.get('repository','owner/repo'),kw.get('tag','test-1'),
                             kw.get('boards',['osptek-p4-st7102','aster.esp32s3.lckfb']),origin=kw.get('origin'))
    def test_bucket_origin_addresses(self):
        origin='https://aster-releases.obs.cn-south-1.myhuaweicloud.com'
        channel=self.export(origin=origin)
        self.assertEqual(channel,origin+'/aster-source.json')
        plan=json.loads(next(self.output.glob('setup-*-riscv.json')).read_bytes())
        self.assertEqual(plan['market_url'],channel)
        static.validate_export(self.output,'owner/repo','test-1',origin)
    def test_rejects_unsafe_origins(self):
        for origin in ['http://bucket.test','https://u:p@bucket.test','https://bucket.test/path',
                       'https://bucket.test?token=x','https://bucket.test/#fragment','https://'+'x'*190+'.test']:
            with self.subTest(origin=origin),self.assertRaises(ValueError):
                static.source_urls('owner/repo','test-1',origin)
    def test_profiles_catalogs_pin_release_and_deduplicate(self):
        channel=self.export();self.assertTrue(channel.endswith('/latest/download/aster-source.json'))
        source=json.loads((self.output/'aster-source.json').read_bytes())
        self.assertEqual(source['base_url'],'https://github.com/owner/repo/releases/download/test-1')
        for board,target in [('osptek-p4-st7102','riscv'),('aster.esp32s3.lckfb','xtensa')]:
            plan=json.loads((self.output/f'setup-{board}-{target}.json').read_bytes())
            self.assertEqual(plan['schema'],2);self.assertEqual(plan['market_url'],channel)
            self.assertTrue(plan['features'] & (1<<11));self.assertTrue(plan['features'] & (1<<17))
            catalog=json.loads((self.output/f'catalog-{board}-{target}-0.json').read_bytes())
            self.assertEqual(catalog['total'],1);self.assertEqual(catalog['items'][0]['target'],target)
            for index in range(11):
                item=json.loads((self.output/f"setup-{plan['release']}-{index}.json").read_bytes())
                self.assertEqual(item['url'],'blob-'+item['sha256'])
        # Mock fonts are identical; both profiles share one font and one Store blob.
        self.assertEqual(len(list(self.output.glob('blob-*'))),4)
        for line in (self.output/'SHA256SUMS').read_text().splitlines():
            digest,name=line.split('  ');self.assertEqual(setup.digest_file(self.output/name),digest)
        static.validate_export(self.output,'owner/repo','test-1')
    def test_corrupt_payload_never_creates_output(self):
        (self.store/'releases'/self.ids[0]/'payload/0').write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError,'Payload'):self.export()
        self.assertFalse(self.output.exists())
    def test_metadata_digest_checked(self):
        path=self.store/'releases'/self.ids[0]/'release.json';path.write_bytes(path.read_bytes()+b' ')
        with self.assertRaisesRegex(ValueError,'digest'):self.export()
    def test_explicit_board_and_safe_tag_required(self):
        for changed in [{'boards':[]},{'boards':['../escape']},{'repository':'owner/repo/extra'},{'tag':'../tag'}]:
            with self.subTest(changed=changed),self.assertRaises(ValueError):self.export(**changed)
    def test_existing_output_is_not_overwritten(self):
        self.export();previous=(self.output/'aster-source.json').read_bytes()
        with self.assertRaisesRegex(ValueError,'already exists'):self.export()
        self.assertEqual((self.output/'aster-source.json').read_bytes(),previous)
    def test_package_verification_failure_is_atomic(self):
        self.validator.side_effect=ValueError('bad AOT')
        with self.assertRaisesRegex(ValueError,'AOT'):self.export()
        self.assertFalse(self.output.exists())
    def test_upload_refuses_extra_files_before_any_network_call(self):
        self.export();(self.output/'private-notes.txt').write_text('not a release asset')
        with patch.object(static.subprocess,'run') as run,self.assertRaisesRegex(ValueError,'outside'):
            static.publish(self.output,'owner/repo','test-1')
        run.assert_not_called()
    def test_upload_refuses_wrong_tag_or_modified_export(self):
        self.export()
        with self.assertRaisesRegex(ValueError,'destination'):static.validate_export(self.output,'owner/repo','other')
        next(self.output.glob('blob-*')).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'changed'):static.validate_export(self.output,'owner/repo','test-1')
