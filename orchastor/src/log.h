// ========================= src/log.h =========================
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#include <stdarg.h>
static inline void logf_tag(const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
#endif
