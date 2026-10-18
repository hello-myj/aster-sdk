/* SPDX-License-Identifier: MIT
 * HTTP Range resume pattern (N5.4 reference; firmware protocol stack and ABI
 * unchanged — built against the existing ABI 3.9 HTTP/fs imports). Download
 * into /data/file.part, resume across transport drops with Range + If-Range
 * (ETag, else Last-Modified), and never stitch versions: a 200 at non-zero
 * offset restarts from zero. Before "committing", the complete temp file is
 * content-verified again while copying to the final name (no aster_fs_rename
 * in the public API), then the temp is removed. Cancel, app stop or a retry
 * keeps the .part so a later run resumes. One HTTP read and one file write
 * per frame; touch pauses/resumes and drawing stays responsive.
 *
 * Server contract (tools/validation/network_resume_server.cjs):
 *  - 206 + "bytes S-E/T": S must equal the on-disk temp size, T the total.
 *  - 200 whenever If-Range is absent/mismatched or Range unsupported.
 *  - 416 + "bytes * /T" when the temp is already complete (T==size).
 */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#ifndef HTTP_URL
#define HTTP_URL "http://127.0.0.1:9080/resume"
#endif
#ifndef HTTP_PATTERN
#define HTTP_PATTERN 1
#endif
#ifndef HTTP_MAX_ATTEMPTS
#define HTTP_MAX_ATTEMPTS 6
#endif
#define BODY_LIMIT 1048576u
static const char url[] = HTTP_URL;
static const char temp_path[] = "/data/file.part";
static const char final_path[] = "/data/file.bin";
static int32_t request;
static aster_handle_t surface, file, commit_src;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[12];
static uint8_t arena[256], buffer[512];
static char headers[320], value[128], etag[96], modified[96];
static uint32_t offset, total, done_bytes, attempts, stage, pause_ui, commit_pos;
static int commit_final_open, failed_flag, closing;
static void hard_fail(const char *where, int32_t code);
static void close_file(void) { if (file) { (void)aster_fs_close(file); file = 0; } }
/* Retire the transport slot: cancel is not enough (a terminal handle keeps
 * the quota), so request an async close and finish it on a later frame. */
static void retire_request(void)
{
    if (request > 0) closing = 1;
}
static int pump_close(void)
{
    if (!closing) return 0;
    int32_t rc = aster_http_close(request);
    if (rc == ASTER_EAGAIN) return 1; /* still contending: pause this stage */
    if (rc) hard_fail("close", rc);
    request = 0; closing = 0;
    return 0;
}
static void hard_fail(const char *where, int32_t code)
{
    if (failed_flag) return;
    failed_flag = 1;
    aster_logf(ASTER_LOG_ERROR, "DLAPP FAIL %s code=%d stage=%u", where, (int)code, stage);
    close_file();
    if (commit_src) { (void)aster_fs_close(commit_src); commit_src = 0; }
    retire_request();
    /* .part is kept: only verified bytes were ever written to it. */
}
static void drop_temp(void)
{
    close_file();
    (void)aster_fs_unlink(temp_path, sizeof(temp_path) - 1);
    offset = 0;
}
/* Verified prefix on disk may remain; request a fresh attempt from stage 0. */
static void retry(const char *where, int32_t code)
{
    if (failed_flag) return;
    close_file(); /* flush the verified prefix */
    retire_request();
    if (++attempts >= HTTP_MAX_ATTEMPTS) { hard_fail(where, code); return; }
    aster_logf(ASTER_LOG_WARN, "DLAPP retry %s code=%d attempt=%u", where, (int)code, attempts);
    stage = 0;
}
static uint64_t scan_uint(const char *s, const char **end)
{
    uint64_t v = 0; const char *p = s;
    while (*p >= '0' && *p <= '9') v = v * 10u + (uint32_t)(*p++ - '0');
    if (end) *end = p;
    return v;
}
static bool has_prefix(const char *s, const char *p, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (s[i] != p[i]) return false;
    return true;
}
/* "bytes S-E/T" or "bytes * /T"; returns start (0 for *) and total. */
static bool parse_content_range(const char *v, uint64_t *start, uint64_t *total_out)
{
    while (*v == ' ') v++;
    if (!has_prefix(v, "bytes", 5)) return false;
    v += 5;
    while (*v == ' ' || *v == '=') v++;
    const char *e;
    if (*v == '*') {
        while (*v && *v != '/') v++;
        if (*v != '/') return false;
        *start = 0;
        *total_out = scan_uint(v + 1, NULL);
        return *total_out > 0;
    }
    uint64_t s = scan_uint(v, &e);
    if (e == v || *e != '-') return false;
    uint64_t en = scan_uint(e + 1, &v);
    if (v == e + 1 || *v != '/' || en < s) return false;
    *start = s;
    *total_out = scan_uint(v + 1, NULL);
    return *total_out > 0;
}
/* The HTTP ABI copies header bytes without appending NUL; terminate here and
 * treat a not-smaller-than-cap copy as unusable (value stays cleared). */
static int32_t read_header(const char *name, char *out, uint32_t cap)
{
    uint32_t required = 0;
    out[0] = 0;
    int32_t rc = aster_http_header(request, name, (uint32_t)strlen(name), 0, out, cap, &required);
    if (rc > 0) {
        if ((uint32_t)rc < cap) out[rc] = 0;
        else { out[0] = 0; rc = -20; }
    }
    return rc;
}
int aster_app_init(void)
{
    aster_surface_desc_t desc = { sizeof(desc), 1, 0, 0, ASTER_PIXEL_RGB565,
                                  ASTER_SURFACE_F_PANEL_CANVAS };
    aster_surface_buffer_t b = { .size = sizeof(b), .version = 1 };
    int rc = aster_surface_create(&desc, sizeof(desc), &surface);
    if (rc) return rc;
    rc = aster_surface_acquire(surface, &b, sizeof(b));
    if (rc) return rc;
    aster_ge_init(&ge, surface, b.width, b.height, commands, 12, arena, sizeof(arena));
    stage = 0;
    return ASTER_OK;
}
static void draw(void)
{
    char label[80];
    uint32_t have = stage >= 5 ? total : offset + done_bytes;
    aster_ge_clear(&ge, ASTER_RGB565(16, 20, 32));
    aster_ge_text(&ge, 12, 34, 20, ASTER_WHITE, 0, "HTTP Resume");
    aster_snprintf(label, sizeof(label), "%s %u/%u a%u",
                   failed_flag ? "Failed" : stage == 7 ? "Complete" : pause_ui ? "Paused" : "Working",
                   have, total, attempts);
    aster_ge_text(&ge, 12, 68, 14, failed_flag ? ASTER_RED : ASTER_CYAN, 0, label);
    aster_ge_text(&ge, 12, 96, 12, ASTER_WHITE, 0, "Tap: pause/resume");
    aster_ge_rect(&ge, (aster_time_now_ms() / 15) % (ge.w - 24), 124, 24, 24, ASTER_GREEN);
    if (aster_ge_present(&ge) < 0) hard_fail("draw", ASTER_EIO);
}
void aster_app_event(const aster_event_t *e)
{
    if (e->type == ASTER_EVENT_TOUCH && e->u.touch.action == ASTER_TOUCH_UP) {
        pause_ui = !pause_ui;
        return;
    }
    if (e->type != ASTER_EVENT_FRAME) return;
    draw();
    /* Finish any retiring transport slot even when failed/paused, or the
     * terminal handle would retain the quota forever. */
    if (pump_close()) return;
    if (failed_flag || pause_ui) return;

    if (stage == 0) { /* measure the verified prefix on disk */
        aster_stat_t st = { .size = sizeof(st), .version = 1 };
        int32_t rc = aster_fs_stat(temp_path, sizeof(temp_path) - 1, &st, sizeof(st));
        if (!rc && !(st.mode & ASTER_STAT_DIR) && st.filesize <= BODY_LIMIT)
            offset = (uint32_t)st.filesize;
        else if (rc && rc != ASTER_ENOENT) { hard_fail("stat", rc); return; }
        else offset = 0;
        done_bytes = 0;
        stage = 1;
    }
    if (stage == 1) {
        headers[0] = 0;
        if (offset && !(etag[0] || modified[0])) {
            aster_logf(ASTER_LOG_INFO, "DLAPP no-validator restart");
            drop_temp(); /* cannot prove the prefix belongs to this version */
        } else if (offset) {
            int n = aster_snprintf(headers, sizeof(headers),
                                   "Range: bytes=%u-\r\nIf-Range: %s\r\n", offset,
                                   etag[0] ? etag : modified);
            if (n <= 0 || (uint32_t)n >= sizeof(headers)) { drop_temp(); headers[0] = 0; }
        }
        int32_t rc = aster_http_request(ASTER_HTTP_METHOD_GET, url, sizeof(url) - 1,
                                        headers[0] ? headers : 0,
                                        headers[0] ? (uint32_t)strlen(headers) : 0,
                                        0, 0, 60000, BODY_LIMIT);
        if (rc == ASTER_EAGAIN) return;
        if (rc < 0) { retry("request", rc); return; }
        request = rc;
        stage = 2;
    }
    if (stage == 2) {
        aster_http_status_t st;
        int32_t rc = aster_http_status(request, &st, sizeof(st));
        if (rc == ASTER_EAGAIN) return;
        if (rc) { retry("status", rc); return; }
        if (!(st.flags & ASTER_HTTP_FLAG_HEADERS)) return;
        if (st.status == 416) {
            uint64_t start = 0, t = 0;
            rc = read_header("Content-Range", value, sizeof(value));
            if (rc > 0 && parse_content_range(value, &start, &t) && t == offset) {
                retire_request();
                stage = 5; /* temp already complete: commit it */
                return;
            }
            aster_logf(ASTER_LOG_INFO, "DLAPP 416 restart");
            retire_request(); drop_temp(); stage = 0; return;
        }
        if (st.status == 200 && offset) {
            /* If-Range mismatched or Range ignored: prefix unverified. */
            aster_logf(ASTER_LOG_INFO, "DLAPP restart-from-zero");
            retire_request(); drop_temp(); stage = 0; return;
        }
        if (st.status == 200) {
            if (!(st.flags & ASTER_HTTP_FLAG_LENGTH) || st.content_length > BODY_LIMIT) {
                hard_fail("no-length", ASTER_EPROTO); return;
            }
            total = (uint32_t)st.content_length;
        } else if (st.status == 206) {
            uint64_t start = 0, t = 0;
            rc = read_header("Content-Range", value, sizeof(value));
            if (rc <= 0 || !parse_content_range(value, &start, &t) ||
                start != offset || t <= offset || t > BODY_LIMIT) {
                hard_fail("bad-content-range", ASTER_EPROTO); return;
            }
            total = (uint32_t)t;
        } else { hard_fail("http-status", -(int32_t)st.status); return; }
        /* validators for the next attempt; over-long values disable resume */
        if (read_header("ETag", etag, sizeof(etag)) <= 0) etag[0] = 0;
        if (!etag[0] && read_header("Last-Modified", modified, sizeof(modified)) <= 0)
            modified[0] = 0;
        uint32_t flags = ASTER_FS_O_WRONLY | ASTER_FS_O_CREAT | (offset ? 0u : ASTER_FS_O_TRUNC);
        rc = aster_fs_open(temp_path, sizeof(temp_path) - 1, flags, &file);
        if (rc) { hard_fail("open-temp", rc); return; }
        if (offset) {
            rc = aster_fs_seek(file, (int32_t)offset, ASTER_FS_SEEK_SET);
            if (rc) { drop_temp(); hard_fail("seek", rc); return; }
        }
        done_bytes = 0;
        stage = 3;
    }
    if (stage == 3) {
        aster_http_status_t st;
        int32_t rc = aster_http_read(request, buffer, sizeof(buffer));
        if (rc == ASTER_EAGAIN) return;
        if (rc < 0) { retry("read", rc); return; }
        if (rc > 0) {
#if HTTP_PATTERN
            for (int i = 0; i < rc; ++i)
                if (buffer[i] != (uint8_t)((offset + done_bytes + (uint32_t)i) % 251)) {
                    close_file(); retire_request();
                    aster_logf(ASTER_LOG_INFO, "DLAPP pattern restart");
                    drop_temp(); /* corrupt prefix: full redownload from zero */
                    if (++attempts >= HTTP_MAX_ATTEMPTS) hard_fail("pattern", ASTER_ECORRUPT);
                    else stage = 0;
                    return;
                }
#endif
            int32_t w = aster_fs_write(file, buffer, (uint32_t)rc);
            if (w != rc) { hard_fail("write", w); return; }
            done_bytes += (uint32_t)rc;
            if (total && offset + done_bytes > total) { hard_fail("overflow", ASTER_EMSGSIZE); return; }
            return;
        }
        rc = aster_http_status(request, &st, sizeof(st));
        if (rc == ASTER_EAGAIN) return;
        if (rc || st.state != ASTER_HTTP_STATE_DONE) { retry("final-status", rc ? rc : ASTER_EPROTO); return; }
        if (total && offset + done_bytes != total) { retry("short", ASTER_EMSGSIZE); return; }
        retire_request();
        close_file();
        offset += done_bytes;
        stage = 5;
    }
    if (stage == 5) { /* re-open the complete temp for verified commit */
        aster_stat_t st = { .size = sizeof(st), .version = 1 };
        int32_t rc = aster_fs_stat(temp_path, sizeof(temp_path) - 1, &st, sizeof(st));
        if (rc) { hard_fail("commit-stat", rc); return; }
        if (st.filesize != total) {
            aster_logf(ASTER_LOG_INFO, "DLAPP length-mismatch restart");
            drop_temp(); stage = 0; return;
        }
        rc = aster_fs_open(temp_path, sizeof(temp_path) - 1, ASTER_FS_O_RDONLY, &commit_src);
        if (rc) { hard_fail("commit-open", rc); return; }
        commit_pos = 0; commit_final_open = 0;
        stage = 6;
    }
    if (stage == 6) { /* verify-and-copy to final (rename is not public ABI) */
        if (!commit_final_open) {
            int32_t rc = aster_fs_open(final_path, sizeof(final_path) - 1,
                                       ASTER_FS_O_WRONLY | ASTER_FS_O_CREAT | ASTER_FS_O_TRUNC,
                                       &file);
            if (rc) { hard_fail("open-final", rc); return; }
            commit_final_open = 1;
        }
        int32_t r = aster_fs_read(commit_src, buffer, sizeof(buffer));
        if (r == ASTER_EAGAIN) return;
        if (r < 0) { hard_fail("commit-read", r); return; }
        if (r > 0) {
#if HTTP_PATTERN
            for (int i = 0; i < r; ++i)
                if (buffer[i] != (uint8_t)((commit_pos + (uint32_t)i) % 251)) {
                    (void)aster_fs_close(commit_src); commit_src = 0;
                    close_file(); commit_final_open = 0;
                    aster_logf(ASTER_LOG_INFO, "DLAPP temp-corrupt restart");
                    drop_temp();
                    if (++attempts >= HTTP_MAX_ATTEMPTS) hard_fail("temp-corrupt", ASTER_ECORRUPT);
                    else stage = 0;
                    return;
                }
#endif
            int32_t w = aster_fs_write(file, buffer, (uint32_t)r);
            if (w != r) { hard_fail("commit-write", w); return; }
            commit_pos += (uint32_t)r;
            return;
        }
        (void)aster_fs_close(commit_src); commit_src = 0;
        int32_t rc = aster_fs_close(file); file = 0; commit_final_open = 0;
        if (rc) { hard_fail("close-final", rc); return; }
        rc = aster_fs_unlink(temp_path, sizeof(temp_path) - 1);
        if (rc) { hard_fail("unlink-temp", rc); return; }
        stage = 7;
        aster_logf(ASTER_LOG_INFO, "DLAPP PASS bytes=%u attempts=%u", total, attempts);
    }
}
void aster_app_stop(void)
{
    /* .part stays on purpose: the next run resumes from verified bytes. */
    close_file();
    if (commit_src) (void)aster_fs_close(commit_src);
}
void aster_app_resume(void) {}
