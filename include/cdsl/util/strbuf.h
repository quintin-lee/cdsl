/**
 * @file strbuf.h
 * @brief Simple dynamic string buffer.
 */

#ifndef CDSL_UTIL_STRBUF_H
#define CDSL_UTIL_STRBUF_H

#include "cdsl/util/portability.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct cdsl_strbuf {
	char* buf;
	size_t size;
	size_t cap;
} cdsl_strbuf_t;

static inline void
cdsl_strbuf_init(cdsl_strbuf_t* sb, size_t initial_cap)
{
	sb->cap = initial_cap > 0 ? initial_cap : 128;
	sb->buf = malloc(sb->cap);
	if (sb->buf) {
		sb->buf[0] = '\0';
	}
	sb->size = 0;
}

static inline CDSL_PRINTF_FORMAT(2,
				 3) void cdsl_strbuf_printf(cdsl_strbuf_t* sb, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) {
		return;
	}

	if (sb->size + n + 1 > sb->cap) {
		sb->cap = sb->size + n + 1 + 1024;
		char* new_buf = realloc(sb->buf, sb->cap);
		if (!new_buf) {
			return;
		}
		sb->buf = new_buf;
	}

	va_start(ap, fmt);
	vsnprintf(sb->buf + sb->size, sb->cap - sb->size, fmt, ap);
	va_end(ap);
	sb->size += n;
}

#endif /* CDSL_UTIL_STRBUF_H */
