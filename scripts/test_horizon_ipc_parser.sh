#!/bin/zsh
set -eu
task_root="${0:A:h:h}"
python3 "$task_root/Tools/HorizonIPC/build.py"
