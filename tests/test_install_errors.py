"""Deployment failures retain the installer phase without treating background logs as errors."""
import sys
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import deploy


class InstallErrors(unittest.TestCase):
    def test_storage_failure_includes_command_error_stage_and_remedy(self):
        response = ('aster app install /SD:/test.ast\n'
                    '[00:01] <err> aster_pkg: install: stage=manifest operation=sync path=/SD:/aster/apps/test/manifest.json result=-28\n'
                    'install failed: -7 (I/O error)\naster:~$')
        reason = deploy.installation_error(response)
        self.assertIn('installation failed: -7 (I/O error)', reason)
        self.assertIn('stage=manifest operation=sync', reason)
        self.assertIn('free space', reason)

    def test_abi_failure_is_not_misdiagnosed_as_storage(self):
        reason = deploy.installation_error('install failed: -13 (ABI mismatch)\n')
        self.assertIn('ABI mismatch', reason)
        self.assertNotIn('free space', reason)

    def test_missing_ack_is_distinct_from_background_failures(self):
        reason = deploy.installation_error('[00:01] SNTP sync failed: -11\n')
        self.assertIn('missing installed OK acknowledgement', reason)
        self.assertNotIn('SNTP', reason)

    def test_deploy_stops_before_start_and_retains_manifest_write_diagnostic(self):
        import aster_send
        response = ('install: stage=manifest operation=write path=/SD:/test/manifest.json result=2 expected=200\n'
                    'install failed: -7 (I/O error)\naster:~$')
        with patch.object(deploy.aster_board, 'get', return_value={'deployment': {'package_dir': '/SD:'}}), \
             patch.object(deploy, 'check', return_value={'id': 'test.app'}), \
             patch.object(deploy, 'validate_device'), \
             patch.object(aster_send, 'find_port', return_value=('TRANSFER', 'PONG target=xtensa')), \
             patch.object(aster_send, 'send_file', return_value='OK'), \
             patch.object(deploy, 'shell_command', return_value=response) as shell:
            with self.assertRaisesRegex(RuntimeError, 'stage=manifest operation=write'):
                deploy.main(['test.ast', '--target', 'xtensa', '--port', 'TRANSFER', '--console', 'CONSOLE', '--run'])
        shell.assert_called_once()

    def test_background_failure_does_not_invalidate_successful_deployment(self):
        import aster_send
        with patch.object(deploy.aster_board, 'get', return_value={'deployment': {'package_dir': '/SD:'}}), \
             patch.object(deploy, 'check', return_value={'id': 'test.app'}), \
             patch.object(deploy, 'validate_device'), \
             patch.object(aster_send, 'find_port', return_value=('TRANSFER', 'PONG target=xtensa')), \
             patch.object(aster_send, 'send_file', return_value='OK'), \
             patch.object(deploy, 'shell_command', side_effect=['SNTP sync failed: -11\ninstalled OK\n', 'started test.app\n']) as shell:
            self.assertEqual(deploy.main(['test.ast', '--target', 'xtensa', '--port', 'TRANSFER', '--console', 'CONSOLE', '--run']), 0)
        self.assertEqual(shell.call_count, 2)

    def test_busy_install_explains_how_to_leave_the_live_application(self):
        reason = deploy.installation_error('install failed: -5 (busy)\n')
        self.assertIn('Return to the launcher', reason)
        self.assertNotIn('free space', reason)
