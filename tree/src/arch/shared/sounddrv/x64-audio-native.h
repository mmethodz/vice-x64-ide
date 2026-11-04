/* x64-owned native capture adapter for the retained VICE s16 sample stream.
 *
 * All functions run on the emulator core thread, including the synchronous
 * command service that copies samples into its owned response. This is NOT a
 * shared-reader or audio callback ring. Device playback is a separate adapter.
 */
#ifndef X64_AUDIO_NATIVE_H
#define X64_AUDIO_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Interleaved signed 16-bit samples, not frames. The borrowed contiguous view
 * is invalidated by write, consume, copy, reset, enable change, init or close.
 * Copy its bytes synchronously before returning to the emulator core loop.
 */
const int16_t *x64_audio_native_samples(size_t *contiguous_count);
size_t x64_audio_native_queued(void);
size_t x64_audio_native_capacity(void);
uint32_t x64_audio_native_sample_rate(void);
uint32_t x64_audio_native_channels(void);
/* Number of samples dropped because the bounded handoff was full. */
uint64_t x64_audio_native_overruns(void);

/* Clamp to available samples. copy drains at most capacity_samples, including
 * a wrapped second span. Its destination must not alias capture storage.
 * Null destination or zero capacity consumes nothing.
 */
size_t x64_audio_native_consume(size_t sample_count);
size_t x64_audio_native_copy(int16_t *destination, size_t capacity_samples);
void x64_audio_native_reset(void);
/* Capture starts disabled, matching the Web64 bridge. Changing enable state
 * clears buffered samples and overrun count; reset does not change enable state.
 */
void x64_audio_native_set_enabled(int enabled);

#ifdef __cplusplus
}
#endif
#endif
