/* x64-owned helper-process playback adapter. GPL-2.0-or-later, like VICE.
 * DLL lifecycle runs on its worker; core callbacks never wait or allocate.
 */
#ifndef X64_NATIVE_AUDIO_HOST_H
#define X64_NATIVE_AUDIO_HOST_H
#include <stdint.h>
#include <stddef.h>
void x64_native_audio_host_prepare(int *argc, char **argv, uint64_t runtime,
    uint64_t epoch, uint64_t generation, int paused);
int x64_native_audio_host_available(void);
void x64_native_audio_host_poll(void);
void x64_native_audio_host_safe_point(void);
void x64_native_audio_host_context(uint64_t epoch, uint64_t generation, int paused, int invalidate);
void x64_native_audio_host_reset(void);
void x64_native_audio_host_submit(const int16_t *, size_t, uint32_t rate, uint32_t channels);
int x64_native_audio_host_control(uint32_t operation, uint32_t value, uint32_t volume_q16);
void x64_native_audio_host_status(uint32_t *loader_state, int32_t *loader_error, unsigned char status[176]);
void x64_native_audio_host_request_stop(void);
int x64_native_audio_host_shutdown_complete(void);
#endif
