/* SPDX-License-Identifier: MIT
 * trap - crash-isolation test app (verification only, not shipped).
 *
 * Creates a surface, runs a few frames, then executes `unreachable` in
 * aster_app_event. The runtime must catch the trap, mark the app CRASHED,
 * reclaim the surface/instance/memory, keep the shell alive, and allow
 * starting another app afterwards.
 */
#include <aster_imports.h>
#include <aster_gfx.h>

static aster_handle_t surf = ASTER_HANDLE_INVALID;
static uint32_t frames;

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_app_set_framerate(30);
	aster_surface_desc_t d = { .size = sizeof(d), .version = 1, .format = ASTER_PIXEL_NATIVE };
	aster_handle_t h = ASTER_HANDLE_INVALID;
	int32_t err = aster_surface_create(&d, sizeof(d), &h);
	if (err != ASTER_OK) {
		aster_logf(ASTER_LOG_ERROR, "surface_create: %d", err);
		return err;
	}
	surf = h;
	aster_surface_buffer_t buf = { .size = sizeof(buf), .version = 1 };
	err = aster_surface_acquire(surf, &buf, sizeof(buf));
	if (err == ASTER_OK) {
		aster_gfx_t gfx;
		aster_gfx_init(&gfx, buf.ptr, buf.width, buf.height, buf.pitch);
		aster_gfx_clear(&gfx, ASTER_RED);
	}
	aster_logf(ASTER_LOG_INFO, "trap: alive, will crash on frame 10");
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	if (surf != ASTER_HANDLE_INVALID) {
		aster_surface_destroy(surf);
		surf = ASTER_HANDLE_INVALID;
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	if (ev->type != ASTER_EVENT_FRAME) {
		return;
	}
	if (++frames == 10) {
		aster_logf(ASTER_LOG_INFO, "trap: crashing now");
		__builtin_trap(); /* wasm `unreachable` -> runtime trap */
	}
	aster_surface_present(surf);
}
