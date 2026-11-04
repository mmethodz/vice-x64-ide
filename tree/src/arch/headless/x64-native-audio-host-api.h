#ifndef X64_PRIVATE_AUDIO_HOST_V1_H
#define X64_PRIVATE_AUDIO_HOST_V1_H
/* Loaded inside the native VICE helper, never Qt. Versioned same-release C ABI.
 * create/join/destroy are lifecycle operations OFF the core/audio callback.
 * try_submit/control/status are bounded, allocation-free and never wait.
 * The core producer alone writes ingress. Full ingress drops NEW samples.
 * All pointers are caller-owned for the duration of a call. No allocator or C++
 * layout crosses the boundary. Unload only after join+destroy succeeds.
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct X64AudioHost X64AudioHost;
enum { X64_AUDIO_HOST_OK=0, X64_AUDIO_HOST_DROPPED=1, X64_AUDIO_HOST_PENDING=2,
       X64_AUDIO_HOST_INVALID=-1, X64_AUDIO_HOST_ABI=-2, X64_AUDIO_HOST_CLOSED=-3 };
enum { X64_AUDIO_DEVICE_STARTING=0, X64_AUDIO_DEVICE_RUNNING=1,
       X64_AUDIO_DEVICE_NO_ENDPOINT=2, X64_AUDIO_DEVICE_RECONFIGURING=3,
       X64_AUDIO_DEVICE_STOPPED=4, X64_AUDIO_DEVICE_FAILED=5 };
enum { X64_AUDIO_CONTROL_RUNNING=1, X64_AUDIO_CONTROL_MUTE=2,
       X64_AUDIO_CONTROL_VOLUME=3, X64_AUDIO_CONTROL_FLUSH=4,
       X64_AUDIO_CONTROL_REOPEN=5, X64_AUDIO_CONTROL_SUSPEND=6,
       X64_AUDIO_CONTROL_LATENCY=7 };
typedef struct X64AudioHostConfig {
    uint32_t abi_version, struct_bytes, latency_ms, initial_input_rate;
    uint32_t initial_input_channels, initial_mute; float initial_volume;
    uint32_t reserved[9];
} X64AudioHostConfig;
typedef struct X64AudioHostInput {
    uint32_t abi_version, struct_bytes;
    uint64_t runtime_id, epoch, application_generation, ring_generation, observed_core_clock;
    uint32_t sample_rate, channels, frame_count, reserved;
} X64AudioHostInput;
typedef struct X64AudioHostControl {
    uint32_t abi_version, struct_bytes, operation, value;
    float volume; uint32_t reserved[3];
} X64AudioHostControl;
typedef struct X64AudioHostStatus {
    uint32_t abi_version, struct_bytes, state, flags;
    uint32_t input_rate, input_channels, device_rate, device_channels;
    uint32_t device_buffer_frames, device_period_frames, latency_ms; int32_t last_hresult;
    uint64_t device_generation, input_generation, submitted_frames, input_dropped_frames;
    uint64_t rendered_frames, silent_frames, underrun_frames, device_reopens;
    uint64_t default_notifications, invalidations, callback_count, max_callback_qpc_ticks;
    uint64_t callback_qpc_frequency, dsp_underrun_frames, dsp_discontinuities, observed_core_clock;
} X64AudioHostStatus;
/* status.flags: bit0 real WASAPI endpoint, bit1 muted, bit2 running requested,
 * bit3 suspended, bit4 stop requested. Tests never label fake endpoints real.
 */
typedef struct X64AudioHostApi {
    uint32_t abi_version, struct_bytes, max_submit_frames, reserved;
    int32_t (*create)(const X64AudioHostConfig *, X64AudioHost **);
    int32_t (*try_submit)(X64AudioHost *, const X64AudioHostInput *, const int16_t *);
    int32_t (*control)(X64AudioHost *, const X64AudioHostControl *);
    int32_t (*status)(X64AudioHost *, X64AudioHostStatus *);
    void (*request_stop)(X64AudioHost *);
    int32_t (*join)(X64AudioHost *, uint32_t timeout_ms);
    int32_t (*destroy)(X64AudioHost *);
} X64AudioHostApi;
/* Only this symbol is dynamically resolved. Validate exact table bytes/major
 * before exposing any core callback. DSP DLL is an adjacent pinned dependency.
 */
int32_t x64_audio_host_v1_get_api(uint32_t abi_version,uint32_t table_bytes,X64AudioHostApi *out);
#ifdef __cplusplus
}
#endif
#endif
