#!/usr/bin/env bash
set -euo pipefail
build=${1:?build directory required}
python3 - "$build" <<'PY'
import json,sys
from pathlib import Path
commands=json.loads((Path(sys.argv[1])/'compile_commands.json').read_text())
owned=[c for c in commands if '/src/core/' in c['file'] or '/src/backend/macos/' in c['file'] or '/test/macos/' in c['file']]
assert owned and all('-fsanitize=address,undefined' in c['command'] for c in owned)
assert not any(c['file'].endswith('/rt_alloc.cpp') for c in commands)
PY
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/witness.cpp" <<'CPP'
int main(int argc,char**) {
    volatile int* values=new int[4];
    values[argc+7]=42;
    delete[] values;
}
CPP
"${CXX:-clang++}" -g -fsanitize=address,undefined "$scratch/witness.cpp" -o "$scratch/witness"
if ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$scratch/witness" > "$build/asan-witness.log" 2>&1; then
    echo "ASan failed to reject a heap buffer overflow" >&2
    exit 1
fi
grep -q 'AddressSanitizer: heap-buffer-overflow' "$build/asan-witness.log"
