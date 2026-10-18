/* SPDX-License-Identifier: MIT. Event-driven bounded WebSocket echo example.
 * Demonstrates: text/binary/empty round trips with content checks, the ABI
 * boundary probes a real client should validate once (short status struct,
 * foreign handle close, oversized send, 4096 boundary), the single send/receive
 * slot backpressure (EAGAIN retry, never blind resend), and reconnection as a
 * business rule after terminal errors (close, then connect fresh with bounded
 * backoff). No APP-owned socket, TLS or shell string; idle keepalive, server
 * fragmentation and close handshake stay in firmware. */
#include <aster/aster.h>
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#ifndef WS_HOST
#define WS_HOST "127.0.0.1"
#endif
#ifndef WS_PORT
#define WS_PORT 17880
#endif
#ifndef WS_TLS
#define WS_TLS 0
#endif
static const char path[] = "/echo";
static char url[128];
static int32_t handle;
static uint32_t connection, rounds, paused, last, round_state, probe_state, retry_at;
static uint8_t sent[4097];
static uint32_t sent_len, sent_op;
static int sent_pending;
static aster_ws_status_t status;
static aster_ws_message_t message;
static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[12];
static uint8_t arena[256];
static void fail(const char *where, int32_t code)
{ aster_logf(ASTER_LOG_ERROR, "WSAPP FAIL %s code=%d", where, code); aster_app_exit(1); }
static void drop_reconnect(int32_t where_code)
{
    /* Terminal handling stays a business rule: invalidate this session,
     * then reconnect fresh after a bounded backoff. Never resend a possibly
     * accepted message blindly. */
    aster_logf(ASTER_LOG_WARN, "WSAPP recover code=%d; reconnecting", where_code);
    if (handle > 0) (void)aster_ws_close(handle);
    handle = 0; sent_pending = 0; round_state = 0; probe_state = 0;
    retry_at = aster_time_now_ms() + 3000;
}
static void draw(void)
{
    char label[80];
    aster_ge_clear(&ge, ASTER_RGB565(16, 20, 32));
    aster_ge_text(&ge, 12, 32, 20, ASTER_WHITE, 0, "WS Echo");
    aster_snprintf(label, sizeof(label), "State %u  rounds %u", connection, rounds);
    aster_ge_text(&ge, 12, 64, 14, ASTER_CYAN, 0, label);
    aster_ge_text(&ge, 12, 90, 12, ASTER_WHITE, 0, paused ? "Tap: continue" : "Tap: pause sending");
    aster_ge_rect(&ge, (aster_time_now_ms() / 15) % (ge.w - 24), 124, 24, 24, ASTER_GREEN);
    int rc = aster_ge_present(&ge); if (rc < 0) fail("draw", rc);
}
/* Build the next message: mostly text, occasionally binary, and an empty
 * frame every eighth round so the app exercises the opcode and length-0
 * boundary that distinguishes an empty message from no message. */
static void prepare(uint32_t n)
{
    if (n % 8 == 7) { sent_op = ASTER_WS_OPCODE_BINARY; sent_len = 0; return; }
    if (n % 4 == 3)
    {
        sent_op = ASTER_WS_OPCODE_BINARY; sent_len = 4;
        sent[0] = 0x00; sent[1] = 0xFF; sent[2] = 0x7F; sent[3] = 0x80; return;
    }
    sent_op = ASTER_WS_OPCODE_TEXT;
    sent_len = (uint32_t)aster_snprintf((char *)sent, sizeof(sent), "aster-ws-%u", n);
}
/* One-time ABI boundary probes against the live session. Each must answer with
 * the stable public error and leave the connection usable; anything else is a
 * real contract break and fails loudly. */
static int run_probe(void)
{
    switch (probe_state) {
    case 0:
        if (aster_ws_status(handle, &status, sizeof(status) - 1) != ASTER_EINVAL) return 0;
        ++probe_state; return 1;
    case 1:
        if (aster_ws_close(handle + 65536) != ASTER_EBADF) return 0;
        ++probe_state; return 1;
    case 2:
        for (uint32_t i = 0; i < sizeof(sent); ++i) sent[i] = (uint8_t)(i % 251);
        if (aster_ws_send(handle, ASTER_WS_OPCODE_BINARY, sent, 4097) != ASTER_EMSGSIZE) return 0;
        ++probe_state; return 1;
    case 3:
        sent_op = ASTER_WS_OPCODE_BINARY; sent_len = 4096;
        {
            int rc = aster_ws_send(handle, sent_op, sent, sent_len);
            if (rc == ASTER_EAGAIN) return 1;
            if (rc != ASTER_OK) { fail("probe 4096 send", rc); return 0; }
        }
        ++probe_state; return 1;
    case 4: {
        int rc = aster_ws_recv(handle, &message, sizeof(message));
        if (rc == ASTER_EAGAIN) return 1;
        if (rc != ASTER_OK) { fail("probe 4096 recv", rc); return 0; }
        if (message.opcode != ASTER_WS_OPCODE_BINARY || message.length != 4096) return 0;
        for (uint32_t i = 0; i < 4096; ++i)
            if (message.data[i] != (uint8_t)(i % 251)) return 0;
        ++probe_state;
        aster_logf(ASTER_LOG_INFO, "WSAPP PROBES ok");
        return 1;
    }
    default: return 1;
    }
}
int aster_app_init(void)
{
    aster_surface_desc_t desc = { sizeof(desc), 1, 0, 0, ASTER_PIXEL_RGB565, ASTER_SURFACE_F_PANEL_CANVAS };
    aster_surface_buffer_t buffer = { .size = sizeof(buffer), .version = 1 };
    int rc = aster_surface_create(&desc, sizeof(desc), &surface); if (rc) return rc;
    rc = aster_surface_acquire(surface, &buffer, sizeof(buffer)); if (rc) return rc;
    if (buffer.width <= 24) return ASTER_ENOTSUP;
    aster_ge_init(&ge, surface, buffer.width, buffer.height, commands, 12, arena, sizeof(arena));
    last = aster_time_now_ms(); return ASTER_OK;
}
void aster_app_event(const aster_event_t *e)
{
    if (e->type == ASTER_EVENT_TOUCH && e->u.touch.action == ASTER_TOUCH_UP) { paused = !paused; return; }
    if (e->type != ASTER_EVENT_FRAME) return;
    draw();
    uint32_t now = aster_time_now_ms();
    if (!handle) {
        if (now < retry_at) return;
        int n = aster_snprintf(url, sizeof(url), (WS_TLS ? "wss://%s:%u%s" : "ws://%s:%u%s"),
                               WS_HOST, (unsigned)WS_PORT, path);
        int rc = aster_ws_connect(url, (uint32_t)n, 15000);
        if (rc == ASTER_EAGAIN) return;
        if (rc < 0)
        {
            if (rc == ASTER_EINVAL) { fail("connect url", rc); return; }
            retry_at = now + 3000; /* transient network/quota: try again later */
            return;
        }
        handle = rc; return;
    }
    int rc = aster_ws_status(handle, &status, sizeof(status));
    if (rc == ASTER_EAGAIN) return;
    if (rc < 0) { drop_reconnect(rc); return; }
    connection = status.state;
    if (status.state == ASTER_WS_STATE_FAILED || status.state == ASTER_WS_STATE_CANCELLED)
    { drop_reconnect(status.error); return; }
    if (status.state != ASTER_WS_STATE_READY) { last = now; return; }
    if (probe_state < 5) { if (run_probe()) return; fail("probe mismatch", ASTER_EPROTO); return; }
    if (!sent_pending) {
        if (paused || now - last < 500) return;
        prepare(rounds);
        rc = aster_ws_send(handle, sent_op, sent_len ? sent : 0, sent_len);
        if (rc == ASTER_EAGAIN) return;
        if (rc < 0) { drop_reconnect(rc); return; }
        sent_pending = 1; return;
    }
    rc = aster_ws_recv(handle, &message, sizeof(message));
    if (rc == ASTER_EAGAIN) { if (now - last > 15000) drop_reconnect(ASTER_ETIMEDOUT); return; }
    if (rc == ASTER_EPIPE) { drop_reconnect(rc); return; }
    if (rc < 0) { drop_reconnect(rc); return; }
    if (message.opcode != sent_op || message.length != sent_len ||
        memcmp(message.data, sent, sent_len) != 0) { fail("content", ASTER_ECORRUPT); sent_pending = 0; return; }
    sent_pending = 0; ++rounds; last = now;
    if (rounds == 1 || rounds % 20 == 0)
        aster_logf(ASTER_LOG_INFO, "WSAPP round=%u op=%u bytes=%u content=ok", rounds, sent_op, sent_len);
}
void aster_app_stop(void) { if (handle > 0) (void)aster_ws_close(handle); }
void aster_app_resume(void) { last = aster_time_now_ms(); }
