/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <sys/time.h>

#include "sapient.h"

#if defined(ESP_PLATFORM)
#include "esp_random.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

/* ---------------------------------------------------------------- platform */

static uint64_t default_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000u + (uint64_t)(tv.tv_usec / 1000);
}

static void default_random(void *buf, size_t len)
{
#if defined(ESP_PLATFORM)
    esp_fill_random(buf, len);
#else
    static FILE *urandom = NULL;
    if (!urandom) {
        urandom = fopen("/dev/urandom", "rb");
    }
    if (urandom && fread(buf, 1, len, urandom) == len) {
        return;
    }
    /* Fallback: not cryptographic, but unique enough for IDs on hosts without /dev/urandom */
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        p[i] = (uint8_t)(rand() & 0xFF);
    }
#endif
}

static sapient_time_ms_fn s_time_fn = default_time_ms;
static sapient_random_fn s_random_fn = default_random;

void sapient_set_platform(sapient_time_ms_fn time_fn, sapient_random_fn random_fn)
{
    s_time_fn = time_fn ? time_fn : default_time_ms;
    s_random_fn = random_fn ? random_fn : default_random;
}

uint64_t sapient_now_ms(void)
{
    return s_time_fn();
}

/* --------------------------------------------------------------------- IDs */

void sapient_uuid_v4(char *out)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t b[16];
    s_random_fn(b, sizeof(b));
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x40); /* version 4 */
    b[8] = (uint8_t)((b[8] & 0x3F) | 0x80); /* RFC 4122 variant */

    size_t o = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out[o++] = '-';
        }
        out[o++] = hex[b[i] >> 4];
        out[o++] = hex[b[i] & 0x0F];
    }
    out[o] = '\0';
}

/* ULID: 48-bit ms timestamp + 80 random bits, Crockford base32, 26 chars */
void sapient_ulid(char *out)
{
    static const char enc[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    uint64_t t = sapient_now_ms() & 0xFFFFFFFFFFFFull;
    for (int i = 9; i >= 0; i--) {
        out[i] = enc[t & 0x1F];
        t >>= 5;
    }

    uint8_t r[10];
    s_random_fn(r, sizeof(r));
    /* 80 bits -> 16 x 5 bits, big-endian bit order */
    uint32_t acc = 0;
    int bits = 0;
    int o = 10;
    for (int i = 0; i < 10; i++) {
        acc = (acc << 8) | r[i];
        bits += 8;
        while (bits >= 5) {
            out[o++] = enc[(acc >> (bits - 5)) & 0x1F];
            bits -= 5;
        }
    }
    out[26] = '\0';
}

/* ------------------------------------------------------------------ errors */

const char *sapient_err_str(sapient_err_t err)
{
    switch (err) {
    case SAPIENT_OK: return "ok";
    case SAPIENT_ERR_ARG: return "invalid argument";
    case SAPIENT_ERR_ENCODE: return "protobuf encode failed";
    case SAPIENT_ERR_DECODE: return "protobuf decode failed";
    case SAPIENT_ERR_NO_SPACE: return "buffer too small";
    case SAPIENT_ERR_FRAME_TOO_LARGE: return "frame too large";
    case SAPIENT_ERR_NO_MEM: return "out of memory";
    }
    return "unknown error";
}

const char *sapient_content_name(pb_size_t which_content)
{
    switch (which_content) {
    case SAPIENT_CONTENT_REGISTRATION: return "Registration";
    case SAPIENT_CONTENT_REGISTRATION_ACK: return "RegistrationAck";
    case SAPIENT_CONTENT_STATUS_REPORT: return "StatusReport";
    case SAPIENT_CONTENT_DETECTION_REPORT: return "DetectionReport";
    case SAPIENT_CONTENT_TASK: return "Task";
    case SAPIENT_CONTENT_TASK_ACK: return "TaskAck";
    case SAPIENT_CONTENT_ALERT: return "Alert";
    case SAPIENT_CONTENT_ALERT_ACK: return "AlertAck";
    case SAPIENT_CONTENT_ERROR: return "Error";
    default: return "None";
    }
}

void sapient_strlcpy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_size) {
        n = dst_size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}
