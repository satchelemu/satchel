#!/bin/zsh
set -euo pipefail
root="${0:A:h:h}"
exec python3 "$root/Tools/VulkanBackend/build_flat.py" "$@"
