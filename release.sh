#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python=${ASTER_RELEASE_PYTHON:-}
if [ -z "$python" ]; then
    if [ -x "$root/.venv-linux/bin/python" ]; then python="$root/.venv-linux/bin/python"; else python=python3; fi
fi
exec "$python" "$root/tools/package_portable.py" "$@"
