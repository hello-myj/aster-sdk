/* SPDX-License-Identifier: MIT
 * aster_printf: tiny formatter on top of aster_log() supporting %d %i %u %x %X
 * %s %c %% and long/unsigned-long modifiers. Enough for logging, avoids
 * dragging newlib into the package.
 *
 * Anything else -- %f above all -- is rendered as "%!" + the offending
 * character rather than copied through. An app that writes "%f" gets text
 * that says it is not a number, not text that looks like one; see the
 * default case in aster_printf.c.
 */
#ifndef ASTER_PRINTF_H
#define ASTER_PRINTF_H

#include <aster/log.h>

void aster_logf(int level, const char *fmt, ...);
int  aster_snprintf(char *buf, int max, const char *fmt, ...);
#define aster_printf(fmt, ...) aster_logf(ASTER_LOG_INFO, fmt, ##__VA_ARGS__)
#define aster_errfmt(fmt, ...) aster_logf(ASTER_LOG_ERROR, fmt, ##__VA_ARGS__)

#endif
