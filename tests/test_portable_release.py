import json
import hashlib
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import import_contract
import package_portable
import toolchain_catalog
ROOT = Path(__file__).resolve().parents[1]


class PortableReleaseTests(unittest.TestCase):
    def test_hardware_contract_preserves_cpu_target_and_regenerates_hashes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'sdk'
            (root / 'targets').mkdir(parents=True)
            platform = (ROOT / 'targets/xtensa.json').read_bytes()
            (root / 'targets/xtensa.json').write_bytes(platform)
            api = (ROOT / 'contract/api.json').read_bytes()
            files = {'contract/api.json': api, 'include/aster/new.h': b'void aster_new(void);'}
            for name, limit, caps in [('lckfb-s3', 7864320, ['camera']),
                                      ('waveshare-35b', 4194304, ['display'])]:
                target = {'schema': 1, 'id': name, 'product': 'aster', 'abi': '3.7.0',
                          'cpu': 'xtensa', 'aot_target': 'xtensa', 'system': 'zephyr',
                          'capabilities': caps, 'display': {'width': 320, 'height': 240},
                          'memory': {'max_linear': limit}}
                files['targets/' + name + '.json'] = json.dumps(target).encode()
            # An old hardware target must be removed on migration.
            (root / 'targets/lckfb-s3.json').write_bytes(files['targets/lckfb-s3.json'])
            (root / 'contract').mkdir()
            previous = {'sha256': {'targets/lckfb-s3.json': 'unused'}}
            (root / 'contract/lock.json').write_text(json.dumps(previous))
            bundle = Path(folder) / 'contract.zip'
            def write_bundle():
                lock = {'schema': 1, 'product': 'aster', 'abi': '3.7.0',
                        'sha256': {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}}
                with zipfile.ZipFile(bundle, 'w') as archive:
                    archive.writestr('contract/lock.json', json.dumps(lock))
                    for name, data in files.items():
                        archive.writestr(name, data)
            write_bundle()
            imported = import_contract.import_bundle(bundle, root)
            self.assertEqual({p.name for p in (root / 'targets').glob('*.json')}, {'xtensa.json'})
            self.assertEqual(imported['targets/xtensa.json'], platform)
            self.assertEqual(imported['contract/api.json'], api)
            lock = json.loads(imported['contract/lock.json'])
            for name, digest in lock['sha256'].items():
                self.assertEqual(hashlib.sha256((root / name).read_bytes()).hexdigest(), digest)
            self.assertEqual(import_contract.import_bundle(bundle, root), imported)
            # Backend incompatibility must fail before updating the SDK.
            name = 'targets/waveshare-35b.json'
            target = json.loads(files[name]);target['aot_target'] = 'riscv32'
            files[name] = json.dumps(target).encode();write_bundle()
            with self.assertRaisesRegex(ValueError, 'AOT backend differs'):
                import_contract.import_bundle(bundle, root)
            self.assertEqual((root / 'contract/lock.json').read_bytes(), imported['contract/lock.json'])

    def test_release_carries_self_contained_manual_and_skill(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)/'aster-sdk'
            out.mkdir()
            package_portable.copy_sdk(out, ['xtensa'], 'windows')
            source = package_portable.ROOT
            for name in ('README.md', 'docs/app-development.md', 'docs/serial-console-transfer.md', 'apps/imucube/app.toml',
                         '.agents/skills/aster-app-development/SKILL.md'):
                self.assertEqual((out/name).read_bytes(), (source/name).read_bytes())
            skill = out/'.agents/skills/aster-app-development/SKILL.md'
            manual = (skill.parent/'../../../docs/app-development.md').resolve()
            self.assertTrue(manual.is_relative_to(out.resolve()))
            self.assertTrue(manual.is_file())

    def test_contract_import_updates_interfaces_and_rejects_inconsistent_abi(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);bundle=root/'contract.zip'
            files={'contract/api.json':json.dumps({'product':'aster','abi':'4.0.0'}).encode(),
                   'include/aster/new.h':b'void aster_new(void);',
                   'targets/new-board.json':json.dumps({'product':'aster','abi':'4.0.0','id':'new-board'}).encode()}
            lock={'schema':1,'product':'aster','abi':'4.0.0','sha256':{n:hashlib.sha256(d).hexdigest() for n,d in files.items()}}
            def write_bundle():
                with zipfile.ZipFile(bundle,'w') as z:
                    z.writestr('contract/lock.json',json.dumps(lock))
                    for name,data in files.items():z.writestr(name,data)
            write_bundle();import_contract.import_bundle(bundle,root/'sdk')
            self.assertEqual((root/'sdk/include/aster/new.h').read_bytes(),files['include/aster/new.h'])
            lock['abi']='5.0.0';write_bundle()
            with self.assertRaisesRegex(ValueError,'ABI differs'):
                import_contract.import_bundle(bundle,root/'sdk')
            self.assertEqual(json.loads((root/'sdk/contract/lock.json').read_text())['abi'],'4.0.0')

    def test_bad_contract_does_not_modify_sdk(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);bundle=root/'bad.zip'
            lock={'schema':1,'product':'aster','sha256':{'contract/api.json':'0'*64}}
            with zipfile.ZipFile(bundle,'w') as z:
                z.writestr('contract/lock.json',json.dumps(lock));z.writestr('contract/api.json','{}')
            with self.assertRaisesRegex(ValueError,'digest mismatch'):
                import_contract.import_bundle(bundle,root/'sdk')
            self.assertFalse((root/'sdk').exists())

    def test_zip_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);bundle=root/'bad.zip'
            with zipfile.ZipFile(bundle,'w') as z:z.writestr('../escape','bad')
            with self.assertRaisesRegex(ValueError,'escapes'):
                package_portable.extract_zip(bundle,root/'out')
            self.assertFalse((root/'escape').exists())

    def test_board_cpu_change_requires_new_binding(self):
        root=package_portable.ROOT
        target,binding,profile,host=toolchain_catalog.select(root,'xtensa','linux-x64')
        self.assertEqual(binding['aot_backend'],'xtensa');self.assertEqual(profile['aot_format'],6)
        with patch.object(toolchain_catalog.json,'loads',side_effect=[{'boards':{'xtensa':{'cpu':'riscv32'}}},{'cpu':'xtensa'}]):
            with self.assertRaisesRegex(ValueError,'differs'):
                toolchain_catalog.select(root,'xtensa','linux-x64')

    def test_package_carries_every_registered_board(self):
        source=package_portable.ROOT
        boards=sorted(p.stem for p in (source/'targets').glob('*.json'))
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'aster-sdk'
            out.mkdir()
            package_portable.copy_sdk(out,boards,'windows')
            for relative in ('lib/aster/aster_scene.c', 'lib/aster/aster_layout.c',
                             'lib/aster/aster_widgets.c', 'templates/scene-list/src/main.c',
                             'docs/app-development.md', '.agents/skills/aster-app-development/SKILL.md'):
                self.assertEqual((out/relative).read_bytes(), (source/relative).read_bytes())
            self.assertFalse((out/'templates/scene-list/dist').exists())
            self.assertEqual([p.name for p in (out/'targets').glob('*.json')],
                             [board+'.json' for board in boards])
            for board in boards:
                self.assertEqual((out/'targets'/(board+'.json')).read_bytes(),
                                 (source/'targets'/(board+'.json')).read_bytes())

    def test_one_package_serves_boards_sharing_a_profile(self):
        plan=package_portable.toolchain_plan(['xtensa'],'linux-x64')
        self.assertEqual(sorted(plan),['esp32s3-wamr-2.4.3-v6'])
        self.assertEqual(plan['esp32s3-wamr-2.4.3-v6']['backends'],
                         {'xtensa':['xtensa']})

    def test_same_backend_cannot_span_two_profiles(self):
        def fake_select(root,board,host):
            name={'lckfb-s3':'p1','waveshare-35b':'p2'}[board]
            return {}, {'profile':name,'aot_backend':'xtensa'}, {'aot_format':6,'wasi_sdk':'24.0'}, {}
        with patch.object(package_portable,'select',fake_select):
            with self.assertRaisesRegex(ValueError,'cannot choose'):
                package_portable.toolchain_plan(['lckfb-s3','waveshare-35b'],'linux-x64')

    def test_distinct_backends_plan_side_by_side(self):
        def fake_select(root,board,host):
            name,backend={'lckfb-s3':('p1','xtensa'),'riscv-demo':('p2','riscv32')}[board]
            return {}, {'profile':name,'aot_backend':backend}, {'aot_format':6,'wasi_sdk':'24.0'}, {}
        with patch.object(package_portable,'select',fake_select):
            plan=package_portable.toolchain_plan(['lckfb-s3','riscv-demo'],'linux-x64')
        self.assertEqual(sorted(plan),['p1','p2'])
        self.assertEqual(plan['p2']['backends'],{'riscv32':['riscv-demo']})

    def test_package_requires_one_wasi_version(self):
        def fake_select(root,board,host):
            version={'lckfb-s3':'24.0','riscv-demo':'25.0'}[board]
            backend='xtensa' if board=='lckfb-s3' else 'riscv32'
            return {}, {'profile':board,'aot_backend':backend}, {'aot_format':6,'wasi_sdk':version}, {}
        with patch.object(package_portable,'select',fake_select):
            with self.assertRaisesRegex(ValueError,'wasi-sdk'):
                package_portable.toolchain_plan(['lckfb-s3','riscv-demo'],'linux-x64')

    def test_target_backend_selects_bundled_compiler(self):
        import build
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'backends/riscv32').mkdir(parents=True)
            flat=root/'wamrc';flat.write_bytes(b'xtensa build')
            riscv=root/'backends/riscv32/wamrc';riscv.write_bytes(b'riscv build')
            with patch.dict(os.environ,{'ASTER_WAMRC':''}),patch.object(build,'BUNDLED_TOOLCHAIN',root):
                self.assertEqual(build.find_wamrc('xtensa'),flat)     # single-CPU packages keep the flat layout
                self.assertEqual(build.find_wamrc('riscv32'),riscv)   # multi-CPU packages pick per backend
                self.assertEqual(build.bundled_wamrcs(),{'riscv32':riscv})

    def test_wrong_native_compiler_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'wamrc';binary.write_bytes(b'wrong architecture')
            with self.assertRaisesRegex(ValueError,'SHA256'):
                toolchain_catalog.check_wamrc(binary,'0'*64)


if __name__=='__main__':unittest.main()
