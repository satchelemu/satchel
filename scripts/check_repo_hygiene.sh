#!/bin/sh
# Repository hygiene checks, run in CI and before publishing.
# 1. No em dashes in text we wrote (vendored code, third-party notices and the
#    archived history are exempt).
# 2. No leftover pre-rename project names outside the rename tool and the archive.
# 3. No console keys, game dumps, signing identities or prepared game code tracked.
# 4. No game title ID in host or importer code (scripts/check_architecture.py).
# Exit status is non-zero when any check fails; the offending lines are printed.
set -u
cd "$(dirname "$0")/.."
status=0

exempt=':!Validation/history/*
:!**/upstream/**
:!**/Vendor/**
:!**/third_party/**
:!**/licenses/**
:!App/Resources/Notices/*
:!NativeHost/services/RYUJINX-LICENSE.txt
:!Tools/VulkanBackend/Notices/*
:!Tools/ShaderBridge/LICENSE'

# shellcheck disable=SC2086
if git grep -I -n -e '—' -- . $exempt ':!scripts/check_repo_hygiene.sh'; then
  echo "FAIL: em dashes found (use a comma, colon, period or parentheses)." >&2
  status=1
fi

# shellcheck disable=SC2086
if git grep -I -n -i -E -e 'pocket ?lab|pocketlab' -- . $exempt ':!Tools/Rename/*' ':!scripts/check_repo_hygiene.sh'; then
  echo "FAIL: pre-rename project name found." >&2
  status=1
fi

# Maintainer-private patterns (team IDs, names, addresses) live in the untracked file
# local/forbidden-patterns, one extended regex per line, so the check itself reveals nothing.
if [ -f local/forbidden-patterns ]; then
  # shellcheck disable=SC2086
  if git grep -I -n -i -E -f local/forbidden-patterns -- . $exempt; then
    echo "FAIL: a pattern from local/forbidden-patterns is in the tree." >&2
    status=1
  fi
fi

if git ls-files | grep -E -i '(^|/)(prod|title)\.keys$|\.(p12|pfx|mobileprovision|xci|xcz|nsp|nsz|nca|ncz|ipa|dylib|metallib)$'; then
  echo "FAIL: a key, dump, identity or prepared binary is tracked." >&2
  status=1
fi

if ! python3 -B scripts/check_architecture.py; then
  status=1
fi
if ! python3 -B scripts/check_architecture.py --self-test >/dev/null 2>&1; then
  echo "FAIL: the architecture check's own pattern tests fail (python3 scripts/check_architecture.py --self-test)." >&2
  status=1
fi

if [ "$status" -eq 0 ]; then echo "Repository hygiene checks passed."; fi
exit "$status"
