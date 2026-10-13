import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import aster_board
from import_contract import read_bundle
from target_checks import validate_device, validate_manifest
from verify_ast import check


class Compatibility(unittest.TestCase):
    def setUp(self):
        self.target = aster_board.get("xtensa")
        abi = self.target["abi"]
        self.manifest = {"id": "test.hello", "product": "aster", "target_id": self.target["id"],
                         "abi": {"min": ".".join(abi.split(".")[:2]), "target": abi}, "requires": ["display"],
                         "memory": {"initial": 131072, "max": 262144, "stack": 16384}}

    def test_compatible_release(self):
        validate_manifest(self.manifest, self.target)

    def test_wrong_hardware_even_without_aot(self):
        with self.assertRaisesRegex(ValueError, "target"):
            validate_manifest(self.manifest, dict(self.target, id="different-hardware"))

    def test_missing_capability(self):
        with self.assertRaisesRegex(ValueError, "display"):
            validate_manifest(self.manifest, dict(self.target, capabilities=[]))

    def test_incompatible_abi(self):
        major, minor = map(int, self.target["abi"].split(".")[:2])
        for version in (f"{major + 1}.0", f"{major}.{minor + 1}", "invalid"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                self.manifest["abi"]["min"] = version
                validate_manifest(self.manifest, self.target)

    def test_invalid_memory(self):
        for memory in ({"initial": 1, "stack": 1}, {"initial": 131072, "stack": 262144},
                       {"initial": 131072, "stack": 16384, "max": 65536},
                       {"initial": 131072, "stack": 16384, "max": 100000}):
            with self.subTest(memory=memory), self.assertRaises(ValueError):
                self.manifest["memory"] = memory
                validate_manifest(self.manifest, self.target)

    def test_identifier_cannot_escape_or_inject_shell(self):
        for name in ("../other", "hello;reboot", "hello\nreboot", "a" * 64):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.manifest["id"] = name
                validate_manifest(self.manifest, self.target)

    def test_device_identity(self):
        abi = self.target["abi"]
        validate_device(f"PONG sd=1 product=aster target=xtensa abi={abi}", self.target)
        for pong in ("PONG sd=1", "PONG sd=1 product=mjx target=xtensa abi=3.2",
                     f"PONG sd=1 product=aster target=different-hardware abi={abi}",
                     f"PONG sd=0 product=aster target=xtensa abi={abi}"):
            with self.subTest(pong=pong), self.assertRaises(ValueError):
                validate_device(pong, self.target)

    def test_contract_hashes(self):
        lock = json.loads((ROOT / "contract/lock.json").read_text())
        for name, digest in lock["sha256"].items():
            with self.subTest(file=name):
                self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest)

    def test_bundle_rejects_traversal_and_digest_corruption(self):
        for name, data, digest in (("targets/../../escape", b"test", hashlib.sha256(b"test").hexdigest()),
                                   ("targets/test.json", b"wrong", hashlib.sha256(b"test").hexdigest())):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / "bundle.zip"
                with zipfile.ZipFile(archive, "w") as bundle:
                    bundle.writestr(name, data)
                    bundle.writestr("contract/lock.json", json.dumps({"schema": 1, "product": "aster", "sha256": {name: digest}}))
                with self.assertRaises(ValueError):
                    read_bundle(archive)


class BuildPipeline(unittest.TestCase):
    def test_build_for_platform_target(self):
        board = "xtensa"
        subprocess.run([sys.executable, str(ROOT / "tools/build.py"), str(ROOT / "apps/hello"),
                        "--target", board, "--no-aot"], check=True, capture_output=True, text=True)
        manifest = check(ROOT / f"dist/{board}/aster.hello.ast", board=board)
        self.assertEqual(manifest["target_id"], board)
        target = aster_board.get(board)
        self.assertNotIn("max_linear", target.get("memory", {}))
        self.assertEqual(set(aster_board.targets()), {"xtensa"})
        abi = target["abi"]
        validate_manifest({"id": "test.hello", "product": "aster", "target_id": board,
                           "abi": {"min": ".".join(abi.split(".")[:2]), "target": abi},
                           "requires": ["display"],
                           "memory": {"initial": 4194304, "max": 8388608, "stack": 16384}}, target)

    def test_target_is_mandatory(self):
        import os
        env = dict(os.environ)
        env.pop("ASTER_TARGET", None)
        result = subprocess.run([sys.executable, str(ROOT / "tools/build.py"), str(ROOT / "apps/hello")],
                                capture_output=True, text=True, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Select --target", result.stderr)

    def test_cpu_override_cannot_mislabel_package(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/build.py"), str(ROOT / "apps/hello"),
                                 "--target", "xtensa", "--aot-target", "x86_64"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("conflicts", result.stderr)

    def test_screen_mode_is_not_a_hardware_target(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/build.py"), str(ROOT / "apps/hello"),
                                 "--target", "headless", "--no-aot"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Select --target", result.stderr)

    def test_headless_device_rejects_graphical_app(self):
        target = aster_board.get("xtensa")
        pong = f"PONG sd=1 product=aster target=xtensa abi={target['abi']} caps="
        with self.assertRaisesRegex(ValueError, "display"):
            validate_device(pong + "0", target, ["display"])
        validate_device(pong + "1", target, ["display"])
        validate_device(pong + "0", target, [])


class Deployment(unittest.TestCase):
    def test_background_error_does_not_hide_install_result(self):
        import deploy
        abi = aster_board.get("xtensa")["abi"]
        sender = SimpleNamespace(find_port=lambda *a, **kw: (
            "/dev/fake", f"PONG sd=1 product=aster target=xtensa abi={abi}"),
            send_file=lambda *a, **kw: "OK")
        for response, success in (
                ("[00:01:05] SNTP sync failed: -11\r\ninstalled OK\r\naster:~$ ", True),
                ("install failed: -7\r\naster:~$ ", False),
                ("[00:01:05] another app was installed\r\naster:~$ ", False)):
            with self.subTest(response=response), \
                 patch.object(deploy, "check", return_value={"id": "test.hello"}), \
                 patch.dict(sys.modules, {"aster_send": sender}), \
                 patch.object(deploy, "shell_command", return_value=response):
                args = ["test.ast", "--target", "xtensa"]
                if success:
                    self.assertEqual(deploy.main(args), 0)
                else:
                    with self.assertRaisesRegex(RuntimeError, "APP installation failed" if "install failed:" in response else "did not confirm"):
                        deploy.main(args)

    def test_failed_transfer_cannot_install_stale_package(self):
        import deploy
        abi = aster_board.get("xtensa")["abi"]
        sender = SimpleNamespace(find_port=lambda *a, **kw: (
            "/dev/fake", f"PONG sd=1 product=aster target=xtensa abi={abi}"),
            send_file=lambda *a, **kw: "ERR -5")
        with patch.object(sys, "argv", ["deploy", "test.ast", "--target", "xtensa"]), \
             patch.object(deploy, "check", return_value={"id": "test.hello"}), \
             patch.dict(sys.modules, {"aster_send": sender}), \
             patch.object(deploy, "shell_command") as shell:
            with self.assertRaisesRegex(RuntimeError, "transfer failed"):
                deploy.main()
            shell.assert_not_called()

    def test_wrong_device_cannot_receive_package(self):
        import deploy
        sender = SimpleNamespace(find_port=lambda *a, **kw: ("/dev/fake", "PONG sd=1"))
        with patch.object(sys, "argv", ["deploy", "test.ast", "--target", "xtensa"]), \
             patch.object(deploy, "check", return_value={"id": "test.hello"}), \
             patch.dict(sys.modules, {"aster_send": sender}):
            with self.assertRaisesRegex(ValueError, "not Aster"):
                deploy.main()


if __name__ == "__main__":
    unittest.main()
