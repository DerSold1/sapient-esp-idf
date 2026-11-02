"""Independent ICD check of framed SAPIENT messages using sapient-pydantic
(https://pypi.org/project/sapient-pydantic/), which validates mandatory
fields and UUID/ULID formats per BSI Flex 335 v2.0.

Run in a separate environment (it ships its own sapient_msg package):
    python3 -m venv .venv && .venv/bin/pip install sapient-pydantic
    .venv/bin/python icd_validate.py frames.bin
"""
import struct
import sys

from sapient_msg.bsi_flex_335_v2_0.sapient_message_pb2 import SapientMessage
from sapient_msg_pydantic.bsi_flex_335_v2_0.sapient_message import SapientMessage as Validated

data = open(sys.argv[1], "rb").read()
off, failed = 0, 0
while off < len(data):
    (n,) = struct.unpack_from("<I", data, off)
    msg = SapientMessage()
    msg.ParseFromString(data[off + 4: off + 4 + n])
    off += 4 + n
    kind = msg.WhichOneof("content")
    try:
        Validated.from_pb2(msg)
        print(f"ICD OK    {kind}")
    except Exception as e:  # noqa: BLE001
        failed += 1
        print(f"ICD FAIL  {kind}: {e}")
sys.exit(1 if failed else 0)
