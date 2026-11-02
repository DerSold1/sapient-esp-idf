/*
 * sapient-esp-idf — SAPIENT (BSI Flex 335 v2.0) for ESP32 / embedded C
 *
 * Thin, allocation-free helper layer on top of nanopb-generated message
 * structs. Covers what an edge node (ASM) needs:
 *   - UUID / ULID generation
 *   - building StatusReport, DetectionReport, Alert, TaskAck
 *   - a simplified Registration builder
 *   - encode / decode and the 4-byte length-prefixed stream framing
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SAPIENT_H
#define SAPIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sapient_msg/bsi_flex_335_v2_0/sapient_message.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAPIENT_ICD_VERSION "BSI_Flex_335_v2.0"
#define SAPIENT_UUID_LEN 37 /* 36 chars + NUL */
#define SAPIENT_ULID_LEN 27 /* 26 chars + NUL */
#define SAPIENT_FRAME_HEADER_LEN 4

#ifndef SAPIENT_MAX_FRAME_LEN
#define SAPIENT_MAX_FRAME_LEN 16384 /* reject frames larger than this */
#endif

/* ------------------------------------------------------------------------ */
/* Short type aliases for the generated nanopb structs                       */
/* ------------------------------------------------------------------------ */
typedef sapient_msg_bsi_flex_335_v2_0_SapientMessage sapient_message_t;
typedef sapient_msg_bsi_flex_335_v2_0_Registration sapient_registration_t;
typedef sapient_msg_bsi_flex_335_v2_0_RegistrationAck sapient_registration_ack_t;
typedef sapient_msg_bsi_flex_335_v2_0_StatusReport sapient_status_report_t;
typedef sapient_msg_bsi_flex_335_v2_0_DetectionReport sapient_detection_report_t;
typedef sapient_msg_bsi_flex_335_v2_0_Task sapient_task_t;
typedef sapient_msg_bsi_flex_335_v2_0_TaskAck sapient_task_ack_t;
typedef sapient_msg_bsi_flex_335_v2_0_Alert sapient_alert_t;
typedef sapient_msg_bsi_flex_335_v2_0_AlertAck sapient_alert_ack_t;
typedef sapient_msg_bsi_flex_335_v2_0_Error sapient_error_msg_t;
typedef sapient_msg_bsi_flex_335_v2_0_Location sapient_location_t;
typedef sapient_msg_bsi_flex_335_v2_0_RangeBearing sapient_range_bearing_t;
typedef sapient_msg_bsi_flex_335_v2_0_Registration_NodeType sapient_node_type_t;

/* Which message is inside a sapient_message_t (msg->which_content) */
typedef enum {
    SAPIENT_CONTENT_NONE = 0,
    SAPIENT_CONTENT_REGISTRATION = sapient_msg_bsi_flex_335_v2_0_SapientMessage_registration_tag,
    SAPIENT_CONTENT_REGISTRATION_ACK = sapient_msg_bsi_flex_335_v2_0_SapientMessage_registration_ack_tag,
    SAPIENT_CONTENT_STATUS_REPORT = sapient_msg_bsi_flex_335_v2_0_SapientMessage_status_report_tag,
    SAPIENT_CONTENT_DETECTION_REPORT = sapient_msg_bsi_flex_335_v2_0_SapientMessage_detection_report_tag,
    SAPIENT_CONTENT_TASK = sapient_msg_bsi_flex_335_v2_0_SapientMessage_task_tag,
    SAPIENT_CONTENT_TASK_ACK = sapient_msg_bsi_flex_335_v2_0_SapientMessage_task_ack_tag,
    SAPIENT_CONTENT_ALERT = sapient_msg_bsi_flex_335_v2_0_SapientMessage_alert_tag,
    SAPIENT_CONTENT_ALERT_ACK = sapient_msg_bsi_flex_335_v2_0_SapientMessage_alert_ack_tag,
    SAPIENT_CONTENT_ERROR = sapient_msg_bsi_flex_335_v2_0_SapientMessage_error_tag,
} sapient_content_t;

typedef enum {
    SAPIENT_OK = 0,
    SAPIENT_ERR_ARG,
    SAPIENT_ERR_ENCODE,
    SAPIENT_ERR_DECODE,
    SAPIENT_ERR_NO_SPACE,
    SAPIENT_ERR_FRAME_TOO_LARGE,
    SAPIENT_ERR_NO_MEM,
} sapient_err_t;

const char *sapient_err_str(sapient_err_t err);
const char *sapient_content_name(pb_size_t which_content);

/* ------------------------------------------------------------------------ */
/* Platform hooks (time + randomness)                                        */
/* Defaults: ESP-IDF -> gettimeofday() + esp_fill_random()                   */
/*           host    -> gettimeofday() + /dev/urandom                        */
/* NOTE: SAPIENT needs real UTC time. On ESP32, start SNTP before sending.   */
/* ------------------------------------------------------------------------ */
typedef uint64_t (*sapient_time_ms_fn)(void);                /* ms since Unix epoch, UTC */
typedef void (*sapient_random_fn)(void *buf, size_t len);    /* cryptographic quality preferred */

void sapient_set_platform(sapient_time_ms_fn time_fn, sapient_random_fn random_fn);
uint64_t sapient_now_ms(void);

/* IDs: out must hold SAPIENT_UUID_LEN / SAPIENT_ULID_LEN bytes */
void sapient_uuid_v4(char *out);
void sapient_ulid(char *out);

/* ------------------------------------------------------------------------ */
/* Building messages                                                         */
/* sapient_message_t is ~6 KB: keep it static or on the heap, NOT on a small */
/* FreeRTOS task stack.                                                      */
/* ------------------------------------------------------------------------ */

/* Zero the message, set timestamp (now) and node_id. destination_id may be NULL. */
void sapient_message_init(sapient_message_t *msg, const char *node_id, const char *destination_id);

/* Select the content type, zero it and fill mandatory IDs (report_id/ULID etc.).
 * Return a pointer to the content struct for further filling. */
sapient_status_report_t *sapient_message_status_report(sapient_message_t *msg,
        sapient_msg_bsi_flex_335_v2_0_StatusReport_System system,
        sapient_msg_bsi_flex_335_v2_0_StatusReport_Info info);

/* object_id: pass NULL to create a fresh ULID (= new object, no tracking) */
sapient_detection_report_t *sapient_message_detection_report(sapient_message_t *msg,
        const char *object_id);

sapient_alert_t *sapient_message_alert(sapient_message_t *msg,
        sapient_msg_bsi_flex_335_v2_0_Alert_AlertType type, const char *description);

sapient_task_ack_t *sapient_message_task_ack(sapient_message_t *msg, const char *task_id,
        sapient_msg_bsi_flex_335_v2_0_TaskAck_TaskStatus status, const char *reason);

/* Location helpers (WGS84, degrees / metres) */
void sapient_location_wgs84(sapient_location_t *loc, double lat_deg, double lon_deg, double alt_m);
void sapient_location_set_error(sapient_location_t *loc, double lat_err_deg, double lon_err_deg, double alt_err_m);

/* Range/bearing relative to the node: degrees / metres, datum = true north */
void sapient_range_bearing_deg_m(sapient_range_bearing_t *rb, double azimuth_deg, double elevation_deg,
        double range_m);

/* Detection helpers */
void sapient_detection_set_location(sapient_detection_report_t *dr, const sapient_location_t *loc);
void sapient_detection_set_range_bearing(sapient_detection_report_t *dr, const sapient_range_bearing_t *rb);
bool sapient_detection_add_class(sapient_detection_report_t *dr, const char *type, float confidence);
bool sapient_detection_add_track_info(sapient_detection_report_t *dr, const char *type, const char *value);
void sapient_detection_set_enu_velocity(sapient_detection_report_t *dr, double east_ms, double north_ms,
        double up_ms);

/* Status helpers */
void sapient_status_set_location(sapient_status_report_t *sr, const sapient_location_t *loc);
bool sapient_status_add(sapient_status_report_t *sr,
        sapient_msg_bsi_flex_335_v2_0_StatusReport_StatusLevel level,
        sapient_msg_bsi_flex_335_v2_0_StatusReport_StatusType type, const char *value);
void sapient_status_set_power(sapient_status_report_t *sr,
        sapient_msg_bsi_flex_335_v2_0_StatusReport_PowerSource source,
        sapient_msg_bsi_flex_335_v2_0_StatusReport_PowerStatus status, int32_t level_percent);

/* Bounded string copy into a nanopb char array: SAPIENT_STRCPY(dst_array, src) */
void sapient_strlcpy(char *dst, const char *src, size_t dst_size);
#define SAPIENT_STRCPY(dst, src) sapient_strlcpy((dst), (src), sizeof(dst))

/* ------------------------------------------------------------------------ */
/* Encoding / decoding                                                       */
/* ------------------------------------------------------------------------ */

/* Raw protobuf payload (no length prefix) */
sapient_err_t sapient_encode(const sapient_message_t *msg, uint8_t *buf, size_t cap, size_t *out_len);
sapient_err_t sapient_decode(const uint8_t *buf, size_t len, sapient_message_t *msg);

/* Framed for TCP: 4-byte little-endian payload length + payload */
sapient_err_t sapient_encode_frame(const sapient_message_t *msg, uint8_t *buf, size_t cap, size_t *out_len);

/* Stream reassembly: feed bytes as they arrive from the socket. For every
 * complete frame `cb` is called with the payload (without header). */
typedef void (*sapient_frame_cb)(const uint8_t *payload, size_t len, void *ctx);

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} sapient_framer_t;

void sapient_framer_init(sapient_framer_t *f, uint8_t *buf, size_t cap);
void sapient_framer_reset(sapient_framer_t *f);
/* Returns SAPIENT_ERR_FRAME_TOO_LARGE if a frame exceeds the buffer or
 * SAPIENT_MAX_FRAME_LEN; the framer is reset and you should reconnect. */
sapient_err_t sapient_framer_feed(sapient_framer_t *f, const uint8_t *data, size_t len, sapient_frame_cb cb,
        void *ctx);

/* ------------------------------------------------------------------------ */
/* Registration (simplified)                                                 */
/* Covers a typical single-mode sensor node. For anything more exotic, fill  */
/* sapient_registration_t yourself and provide nanopb encode callbacks.      */
/* ------------------------------------------------------------------------ */
typedef struct {
    const char *category; /* e.g. "Acoustic" */
    const char *type;     /* e.g. "MaxRange" */
    const char *value;    /* e.g. "300" (optional) */
    const char *units;    /* e.g. "m" (optional) */
} sapient_capability_t;

typedef enum {
    SAPIENT_REPORT_LOCATION_WGS84 = 0,  /* detections carry lat/lon/alt */
    SAPIENT_REPORT_RANGE_BEARING = 1,   /* detections carry az/el/range (deg, m) */
} sapient_report_location_t;

typedef struct {
    /* node */
    sapient_node_type_t node_type;       /* e.g. ..._Registration_NodeType_NODE_TYPE_ACOUSTIC */
    const char *node_sub_type;           /* optional, free text */
    const char *name;                    /* e.g. "DTU acoustic node" */
    const char *short_name;              /* e.g. "ACU-1" */

    /* configuration data (manufacturer + model are mandatory) */
    const char *manufacturer;
    const char *model;
    const char *serial_number;           /* optional */
    const char *hardware_version;        /* optional */
    const char *software_version;        /* optional */

    /* heartbeat */
    float status_interval_s;             /* StatusReport period */
    bool reports_node_location;          /* StatusReport includes node_location (WGS84) */

    /* capabilities (mandatory, at least one recommended) */
    const sapient_capability_t *capabilities;
    size_t capabilities_count;

    /* single default mode */
    const char *mode_name;               /* default "Default" */
    sapient_report_location_t detection_location;
    sapient_msg_bsi_flex_335_v2_0_Registration_TrackingType tracking_type;  /* 0 -> NONE */
    sapient_msg_bsi_flex_335_v2_0_Registration_ScanType scan_type;          /* 0 -> FIXED */
    const char *const *classes;          /* e.g. {"UAV", "Bird", "Unknown"} */
    size_t classes_count;
    float maximum_latency_s;             /* 0 -> omitted */
} sapient_registration_cfg_t;

/* Encodes a complete, framed SapientMessage{Registration}. Uses ~5 KB of heap
 * temporarily (malloc/free) — call it once at start-up. */
sapient_err_t sapient_encode_registration_frame(const char *node_id, const sapient_registration_cfg_t *cfg,
        uint8_t *buf, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* SAPIENT_H */
