#!/usr/bin/env bash
# Everything that must be true before a push, in the order that makes it true.
#
# The order matters and is the reason this file exists: adding a test changes
# the test-count badge, so `badges.py` has to run *before* the badge drift
# check, not after. Doing it by hand got that wrong once and pushed a red
# tree.
#
#   ./tools/preflight.sh
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> regenerating badges (test counts may have changed)"
python3 tools/badges.py

echo "==> gateway + tool tests"
python3 -m pytest

echo "==> firmware core tests"
( cd firmware && pio test -e native )

echo "==> firmware builds"
( cd firmware && pio run -e cardputer )

echo "==> mutation probes"
python3 tools/mutate.py gateway
python3 tools/mutate.py firmware
# The Arduino shell has no host tests; its invariants are pinned against the
# source, and those pins deserve the same distrust as any other test.
python3 tools/mutate.py shell

echo "==> IR table matches its source"
python3 tools/gen_ir_table.py
git diff --quiet firmware/lib/core/ir_teufel.h \
  || { echo "IR table drifted; commit the regenerated header"; exit 1; }

echo "==> control tables match their source"
python3 tools/gen_controls.py --check

echo "==> badges are current"
python3 tools/badges.py --check

echo "==> no secrets anywhere (tree and built binaries)"
python3 tools/scan_secrets.py --with-local

echo
echo "preflight passed — safe to push"
