/*
 * web_platform.c - Browser platform abstraction for the VICE WebAssembly lane.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "vice.h"

#include <string.h>

#include "web_platform.h"

static web_platform_callbacks_t installed_callbacks;
static int callbacks_installed;

static void emit_missing_callback(const char *name)
{
    if (callbacks_installed && installed_callbacks.diagnostic != NULL) {
        installed_callbacks.diagnostic(installed_callbacks.context,
                                       WEB_PLATFORM_DIAGNOSTIC_LEVEL_ERROR,
                                       "platform",
                                       name);
    }
}

int web_platform_install(const web_platform_callbacks_t *callbacks)
{
    if (callbacks == NULL
            || callbacks->monotonic_time_ms == NULL
            || callbacks->diagnostic == NULL
            || callbacks->lifecycle == NULL) {
        callbacks_installed = 0;
        memset(&installed_callbacks, 0, sizeof(installed_callbacks));
        return -1;
    }

    installed_callbacks = *callbacks;
    callbacks_installed = 1;
    installed_callbacks.lifecycle(installed_callbacks.context, "installed");
    return 0;
}

void web_platform_shutdown(void)
{
    if (callbacks_installed && installed_callbacks.lifecycle != NULL) {
        installed_callbacks.lifecycle(installed_callbacks.context, "shutdown");
    }

    callbacks_installed = 0;
    memset(&installed_callbacks, 0, sizeof(installed_callbacks));
}

int web_platform_is_installed(void)
{
    return callbacks_installed;
}

uint64_t web_platform_monotonic_time_ms(void)
{
    if (!callbacks_installed || installed_callbacks.monotonic_time_ms == NULL) {
        return 0;
    }

    return installed_callbacks.monotonic_time_ms(installed_callbacks.context);
}

void web_platform_sleep_ms(uint32_t milliseconds)
{
    if (!callbacks_installed || installed_callbacks.sleep_ms == NULL) {
        emit_missing_callback("sleep_ms");
        return;
    }

    installed_callbacks.sleep_ms(installed_callbacks.context, milliseconds);
}

int web_platform_read_resource(const char *path, uint8_t *dest, size_t dest_size, size_t *bytes_read)
{
    if (!callbacks_installed || installed_callbacks.read_resource == NULL) {
        emit_missing_callback("read_resource");
        return -1;
    }

    return installed_callbacks.read_resource(installed_callbacks.context, path, dest, dest_size, bytes_read);
}

int web_platform_write_file(const char *path, const uint8_t *src, size_t src_size)
{
    if (!callbacks_installed || installed_callbacks.write_file == NULL) {
        emit_missing_callback("write_file");
        return -1;
    }

    return installed_callbacks.write_file(installed_callbacks.context, path, src, src_size);
}

int web_platform_read_file(const char *path, uint8_t *dest, size_t dest_size, size_t *bytes_read)
{
    if (!callbacks_installed || installed_callbacks.read_file == NULL) {
        emit_missing_callback("read_file");
        return -1;
    }

    return installed_callbacks.read_file(installed_callbacks.context, path, dest, dest_size, bytes_read);
}

char *web_platform_resolve_path(const char *base, const char *path)
{
    if (!callbacks_installed || installed_callbacks.resolve_path == NULL) {
        emit_missing_callback("resolve_path");
        return NULL;
    }

    return installed_callbacks.resolve_path(installed_callbacks.context, base, path);
}

void web_platform_diagnostic(unsigned int level, const char *channel, const char *message)
{
    if (!callbacks_installed || installed_callbacks.diagnostic == NULL) {
        return;
    }

    installed_callbacks.diagnostic(installed_callbacks.context, level, channel, message);
}

void web_platform_lifecycle(const char *state)
{
    if (!callbacks_installed || installed_callbacks.lifecycle == NULL) {
        return;
    }

    installed_callbacks.lifecycle(installed_callbacks.context, state);
}
