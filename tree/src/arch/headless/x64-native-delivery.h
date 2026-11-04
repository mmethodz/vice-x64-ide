/* x64-owned nonblocking host delivery. GPL-2.0-or-later, like VICE.
 * Called only by the retained emulator core thread. No guest algorithm,
 * reference capture, device playback, allocation, IPC write or wait lives here.
 */
#ifndef X64_NATIVE_DELIVERY_H
#define X64_NATIVE_DELIVERY_H
#include <stddef.h>
#include <stdint.h>
void x64_native_delivery_prepare(uint64_t epoch, uint64_t generation, int paused);
int x64_native_delivery_available(void);
uint64_t x64_native_delivery_runtime(void);
int x64_native_frame_geometry(uint32_t values[8], uint64_t *frame, uint64_t *clock);
void x64_native_delivery_poll(void);
void x64_native_delivery_context(uint64_t epoch, uint64_t generation, int paused, int invalidate);
void x64_native_delivery_audio_reset(void);
void x64_native_delivery_frame(const uint8_t *pixels, uint32_t width, uint32_t height, uint64_t frame);
void x64_native_delivery_audio(const int16_t *samples, size_t count, uint32_t rate, uint32_t channels);
#endif
