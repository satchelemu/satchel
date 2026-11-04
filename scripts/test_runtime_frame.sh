#!/bin/zsh
set -euo pipefail
cd "${0:A:h:h}"
python3 Tools/RuntimeFrame/build.py --output "${1:?Pass a fresh output directory outside the repository}"
