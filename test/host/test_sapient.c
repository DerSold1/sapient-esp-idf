/* Host test for sapient-esp-idf.
 *
 *   test_sapient encode <out.bin>   write framed Registration/Status/Detection/Alert/TaskAck
 *   test_sapient decode <in.bin>    decode framed stream (e.g. Task, RegistrationAck from Python)
 *   test_sapient selftest           unit checks (IDs, framing, round trip)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sapient.h"

#define P(x) sapient_msg_bsi_flex_335_v2_0_##x
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static const char *NODE_ID = "1f0e2d3c-4b5a-4697-8877-665544332211";
static sapient_message_t g_msg; /* ~6 KB, keep off the stack */
static uint8_t g_buf[8192];

static size_t write_frame(FILE *f, const sapient_message_t *m)
{
    size_t n = 0;
    sapient_err_t err = sapient_encode_frame(m, g_buf, sizeof(g_buf), &n);
    if (err != SAPIENT_OK) {
        fprintf(stderr, "encode failed: %s\n", sapient_err_str(err));
        exit(1);
    }
    fwrite(g_buf, 1, n, f);
    return n;
}

static int cmd_encode(const char *path)
{
    FILE *f = fopen(path, "wb");
    CHECK(f);

    /* 1. Registration */
    static const sapient_capability_t caps[] = {
        {"Acoustic", "MaxRange", "300", "m"},
        {"Acoustic", "Microphones", "4", NULL},
    };
    static const char *const classes[] = {"UAV", "Bird", "Unknown"};
    sapient_registration_cfg_t cfg = {
        .node_type = P(Registration_NodeType_NODE_TYPE_ACOUSTIC),
        .node_sub_type = "VolAnti",
        .name = "DTU acoustic node",
        .short_name = "ACU-1",
        .manufacturer = "DTU community",
        .model = "VolAnti-ESP32S3",
        .serial_number = "0001",
        .software_version = "0.1.0",
        .status_interval_s = 10.0f,
        .reports_node_location = true,
        .capabilities = caps,
        .capabilities_count = 2,
        .detection_location = SAPIENT_REPORT_RANGE_BEARING,
        .classes = classes,
        .classes_count = 3,
        .maximum_latency_s = 0.5f,
    };
    size_t n = 0;
    CHECK(sapient_encode_registration_frame(NODE_ID, &cfg, g_buf, sizeof(g_buf), &n) == SAPIENT_OK);
    fwrite(g_buf, 1, n, f);
    printf("Registration     %4zu bytes\n", n);

    /* 2. StatusReport */
    sapient_message_init(&g_msg, NODE_ID, NULL);
    sapient_status_report_t *sr = sapient_message_status_report(&g_msg,
            P(StatusReport_System_SYSTEM_OK), P(StatusReport_Info_INFO_NEW));
    SAPIENT_STRCPY(sr->mode, "Default");
    sr->has_mode = true;
    sapient_location_t loc;
    sapient_location_wgs84(&loc, 48.8008, 9.0129, 385.0);
    sapient_status_set_location(sr, &loc);
    sapient_status_set_power(sr, P(StatusReport_PowerSource_POWERSOURCE_INTERNAL_BATTERY),
            P(StatusReport_PowerStatus_POWERSTATUS_OK), 87);
    CHECK(sapient_status_add(sr, P(StatusReport_StatusLevel_STATUS_LEVEL_INFORMATION_STATUS),
            P(StatusReport_StatusType_STATUS_TYPE_OTHER), "wind 3 m/s"));
    printf("StatusReport     %4zu bytes\n", write_frame(f, &g_msg));

    /* 3. DetectionReport (range/bearing + class) */
    sapient_message_init(&g_msg, NODE_ID, NULL);
    sapient_detection_report_t *dr = sapient_message_detection_report(&g_msg, NULL);
    sapient_range_bearing_t rb;
    sapient_range_bearing_deg_m(&rb, 123.5, 12.0, 0);
    sapient_detection_set_range_bearing(dr, &rb);
    dr->has_detection_confidence = true;
    dr->detection_confidence = 0.82f;
    CHECK(sapient_detection_add_class(dr, "UAV", 0.82f));
    CHECK(sapient_detection_add_class(dr, "Bird", 0.10f));
    printf("DetectionReport  %4zu bytes\n", write_frame(f, &g_msg));

    /* 4. Alert */
    sapient_message_init(&g_msg, NODE_ID, NULL);
    sapient_message_alert(&g_msg, P(Alert_AlertType_ALERT_TYPE_WARNING), "Microphone 3 saturated");
    printf("Alert            %4zu bytes\n", write_frame(f, &g_msg));

    /* 5. TaskAck */
    sapient_message_init(&g_msg, NODE_ID, NULL);
    sapient_message_task_ack(&g_msg, "01JBQZ5V3K8N2W0R7T4Y6M9P1C", P(TaskAck_TaskStatus_TASK_STATUS_ACCEPTED), NULL);
    printf("TaskAck          %4zu bytes\n", write_frame(f, &g_msg));

    fclose(f);
    return 0;
}

static void on_frame(const uint8_t *payload, size_t len, void *ctx)
{
    int *count = (int *)ctx;
    sapient_err_t err = sapient_decode(payload, len, &g_msg);
    CHECK(err == SAPIENT_OK);
    (*count)++;
    printf("frame %d: %s from %s", *count, sapient_content_name(g_msg.which_content), g_msg.node_id);
    if (g_msg.which_content == SAPIENT_CONTENT_TASK) {
        const sapient_task_t *t = &g_msg.content.task;
        printf(" task_id=%s control=%d", t->task_id, (int)t->control);
        if (t->has_command && t->command.which_command == P(Task_Command_mode_change_tag)) {
            printf(" mode_change=%s", t->command.command.mode_change);
        }
        printf(" regions=%u", (unsigned)t->region_count);
    } else if (g_msg.which_content == SAPIENT_CONTENT_REGISTRATION_ACK) {
        printf(" acceptance=%d", g_msg.content.registration_ack.acceptance);
    }
    printf("\n");
}

static int cmd_decode(const char *path)
{
    FILE *f = fopen(path, "rb");
    CHECK(f);
    static uint8_t rx[8192];
    sapient_framer_t fr;
    sapient_framer_init(&fr, rx, sizeof(rx));
    int count = 0;
    uint8_t chunk[7]; /* deliberately odd chunk size to exercise reassembly */
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        CHECK(sapient_framer_feed(&fr, chunk, n, on_frame, &count) == SAPIENT_OK);
    }
    fclose(f);
    CHECK(fr.len == 0);
    printf("decoded %d frames\n", count);
    return count > 0 ? 0 : 1;
}

static int cmd_selftest(void)
{
    /* UUID format */
    char uuid[SAPIENT_UUID_LEN];
    sapient_uuid_v4(uuid);
    CHECK(strlen(uuid) == 36 && uuid[8] == '-' && uuid[13] == '-' && uuid[14] == '4' && uuid[18] == '-' &&
          uuid[23] == '-');
    CHECK(strchr("89ab", uuid[19]) != NULL);

    /* ULID format + time prefix monotonic */
    char a[SAPIENT_ULID_LEN], b[SAPIENT_ULID_LEN];
    sapient_ulid(a);
    sapient_ulid(b);
    CHECK(strlen(a) == 26 && strlen(b) == 26);
    for (int i = 0; i < 26; i++) {
        CHECK(isdigit((unsigned char)a[i]) || (isupper((unsigned char)a[i]) && !strchr("ILOU", a[i])));
    }
    CHECK(a[0] <= '7'); /* 48-bit timestamp fits */
    CHECK(strncmp(a, b, 10) <= 0);
    CHECK(strcmp(a, b) != 0);

    /* round trip */
    sapient_message_init(&g_msg, NODE_ID, NULL);
    sapient_detection_report_t *dr = sapient_message_detection_report(&g_msg, "01JBQZ5V3K8N2W0R7T4Y6M9P1C");
    sapient_location_t loc;
    sapient_location_wgs84(&loc, 48.8, 9.01, 400);
    sapient_detection_set_location(dr, &loc);
    sapient_detection_set_enu_velocity(dr, 3.0, -1.5, 0.2);
    CHECK(sapient_detection_add_class(dr, "UAV", 0.9f));
    size_t n = 0;
    CHECK(sapient_encode_frame(&g_msg, g_buf, sizeof(g_buf), &n) == SAPIENT_OK);

    /* too-small buffer is reported */
    size_t dummy;
    CHECK(sapient_encode_frame(&g_msg, g_buf, 20, &dummy) == SAPIENT_ERR_NO_SPACE);

    static sapient_message_t back;
    CHECK(sapient_decode(g_buf + 4, n - 4, &back) == SAPIENT_OK);
    CHECK(back.which_content == SAPIENT_CONTENT_DETECTION_REPORT);
    CHECK(strcmp(back.node_id, NODE_ID) == 0);
    CHECK(strcmp(back.content.detection_report.object_id, "01JBQZ5V3K8N2W0R7T4Y6M9P1C") == 0);
    CHECK(back.content.detection_report.location_oneof.location.y == 48.8);
    CHECK(back.content.detection_report.classification_count == 1);

    /* framer: two frames delivered byte-by-byte */
    static uint8_t stream[16384];
    memcpy(stream, g_buf, n);
    memcpy(stream + n, g_buf, n);
    static uint8_t rx[4096];
    sapient_framer_t fr;
    sapient_framer_init(&fr, rx, sizeof(rx));
    int count = 0;
    for (size_t i = 0; i < 2 * n; i++) {
        CHECK(sapient_framer_feed(&fr, &stream[i], 1, on_frame, &count) == SAPIENT_OK);
    }
    CHECK(count == 2);

    /* framer: oversized length is rejected */
    uint8_t bad[4] = {0xFF, 0xFF, 0xFF, 0x7F};
    CHECK(sapient_framer_feed(&fr, bad, 4, on_frame, &count) == SAPIENT_ERR_FRAME_TOO_LARGE);

    printf("sizeof(sapient_message_t) = %zu bytes\n", sizeof(sapient_message_t));
    printf("selftest OK\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "selftest") == 0) {
        return cmd_selftest();
    }
    if (argc >= 3 && strcmp(argv[1], "encode") == 0) {
        return cmd_encode(argv[2]);
    }
    if (argc >= 3 && strcmp(argv[1], "decode") == 0) {
        return cmd_decode(argv[2]);
    }
    fprintf(stderr, "usage: %s selftest | encode <out.bin> | decode <in.bin>\n", argv[0]);
    return 2;
}
