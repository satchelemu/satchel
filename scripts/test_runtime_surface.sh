#!/bin/zsh
set -euo pipefail
repo_root="${0:A:h:h}"
python3 "$repo_root/Tools/RuntimeSurface/build.py" "$@"
