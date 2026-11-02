#!/usr/bin/env bash
# Build and run the host tests (C selftest + C<->Python interop).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/build"
cmake -S "$HERE" -B "$B" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" >/dev/null
cmake --build "$B" -j >/dev/null
"$B/test_sapient" selftest
"$B/test_sapient" encode "$B/from_c.bin"
python3 "$HERE/interop.py" check "$B/from_c.bin"
python3 "$HERE/interop.py" make "$B/from_py.bin"
"$B/test_sapient" decode "$B/from_py.bin"
# Optional independent ICD validation (needs a venv with sapient-pydantic)
ICD_PY="${ICD_PY:-$HERE/.venv/bin/python}"
if [ -x "$ICD_PY" ] && "$ICD_PY" -c "import sapient_msg_pydantic" 2>/dev/null; then
    "$ICD_PY" "$HERE/icd_validate.py" "$B/from_c.bin"
    "$ICD_PY" "$HERE/icd_validate.py" "$B/from_py.bin"
else
    echo "(skipping ICD validation: create $HERE/.venv with 'pip install sapient-pydantic')"
fi
echo "ALL HOST TESTS PASSED"
