/* SPDX-License-Identifier: MIT
 * crt.c - ASTER app C runtime.
 *
 * Provides weak default lifecycle exports so a minimal app only has to
 * implement what it cares about, plus the __aster_abi version guard symbol.
 * Link with: -Wl,--export=aster_app_init -Wl,--export=aster_app_event ...
 */
#include <aster/aster.h>
#include <stddef.h>
#include <stdint.h>

/* ---- libc memory primitives: DEFINED here, not imported from env. ----
 * WAMR's AOT pipeline lowers `memcpy`/`memset` calls to its builtins with
 * the native-pointer convention; a user-registered env symbol of the same
 * name then shadows the builtin and receives already-native pointers,
 * failing app-offset validation (silent copy loss -> trap). Keeping these
 * as ordinary compiled-in functions makes interpreter and AOT behave
 * identically. gc-sections drops them from apps that never call them. */
void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	for (size_t i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	if (d == s || n == 0) {
		return dst;
	}
	if (d < s) {
		for (size_t i = 0; i < n; i++) {
			d[i] = s[i];
		}
	} else {
		for (size_t i = n; i > 0; i--) {
			d[i - 1] = s[i - 1];
		}
	}
	return dst;
}

void *memset(void *s, int c, size_t n)
{
	uint8_t *d = s;

	for (size_t i = 0; i < n; i++) {
		d[i] = (uint8_t)c;
	}
	return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *x = a;
	const uint8_t *y = b;

	for (size_t i = 0; i < n; i++) {
		if (x[i] != y[i]) {
			return (int)x[i] - (int)y[i];
		}
	}
	return 0;
}

/* ABI guard: exported data symbol checked by the ASTER loader. */
__attribute__((used)) volatile uint32_t __aster_abi = ASTER_ABI_VERSION;

__attribute__((weak)) int aster_app_init(void)
{
	return ASTER_OK;
}
__attribute__((weak)) void aster_app_start(void) {}
__attribute__((weak)) void aster_app_pause(void) {}
__attribute__((weak)) void aster_app_resume(void) {}
__attribute__((weak)) void aster_app_stop(void) {}
__attribute__((weak)) void aster_app_destroy(void) {}

/* Default event sink: request exit on the very first frame so a stub app
 * does not spin forever. */
__attribute__((weak)) void aster_app_event(const aster_event_t *ev)
{
	(void)ev;
	aster_app_exit(0);
}
