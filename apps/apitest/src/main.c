/* SPDX-License-Identifier: MIT
 * apitest - covers the ASTER ABI 1.0 surface no other sample exercises:
 * aster_display_count/get_info, aster_input_get_info/touch, aster_fs_seek/unlink
 * (incl. 3 KB DMA-bounce boundary writes), aster_kv_delete, and the standalone
 * aster_timer_create/destroy path with ASTER_EVENT_TIMER delivery.
 *
 * Synchronous checks run in aster_app_start; a repeating timer then verifies
 * event delivery + clean destroy, after which the app exits with #fails.
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

#define FNAME  "/data/apitest.bin"
#define FLEN   3000 /* spans several 1 KB DMA bounce chunks */

static uint8_t fbuf[FLEN];
static uint8_t rbuf[FLEN];

static uint8_t pat(uint32_t i)
{
	return (uint8_t)(i * 7u + 3u);
}

static void fs_tests(void)
{
	aster_handle_t h = ASTER_HANDLE_INVALID;
	/* RDWR: the seek tests read back through the same handle */
	int32_t e = aster_fs_open(FNAME, sizeof(FNAME) - 1,
				ASTER_FS_O_RDWR | ASTER_FS_O_CREAT | ASTER_FS_O_TRUNC, &h);
	CHECK(e == ASTER_OK, "fs_open create %d", e);
	if (e != ASTER_OK) {
		return;
	}

	for (uint32_t i = 0; i < FLEN; i++) {
		fbuf[i] = pat(i);
	}
	int32_t w = aster_fs_write(h, fbuf, FLEN);
	CHECK(w == FLEN, "fs_write 3KB (DMA bounce) %d", w);

	/* SEEK_SET across the bounce boundary */
	e = aster_fs_seek(h, 1000, ASTER_FS_SEEK_SET);
	int32_t n = aster_fs_read(h, rbuf, 4);
	CHECK(e == ASTER_OK && n == 4 && rbuf[0] == pat(1000) && rbuf[3] == pat(1003),
	      "fs_seek SET+read %d/%d", e, n);

	/* SEEK_CUR backwards from current (1004) -> 996 */
	e = aster_fs_seek(h, -8, ASTER_FS_SEEK_CUR);
	n = aster_fs_read(h, rbuf, 8);
	CHECK(e == ASTER_OK && n == 8 && rbuf[0] == pat(996),
	      "fs_seek CUR-back %d/%d", e, n);

	/* SEEK_END negative */
	e = aster_fs_seek(h, -4, ASTER_FS_SEEK_END);
	n = aster_fs_read(h, rbuf, 4);
	CHECK(e == ASTER_OK && n == 4 && rbuf[0] == pat(FLEN - 4) && rbuf[3] == pat(FLEN - 1),
	      "fs_seek END %d/%d", e, n);

	/* read at EOF == 0 */
	n = aster_fs_read(h, rbuf, 4);
	CHECK(n == 0, "fs_read EOF returns 0 %d", n);

	aster_fs_close(h);

	/* stat reflects size */
	aster_stat_t st = { .size = sizeof(st), .version = 1 };
	e = aster_fs_stat(FNAME, sizeof(FNAME) - 1, &st, sizeof(st));
	CHECK(e == ASTER_OK && st.filesize == FLEN, "fs_stat size %d/%u", e,
	      (uint32_t)st.filesize);

	/* reopen RDONLY, verify content start */
	e = aster_fs_open(FNAME, sizeof(FNAME) - 1, ASTER_FS_O_RDONLY, &h);
	n = aster_fs_read(h, rbuf, 16);
	aster_fs_close(h);
	CHECK(e == ASTER_OK && n == 16 && rbuf[0] == pat(0) && rbuf[15] == pat(15),
	      "reopen+read head %d/%d", e, n);

	/* unlink works, double unlink -> ENOENT */
	e = aster_fs_unlink(FNAME, sizeof(FNAME) - 1);
	CHECK(e == ASTER_OK, "fs_unlink %d", e);
	e = aster_fs_stat(FNAME, sizeof(FNAME) - 1, &st, sizeof(st));
	CHECK(e == ASTER_ENOENT, "stat after unlink == ENOENT %d", e);
	e = aster_fs_unlink(FNAME, sizeof(FNAME) - 1);
	CHECK(e == ASTER_ENOENT, "double unlink == ENOENT %d", e);

	/* opening a missing file without CREAT must fail (<0) */
	e = aster_fs_open("/data/nope.bin", 12, ASTER_FS_O_RDONLY, &h);
	CHECK(e < 0, "open missing fails %d", e);

	/* sandbox escape still refused */
	e = aster_fs_open("/SD:/evil", 9, ASTER_FS_O_WRONLY | ASTER_FS_O_CREAT, &h);
	CHECK(e == ASTER_EPERM, "sandbox escape refused %d", e);

	/* /sd requires permissions=["sd"]; apitest only has fs */
	e = aster_fs_opendir("/sd", 3, &h);
	CHECK(e == ASTER_EPERM, "opendir /sd without sd perm %d", e);
	e = aster_fs_open("/sd", 3, ASTER_FS_O_RDONLY, &h);
	CHECK(e == ASTER_EPERM, "open /sd without sd perm %d", e);
}

static void kv_tests(void)
{
	static const uint8_t val[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
	int32_t e = aster_kv_set("tk", 2, val, 4);
	CHECK(e == ASTER_OK, "kv_set %d", e);

	uint8_t out[8] = { 0 };
	e = aster_kv_get("tk", 2, out, sizeof(out));
	CHECK(e == 4 && out[0] == 0xDE && out[3] == 0xEF, "kv_get roundtrip %d", e);

	e = aster_kv_delete("tk", 2);
	CHECK(e == ASTER_OK, "kv_delete %d", e);

	e = aster_kv_get("tk", 2, out, sizeof(out));
	CHECK(e == ASTER_ENOENT, "kv_get after delete == ENOENT %d", e);

	e = aster_kv_delete("tk", 2);
	CHECK(e == ASTER_OK, "kv_delete idempotent %d", e);
}

static aster_handle_t g_timer = ASTER_HANDLE_INVALID;
static uint32_t g_timer_hits;
static uint32_t g_timer_last_ms;
static int g_timer_destroyed;

/* key-edge capture (async verification of ASTER_EVENT_KEY delivery) */
static int g_keys_seen;
static int g_expect_keys;
static uint32_t g_key_down;
static uint32_t g_key_up;
static uint32_t g_key_code;

static void display_input_tests(void)
{
	int32_t cnt = aster_display_count();
	CHECK(cnt >= 1, "display_count %d", cnt);

	aster_display_info_t di = { 0 };
	int32_t e = aster_display_get_info(0, &di, sizeof(di));
	/* Resolution-agnostic: the logical geometry follows the board's
	 * rotation (reference port scans 320x240, rot=90 -> logical 240x320).
	 * Assert only that info is sane + orientation matches the aspect. */
	CHECK(e == ASTER_OK && di.width > 0 && di.height > 0,
	      "display_get_info 0 -> %ux%u (%d)", di.width, di.height, e);
	int portrait = di.height > di.width;
	CHECK((di.orientation == ASTER_ORIENT_PORTRAIT) == portrait,
	      "orientation %u matches %ux%u aspect", di.orientation,
	      di.width, di.height);

	/* [3.2] app_width/app_height are the space surfaces are created in and
	 * touch is reported in. Two properties are load-bearing, not cosmetic:
	 *
	 *   - app space is NEVER larger than the glass. A board scales DOWN
	 *     (app space 240x240 on a 720x720 panel); scaling up is the other
	 *     design and it was rejected -- it multiplies rasterisation on the
	 *     fastest board. A frame where this is false means a board told the
	 *     app it has more room than it does.
	 *   - if it is smaller, it is a whole-number divisor on BOTH axes and
	 *     the SAME one. That is what makes the magnify affine (dst = src *
	 *     DIV) rather than a lookup table, and what makes the touch inverse
	 *     unambiguous. It is asserted at build time in the port too, but a
	 *     host-side check here is what catches a port that forgot to. */
	CHECK(di.app_width > 0 && di.app_height > 0 &&
	      di.app_width <= di.width && di.app_height <= di.height,
	      "app space %ux%u <= glass %ux%u", di.app_width, di.app_height,
	      di.width, di.height);
	CHECK(di.width % di.app_width == 0 && di.height % di.app_height == 0 &&
	      di.width / di.app_width == di.height / di.app_height,
	      "app space %ux%u divides glass %ux%u evenly on both axes",
	      di.app_width, di.app_height, di.width, di.height);

	/* Truncated request: bytes past the caller's size must be left alone.
	 * Offsets come from offsetof() rather than a literal so that a field
	 * added in a later minor cannot silently turn this into a tautology --
	 * a literal would still compile, and would stop testing anything. */
	aster_display_info_t part;
	memset(&part, 0xA5, sizeof(part));
	e = aster_display_get_info(0, &part,
				 (uint32_t)offsetof(aster_display_info_t, app_width));
	CHECK(e == ASTER_OK && part.width == di.width && part.height == di.height &&
	      part.app_width == 0xA5A5A5A5u && part.app_height == 0xA5A5A5A5u,
	      "prefix fill stops before app_width (0x%x)", part.app_width);

	e = aster_display_get_info(99, &di, sizeof(di));
	CHECK(e < 0, "display_get_info bad index fails %d", e);

	aster_input_info_t ii = { 0 };
	e = aster_input_get_info(&ii, sizeof(ii));
	CHECK(e == ASTER_OK && (ii.kinds & ASTER_INPUT_TOUCH),
	      "input_get_info kinds=0x%x max_pts=%u (%d)", ii.kinds, ii.max_points, e);

	/* poll touch: 0..2 points is legal with no finger down; must be >=0 */
	uint32_t pts[2 * 4];
	e = aster_input_touch((uint32_t)(uintptr_t)pts, 2);
	CHECK(e >= 0, "input_touch poll n=%d", e);

	/* keyboard / buttons (ABI 1.1): capability bit + state poll must be
	 * consistent. kinds must report KEYS iff feature KEYBOARD present. */
	aster_input_info_t ki = { 0 };
	e = aster_input_get_info(&ki, sizeof(ki));
	int has_kbd = aster_system_has_feature(ASTER_FEATURE_KEYBOARD);
	CHECK(e == ASTER_OK &&
	      (((ki.kinds & ASTER_INPUT_KEYS) != 0) == (has_kbd != 0)),
	      "keys kind==keyboard feature kinds=0x%x kbd=%d (%d)", ki.kinds, has_kbd, e);
	uint32_t codes[8];
	e = aster_input_keys_state((uint32_t)(uintptr_t)codes, 8);
	CHECK(e >= 0, "input_keys_state poll n=%d", e);
}

/* --- appmgr (ABI 1.2): apitest declares permissions=["fs","kv","appmgr"],
 * so these must be granted and the registry readable. Bad index / self-start
 * / unknown-id must be rejected without launching anything. */
static void appmgr_tests(void)
{
	int32_t n = aster_appmgr_count();
	CHECK(n >= 1, "appmgr_count %d (gate open)", n);

	aster_app_info_t ai = { 0 };
	int32_t e = aster_appmgr_list(0, &ai, sizeof(ai));
	CHECK(e == ASTER_OK && ai.id[0] != 0 && ai.name[0] != 0,
	      "appmgr_list[0] '%s' name='%s' state=%u (%d)",
	      ai.id, ai.name, ai.state, e);

	e = aster_appmgr_list(9999, &ai, sizeof(ai));
	CHECK(e < 0, "appmgr_list bad index fails %d", e);

	e = aster_appmgr_start("aster.apitest", (uint32_t)(sizeof("aster.apitest") - 1));
	CHECK(e == ASTER_EINVAL, "appmgr_start(self) == EINVAL %d", e);
	e = aster_appmgr_start("no.such.app", (uint32_t)(sizeof("no.such.app") - 1));
	CHECK(e == ASTER_ENOENT, "appmgr_start(unknown) == ENOENT %d", e);
	e = aster_appmgr_uninstall("no.such.app", (uint32_t)(sizeof("no.such.app") - 1));
	CHECK(e == ASTER_ENOENT, "appmgr_uninstall(unknown) == ENOENT %d", e);

	/* The frozen "id_len or -1" form (`types.h`): the string runs to its
	 * NUL instead of carrying a length. It must resolve the *same* id, so
	 * the answers above repeat -- and len 0 is the one value that is NOT
	 * that form, so it must be rejected rather than treated as "scan". */
	e = aster_appmgr_start("no.such.app", (uint32_t)-1);
	CHECK(e == ASTER_ENOENT, "appmgr_start(nul-term) == ENOENT %d", e);
	e = aster_appmgr_uninstall("no.such.app", (uint32_t)-1);
	CHECK(e == ASTER_ENOENT, "appmgr_uninstall(nul-term) == ENOENT %d", e);
	e = aster_appmgr_uninstall("no.such.app", 0);
	CHECK(e == ASTER_EINVAL, "appmgr_uninstall(len=0) == EINVAL %d", e);
}

/* ABI 1.3 motion sensors (pull model). Capability-driven: on boards without
 * an IMU both reads must fail with ASTER_ENODEV instead of trapping. */
static void sensor_tests(void)
{
	int32_t has_a = aster_system_has_feature(ASTER_FEATURE_ACCELEROMETER);
	int32_t has_g = aster_system_has_feature(ASTER_FEATURE_GYROSCOPE);

	float a[3] = { 0 }, g[3] = { 0 };
	int32_t e = aster_accel_read(a, sizeof(a));
	if (has_a) {
		CHECK(e == ASTER_OK, "accel_read %d", e);
		float m2 = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
		/* at rest the vector magnitude ~ g (accept 5..15 m/s^2).
		 * Printed as milli-units, not "%.1f": the guest's logf has no
		 * float formatting, so a %f in the message comes out as the
		 * literal characters -- an assertion whose evidence line reads
		 * "|a|^2=%.1f in [25,225]" looks like a broken test even when
		 * the test passed. Integer math keeps the message true. */
		CHECK(m2 > 25.0f && m2 < 225.0f, "accel |a|^2=%d (milli) in [25000,225000]",
		      (int)(m2 * 1000.0f));
		e = aster_accel_read(a, 8); /* short buffer rejected */
		CHECK(e == ASTER_EINVAL, "accel_read size=8 == EINVAL %d", e);
	} else {
		CHECK(e == ASTER_ENODEV, "no accel -> ENODEV %d", e);
	}

	e = aster_gyro_read(g, sizeof(g));
	if (has_g) {
		CHECK(e == ASTER_OK, "gyro_read %d", e);
	} else {
		CHECK(e == ASTER_ENODEV, "no gyro -> ENODEV %d", e);
	}
	/* second accel read hits the 50 ms host cache: must stay ASTER_OK */
	if (has_a) {
		e = aster_accel_read(a, sizeof(a));
		CHECK(e == ASTER_OK, "accel_read cached %d", e);
		/* milli-units again, for the same reason as the |a|^2 line. */
		aster_logf(ASTER_LOG_INFO, "apitest: a=(%d,%d,%d) g=(%d,%d,%d) milli",
			 (int)(a[0] * 1000.0f), (int)(a[1] * 1000.0f),
			 (int)(a[2] * 1000.0f), (int)(g[0] * 1000.0f),
			 (int)(g[1] * 1000.0f), (int)(g[2] * 1000.0f));
	}
}

/* ABI 1.3 camera (single-shot still). Blocking: called once from start(),
 * never from a frame callback. Frame sanity = not uniformly one value
 * (real sensor noise/scene always leaves >=2 distinct samples). */
static uint8_t g_frame[320 * 240 * 2];

static void camera_tests(void)
{
	int32_t has_cam = aster_system_has_feature(ASTER_FEATURE_CAMERA);
	uint32_t w = 0, h = 0;
	int32_t e = aster_cam_info(&w, &h);
	if (!has_cam) {
		CHECK(e == ASTER_ENODEV, "no cam -> info ENODEV %d", e);
		e = aster_cam_read(g_frame, sizeof(g_frame));
		CHECK(e == ASTER_ENODEV, "no cam -> read ENODEV %d", e);
		return;
	}
	CHECK(e == ASTER_OK && w > 0 && h > 0, "cam_info %u x %u (e=%d)", w, h, e);
	CHECK((int32_t)(w * h * 2) <= (int32_t)sizeof(g_frame), "frame fits probe buffer");

	e = aster_cam_read(g_frame, 1024); /* too small */
	CHECK(e == ASTER_EINVAL, "cam_read small == EINVAL %d", e);

	e = aster_cam_read(g_frame, w * h * 2); /* real capture (~1-2 s block) */
	CHECK(e == ASTER_OK, "cam_read frame %d", e);
	if (e == ASTER_OK) {
		uint16_t *px = (uint16_t *)g_frame;
		uint16_t first = px[0];
		int varied = 0;
		uint32_t lum = 0;
		for (uint32_t i = 0; i < w * h && !varied; i += 997) {
			if (px[i] != first) {
				varied = 1;
			}
		}
		for (uint32_t i = 0; i < w * h; i += 4001) {
			lum += (px[i] >> 5) & 0x3F; /* ~green5 sample */
		}
		CHECK(varied, "frame has sensor variation");
		aster_logf(ASTER_LOG_INFO, "apitest: cam %ux%u sampled-lum=%u", w, h,
			 (unsigned)(lum / ((w * h) / 4001 + 1)));
	}
}

/* ABI 2.0 audio surface: volume + background-session state machine +
 * blocking record. Playback of real media runs through
 * aster_audio_start_file (needs an SD file; covered by the music app), so
 * apitest only asserts the idle state machine and error paths.
 * `aster_audio_get_info/play/start` were removed in 2.0. */
static int16_t g_pcm[512]; /* 256 stereo frames = 16 ms @ 16 kHz */

static void audio_tests(void)
{
	int32_t has_out = aster_system_has_feature(ASTER_FEATURE_AUDIO_OUTPUT);
	int32_t has_in = aster_system_has_feature(ASTER_FEATURE_AUDIO_INPUT);
	int32_t e;

	if (!has_out && !has_in) {
		e = aster_audio_record((uint8_t *)g_pcm, sizeof(g_pcm));
		CHECK(e == ASTER_ENODEV, "no audio -> record ENODEV %d", e);
		e = aster_audio_set_volume(40);
		CHECK(e == ASTER_ENODEV, "no audio -> vol ENODEV %d", e);
		e = aster_audio_pause();
		CHECK(e == ASTER_ENODEV, "no audio -> pause ENODEV %d", e);
		return;
	}
	e = aster_audio_record((uint8_t *)g_pcm, 2);
	CHECK(e == ASTER_EINVAL, "record size%%4 -> EINVAL %d", e);

	if (has_out) {
		e = aster_audio_set_volume(-1);
		CHECK(e == ASTER_EINVAL, "vol -1 -> EINVAL %d", e);
		e = aster_audio_set_volume(101);
		CHECK(e == ASTER_EINVAL, "vol 101 -> EINVAL %d", e);
		e = aster_audio_set_volume(40);
		CHECK(e == ASTER_OK, "vol set 40 %d", e);
		e = aster_audio_get_volume();
		CHECK(e == 40, "vol get %d", e);
		e = aster_audio_set_volume(70);
		CHECK(e == ASTER_OK, "vol restore 70 %d", e);

		/* idle state machine (no session started) */
		e = aster_audio_pause();
		CHECK(e == ASTER_ESTATE, "pause idle -> ESTATE %d", e);
		e = aster_audio_resume();
		CHECK(e == ASTER_ESTATE, "resume idle -> ESTATE %d", e);
		e = aster_audio_stop();
		CHECK(e == ASTER_OK, "stop idle %d", e);
		e = aster_audio_seek(1000);
		CHECK(e == ASTER_ESTATE, "seek idle -> ESTATE %d", e);

		aster_audio_state_t st;
		memset(&st, 0, sizeof(st));
		e = aster_audio_get_state(&st, 4);
		CHECK(e == ASTER_EINVAL, "get_state small -> EINVAL %d", e);
		e = aster_audio_get_state(&st, sizeof(st));
		CHECK(e == ASTER_OK && st.state == ASTER_AUDIO_IDLE,
		      "idle state %u (e=%d)", st.state, e);

		/* [3.2] The native format is asked for, not assumed. Two things
		 * worth pinning: a size below the size+version floor is refused
		 * (not filled halfway with garbage), and a size between the floor
		 * and the full struct is filled as far as it goes -- that is the
		 * promise that lets this struct grow later. */
		aster_audio_info_t ai;
		memset(&ai, 0, sizeof(ai));
		e = aster_audio_get_info(&ai, 4);
		CHECK(e == ASTER_EINVAL, "get_info small -> EINVAL %d", e);
		e = aster_audio_get_info(&ai, 8);
		CHECK(e == ASTER_OK && ai.size == sizeof(ai) && ai.version == 1,
		      "get_info prefix %u/%u (e=%d)", ai.size, ai.version, e);
		memset(&ai, 0, sizeof(ai));
		e = aster_audio_get_info(&ai, sizeof(ai));
		CHECK(e == ASTER_OK && ai.sample_rate > 0 && ai.channels >= 1 &&
		      ai.channels <= 8 && ai.bits >= 8 && ai.bits <= 32,
		      "native fmt %u Hz %u ch %u bit (e=%d)",
		      ai.sample_rate, ai.channels, ai.bits, e);
	} else {
		e = aster_audio_set_volume(40);
		CHECK(e == ASTER_ENODEV, "no out -> vol ENODEV %d", e);
	}
	if (has_in) {
		e = aster_audio_record((uint8_t *)g_pcm, sizeof(g_pcm));
		CHECK(e == ASTER_OK, "audio_record short %d", e);
	}
}

/* ABI 1.4 dirty-region flush: create an RGB565 surface, push a small square
 * straight from linear memory, and exercise the bounds/stride contract. The
 * visible red square at (8,8) is the on-device confirmation; the error codes
 * are the automated half. */
static void surface_flush_tests(void)
{
	if (!aster_system_has_feature(ASTER_FEATURE_DISPLAY)) {
		return; /* headless profile: nothing to flush onto */
	}
	aster_surface_desc_t d = { .size = sizeof(d), .version = 1,
				 .width = 0, .height = 0,
				 .format = ASTER_PIXEL_RGB565, .flags = 0 };
	aster_handle_t s = ASTER_HANDLE_INVALID;
	int32_t e = aster_surface_create(&d, sizeof(d), &s);
	CHECK(e == ASTER_OK && s != ASTER_HANDLE_INVALID, "surface_create %d h=0x%x",
	      e, s);
	if (e != ASTER_OK) {
		return;
	}

	static uint16_t sq[16 * 16];
	for (unsigned i = 0; i < 16 * 16; i++) {
		sq[i] = 0xF800; /* pure red, LE 565 */
	}
	e = aster_surface_flush(s, sq, 8, 8, 16, 16, 0); /* pitch 0 == w */
	CHECK(e == ASTER_OK, "flush 16x16 red @8,8 %d", e);

	e = aster_surface_flush(s, sq, 30, 8, 16, 16, 16); /* explicit pitch */
	CHECK(e == ASTER_OK, "flush explicit pitch %d", e);

	e = aster_surface_flush(s, sq, 0xFFFF, 0, 16, 16, 0); /* off-surface */
	CHECK(e == ASTER_EINVAL, "flush OOB == EINVAL %d", e);

	e = aster_surface_flush(s, sq, 8, 8, 16, 16, 4); /* stride < width */
	CHECK(e == ASTER_EINVAL, "flush pitch<w == EINVAL %d", e);

	e = aster_surface_flush(s, sq, 8, 8, 0, 16, 0); /* empty region */
	CHECK(e == ASTER_EINVAL, "flush w=0 == EINVAL %d", e);

	aster_surface_destroy(s);
}

/* Present a non-native canvas that is bigger than the firmware's heap.
 *
 * aster_surface_present() converts a non-RGB565 surface to panel order through
 * a scratch buffer on the firmware's DRAM heap, and that scratch used to be
 * one k_malloc(w*h*2) for the entire frame. The heap is 65,536 B, so a full
 * 240x320 present asked for 153,600 B -- more than twice the whole heap -- and
 * every one of them returned ASTER_ENOMEM. Nothing exercised it, so nothing
 * noticed: the shipped apps all present RGB565, which takes a different path.
 *
 * That makes the *size* the thing under test, not the format. A small canvas
 * would pass against the broken firmware (240x135 needs 64,800 B, which fit
 * by 736 B), so every case below is checked to exceed the pool first, and the
 * sizes are derived from the display's own geometry so this holds on a board
 * with a different panel. The fix converts a band at a time, making the
 * request independent of canvas size; a regression here would show up as
 * ENOMEM on the first case, on any board.
 *
 * The scratch is w*h*2 whatever the source format is (it holds the RGB565
 * result), so the canvas *pixel count* is what has to clear the pool: over
 * 32,768 px. The formats differ in the source bytes they need and how wide a
 * band step that makes.
 *
 * The solid fill is the visible half: the converted frame should reach the
 * glass uniformly coloured, with no unconverted stripes at a band edge. */
static void surface_present_tests(void)
{
	if (!aster_system_has_feature(ASTER_FEATURE_DISPLAY)) {
		return; /* headless profile: nothing to present onto */
	}
	aster_display_info_t di = { 0 };
	if (aster_display_get_info(0, &di, sizeof(di)) != ASTER_OK ||
	    di.width == 0 || di.height == 0) {
		return;
	}

	const uint32_t POOL = 65536; /* CONFIG_HEAP_MEM_POOL_SIZE */

	struct {
		uint32_t fmt, w, h, bpp, fill;
		const char *name;
	} cases[] = {
		{ ASTER_PIXEL_BGR565, 0, 0, 2, 0x1f, "BGR565 full screen" },
		{ ASTER_PIXEL_RGB888, 0, di.height / 2, 3, 0x3f, "RGB888" },
		{ ASTER_PIXEL_RGBA8888, 0, di.height / 2, 4, 0x7f, "RGBA8888" },
	};

	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		uint32_t cw = cases[i].w ? cases[i].w : di.width;
		uint32_t ch = cases[i].h ? cases[i].h : di.height;
		uint32_t conv = cw * ch * 2u; /* the old whole-frame scratch size */

		/* The premise of the test: this canvas would not have fit. If a
		 * future panel makes it fit, the case stops testing anything --
		 * resize it rather than quietly keep a test that asserts nothing.
		 * (This is what caught the first draft of these cases: half-height
		 * RGB888 and quarter-height RGBA8888 land under 65,536 B.) */
		CHECK(conv > POOL, "%s %ux%u: scratch %u vs heap %u (must exceed)",
		      cases[i].name, cw, ch, conv, POOL);

		aster_surface_desc_t d = { .size = sizeof(d), .version = 1,
					 .width = cases[i].w, .height = cases[i].h,
					 .format = cases[i].fmt, .flags = 0 };
		aster_handle_t s = ASTER_HANDLE_INVALID;
		int32_t e = aster_surface_create(&d, sizeof(d), &s);
		CHECK(e == ASTER_OK, "%s %ux%u: create %d", cases[i].name, cw, ch, e);
		if (e != ASTER_OK) {
			continue;
		}

		aster_surface_buffer_t sb = { .size = sizeof(sb), .version = 1 };
		e = aster_surface_acquire(s, &sb, sizeof(sb));
		CHECK(e == ASTER_OK && sb.ptr && sb.width == cw && sb.height == ch &&
			      sb.pitch == cw * cases[i].bpp,
		      "%s: acquire %d %ux%u pitch=%u", cases[i].name, e, sb.width,
		      sb.height, sb.pitch);

		if (e == ASTER_OK && sb.ptr) {
			memset(sb.ptr, cases[i].fill, (size_t)sb.pitch * sb.height);
			e = aster_surface_present(s);
			CHECK(e == ASTER_OK, "%s: present %d", cases[i].name, e);
		}
		aster_surface_destroy(s);
	}
}

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_logf(ASTER_LOG_INFO, "apitest: init");
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_start(void)
{
	aster_app_set_framerate(30);

	display_input_tests();
	fs_tests();
	kv_tests();
	appmgr_tests();
	sensor_tests();
	camera_tests();
	audio_tests();
	surface_flush_tests();
	surface_present_tests();

	/* Key edges injected before/while running persist in the platform ring
	 * and are drained on our loop, so we capture them passively and assert
	 * at exit only if the device reported a keyboard AND a key arrived. */
	g_expect_keys = aster_system_has_feature(ASTER_FEATURE_KEYBOARD);

	/* async part: repeating timer -> ASTER_EVENT_TIMER -> destroy */
	int32_t e = aster_timer_create(200, 1, 0xABCD, &g_timer);
	CHECK(e == ASTER_OK, "timer_create %d h=0x%x", e, g_timer);
	if (e != ASTER_OK) {
		aster_logf(ASTER_LOG_ERROR, "APITEST done pass=%d fail=%d", g_pass, g_fail + 1);
		aster_app_exit(1);
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	if (ev->type == ASTER_EVENT_TIMER) {
		if (g_timer_destroyed) {
			aster_logf(ASTER_LOG_ERROR, "FAIL timer fired after destroy");
			g_fail++;
			aster_app_exit(g_fail);
			return;
		}
		g_timer_hits++;
		g_timer_last_ms = aster_time_now_ms();
		CHECK(ev->u.timer.id == g_timer && ev->u.timer.user == 0xABCD,
		      "timer evt #%u id/user ok", g_timer_hits);
		if (g_timer_hits == 3) {
			int32_t e = aster_timer_destroy(g_timer);
			CHECK(e == ASTER_OK, "timer_destroy %d", e);
			e = aster_timer_destroy(g_timer);
			CHECK(e == ASTER_EBADF, "double destroy == EBADF %d", e);
			g_timer_destroyed = 1;
		}
	} else if (ev->type == ASTER_EVENT_KEY) {
		g_keys_seen++;
		g_key_code = ev->u.key.code;
		if (ev->u.key.pressed) {
			g_key_down++;
		} else {
			g_key_up++;
		}
	} else if (ev->type == ASTER_EVENT_FRAME) {
		/* give any rogue post-destroy firing 500 ms to show up */
		if (g_timer_destroyed && aster_time_now_ms() - g_timer_last_ms > 500) {
			/* end-to-end ASTER_EVENT_KEY delivery: asserted only if a
			 * key actually arrived (injected by the harness); a plain
			 * run without injection must stay green. */
			if (g_keys_seen) {
				CHECK(g_key_down >= 1 && g_key_up >= 1,
				      "key e2e down=%u up=%u code=%u",
				      g_key_down, g_key_up, g_key_code);
			}
			aster_logf(ASTER_LOG_INFO,
				 "APITEST keys expect=%d seen=%d down=%u up=%u code=%u",
				 g_expect_keys, g_keys_seen, g_key_down, g_key_up,
				 g_key_code);
			aster_logf(ASTER_LOG_INFO, "APITEST done pass=%d fail=%d", g_pass, g_fail);
			aster_app_exit(g_fail);
		}
	}
}
