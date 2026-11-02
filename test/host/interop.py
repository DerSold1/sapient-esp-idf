"""Interop test: C (nanopb) <-> Python (google protobuf), same Dstl protos.

    python3 interop.py check <frames_from_c.bin>
    python3 interop.py make  <frames_for_c.bin>
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from sapient_frames import SapientMessage, decode, encode_frame, iter_frames  # noqa: E402
from sapient_msg.bsi_flex_335_v2_0 import range_bearing_pb2, registration_pb2, task_pb2  # noqa: E402

ULID = re.compile(r"^[0-9A-HJKMNP-TV-Z]{26}$")
UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")


def check(path):
    data = open(path, "rb").read()
    msgs = [decode(p) for p in iter_frames(data)]
    kinds = [m.WhichOneof("content") for m in msgs]
    assert kinds == ["registration", "status_report", "detection_report", "alert", "task_ack"], kinds
    now = time.time()
    for m in msgs:
        assert UUID.match(m.node_id), m.node_id
        assert abs(m.timestamp.seconds - now) < 60, "timestamp not current"

    reg = msgs[0].registration
    assert reg.icd_version == "BSI_Flex_335_v2.0"
    assert reg.node_definition[0].node_type == registration_pb2.Registration.NODE_TYPE_ACOUSTIC
    assert [c.type for c in reg.capabilities] == ["MaxRange", "Microphones"]
    assert reg.status_definition.status_interval.value == 10.0
    assert reg.config_data[0].manufacturer == "DTU community"
    mode = reg.mode_definition[0]
    assert mode.mode_name == "Default" and mode.HasField("task")
    classes = [c.type for c in mode.detection_definition[0].detection_class_definition[0].class_definition]
    assert classes == ["UAV", "Bird", "Unknown"], classes

    sr = msgs[1].status_report
    assert ULID.match(sr.report_id)
    assert abs(sr.node_location.y - 48.8008) < 1e-9 and sr.power.level == 87

    dr = msgs[2].detection_report
    assert ULID.match(dr.report_id) and ULID.match(dr.object_id)
    assert dr.WhichOneof("location_oneof") == "range_bearing"
    assert dr.range_bearing.azimuth == 123.5
    assert [(c.type, round(c.confidence, 2)) for c in dr.classification] == [("UAV", 0.82), ("Bird", 0.1)]

    assert ULID.match(msgs[3].alert.alert_id)
    assert msgs[4].task_ack.task_status == 1
    print(f"interop check OK ({len(msgs)} messages, {len(data)} bytes)")


def make(path):
    out = b""
    ack = SapientMessage()
    ack.timestamp.GetCurrentTime()
    ack.node_id = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
    ack.destination_id = "1f0e2d3c-4b5a-4697-8877-665544332211"
    ack.registration_ack.acceptance = True
    out += encode_frame(ack)

    t = SapientMessage()
    t.timestamp.GetCurrentTime()
    t.node_id = ack.node_id
    t.destination_id = ack.destination_id
    t.task.task_id = "01JBQZ5V3K8N2W0R7T4Y6M9P1C"
    t.task.control = task_pb2.Task.CONTROL_START
    t.task.command.mode_change = "Default"
    r = t.task.region.add()
    r.type = task_pb2.Task.REGION_TYPE_IGNORE
    r.region_id = "01JBQZ5V3K8N2W0R7T4Y6M9P1D"
    r.region_name = "Treeline"
    rb = r.region_area.range_bearing
    rb.azimuth, rb.elevation, rb.horizontal_extent, rb.vertical_extent = 90, 0, 20, 10
    rb.coordinate_system = range_bearing_pb2.RANGE_BEARING_COORDINATE_SYSTEM_DEGREES_M
    rb.datum = range_bearing_pb2.RANGE_BEARING_DATUM_TRUE
    out += encode_frame(t)
    open(path, "wb").write(out)
    print(f"wrote {len(out)} bytes")


if __name__ == "__main__":
    {"check": check, "make": make}[sys.argv[1]](sys.argv[2])
