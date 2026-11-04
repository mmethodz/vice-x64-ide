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
