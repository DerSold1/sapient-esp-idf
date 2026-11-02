/* POSIX SAPIENT node: same flow as the ESP32 example, for testing on a PC.
 *   tcp_node <host> <port> [seconds]
 * SPDX-License-Identifier: Apache-2.0
 */
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "sapient.h"

#define P(x) sapient_msg_bsi_flex_335_v2_0_##x

static char node_id[SAPIENT_UUID_LEN];
static sapient_message_t tx, rx;
static uint8_t txbuf[4096], rxbuf[8192];
static int registered, tasks_acked;

static void send_msg(int s, const sapient_message_t *m)
{
    size_t n;
    if (sapient_encode_frame(m, txbuf, sizeof(txbuf), &n) == SAPIENT_OK) {
        send(s, txbuf, n, 0);
    }
}

static void on_frame(const uint8_t *p, size_t len, void *ctx)
{
    int s = *(int *)ctx;
    if (sapient_decode(p, len, &rx) != SAPIENT_OK) {
        return;
    }
    printf("<- %s\n", sapient_content_name(rx.which_content));
    if (rx.which_content == SAPIENT_CONTENT_REGISTRATION_ACK) {
        registered = rx.content.registration_ack.acceptance;
    } else if (rx.which_content == SAPIENT_CONTENT_TASK) {
        sapient_message_init(&tx, node_id, rx.node_id);
        sapient_message_task_ack(&tx, rx.content.task.task_id, P(TaskAck_TaskStatus_TASK_STATUS_ACCEPTED), NULL);
        send_msg(s, &tx);
        tasks_acked++;
        printf("-> TaskAck %s\n", rx.content.task.task_id);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s host port [seconds]\n", argv[0]);
        return 2;
    }
    int seconds = argc > 3 ? atoi(argv[3]) : 5;
    setvbuf(stdout, NULL, _IOLBF, 0);
    sapient_uuid_v4(node_id);

    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res;
    if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) {
        return 1;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        perror("connect");
        freeaddrinfo(res);
        return 1;
    }
    freeaddrinfo(res);
    struct timeval tv = {0, 100000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static const sapient_capability_t caps[] = {{"Acoustic", "MaxRange", "300", "m"}};
    static const char *const classes[] = {"UAV", "Unknown"};
    sapient_registration_cfg_t cfg = {
        .node_type = P(Registration_NodeType_NODE_TYPE_ACOUSTIC), .name = "host test node", .short_name = "HOST",
        .manufacturer = "DTU community", .model = "tcp_node", .status_interval_s = 1,
        .capabilities = caps, .capabilities_count = 1,
        .detection_location = SAPIENT_REPORT_RANGE_BEARING, .classes = classes, .classes_count = 2,
    };
    size_t n;
    sapient_encode_registration_frame(node_id, &cfg, txbuf, sizeof(txbuf), &n);
    send(s, txbuf, n, 0);
    printf("-> Registration\n");

    sapient_framer_t fr;
    sapient_framer_init(&fr, rxbuf, sizeof(rxbuf));
    time_t end = time(NULL) + seconds, last = 0;
    while (time(NULL) < end) {
        uint8_t chunk[512];
        ssize_t r = recv(s, chunk, sizeof(chunk), 0);
        if (r == 0) {
            break;
        }
        if (r > 0) {
            sapient_framer_feed(&fr, chunk, (size_t)r, on_frame, &s);
        }
        if (registered && time(NULL) != last) {
            last = time(NULL);
            sapient_message_init(&tx, node_id, NULL);
            sapient_message_status_report(&tx, P(StatusReport_System_SYSTEM_OK), P(StatusReport_Info_INFO_UNCHANGED));
            send_msg(s, &tx);
            sapient_message_init(&tx, node_id, NULL);
            sapient_detection_report_t *dr = sapient_message_detection_report(&tx, NULL);
            sapient_range_bearing_t rb;
            sapient_range_bearing_deg_m(&rb, (double)(last % 360), 10, 0);
            sapient_detection_set_range_bearing(dr, &rb);
            sapient_detection_add_class(dr, "UAV", 0.7f);
            send_msg(s, &tx);
            printf("-> StatusReport + DetectionReport\n");
        }
    }
    close(s);
    printf("registered=%d tasks_acked=%d\n", registered, tasks_acked);
    return registered ? 0 : 1;
}
