#!/bin/zsh
set -euo pipefail
root="${0:A:h:h}"
exec python3 "$root/Tools/CapturedFlatCompiler/build_host_facts.py" "$@"
