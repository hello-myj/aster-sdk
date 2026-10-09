"""Compatibility rules shared by build, package verification and deployment."""
import re


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", value):
        raise ValueError("invalid ABI version: " + repr(value))
    parts = tuple(map(int, value.split(".")))
    return parts + (0,) * (3 - len(parts))


def validate_manifest(manifest, target):
    app_id = manifest.get("id", "")
    if not isinstance(app_id, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,62}", app_id) or ".." in app_id:
        raise ValueError("invalid application identifier")
    if manifest.get("product") != "aster":
        raise ValueError("package is not an Aster application")
    if manifest.get("target_id") != target["id"]:
        raise ValueError("package target does not match selected platform")
    minimum = version(manifest["abi"]["min"])
    built = version(manifest["abi"]["target"])
    device = version(target["abi"])
    if minimum[0] != device[0] or built[0] != device[0] or minimum > device or minimum > built:
        raise ValueError("incompatible application/device ABI")
    missing = set(manifest.get("requires", [])) - set(target["capabilities"])
    if missing:
        raise ValueError("target lacks required capabilities: " + ", ".join(sorted(missing)))
    memory = manifest.get("memory", {})
    initial, maximum, stack = (int(memory.get(k, 0)) for k in ("initial", "max", "stack"))
    # Alignment only. Each board's firmware config is the memory ceiling;
    # packaging does not impose one maximum on every board.
    if initial <= 0 or initial % 65536 or stack <= 0 or stack >= initial:
        raise ValueError("invalid initial memory/stack for target")
    if maximum and (maximum <= 0 or maximum % 65536 or maximum < initial):
        raise ValueError("invalid maximum memory for target")


def validate_app(app, target):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,62}", app.id) or ".." in app.id:
        raise ValueError("app.id must be a safe package identifier (max 63 characters)")
    validate_manifest({"id": app.id, "product": "aster", "target_id": target["id"],
                       "abi": {"min": app.abi_min, "target": app.abi},
                       "requires": app.requires,
                       "memory": {"initial": app.initial_memory, "max": app.max_memory,
                                  "stack": app.stack}}, target)


def validate_device(pong, target, requires=()):
    fields = dict(token.split("=", 1) for token in pong.split() if "=" in token)
    if not pong.startswith("PONG ") or fields.get("product") != "aster":
        raise ValueError("connected device is not Aster firmware")
    if fields.get("target") != target["id"]:
        raise ValueError("connected device target does not match selected platform")
    device, required = version(fields.get("abi", "")), version(target["abi"])
    if device[0] != required[0] or device < required:
        raise ValueError("connected firmware ABI differs from target release")
    if fields.get("sd") != "1":
        raise ValueError("device storage is not mounted")

    import aster_board
    bits = aster_board.API["feature_bits"]
    if requires:
        try:
            caps = int(fields["caps"], 16)
        except (KeyError, ValueError):
            raise ValueError("device did not report runtime capabilities") from None
        missing = [name for name in requires if name not in bits or not caps & (1 << bits[name])]
        if missing:
            raise ValueError("device mode/configuration lacks capabilities: " + ", ".join(missing))
