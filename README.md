# sapient-esp-idf

**SAPIENT (BSI Flex 335 v2.0) for ESP32 and other microcontrollers, in plain C.**

SAPIENT is the open interface standard (UK MOD / Dstl, published as BSI Flex 335) for connecting
autonomous sensor nodes to fusion nodes, used for counter-UAS among other things. Python, Rust and Java
bindings already exist. This repo adds what was missing for **cheap embedded sensor nodes**: a
ready-to-use ESP-IDF component that lets an ESP32 talk SAPIENT without writing protobuf plumbing yourself.



## What's in it

| | |
|---|---|
| `include/sapient.h` | The whole public API |
| `src/` | Helpers: UUID/ULID, message builders, encode/decode, stream framing, Registration builder |
| `generated/` | nanopb C code generated from the official Dstl `.proto` files (BSI Flex 335 **v2.0**) |
| `proto/` | Unmodified copy of [dstl/SAPIENT-Proto-Files](https://github.com/dstl/SAPIENT-Proto-Files) + `sapient.options` (buffer sizes) |
| `third_party/nanopb/` | nanopb 0.4.9.1 runtime (vendored, no external dependency) |
| `examples/sensor_node/` | ESP-IDF app: Wi-Fi, SNTP, NVS node ID, Registration, heartbeat, detections, TaskAck |
| `tools/fusion_stub.py` | Minimal fusion node for bench tests (acks registrations, prints everything) |
| `test/host/` | Host tests: selftest, C ↔ Python interop, independent ICD validation, TCP node |

Properties:

- **No malloc at runtime.** All runtime messages (StatusReport, DetectionReport, Alert, Task, TaskAck, …)
  are static structs. Only the one-off Registration uses about 5 KB of heap for a moment.
- **Wire compatible** with the other SAPIENT bindings: protobuf payload behind a 4-byte little-endian
  length prefix, the same framing used by `sapient-rs` / `sapient-py`.
- Mandatory IDs handled for you: node `UUID`, `ULID`s for report/object/alert IDs, UTC timestamps.
- Works outside ESP-IDF too (plain CMake, any C99 compiler), e.g. STM32, RP2040, Linux.

## Quick start (ESP-IDF ≥ 5.1)

```bash
git clone https://github.com/DerSold1/sapient-esp-idf
cd sapient-esp-idf/examples/sensor_node
idf.py set-target esp32s3
idf.py menuconfig      # Example Connection Configuration -> Wi-Fi; SAPIENT sensor node -> fusion host/port
idf.py build flash monitor
```

On your PC:

```bash
pip install protobuf
python3 tools/fusion_stub.py --port 12000 --task-mode Default
```

Output of the stub with the PC test node (`test/host/build/tcp_node`):

```
<- 12:20:16.247 343629f5 registration     'host test node' icd=BSI_Flex_335_v2.0 node_types=[6] modes=['Default']
-> RegistrationAck to 343629f5
-> Task 01M6E3807S08SVV7PBB58FT09C mode_change='Default'
<- 12:20:16.249 343629f5 status_report    system=1 info=2 mode=''
<- 12:20:16.249 343629f5 detection_report object=01M6E3807SPXCBEY8NEQNZVPG2 az=136.0 el=10.0 [UAV:0.70]
<- 12:20:16.249 343629f5 task_ack         task=01M6E3807S08SVV7PBB58FT09C status=1 []
<- 12:20:17.078 343629f5 status_report    system=1 info=2 mode=''
```

### Use it in your own project

Copy or submodule this repo into your project's `components/` folder, or point `EXTRA_COMPONENT_DIRS` at it.

## API in 30 seconds

```c
#include "sapient.h"
#define P(x) sapient_msg_bsi_flex_335_v2_0_##x

static sapient_message_t msg;     // ~6 KB: static or heap, NOT on a small task stack
static uint8_t buf[2048];
size_t len;

// Detection: bearing-only (acoustic) with class confidence
sapient_message_init(&msg, node_id, NULL);                 // timestamp + node_id
sapient_detection_report_t *dr = sapient_message_detection_report(&msg, NULL); // new ULIDs
sapient_range_bearing_t rb;
sapient_range_bearing_deg_m(&rb, /*az*/ 123.5, /*el*/ 12.0, /*range*/ 0);
sapient_detection_set_range_bearing(dr, &rb);
sapient_detection_add_class(dr, "UAV", 0.82f);
sapient_encode_frame(&msg, buf, sizeof(buf), &len);        // -> send(sock, buf, len, 0)

// Receiving: feed whatever recv() returns, get one callback per complete message
sapient_framer_t fr;
sapient_framer_init(&fr, rx_buf, sizeof(rx_buf));
sapient_framer_feed(&fr, chunk, n, on_frame, ctx);         // on_frame -> sapient_decode()
```

Registration for a typical single-mode sensor:

```c
static const sapient_capability_t caps[] = {{"Acoustic", "MaxRange", "300", "m"}};
static const char *const classes[] = {"UAV", "Bird", "Unknown"};
sapient_registration_cfg_t cfg = {
    .node_type = P(Registration_NodeType_NODE_TYPE_ACOUSTIC),
    .name = "DTU acoustic node", .short_name = "ACU-1",
    .manufacturer = "DTU community", .model = "VolAnti-ESP32S3",
    .status_interval_s = 10, .reports_node_location = true,
    .capabilities = caps, .capabilities_count = 1,
    .detection_location = SAPIENT_REPORT_RANGE_BEARING,
    .classes = classes, .classes_count = 3,
};
sapient_encode_registration_frame(node_id, &cfg, buf, sizeof(buf), &len);
```

The full generated structs are always available if you need fields the helpers don't cover;
see `generated/sapient_msg/bsi_flex_335_v2_0/*.pb.h`.

## Limits (static buffer sizes)

Defined in `proto/sapient.options`. Defaults: strings 48 chars (free text 128), 4 classes per detection,
8 points per polygon, 2 regions per Task. Recursive sub-class trees and filter definitions are callback
fields: they are skipped on decode unless you register a callback. If you change the options, run
`tools/generate.sh` (needs `pip install nanopb==0.4.9.1 grpcio-tools`).

## Testing

```bash
./test/host/run.sh
```

This runs the C selftest (IDs, framing, round trip, error paths) under AddressSanitizer/UBSan, encodes
all message types in C and decodes them with Google's Python protobuf (and the other way round). If
`test/host/.venv` contains [`sapient-pydantic`](https://pypi.org/project/sapient-pydantic/), every
message is also checked against its ICD rules (mandatory fields, UUID/ULID formats).

`test/host/build/tcp_node <host> <port>` is a PC version of the sensor node, useful against the fusion stub
or a real SAPIENT middleware.

**For compliance testing use the official Dstl SAPIENT Test Harness.** The tests here are a sanity
check, not a certification.

## Status

- ✅ BSI Flex 335 v2.0 messages, encode/decode, framing, helpers
- ✅ Host tests incl. independent ICD validation
- ✅ ESP-IDF example builds without warnings (ESP-IDF v5.3.2, esp32 + esp32s3; also checked in CI)
- ⬜ Not yet tested against the Dstl Test Harness or a commercial fusion node. Reports welcome!
- ⬜ TLS transport, v1.0 support

## Licence

Apache-2.0, see `LICENSE` and `NOTICE`. The `.proto` files are Dstl Crown Copyright, Apache-2.0.
nanopb is zlib licensed. Not affiliated with Dstl, UK MOD or BSI.
