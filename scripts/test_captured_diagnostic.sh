#!/bin/zsh
set -euo pipefail
repo_root="${0:A:h:h}"
exec python3 "$repo_root/Tools/GuestGraphics/build_captured_diagnostic.py" "$@"
