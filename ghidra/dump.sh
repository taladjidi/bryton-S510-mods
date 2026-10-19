#!/bin/sh
# Read-only helper: run scripts/Dump.java on a Ghidra project without opening the GUI (close the GUI first).
#   GHIDRA_HOME=/path/to/ghidra PROJECT_DIR=/path/to/projdir PROJECT=Name PROGRAM=IMG.raw ./dump.sh out.txt d:0xd01aaec8 ...
H=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
"$H/support/analyzeHeadless" "$PROJECT_DIR" "$PROJECT" -process "${PROGRAM:-IMG.raw}" -noanalysis -readOnly \
  -scriptPath "$(dirname "$0")/scripts" -postScript Dump.java "$@" > "$1.log" 2>&1
grep ERROR "$1.log" | head -3
