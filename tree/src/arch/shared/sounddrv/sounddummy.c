/*
 * sounddummy.c - Implementation of the dummy sound device
 *
 * Written by
 *  Teemu Rantanen <tvr@cs.hut.fi>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>
#include <string.h>

#include "sound.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <stdint.h>

#define VICE_WEB_AUDIO_BUFFER_SAMPLES ((SOUND_SAMPLE_RATE * SOUND_OUTPUT_CHANNELS_MAX) / 8)

static int16_t vice_web_audio_buffer[VICE_WEB_AUDIO_BUFFER_SAMPLES];
static size_t vice_web_audio_read_pos = 0;
static size_t vice_web_audio_write_pos = 0;
static size_t vice_web_audio_queued = 0;
static unsigned int vice_web_audio_overruns = 0;
static int vice_web_audio_rate = SOUND_SAMPLE_RATE;
static int vice_web_audio_channel_count = SOUND_OUTPUT_CHANNELS_MAX;
static int vice_web_audio_enabled = 0;

static void web_audio_reset_buffer(void)
{
    vice_web_audio_read_pos = 0;
    vice_web_audio_write_pos = 0;
    vice_web_audio_queued = 0;
    vice_web_audio_overruns = 0;
    memset(vice_web_audio_buffer, 0, sizeof(vice_web_audio_buffer));
}

static int web_audio_init(const char *param, int *speed, int *fragsize, int *fragnr, int *channels)
{
    (void)param;
    (void)fragsize;
    (void)fragnr;

    vice_web_audio_rate = speed != NULL && *speed > 0 ? *speed : SOUND_SAMPLE_RATE;
    vice_web_audio_channel_count = channels != NULL && *channels > 0 ? *channels : SOUND_OUTPUT_CHANNELS_MAX;
    if (vice_web_audio_channel_count > SOUND_OUTPUT_CHANNELS_MAX) {
        vice_web_audio_channel_count = SOUND_OUTPUT_CHANNELS_MAX;
        if (channels != NULL) {
            *channels = vice_web_audio_channel_count;
        }
    }
    web_audio_reset_buffer();
    return 0;
}

static int web_audio_write(int16_t *pbuf, size_t nr)
{
    size_t i;

    if (pbuf == NULL || nr == 0) {
        return 0;
    }
    if (!vice_web_audio_enabled) {
        return 0;
    }

    for (i = 0; i < nr; i++) {
        if (vice_web_audio_queued == VICE_WEB_AUDIO_BUFFER_SAMPLES) {
            vice_web_audio_read_pos = (vice_web_audio_read_pos + 1) % VICE_WEB_AUDIO_BUFFER_SAMPLES;
            vice_web_audio_queued--;
            vice_web_audio_overruns++;
        }
        vice_web_audio_buffer[vice_web_audio_write_pos] = pbuf[i];
        vice_web_audio_write_pos = (vice_web_audio_write_pos + 1) % VICE_WEB_AUDIO_BUFFER_SAMPLES;
        vice_web_audio_queued++;
    }

    return 0;
}

static int web_audio_bufferspace(void)
{
    /*
     * The browser bridge is not the actual timing device. Blocking VICE on
     * this small handoff queue can starve the Worker event loop that drains it,
     * which slows both video and SID playback. Accept writes and let the ring
     * drop the oldest samples if JS falls behind; wall-clock vsync remains the
     * timing source because this sound device is not marked as one.
     */
    return VICE_WEB_AUDIO_BUFFER_SAMPLES;
}

static void web_audio_close(void)
{
    web_audio_reset_buffer();
}

static const sound_device_t web_audio_device =
{
    "web",
    web_audio_init,
    web_audio_write,
    NULL,
    NULL,
    web_audio_bufferspace,
    web_audio_close,
    NULL,
    NULL,
    0,
    SOUND_OUTPUT_CHANNELS_MAX,
    false
};

EMSCRIPTEN_KEEPALIVE int vice_web_audio_samples(void)
{
    if (vice_web_audio_queued == 0) {
        return 0;
    }
    return (int)(uintptr_t)&vice_web_audio_buffer[vice_web_audio_read_pos];
}

EMSCRIPTEN_KEEPALIVE int vice_web_audio_sample_count(void)
{
    size_t count;

    if (vice_web_audio_queued == 0) {
        return 0;
    }
    count = VICE_WEB_AUDIO_BUFFER_SAMPLES - vice_web_audio_read_pos;
    if (count > vice_web_audio_queued) {
        count = vice_web_audio_queued;
    }
    return count > (size_t)INT32_MAX ? INT32_MAX : (int)count;
}

EMSCRIPTEN_KEEPALIVE void vice_web_audio_consume(int sample_count)
{
    size_t count;

    if (sample_count <= 0 || vice_web_audio_queued == 0) {
        return;
    }
    count = (size_t)sample_count;
    if (count > vice_web_audio_queued) {
        count = vice_web_audio_queued;
    }
    vice_web_audio_read_pos = (vice_web_audio_read_pos + count) % VICE_WEB_AUDIO_BUFFER_SAMPLES;
    vice_web_audio_queued -= count;
}

EMSCRIPTEN_KEEPALIVE void vice_web_audio_reset(void)
{
    web_audio_reset_buffer();
}

EMSCRIPTEN_KEEPALIVE void vice_web_audio_set_enabled(int enabled)
{
    if (vice_web_audio_enabled != (enabled ? 1 : 0)) {
        web_audio_reset_buffer();
    }
    vice_web_audio_enabled = enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_audio_queued_samples(void)
{
    return vice_web_audio_queued > (size_t)INT32_MAX ? INT32_MAX : (int)vice_web_audio_queued;
}

EMSCRIPTEN_KEEPALIVE int vice_web_audio_sample_rate(void)
{
    return vice_web_audio_rate;
}

EMSCRIPTEN_KEEPALIVE int vice_web_audio_channels(void)
{
    return vice_web_audio_channel_count;
}

EMSCRIPTEN_KEEPALIVE int vice_web_audio_overrun_count(void)
{
    return vice_web_audio_overruns > (unsigned int)INT32_MAX ? INT32_MAX : (int)vice_web_audio_overruns;
}

int sound_init_web_device(void)
{
    return sound_register_device(&web_audio_device);
}
#elif defined(WEB64_HOST_NATIVE)
/* Host-owned transport only. VICE still generates the original interleaved
 * s16 stream. No browser pointer-as-int exports or concurrent consumer is
 * enabled here; the native command service drains on this same core thread.
 */
#include "x64-audio-native.h"
#include "arch/headless/x64-native-delivery.h"

#define X64_AUDIO_NATIVE_BUFFER_SAMPLES ((SOUND_SAMPLE_RATE * SOUND_OUTPUT_CHANNELS_MAX) / 8)

static int16_t x64_audio_buffer[X64_AUDIO_NATIVE_BUFFER_SAMPLES];
static size_t x64_audio_read_pos;
static size_t x64_audio_write_pos;
static size_t x64_audio_queued;
static uint64_t x64_audio_dropped_samples;
static uint32_t x64_audio_rate = SOUND_SAMPLE_RATE;
static uint32_t x64_audio_channel_count = SOUND_OUTPUT_CHANNELS_MAX;
static int x64_audio_enabled;

void x64_audio_native_reset(void)
{
    x64_native_delivery_audio_reset();
    x64_audio_read_pos = 0;
    x64_audio_write_pos = 0;
    x64_audio_queued = 0;
    x64_audio_dropped_samples = 0;
    memset(x64_audio_buffer, 0, sizeof(x64_audio_buffer));
}

static int x64_audio_init(const char *param, int *speed, int *fragsize, int *fragnr, int *channels)
{
    (void)param;
    (void)fragsize;
    (void)fragnr;

    x64_audio_rate = speed != NULL && *speed > 0 ? (uint32_t)*speed : SOUND_SAMPLE_RATE;
    x64_audio_channel_count = channels != NULL && *channels > 0
        ? (uint32_t)*channels : SOUND_OUTPUT_CHANNELS_MAX;
    if (x64_audio_channel_count > SOUND_OUTPUT_CHANNELS_MAX) {
        x64_audio_channel_count = SOUND_OUTPUT_CHANNELS_MAX;
        if (channels != NULL) {
            *channels = (int)x64_audio_channel_count;
        }
    }
    x64_audio_native_reset();
    return 0;
}

static int x64_audio_write(int16_t *samples, size_t count)
{
    size_t overflow, first;

    if (samples == NULL || count == 0 || !x64_audio_enabled) {
        return 0;
    }
    /* Independent lossy host-delivery tap BEFORE Source reference capture's
     * drop-oldest handling. Neither path consumes or rewrites the other. */
    x64_native_delivery_audio(samples, count, x64_audio_rate, x64_audio_channel_count);
    if (count > X64_AUDIO_NATIVE_BUFFER_SAMPLES - x64_audio_queued) {
        overflow = count - (X64_AUDIO_NATIVE_BUFFER_SAMPLES - x64_audio_queued);
        if ((uint64_t)overflow > UINT64_MAX - x64_audio_dropped_samples) {
            x64_audio_dropped_samples = UINT64_MAX;
        } else {
            x64_audio_dropped_samples += (uint64_t)overflow;
        }
        if (count >= X64_AUDIO_NATIVE_BUFFER_SAMPLES) {
            /* Retain exactly the newest bounded span, like Source's sequential
             * drop-oldest handoff, without looping over discarded samples.
             */
            samples += count - X64_AUDIO_NATIVE_BUFFER_SAMPLES;
            count = X64_AUDIO_NATIVE_BUFFER_SAMPLES;
            x64_audio_read_pos = x64_audio_write_pos = x64_audio_queued = 0;
        } else {
            x64_audio_native_consume(overflow);
        }
    }
    first = X64_AUDIO_NATIVE_BUFFER_SAMPLES - x64_audio_write_pos;
    if (first > count) {
        first = count;
    }
    memcpy(x64_audio_buffer + x64_audio_write_pos, samples, first * sizeof(*samples));
    if (count > first) {
        memcpy(x64_audio_buffer, samples + first, (count - first) * sizeof(*samples));
    }
    x64_audio_write_pos = (x64_audio_write_pos + count) % X64_AUDIO_NATIVE_BUFFER_SAMPLES;
    x64_audio_queued += count;
    return 0;
}

static int x64_audio_bufferspace(void)
{
    /* The capture handoff is not a timing device. Never block the core waiting
     * for its own command-service drain; native pacing owns wall-clock timing.
     */
    return X64_AUDIO_NATIVE_BUFFER_SAMPLES;
}

static const sound_device_t x64_audio_device =
{
    "web",
    x64_audio_init,
    x64_audio_write,
    NULL,
    NULL,
    x64_audio_bufferspace,
    x64_audio_native_reset,
    NULL,
    NULL,
    0,
    SOUND_OUTPUT_CHANNELS_MAX,
    false
};

const int16_t *x64_audio_native_samples(size_t *contiguous_count)
{
    size_t count = X64_AUDIO_NATIVE_BUFFER_SAMPLES - x64_audio_read_pos;
    if (count > x64_audio_queued) {
        count = x64_audio_queued;
    }
    if (contiguous_count != NULL) {
        *contiguous_count = count;
    }
    return count == 0 ? NULL : x64_audio_buffer + x64_audio_read_pos;
}

size_t x64_audio_native_queued(void)
{
    return x64_audio_queued;
}

size_t x64_audio_native_capacity(void)
{
    return X64_AUDIO_NATIVE_BUFFER_SAMPLES;
}

uint32_t x64_audio_native_sample_rate(void)
{
    return x64_audio_rate;
}

uint32_t x64_audio_native_channels(void)
{
    return x64_audio_channel_count;
}

uint64_t x64_audio_native_overruns(void)
{
    return x64_audio_dropped_samples;
}

size_t x64_audio_native_consume(size_t sample_count)
{
    if (sample_count > x64_audio_queued) {
        sample_count = x64_audio_queued;
    }
    x64_audio_read_pos = (x64_audio_read_pos + sample_count) % X64_AUDIO_NATIVE_BUFFER_SAMPLES;
    x64_audio_queued -= sample_count;
    return sample_count;
}

size_t x64_audio_native_copy(int16_t *destination, size_t capacity_samples)
{
    size_t copied = 0;
    if (destination == NULL) {
        return 0;
    }
    while (copied < capacity_samples && x64_audio_queued != 0) {
        size_t count;
        const int16_t *samples = x64_audio_native_samples(&count);
        if (count > capacity_samples - copied) {
            count = capacity_samples - copied;
        }
        memcpy(destination + copied, samples, count * sizeof(*samples));
        x64_audio_native_consume(count);
        copied += count;
    }
    return copied;
}

void x64_audio_native_set_enabled(int enabled)
{
    enabled = enabled ? 1 : 0;
    if (x64_audio_enabled != enabled) {
        x64_audio_native_reset();
    }
    x64_audio_enabled = enabled;
}

int sound_init_web_device(void)
{
    return sound_register_device(&x64_audio_device);
}
#endif

static int dummy_write(int16_t *pbuf, size_t nr)
{
    return 0;
}

static const sound_device_t dummy_device =
{
    "dummy",
    NULL,
    dummy_write,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    2,
    false
};

int sound_init_dummy_device(void)
{
    return sound_register_device(&dummy_device);
}
