/* SPDX-License-Identifier: MIT
 * permdeny - ABI 1.2 negative gate matrix.
 *
 * This app declares NO permissions, so every gated API family must return
 * ASTER_EPERM on its very first call: fs (open/stat/opendir/mkdir/unlink),
 * kv (set/get/delete) and appmgr (count/list/start/uninstall). It proves
 * permissions went from "informational" (V1) to enforced, and that the gate
 * sits before any sandbox/path/handle work.
 *
 * Prints a PASS/FAIL line per check and exits with #fails. The ungated APIs
 * (log, time, system, display/input, timer) must keep working: we use them
 * to report, and assert one of each to catch an over-broad gate.
 */
#include <aster_imports.h>

static int g_pass, g_fail;

#define CHECK(cond, name, ...)                                             \
	do {                                                               \
		if (cond) {                                                \
			g_pass++;                                          \
			aster_logf(ASTER_LOG_INFO, "PASS " name, ##__VA_ARGS__);\
		} else {                                                   \
			g_fail++;                                          \
			aster_logf(ASTER_LOG_ERROR, "FAIL " name, ##__VA_ARGS__);\
		}                                                          \
	} while (0)

#define PERM(e) ((e) == ASTER_EPERM)

ASTER_APP_EXPORT int aster_app_init(void)
{
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_start(void)
{
	int32_t e;
	aster_handle_t h = ASTER_HANDLE_INVALID;
	aster_stat_t st;
	aster_app_info_t ai;
	uint8_t b[8];
	const char *p = "/data/x.bin";
	const char *k = "key";

	/* --- fs: every entry gated before sandbox/path work --- */
	e = aster_fs_open(p, 12, ASTER_FS_O_WRONLY | ASTER_FS_O_CREAT, &h);
	CHECK(PERM(e), "fs_open EPERM %d", e);
	e = aster_fs_stat(p, 12, &st, sizeof(st));
	CHECK(PERM(e), "fs_stat EPERM %d", e);
	e = aster_fs_mkdir(p, 12);
	CHECK(PERM(e), "fs_mkdir EPERM %d", e);
	e = aster_fs_unlink(p, 12);
	CHECK(PERM(e), "fs_unlink EPERM %d", e);
	e = aster_fs_opendir(p, 12, &h);
	CHECK(PERM(e), "fs_opendir EPERM %d", e);
	/* write/read/close on a bogus handle: gate must fire before EBADF */
	e = aster_fs_write(0x1000001, b, sizeof(b));
	CHECK(PERM(e), "fs_write EPERM (pre-EBADF) %d", e);
	e = aster_fs_read(0x1000001, b, sizeof(b));
	CHECK(PERM(e), "fs_read EPERM (pre-EBADF) %d", e);
	e = aster_fs_close(0x1000001);
	CHECK(PERM(e), "fs_close EPERM (pre-EBADF) %d", e);

	/* --- kv --- */
	e = aster_kv_set(k, 3, b, sizeof(b));
	CHECK(PERM(e), "kv_set EPERM %d", e);
	e = aster_kv_get(k, 3, b, sizeof(b));
	CHECK(PERM(e), "kv_get EPERM %d", e);
	e = aster_kv_delete(k, 3);
	CHECK(PERM(e), "kv_delete EPERM %d", e);

	/* --- appmgr --- */
	e = aster_appmgr_count();
	CHECK(PERM(e), "appmgr_count EPERM %d", e);
	e = aster_appmgr_list(0, &ai, sizeof(ai));
	CHECK(PERM(e), "appmgr_list EPERM %d", e);
	e = aster_appmgr_start("aster.clock", 9);
	CHECK(PERM(e), "appmgr_start EPERM %d", e);
	e = aster_appmgr_uninstall("aster.clock", 9);
	CHECK(PERM(e), "appmgr_uninstall EPERM %d", e);

	/* --- ungated APIs must still work (catch an over-broad gate) --- */
	e = aster_system_get_abi_version();
	CHECK(e >= 0x00010200, "abi_version %x ungated", e);
	e = aster_display_count();
	CHECK(e >= 0, "display_count %d ungated", e);
	CHECK(aster_time_now_ms() >= 0, "time_now_ms ungated");
	h = ASTER_HANDLE_INVALID;
	e = aster_timer_create(200, 0, 0, &h);
	CHECK(e == ASTER_OK, "timer_create %d ungated", e);

	aster_logf(ASTER_LOG_INFO, "PERMDENY done pass=%d fail=%d", g_pass, g_fail);
	aster_app_exit(g_fail);
}
