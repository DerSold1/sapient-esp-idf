"""Small helpers shared by fusion_stub.py and the host tests.

Uses the Python protobuf bindings generated into tools/sapient_pb/ by
tools/generate.sh (same Dstl proto files as the C side).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "sapient_pb"))

from sapient_msg.bsi_flex_335_v2_0 import sapient_message_pb2  # noqa: E402

SapientMessage = sapient_message_pb2.SapientMessage
HEADER = struct.Struct("<I")  # 4-byte little-endian payload length


def encode_frame(msg) -> bytes:
    payload = msg.SerializeToString()
    return HEADER.pack(len(payload)) + payload


def iter_frames(data: bytes):
    """Yield complete payloads from a byte string; returns the unconsumed rest via StopIteration.value."""
    off = 0
    while len(data) - off >= HEADER.size:
        (n,) = HEADER.unpack_from(data, off)
        if len(data) - off - HEADER.size < n:
            break
        yield data[off + HEADER.size: off + HEADER.size + n]
        off += HEADER.size + n
    return data[off:]


def decode(payload: bytes):
    msg = SapientMessage()
    msg.ParseFromString(payload)
    return msg
