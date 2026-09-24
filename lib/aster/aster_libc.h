/* SPDX-License-Identifier: MIT
 * Minimal libc for freestanding wasm apps. WAMR's libc-builtin registers
 * these under import module "env"; clang may also emit implicit calls to
 * memset/memcpy, which resolve to the same imports via --allow-undefined.
 */
#ifndef ASTER_LIBC_H
#define ASTER_LIBC_H

#include <stdint.h>
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strncpy(char *dst, const char *src, size_t n);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);

#endif
