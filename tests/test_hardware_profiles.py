import argparse
import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import aster_board
from target_checks import validate_manifest

class HardwareProfiles(unittest.TestCase):
    def test_board_identity_and_cpu_are_separate(self):
        boards=aster_board.hardware()
        self.assertTrue({'osptek-p4','lckfb-s3','fakepod-s31','waveshare-35b'}.issubset(boards))
        for name,board in boards.items():
            self.assertNotEqual(name,board['soc'])
            self.assertTrue(board['name'])
            self.assertTrue(board['board_id'])
        p4=aster_board.get_hardware('osptek-p4')
        s31=aster_board.get_hardware('fakepod-s31')
        self.assertEqual(p4['id'],s31['id'])
        self.assertEqual(p4['id'],'riscv')
        self.assertNotEqual(p4['hardware_id'],s31['hardware_id'])
        self.assertEqual(p4['display']['width'],480)
        self.assertEqual(s31['display']['width'],460)
        self.assertIn('鱼鹰光电',p4['hardware_name'])
        self.assertIn('media-player',p4['capabilities'])
        self.assertIn('audio-in',p4['capabilities'])
        self.assertIn('audio-in',s31['capabilities'])
        self.assertIn('media-player',s31['capabilities'])
        self.assertEqual(aster_board.get_hardware('lckfb-s3')['id'],'xtensa')
        with self.assertRaisesRegex(SystemExit,'requires --target riscv'):
            aster_board.get_hardware('osptek-p4','xtensa')
        with self.assertRaisesRegex(SystemExit,'Select --hardware'):
            aster_board.get_hardware('esp32p4')
        with patch.object(aster_board,'get',return_value={'id':'riscv','aot_target':'riscv32','aot_profile':'wrong'}):
            with self.assertRaisesRegex(SystemExit,'profile differs'):
                aster_board.get_hardware('fakepod-s31')

    def test_hardware_memory_limit_and_runtime_capabilities(self):
        target=aster_board.get_hardware('osptek-p4')
        manifest={'id':'test.app','product':'aster','target_id':'riscv',
                  'abi':{'min':'4.0.0','target':target['abi']},
                  'memory':{'initial':65536,'max':8388608,'stack':1024}}
        validate_manifest(manifest,target)
        manifest['memory']['max']=16*1024*1024
        with self.assertRaisesRegex(ValueError,'hardware ceiling'):
            validate_manifest(manifest,target)
        manifest['memory']['max']=8388608
        manifest['requires']=['media-player']
        validate_manifest(manifest,target)

    def test_source_cli_forwards_hardware_without_native_subcommand(self):
        import aster
        project=Path(__file__).resolve().parents[1]/'templates/hello'
        args=argparse.Namespace(project=str(project),target=None,hardware='fakepod-s31',all=False,aot=True,no_aot=False,clean=False)
        with patch.object(aster,'tool_command',return_value=[sys.executable,str(aster.ROOT/'tools/build.py')]),patch.object(aster.subprocess,'run') as run:
            selected_project,target=aster.build_project(args)
        command=run.call_args.args[0]
        self.assertEqual(target,'riscv')
        self.assertNotIn('build',command[2:])
        self.assertEqual(command[command.index('--hardware')+1],'fakepod-s31')
        self.assertEqual(selected_project,project.resolve())
