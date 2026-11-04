#!/bin/zsh
set -euo pipefail
TASK_ROOT="${0:A:h:h}"
python3 "$TASK_ROOT/Tools/MaxwellShaderCompiler/build.py" --platform host "$@"
