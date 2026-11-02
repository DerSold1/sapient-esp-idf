#!/usr/bin/env python3
"""Minimal SAPIENT fusion node for bench testing edge nodes.

- listens for TCP connections from edge nodes (ASMs)
- decodes every framed SapientMessage and prints it
- answers Registration with RegistrationAck(acceptance=true)
- optionally sends a Task after registration and logs the TaskAck

This is a test tool, NOT a fusion engine and NOT a replacement for the
Dstl SAPIENT Test Harness (use that for compliance testing).

    python3 tools/fusion_stub.py --port 12000
    python3 tools/fusion_stub.py --port 12000 --task-mode Default --json

Requires: pip install protobuf
"""
import argparse
import asyncio
import datetime as dt
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from google.protobuf.json_format import MessageToJson  # noqa: E402
from sapient_frames import HEADER, SapientMessage, decode, encode_frame  # noqa: E402
from sapient_msg.bsi_flex_335_v2_0 import task_pb2  # noqa: E402

FUSION_ID = str(uuid.uuid4())


def ulid() -> str:
    enc = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
    v = (int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000) << 80) | int.from_bytes(os.urandom(10), "big")
    return "".join(enc[(v >> (5 * i)) & 31] for i in reversed(range(26)))


def envelope(dest: str) -> SapientMessage:
    m = SapientMessage()
    m.timestamp.GetCurrentTime()
    m.node_id = FUSION_ID
    m.destination_id = dest
    return m


def summary(m: SapientMessage) -> str:
    kind = m.WhichOneof("content")
    c = getattr(m, kind) if kind else None
    if kind == "registration":
        nt = [d.node_type for d in c.node_definition]
        return f"{c.name!r} icd={c.icd_version} node_types={nt} modes={[x.mode_name for x in c.mode_definition]}"
    if kind == "status_report":
        loc = f" loc=({c.node_location.y:.5f},{c.node_location.x:.5f})" if c.HasField("node_location") else ""
        return f"system={c.system} info={c.info} mode={c.mode!r}{loc}"
    if kind == "detection_report":
        where = c.WhichOneof("location_oneof")
        pos = getattr(c, where) if where else None
        if where == "range_bearing":
            pos_s = f"az={pos.azimuth:.1f} el={pos.elevation:.1f}" + (f" r={pos.range:.0f}" if pos.HasField("range") else "")
        elif where == "location":
            pos_s = f"lat={pos.y:.6f} lon={pos.x:.6f} alt={pos.z:.0f}"
        else:
            pos_s = "no position"
        cls = ", ".join(f"{k.type}:{k.confidence:.2f}" for k in c.classification)
        return f"object={c.object_id} {pos_s} [{cls}]"
    if kind == "task_ack":
        return f"task={c.task_id} status={c.task_status} {list(c.reason)}"
    if kind == "alert":
        return f"type={c.alert_type} {c.description!r}"
    return ""


async def handle(reader, writer, args):
    peer = writer.get_extra_info("peername")
    print(f"[+] connection from {peer}")
    buf = b""
    try:
        while True:
            chunk = await reader.read(4096)
            if not chunk:
                break
            buf += chunk
            while len(buf) >= HEADER.size:
                (n,) = HEADER.unpack_from(buf)
                if n > 1 << 20:
                    print(f"[!] absurd frame length {n}, closing")
                    return
                if len(buf) < HEADER.size + n:
                    break
                payload, buf = buf[HEADER.size:HEADER.size + n], buf[HEADER.size + n:]
                msg = decode(payload)
                kind = msg.WhichOneof("content")
                ts = msg.timestamp.ToDatetime().strftime("%H:%M:%S.%f")[:-3]
                print(f"<- {ts} {msg.node_id[:8]} {kind:16s} {summary(msg)}")
                if args.json:
                    print(MessageToJson(msg, indent=2))

                if kind == "registration":
                    ack = envelope(msg.node_id)
                    ack.registration_ack.acceptance = True
                    writer.write(encode_frame(ack))
                    print(f"-> RegistrationAck to {msg.node_id[:8]}")
                    if args.task_mode:
                        t = envelope(msg.node_id)
                        t.task.task_id = ulid()
                        t.task.control = task_pb2.Task.CONTROL_START
                        t.task.command.mode_change = args.task_mode
                        writer.write(encode_frame(t))
                        print(f"-> Task {t.task.task_id} mode_change={args.task_mode!r}")
                    await writer.drain()
    finally:
        print(f"[-] {peer} disconnected")
        writer.close()


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=12000)
    ap.add_argument("--task-mode", help="send a Task with this mode_change after registration")
    ap.add_argument("--json", action="store_true", help="dump every message as JSON")
    args = ap.parse_args()
    server = await asyncio.start_server(lambda r, w: handle(r, w, args), args.host, args.port)
    print(f"SAPIENT fusion stub {FUSION_ID} listening on {args.host}:{args.port}")
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
