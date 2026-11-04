/*
 * web_platform.h - Browser platform abstraction for the VICE WebAssembly lane.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VICE_WEB_PLATFORM_H
#define VICE_WEB_PLATFORM_H

#include "vice.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_PLATFORM_DIAGNOSTIC_LEVEL_DEBUG   0
#define WEB_PLATFORM_DIAGNOSTIC_LEVEL_INFO    1
#define WEB_PLATFORM_DIAGNOSTIC_LEVEL_WARNING 2
#define WEB_PLATFORM_DIAGNOSTIC_LEVEL_ERROR   3

typedef struct web_platform_callbacks_s {
    uint64_t (*monotonic_time_ms)(void *context);
    void (*sleep_ms)(void *context, uint32_t milliseconds);
    int (*read_resource)(void *context, const char *path, uint8_t *dest, size_t dest_size, size_t *bytes_read);
    int (*write_file)(void *context, const char *path, const uint8_t *src, size_t src_size);
    int (*read_file)(void *context, const char *path, uint8_t *dest, size_t dest_size, size_t *bytes_read);
    char *(*resolve_path)(void *context, const char *base, const char *path);
    void (*diagnostic)(void *context, unsigned int level, const char *channel, const char *message);
    void (*lifecycle)(void *context, const char *state);
    void *context;
} web_platform_callbacks_t;

int web_platform_install(const web_platform_callbacks_t *callbacks);
void web_platform_shutdown(void);
int web_platform_is_installed(void);
uint64_t web_platform_monotonic_time_ms(void);
void web_platform_sleep_ms(uint32_t milliseconds);
int web_platform_read_resource(const char *path, uint8_t *dest, size_t dest_size, size_t *bytes_read);
int web_platform_write_file(const char *path, const uint8_t *src, size_t src_size);
int web_platform_read_file(const char *path, uint8_t *dest, size_t dest_size, size_t *bytes_read);
char *web_platform_resolve_path(const char *base, const char *path);
void web_platform_diagnostic(unsigned int level, const char *channel, const char *message);
void web_platform_lifecycle(const char *state);

#ifdef __cplusplus
}
#endif

#endif
