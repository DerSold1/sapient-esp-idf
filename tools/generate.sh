#!/usr/bin/env bash
# Regenerate the nanopb C sources (generated/) and the Python bindings
# (tools/sapient_pb/) from the Dstl SAPIENT proto files in proto/.
#
# Requires: pip install nanopb==0.4.9.1 grpcio-tools
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WKT="$(python3 -c 'import grpc_tools,os;print(os.path.join(os.path.dirname(grpc_tools.__file__),"_proto"))')"
cd "$ROOT/proto"
FILES=$(find sapient_msg -name '*.proto' | sort)

# C (nanopb)
rm -rf "$ROOT/generated" && mkdir -p "$ROOT/generated"
python3 -m nanopb.generator.nanopb_generator -q \
  -I "$ROOT/proto" -I "$WKT" \
  -f "$ROOT/proto/sapient.options" \
  -D "$ROOT/generated" \
  $FILES google/protobuf/timestamp.proto

# Python (google protobuf), used by tools/fusion_stub.py and the host tests
rm -rf "$ROOT/tools/sapient_pb" && mkdir -p "$ROOT/tools/sapient_pb"
python3 -m grpc_tools.protoc -I "$ROOT/proto" -I "$WKT" \
  --python_out="$ROOT/tools/sapient_pb" $FILES
find "$ROOT/tools/sapient_pb" -type d -exec touch {}/__init__.py \;

echo "Generated C into generated/ and Python into tools/sapient_pb/"
