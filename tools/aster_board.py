"""Public target release metadata. Contains no pins, DTS, flash or RTOS build rules."""
import json
import os
from pathlib import Path
from runtime_paths import sdk_root

ROOT = sdk_root()
API = json.loads((ROOT / "contract/api.json").read_text())
AOT_BACKENDS = API["aot_backends"]
ORIENTATIONS = {"portrait", "landscape"}


def targets():
    return {p.stem: json.loads(p.read_text()) for p in (ROOT / "targets").glob("*.json")}


def get(name=None):
    name = name or os.environ.get("ASTER_TARGET")
    all_targets = targets()
    if name not in all_targets:
        raise SystemExit("Select --target: " + ", ".join(sorted(all_targets)))
    target = all_targets[name]
    target["model"] = name
    target["console"] = os.environ.get("ASTER_CONSOLE", target["deployment"].get("console", ""))
    target["data"] = {"glob": os.environ.get("ASTER_CDC", target["deployment"].get("data_glob", ""))}
    return target


def pack_key_for(backend):
    if backend not in AOT_BACKENDS:
        raise SystemExit("Unsupported AOT backend: " + str(backend))
    return AOT_BACKENDS[backend]


def data_glob(name=None):
    return get(name)["data"]["glob"]
