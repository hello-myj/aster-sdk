/* SPDX-License-Identifier: MIT */
#include "aster_printf.h"
#include "aster_libc.h"
#include <aster/app.h>
#include <stdarg.h>
#include <stdint.h>

static void emit(char *buf, int max, int *pos, const char *s, int n)
{
	if (buf) {
		for (int i = 0; i < n && *pos < max - 1; i++) {
			buf[(*pos)++] = s[i];
		}
	} else {
		*pos += n;
	}
}

static void emit_ch(char *buf, int max, int *pos, char c)
{
	if (buf && *pos < max - 1) {
		buf[(*pos)++] = c;
	} else if (!buf) {
		(*pos)++;
	}
}

static void put_num(char *buf, int max, int *pos, unsigned long v, int base, int neg,
		    int uppercase, int pad0, int width)
{
	char tmp[24];
	int n = 0;
	const char *dig = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
	if (v == 0) {
		tmp[n++] = '0';
	}
	while (v) {
		tmp[n++] = dig[v % base];
		v /= base;
	}
	if (neg) {
		tmp[n++] = '-';
	}
	int pad = width - n;
	while (pad-- > 0) {
		emit_ch(buf, max, pos, pad0 ? '0' : ' ');
	}
	for (int i = n - 1; i >= 0; i--) {
		emit_ch(buf, max, pos, tmp[i]);
	}
}

static int format(char *buf, int max, const char *fmt, va_list ap)
{
	int pos = 0;
	for (const char *p = fmt; *p; p++) {
		if (*p != '%') {
			emit_ch(buf, max, &pos, *p);
			continue;
		}
		p++;
		int zero = 0, width = 0, lmod = 0;
		while (*p == '0') {
			zero = 1;
			p++;
		}
		while (*p >= '0' && *p <= '9') {
			width = width * 10 + (*p - '0');
			p++;
		}
		if (*p == 'l') {
			lmod = 1;
			p++;
			if (*p == 'l') {
				p++;
			}
		}
		switch (*p) {
		case 'd':
		case 'i': {
			long v = lmod ? va_arg(ap, long) : (long)va_arg(ap, int);
			if (v < 0) {
				put_num(buf, max, &pos, (unsigned long)-v, 10, 1, 0, zero, width);
			} else {
				put_num(buf, max, &pos, (unsigned long)v, 10, 0, 0, zero, width);
			}
			break;
		}
		case 'u': {
			unsigned long v = lmod ? va_arg(ap, unsigned long)
						: (unsigned long)va_arg(ap, unsigned);
			put_num(buf, max, &pos, v, 10, 0, 0, zero, width);
			break;
		}
		case 'x':
		case 'X': {
			unsigned long v = lmod ? va_arg(ap, unsigned long)
						: (unsigned long)va_arg(ap, unsigned);
			put_num(buf, max, &pos, v, 16, 0, *p == 'X', zero, width);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) {
				s = "(null)";
			}
			emit(buf, max, &pos, s, (int)strlen(s));
			break;
		}
		case 'c':
			emit_ch(buf, max, &pos, (char)va_arg(ap, int));
			break;
		case '%':
			emit_ch(buf, max, &pos, '%');
			break;
		default:
			/* Not a conversion this formatter knows: "%!" + the character
			 * that stopped it, the way libc marks a malformed specifier.
			 *
			 * It used to be copied through verbatim, and THAT was the bug:
			 * `"|a|^2=%.1f"` came out as the literal text "|a|^2=%.1f" --
			 * an app's evidence line that reads like a number the test
			 * never printed, i.e. a wrong report dressed as a right one.
			 * There is no float support here and there is not going to be
			 * (this formatter exists to avoid dragging newlib into the
			 * package), so the honest thing is to make the gap visible in
			 * the place the mistake was made. It shows up in logs and in
			 * any on-screen string the same way. */
			emit_ch(buf, max, &pos, '%');
			emit_ch(buf, max, &pos, '!');
			emit_ch(buf, max, &pos, *p);
			break;
		}
	}
	if (buf) {
		buf[pos < max ? pos : max - 1] = '\0';
	}
	return pos;
}

int aster_snprintf(char *buf, int max, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = format(buf, max, fmt, ap);
	va_end(ap);
	if (buf && n >= max) {
		buf[max - 1] = '\0';
	}
	return n;
}

void aster_logf(int level, const char *fmt, ...)
{
	char buf[160];
	va_list ap;
	va_start(ap, fmt);
	int n = format(buf, (int)sizeof(buf), fmt, ap);
	va_end(ap);
	if (n >= (int)sizeof(buf)) {
		n = (int)sizeof(buf) - 1;
	}
	aster_log((uint32_t)level, buf, (uint32_t)n);
}
