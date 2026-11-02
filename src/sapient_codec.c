/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pb_decode.h"
#include "pb_encode.h"
#include "sapient.h"

#define MSG_FIELDS sapient_msg_bsi_flex_335_v2_0_SapientMessage_fields

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

sapient_err_t sapient_encode(const sapient_message_t *msg, uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!msg || !buf || !out_len) {
        return SAPIENT_ERR_ARG;
    }
    pb_ostream_t os = pb_ostream_from_buffer(buf, cap);
    if (!pb_encode(&os, MSG_FIELDS, msg)) {
        /* nanopb reports "stream full" when the buffer is too small */
        const char *e = PB_GET_ERROR(&os);
        return (e && strstr(e, "stream full")) ? SAPIENT_ERR_NO_SPACE : SAPIENT_ERR_ENCODE;
    }
    *out_len = os.bytes_written;
    return SAPIENT_OK;
}

sapient_err_t sapient_encode_frame(const sapient_message_t *msg, uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!buf || cap < SAPIENT_FRAME_HEADER_LEN) {
        return SAPIENT_ERR_NO_SPACE;
    }
    size_t n = 0;
    sapient_err_t err = sapient_encode(msg, buf + SAPIENT_FRAME_HEADER_LEN, cap - SAPIENT_FRAME_HEADER_LEN, &n);
    if (err != SAPIENT_OK) {
        return err;
    }
    put_u32_le(buf, (uint32_t)n);
    *out_len = n + SAPIENT_FRAME_HEADER_LEN;
    return SAPIENT_OK;
}

sapient_err_t sapient_decode(const uint8_t *buf, size_t len, sapient_message_t *msg)
{
    if (!buf || !msg) {
        return SAPIENT_ERR_ARG;
    }
    memset(msg, 0, sizeof(*msg));
    pb_istream_t is = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&is, MSG_FIELDS, msg)) {
        return SAPIENT_ERR_DECODE;
    }
    return SAPIENT_OK;
}

/* ------------------------------------------------------------------ framer */

void sapient_framer_init(sapient_framer_t *f, uint8_t *buf, size_t cap)
{
    f->buf = buf;
    f->cap = cap;
    f->len = 0;
}

void sapient_framer_reset(sapient_framer_t *f)
{
    f->len = 0;
}

sapient_err_t sapient_framer_feed(sapient_framer_t *f, const uint8_t *data, size_t len, sapient_frame_cb cb,
        void *ctx)
{
    if (!f || !f->buf || (!data && len)) {
        return SAPIENT_ERR_ARG;
    }
    while (len > 0) {
        size_t space = f->cap - f->len;
        size_t n = len < space ? len : space;
        memcpy(f->buf + f->len, data, n);
        f->len += n;
        data += n;
        len -= n;

        /* emit all complete frames currently in the buffer */
        size_t off = 0;
        while (f->len - off >= SAPIENT_FRAME_HEADER_LEN) {
            uint32_t plen = get_u32_le(f->buf + off);
            if (plen > SAPIENT_MAX_FRAME_LEN || plen + SAPIENT_FRAME_HEADER_LEN > f->cap) {
                f->len = 0;
                return SAPIENT_ERR_FRAME_TOO_LARGE;
            }
            if (f->len - off < plen + SAPIENT_FRAME_HEADER_LEN) {
                break;
            }
            if (cb) {
                cb(f->buf + off + SAPIENT_FRAME_HEADER_LEN, plen, ctx);
            }
            off += plen + SAPIENT_FRAME_HEADER_LEN;
        }
        if (off > 0) {
            memmove(f->buf, f->buf + off, f->len - off);
            f->len -= off;
        }
    }
    return SAPIENT_OK;
}
