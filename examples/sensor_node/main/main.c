/*
 * SAPIENT sensor node example for ESP32 / ESP32-S3 (ESP-IDF >= 5.1)
 *
 *  1. Wi-Fi (protocol_examples_common, configure via menuconfig)
 *  2. SNTP time sync (SAPIENT needs UTC timestamps)
 *  3. Stable node UUID stored in NVS
 *  4. TCP connect to the fusion node / middleware
 *  5. Registration -> wait for RegistrationAck
 *  6. StatusReport heartbeat + (simulated) DetectionReports
 *  7. Incoming Task -> TaskAck
 *
 * Replace detection_poll() with your real sensor (e.g. VolAnti acoustics).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "sapient.h"

#define P(x) sapient_msg_bsi_flex_335_v2_0_##x

static const char *TAG = "sapient_node";

static char s_node_id[SAPIENT_UUID_LEN];
static bool s_registered;
static sapient_message_t s_tx_msg; /* ~6 KB each: static, not on the task stack */
static sapient_message_t s_rx_msg;
static uint8_t s_tx_buf[2048];
static uint8_t s_rx_buf[8192];

/* ---------------------------------------------------------------- helpers */

static void load_or_create_node_id(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("sapient", NVS_READWRITE, &h));
    size_t len = sizeof(s_node_id);
    if (nvs_get_str(h, "node_id", s_node_id, &len) != ESP_OK) {
        sapient_uuid_v4(s_node_id);
        ESP_ERROR_CHECK(nvs_set_str(h, "node_id", s_node_id));
        ESP_ERROR_CHECK(nvs_commit(h));
        ESP_LOGI(TAG, "created new node_id");
    }
    nvs_close(h);
    ESP_LOGI(TAG, "node_id = %s", s_node_id);
}

static void sync_time(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SAPIENT_SNTP_SERVER);
    esp_netif_sntp_init(&cfg);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timed out - timestamps will be wrong");
    }
}

static bool send_all(int sock, const uint8_t *data, size_t len)
{
    while (len > 0) {
        int n = send(sock, data, len, 0);
        if (n < 0) {
            ESP_LOGE(TAG, "send failed: errno %d", errno);
            return false;
        }
        data += n;
        len -= (size_t)n;
    }
    return true;
}

static bool send_msg(int sock, const sapient_message_t *msg)
{
    size_t n = 0;
    sapient_err_t err = sapient_encode_frame(msg, s_tx_buf, sizeof(s_tx_buf), &n);
    if (err != SAPIENT_OK) {
        ESP_LOGE(TAG, "encode %s: %s", sapient_content_name(msg->which_content), sapient_err_str(err));
        return false;
    }
    return send_all(sock, s_tx_buf, n);
}

/* ------------------------------------------------------------- messages */

static bool send_registration(int sock)
{
    static const sapient_capability_t caps[] = {
        {"Acoustic", "MaxRange", "300", "m"},
    };
    static const char *const classes[] = {"UAV", "Unknown"};
    const sapient_registration_cfg_t cfg = {
        .node_type = P(Registration_NodeType_NODE_TYPE_ACOUSTIC),
        .name = CONFIG_SAPIENT_NODE_NAME,
        .short_name = CONFIG_SAPIENT_NODE_SHORT_NAME,
        .manufacturer = "DTU community",
        .model = "sapient-esp-idf example",
        .software_version = "0.1.0",
        .status_interval_s = CONFIG_SAPIENT_STATUS_INTERVAL_S,
        .reports_node_location = true,
        .capabilities = caps,
        .capabilities_count = sizeof(caps) / sizeof(caps[0]),
        .detection_location = SAPIENT_REPORT_RANGE_BEARING,
        .classes = classes,
        .classes_count = sizeof(classes) / sizeof(classes[0]),
    };
    size_t n = 0;
    sapient_err_t err = sapient_encode_registration_frame(s_node_id, &cfg, s_tx_buf, sizeof(s_tx_buf), &n);
    if (err != SAPIENT_OK) {
        ESP_LOGE(TAG, "registration: %s", sapient_err_str(err));
        return false;
    }
    ESP_LOGI(TAG, "-> Registration (%u bytes)", (unsigned)n);
    return send_all(sock, s_tx_buf, n);
}

static bool send_status(int sock, bool first)
{
    sapient_message_init(&s_tx_msg, s_node_id, NULL);
    sapient_status_report_t *sr = sapient_message_status_report(&s_tx_msg,
            P(StatusReport_System_SYSTEM_OK),
            first ? P(StatusReport_Info_INFO_NEW) : P(StatusReport_Info_INFO_UNCHANGED));
    sr->has_mode = true;
    SAPIENT_STRCPY(sr->mode, "Default");
    sapient_location_t loc;
    sapient_location_wgs84(&loc, CONFIG_SAPIENT_NODE_LAT_E6 / 1e6, CONFIG_SAPIENT_NODE_LON_E6 / 1e6,
            CONFIG_SAPIENT_NODE_ALT_M);
    sapient_status_set_location(sr, &loc);
    return send_msg(sock, &s_tx_msg);
}

/* Replace with your sensor. Return true if something was detected. */
static bool detection_poll(float *azimuth_deg, float *elevation_deg, float *confidence)
{
#if CONFIG_SAPIENT_SIMULATE_DETECTIONS
    static int64_t last;
    int64_t now = esp_timer_get_time();
    if (now - last < 5 * 1000000LL) {
        return false;
    }
    last = now;
    *azimuth_deg = (float)(esp_random() % 360);
    *elevation_deg = (float)(5 + esp_random() % 30);
    *confidence = 0.5f + (float)(esp_random() % 50) / 100.0f;
    return true;
#else
    return false;
#endif
}

static bool send_detection(int sock, float az, float el, float conf)
{
    sapient_message_init(&s_tx_msg, s_node_id, NULL);
    sapient_detection_report_t *dr = sapient_message_detection_report(&s_tx_msg, NULL);
    sapient_range_bearing_t rb;
    sapient_range_bearing_deg_m(&rb, az, el, 0 /* acoustic: no range */);
    sapient_detection_set_range_bearing(dr, &rb);
    dr->has_detection_confidence = true;
    dr->detection_confidence = conf;
    sapient_detection_add_class(dr, "UAV", conf);
    ESP_LOGI(TAG, "-> DetectionReport az=%.0f el=%.0f conf=%.2f", az, el, conf);
    return send_msg(sock, &s_tx_msg);
}

/* --------------------------------------------------------------- receive */

static void on_frame(const uint8_t *payload, size_t len, void *ctx)
{
    int sock = *(int *)ctx;
    if (sapient_decode(payload, len, &s_rx_msg) != SAPIENT_OK) {
        ESP_LOGW(TAG, "<- undecodable frame (%u bytes)", (unsigned)len);
        return;
    }
    ESP_LOGI(TAG, "<- %s", sapient_content_name(s_rx_msg.which_content));

    switch (s_rx_msg.which_content) {
    case SAPIENT_CONTENT_REGISTRATION_ACK:
        s_registered = s_rx_msg.content.registration_ack.acceptance;
        if (!s_registered) {
            for (int i = 0; i < s_rx_msg.content.registration_ack.ack_response_reason_count; i++) {
                ESP_LOGW(TAG, "registration rejected: %s", s_rx_msg.content.registration_ack.ack_response_reason[i]);
            }
        }
        break;
    case SAPIENT_CONTENT_TASK: {
        const sapient_task_t *t = &s_rx_msg.content.task;
        ESP_LOGI(TAG, "task %s control=%d", t->task_id, (int)t->control);
        /* TODO: apply regions / mode change to your sensor here */
        sapient_message_init(&s_tx_msg, s_node_id, s_rx_msg.node_id);
        sapient_message_task_ack(&s_tx_msg, t->task_id, P(TaskAck_TaskStatus_TASK_STATUS_ACCEPTED), NULL);
        send_msg(sock, &s_tx_msg);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ main */

static int connect_fusion(void)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *res = NULL;
    char port[8];
    snprintf(port, sizeof(port), "%d", CONFIG_SAPIENT_FUSION_PORT);
    if (getaddrinfo(CONFIG_SAPIENT_FUSION_HOST, port, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", CONFIG_SAPIENT_FUSION_HOST);
        return -1;
    }
    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock >= 0 && connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect to %s:%s failed: errno %d", CONFIG_SAPIENT_FUSION_HOST, port, errno);
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock >= 0) {
        struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ESP_LOGI(TAG, "connected to %s:%s", CONFIG_SAPIENT_FUSION_HOST, port);
    }
    return sock;
}

static void sapient_task(void *arg)
{
    sapient_framer_t framer;
    sapient_framer_init(&framer, s_rx_buf, sizeof(s_rx_buf));

    for (;;) {
        int sock = connect_fusion();
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        sapient_framer_reset(&framer);
        s_registered = false;
        bool ok = send_registration(sock);
        int64_t last_reg = esp_timer_get_time();
        int64_t last_status = 0;
        bool first_status = true;

        while (ok) {
            uint8_t chunk[256];
            int n = recv(sock, chunk, sizeof(chunk), 0);
            if (n == 0) {
                ESP_LOGW(TAG, "fusion node closed the connection");
                break;
            }
            if (n > 0 && sapient_framer_feed(&framer, chunk, (size_t)n, on_frame, &sock) != SAPIENT_OK) {
                ESP_LOGE(TAG, "framing error, reconnecting");
                break;
            }
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }

            int64_t now = esp_timer_get_time();
            if (!s_registered) {
                if (now - last_reg > 10 * 1000000LL) { /* re-send until acknowledged */
                    ok = send_registration(sock);
                    last_reg = now;
                }
                continue;
            }
            if (now - last_status >= (int64_t)CONFIG_SAPIENT_STATUS_INTERVAL_S * 1000000LL) {
                ok = send_status(sock, first_status);
                first_status = false;
                last_status = now;
            }
            float az, el, conf;
            if (ok && detection_poll(&az, &el, &conf)) {
                ok = send_detection(sock, az, el, conf);
            }
        }
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    sync_time();
    load_or_create_node_id();

    xTaskCreate(sapient_task, "sapient", 6144, NULL, 5, NULL);
}
