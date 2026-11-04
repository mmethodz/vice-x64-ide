/* Included once after the core APIs. GPL-2.0-or-later, like VICE.
 * Fixed LE bytes, aligned Windows Interlocked64, no native pointer wire ABI.
 * Private paging-file section. The creator owns mapping/reader lease lifetime.
 */
#include "x64-native-delivery.h"
#include "x64-native-audio-host.h"
#include <stdlib.h>
#include <string.h>
#ifdef WINDOWS_COMPILE
#include <windows.h>
#define X64_DLY_TOTAL UINT64_C(12653184)
#define X64_DLY_FRAME_OFFSET 4096
#define X64_DLY_FRAME_STRIDE 4194432
#define X64_DLY_AUDIO_OFFSET 12587392
#define X64_DLY_AUDIO_DATA 12587648
#define X64_DLY_AUDIO_FRAMES 16384
static unsigned char *x64_dly_view;
static int x64_dly_open;
static uint64_t x64_dly_runtime, x64_dly_epoch, x64_dly_apply;
static uint64_t x64_dly_ring, x64_dly_control, x64_dly_sequence, x64_dly_audio_publication;
static uint64_t x64_dly_frame_drops, x64_dly_audio_drops;
static uint32_t x64_dly_rate, x64_dly_channels;
static int x64_dly_paused;
static int x64_dly_last_render_valid;
static uint64_t x64_dly_last_render_epoch, x64_dly_last_render_apply;
static uint64_t x64_dly_last_render_frame, x64_dly_last_render_clock;
static uint32_t x64_dly_last_render_width, x64_dly_last_render_height;
/* Read only at the single-core safe boundary. Never retain a pixel pointer
 * across a possible retained-canvas realloc/resize. */
uint8_t *vice_web_frame_pixels(void);
int vice_web_frame_width(void), vice_web_frame_height(void);
int vice_web_frame_number(void), vice_web_frame_ready(void);
static void x64_dly_republish(void);

static uint32_t x64_dly_get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t x64_dly_get64(const unsigned char *p)
{
    return x64_dly_get32(p) | ((uint64_t)x64_dly_get32(p + 4) << 32);
}
static void x64_dly_put32(unsigned char *p, uint32_t value)
{
    int i;
    for (i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static void x64_dly_put64(unsigned char *p, uint64_t value)
{
    x64_dly_put32(p, (uint32_t)value); x64_dly_put32(p + 4, (uint32_t)(value >> 32));
}
static uint64_t x64_dly_load(size_t offset)
{
    return (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)(x64_dly_view + offset), 0, 0);
}
static void x64_dly_store(size_t offset, uint64_t value)
{
    InterlockedExchange64((volatile LONG64 *)(x64_dly_view + offset), (LONG64)value);
}
static int x64_dly_cas(size_t offset, uint64_t expected, uint64_t value)
{
    return (uint64_t)InterlockedCompareExchange64(
        (volatile LONG64 *)(x64_dly_view + offset), (LONG64)value, (LONG64)expected) == expected;
}
static void x64_dly_add_drop(uint64_t *counter, uint64_t count, size_t offset)
{
    *counter = count > UINT64_MAX - *counter ? UINT64_MAX : *counter + count;
    x64_dly_store(offset, *counter);
}
static void x64_dly_close(int acknowledge)
{
    /* All publisher work and this close acknowledgment use the core thread.
     * No payload or metadata writes occur after acknowledgment. */
    if (x64_dly_control <= UINT64_MAX - 2) {
        x64_dly_store(80, ++x64_dly_control);
        x64_dly_store(112, 4);
        x64_dly_store(80, ++x64_dly_control);
    } else x64_dly_store(112, 4);
    x64_dly_open = 0;
    if (acknowledge) x64_dly_store(144, 1);
}
static int x64_dly_active(void)
{
    if (!x64_dly_open) return 0;
    if (x64_dly_load(136)) {
        x64_dly_close(1);
        return 0;
    }
    return 1;
}
static void x64_dly_invalidate(void)
{
    if (!x64_dly_active()) return;
    if (x64_dly_ring == UINT64_MAX || x64_dly_control > UINT64_MAX - 2) {
        x64_dly_close(0);
        return; /* Explicit delivery exhaustion, never wrap into stale identity. */
    }
    x64_dly_store(80, ++x64_dly_control);
    x64_dly_store(88, x64_dly_epoch);
    x64_dly_store(96, x64_dly_apply);
    x64_dly_store(104, ++x64_dly_ring);
    x64_dly_store(112, 1 | (x64_dly_paused ? 2 : 0));
    x64_dly_store(80, ++x64_dly_control);
    x64_dly_republish();
    /* Never reset or advance the consumer-owned audio read cursor. New audio
     * waits for the reader to flush read=write and acknowledge this generation.
     * Reading frame leases also remain untouched until the reader releases. */
}
#endif

void x64_native_delivery_prepare(uint64_t epoch, uint64_t generation, int paused)
{
#ifdef WINDOWS_COMPILE
    const char *name = getenv("X64_NATIVE_DELIVERY");
    const unsigned char *p;
    HANDLE mapping;
    size_t i;
    if (!name || !*name) return;
    for (p = (const unsigned char *)name; *p; ++p) if (*p < 32 || *p > 126) return;
    mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!mapping) return;
    x64_dly_view = (unsigned char *)MapViewOfFile(mapping,
        FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (SIZE_T)X64_DLY_TOTAL);
    CloseHandle(mapping);
    if (!x64_dly_view) return;
    if ((uintptr_t)x64_dly_view % 64) goto rejected;
    if (memcmp(x64_dly_view, "X64DLY01", 8) ||
        x64_dly_get32(x64_dly_view + 8) != 1 || x64_dly_get32(x64_dly_view + 12) != 4096 ||
        x64_dly_get64(x64_dly_view + 16) != X64_DLY_TOTAL ||
        !(x64_dly_runtime = x64_dly_get64(x64_dly_view + 24)) ||
        x64_dly_get32(x64_dly_view + 32) != 1024 || x64_dly_get32(x64_dly_view + 36) != 1024 ||
        x64_dly_get32(x64_dly_view + 40) != 3 ||
        x64_dly_get32(x64_dly_view + 44) != X64_DLY_FRAME_STRIDE ||
        x64_dly_get64(x64_dly_view + 48) != X64_DLY_FRAME_OFFSET ||
        x64_dly_get64(x64_dly_view + 56) != X64_DLY_AUDIO_OFFSET ||
        x64_dly_get32(x64_dly_view + 64) != X64_DLY_AUDIO_FRAMES ||
        x64_dly_get32(x64_dly_view + 68) != 2) goto rejected;
    for (i = 72; i < 4096; ++i) if (x64_dly_view[i]) goto rejected;
    for (i = 0; i < 3; ++i) {
        if (x64_dly_get64(x64_dly_view + X64_DLY_FRAME_OFFSET + i * X64_DLY_FRAME_STRIDE)) goto rejected;
    }
    for (i = 0; i < 256; ++i) if (x64_dly_view[X64_DLY_AUDIO_OFFSET + i]) goto rejected;
    x64_dly_epoch = epoch; x64_dly_apply = generation; x64_dly_paused = !!paused;
    x64_dly_open = 1;
    x64_dly_invalidate();
    return;
rejected:
    UnmapViewOfFile(x64_dly_view); x64_dly_view = NULL;
#else
    (void)epoch; (void)generation; (void)paused;
#endif
}
int x64_native_delivery_available(void)
{
#ifdef WINDOWS_COMPILE
    return x64_dly_active();
#else
    return 0;
#endif
}
uint64_t x64_native_delivery_runtime(void)
{
#ifdef WINDOWS_COMPILE
    return x64_dly_active() ? x64_dly_runtime : 0;
#else
    return 0;
#endif
}
void x64_native_delivery_poll(void)
{
    x64_native_audio_host_safe_point();
#ifdef WINDOWS_COMPILE
    (void)x64_dly_active();
#endif
}
void x64_native_delivery_context(uint64_t epoch, uint64_t generation, int paused, int invalidate)
{
    x64_native_audio_host_context(epoch, generation, paused, invalidate);
#ifdef WINDOWS_COMPILE
    if (!x64_dly_active()) return;
    if (epoch != x64_dly_epoch || generation != x64_dly_apply || !!paused != x64_dly_paused || invalidate) {
        x64_dly_epoch = epoch; x64_dly_apply = generation; x64_dly_paused = !!paused;
        x64_dly_invalidate();
    }
#else
    (void)epoch; (void)generation; (void)paused; (void)invalidate;
#endif
}
void x64_native_delivery_audio_reset(void)
{
    x64_native_audio_host_reset();
#ifdef WINDOWS_COMPILE
    x64_dly_invalidate();
#endif
}
#ifdef WINDOWS_COMPILE
static void x64_dly_publish_frame(const uint8_t *pixels, uint32_t width, uint32_t height,
    uint64_t frame, uint64_t observed_clock)
{
    size_t i, chosen = 0;
    uint64_t expected = 0, oldest = UINT64_MAX, ticket;
    unsigned char *slot;
    if (!x64_dly_active() || !pixels || !width || !height || width > 1024 || height > 1024) return;
    if (x64_dly_sequence == (UINT64_MAX >> 2)) { x64_dly_close(0); return; }
    for (i = 0; i < 3; ++i) {
        size_t offset = X64_DLY_FRAME_OFFSET + i * X64_DLY_FRAME_STRIDE;
        uint64_t state = x64_dly_load(offset);
        if ((state & 3) == 0) { chosen = offset; expected = state; break; }
        if ((state & 3) == 2 && (state >> 2) < oldest) {
            chosen = offset; expected = state; oldest = state >> 2;
        }
    }
    ticket = ++x64_dly_sequence << 2;
    if (!chosen || !x64_dly_cas(chosen, expected, ticket | 1)) {
        x64_dly_add_drop(&x64_dly_frame_drops, 1, 120); return;
    }
    if ((expected & 3) == 2) x64_dly_add_drop(&x64_dly_frame_drops, 1, 120);
    slot = x64_dly_view + chosen;
    x64_dly_put64(slot + 8, ticket >> 2);
    x64_dly_put64(slot + 16, x64_dly_runtime);
    x64_dly_put64(slot + 24, x64_dly_epoch);
    x64_dly_put64(slot + 32, x64_dly_apply);
    x64_dly_put64(slot + 40, x64_dly_ring);
    x64_dly_put64(slot + 48, frame);
    x64_dly_put64(slot + 56, observed_clock);
    x64_dly_put32(slot + 64, width); x64_dly_put32(slot + 68, height);
    x64_dly_put32(slot + 72, width * 4); x64_dly_put32(slot + 76, 1);
    x64_dly_put32(slot + 80, width * height * 4); x64_dly_put32(slot + 84, 0);
    memset(slot + 88, 0, 40);
    memcpy(slot + 128, pixels, (size_t)width * height * 4);
    x64_dly_store(chosen, ticket | 2);
}
static void x64_dly_republish(void)
{
    /* Presentation/audio flush generations must not erase the paused display.
     * Reuse the actual last completed render ONLY for the same guest identity.
     * Ready + exact frame/dimensions prove the current canonical canvas still
     * holds that render; a resize clears Ready. No render/PC/counter is advanced.
     * Core safe-boundary calls cannot overlap a canvas write or realloc. */
    if (!x64_dly_last_render_valid || x64_dly_last_render_epoch != x64_dly_epoch ||
        x64_dly_last_render_apply != x64_dly_apply || !vice_web_frame_ready() ||
        (uint32_t)vice_web_frame_number() != x64_dly_last_render_frame ||
        (uint32_t)vice_web_frame_width() != x64_dly_last_render_width ||
        (uint32_t)vice_web_frame_height() != x64_dly_last_render_height) return;
    x64_dly_publish_frame(vice_web_frame_pixels(), x64_dly_last_render_width,
        x64_dly_last_render_height, x64_dly_last_render_frame, x64_dly_last_render_clock);
}
#endif
void x64_native_delivery_frame(const uint8_t *pixels, uint32_t width, uint32_t height, uint64_t frame)
{
#ifdef WINDOWS_COMPILE
    if (!x64_dly_active() || !pixels || !width || !height || width > 1024 || height > 1024) return;
    x64_dly_last_render_valid = 1;
    x64_dly_last_render_epoch = x64_dly_epoch; x64_dly_last_render_apply = x64_dly_apply;
    x64_dly_last_render_frame = frame; x64_dly_last_render_clock = (uint64_t)maincpu_clk;
    x64_dly_last_render_width = width; x64_dly_last_render_height = height;
    x64_dly_publish_frame(pixels, width, height, frame, (uint64_t)maincpu_clk);
#else
    (void)pixels; (void)width; (void)height; (void)frame;
#endif
}
void x64_native_delivery_audio(const int16_t *samples, size_t count, uint32_t rate, uint32_t channels)
{
    x64_native_audio_host_submit(samples, count, rate, channels);
#ifdef WINDOWS_COMPILE
    uint64_t write, read, available;
    size_t frames, accepted, i;
    if (!x64_dly_active() || !samples || !count || !rate || channels < 1 || channels > 2 || count % channels) return;
    frames = count / channels;
    if (rate != x64_dly_rate || channels != x64_dly_channels) {
        x64_dly_rate = rate; x64_dly_channels = channels;
        x64_dly_invalidate();
        if (!x64_dly_active()) return;
    }
    if (x64_dly_load(X64_DLY_AUDIO_OFFSET + 16) != x64_dly_ring || x64_dly_paused) {
        x64_dly_add_drop(&x64_dly_audio_drops, frames, 128); return;
    }
    write = x64_dly_load(X64_DLY_AUDIO_OFFSET);
    read = x64_dly_load(X64_DLY_AUDIO_OFFSET + 8);
    if (write > UINT64_MAX - frames || x64_dly_audio_publication > UINT64_MAX - 2) {
        x64_dly_close(0); return;
    }
    if (read > write || write - read > X64_DLY_AUDIO_FRAMES) {
        /* Malformed reader cursor cannot become an out-of-bounds write. */
        x64_dly_add_drop(&x64_dly_audio_drops, frames, 128); return;
    }
    available = X64_DLY_AUDIO_FRAMES - (write - read);
    accepted = frames < available ? frames : (size_t)available;
    for (i = 0; i < accepted; ++i) {
        size_t offset = X64_DLY_AUDIO_DATA + (size_t)((write + i) % X64_DLY_AUDIO_FRAMES) * 4;
        uint16_t a = (uint16_t)samples[i * channels];
        uint16_t b = channels == 2 ? (uint16_t)samples[i * channels + 1] : 0;
        x64_dly_view[offset] = (unsigned char)a; x64_dly_view[offset + 1] = (unsigned char)(a >> 8);
        x64_dly_view[offset + 2] = (unsigned char)b; x64_dly_view[offset + 3] = (unsigned char)(b >> 8);
    }
    /* All metadata words are atomic; bounded consumer snapshot retries never
     * race ordinary mutable metadata or claim an invented sample-start clock. */
    x64_dly_store(X64_DLY_AUDIO_OFFSET + 24, ++x64_dly_audio_publication);
    x64_dly_store(X64_DLY_AUDIO_OFFSET + 32, rate);
    x64_dly_store(X64_DLY_AUDIO_OFFSET + 40, channels);
    x64_dly_store(X64_DLY_AUDIO_OFFSET + 48, (uint64_t)maincpu_clk);
    x64_dly_store(X64_DLY_AUDIO_OFFSET, write + accepted);
    x64_dly_store(X64_DLY_AUDIO_OFFSET + 24, ++x64_dly_audio_publication);
    if (accepted < frames) x64_dly_add_drop(&x64_dly_audio_drops, frames - accepted, 128);
#else
    (void)samples; (void)count; (void)rate; (void)channels;
#endif
}
