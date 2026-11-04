#!/bin/zsh
set -eu
ROOT=${0:A:h:h}
MIGRATION_OUT=${SATCHEL_OUT:-$HOME/SatchelBuild/oneapp}
mkdir -p "$MIGRATION_OUT"
python3 -B -c 'import runpy,sys; runpy.run_path(sys.argv[1])["wait"]()' "$ROOT/NativeApp/build_support.py"
nice -n 10 xcrun swiftc -O -num-threads 2 "$ROOT/NativeMigration/MigrationStore.swift" "$ROOT/NativeMigration/Tests.swift" -o "$MIGRATION_OUT/migration-tests"
nice -n 10 /usr/bin/time -l "$MIGRATION_OUT/migration-tests" "$MIGRATION_OUT/synthetic-$(date +%Y%m%d-%H%M%S)"
# Again under a base path through /var, a symbolic link, as on iOS (/var/mobile/Containers):
# enumerators report /private/var there, and clonefile refuses a path through the link.
VAR_TMP=$(getconf DARWIN_USER_TEMP_DIR); VAR_TMP=${VAR_TMP#/private}
VAR_RUN="${VAR_TMP}satchel-migration-$(date +%Y%m%d-%H%M%S)"
nice -n 10 "$MIGRATION_OUT/migration-tests" "$VAR_RUN"
rm -rf "$VAR_RUN"
