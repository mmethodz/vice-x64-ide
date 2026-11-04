/* x64-owned lifecycle/ABI adapter, GPL-2.0-or-later, like VICE.
 * x64-native-audio-host-api.h is a byte-identical build-stage mirror of
 * native/audio/x64_audio_host.h, the authoritative private MSVC DLL C ABI.
 * Only explicit -x64-audio-host absolute paths can load a playback module.
 */
#include "x64-native-audio-host.h"
#include "x64-native-audio-host-api.h"
#include <stdlib.h>
#include <string.h>
#ifdef WINDOWS_COMPILE
#include <windows.h>
#include <wchar.h>
typedef char x64_audio_config_size[(sizeof(X64AudioHostConfig) == 64) ? 1 : -1];
typedef char x64_audio_input_size[(sizeof(X64AudioHostInput) == 64) ? 1 : -1];
typedef char x64_audio_control_size[(sizeof(X64AudioHostControl) == 32) ? 1 : -1];
typedef char x64_audio_status_size[(sizeof(X64AudioHostStatus) == 176) ? 1 : -1];
typedef char x64_audio_api_size[(sizeof(X64AudioHostApi) == 72) ? 1 : -1];
static HANDLE x64_play_thread, x64_play_stop;
static volatile LONG x64_play_state, x64_play_error, x64_play_calls;
static wchar_t x64_play_path[32768];
static X64AudioHostApi x64_play_api;
static X64AudioHost *x64_play_host;
static uint64_t x64_play_runtime, x64_play_epoch, x64_play_apply, x64_play_generation = 1;
static int x64_play_paused, x64_play_warp, x64_play_suspended, x64_play_running = 1, x64_play_synced;
static int x64_play_resources_ready;

static int x64_play_enter(void)
{
    InterlockedIncrement(&x64_play_calls);
    if (InterlockedCompareExchange(&x64_play_state, 0, 0) == 2) return 1;
    InterlockedDecrement(&x64_play_calls);
    return 0;
}
static void x64_play_leave(void) { InterlockedDecrement(&x64_play_calls); }
static int x64_play_call_control(uint32_t operation, uint32_t value, float volume)
{
    X64AudioHostControl control;
    int result;
    if (!x64_play_enter()) return X64_AUDIO_HOST_PENDING;
    memset(&control, 0, sizeof(control));
    control.abi_version = 1; control.struct_bytes = sizeof(control);
    control.operation = operation; control.value = value; control.volume = volume;
    result = x64_play_api.control(x64_play_host, &control);
    x64_play_leave();
    return result;
}
static DWORD WINAPI x64_play_lifecycle(void *unused)
{
    HMODULE module;
    X64AudioHostConfig config;
    typedef int32_t (*get_api_fn)(uint32_t, uint32_t, X64AudioHostApi *);
    get_api_fn get_api;
    int result;
    (void)unused;
    module = LoadLibraryExW(x64_play_path, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) goto windows_failure;
    /* Windows FARPROC/function-pointer widths match this 64-bit C ABI. memcpy
     * avoids a cross-toolchain C function-pointer cast or allocator boundary. */
    {
        FARPROC address = GetProcAddress(module, "x64_audio_host_v1_get_api");
        if (!address) { InterlockedExchange(&x64_play_error, ERROR_PROC_NOT_FOUND); goto failure; }
        memcpy(&get_api, &address, sizeof(get_api));
    }
    memset(&x64_play_api, 0, sizeof(x64_play_api));
    result = get_api(1, sizeof(x64_play_api), &x64_play_api);
    if (result || x64_play_api.abi_version != 1 || x64_play_api.struct_bytes != sizeof(x64_play_api) ||
        x64_play_api.reserved || !x64_play_api.max_submit_frames || x64_play_api.max_submit_frames > 8192 ||
        !x64_play_api.create || !x64_play_api.try_submit || !x64_play_api.control || !x64_play_api.status ||
        !x64_play_api.request_stop || !x64_play_api.join || !x64_play_api.destroy) {
        InterlockedExchange(&x64_play_error, result ? result : X64_AUDIO_HOST_ABI); goto failure;
    }
    memset(&config, 0, sizeof(config));
    config.abi_version = 1; config.struct_bytes = sizeof(config);
    config.latency_ms = 20; config.initial_input_rate = 48000;
    config.initial_input_channels = 2; config.initial_volume = 1.0f;
    result = x64_play_api.create(&config, &x64_play_host);
    if (result || !x64_play_host) {
        InterlockedExchange(&x64_play_error, result ? result : X64_AUDIO_HOST_ABI);
        if (x64_play_host) {
            /* A failed factory must normally return no handle. If it does
             * expose ownership, do not unload until that handle is quiescent. */
            x64_play_api.request_stop(x64_play_host);
            if (x64_play_api.join(x64_play_host, 1500) || x64_play_api.destroy(x64_play_host)) {
                InterlockedExchange(&x64_play_state, 3); return 0;
            }
            x64_play_host = NULL;
        }
        goto failure;
    }
    /* The entire immutable API/context publication precedes Ready. */
    InterlockedExchange(&x64_play_state, 2);
    WaitForSingleObject(x64_play_stop, INFINITE); /* Lifecycle worker ONLY. */
    InterlockedExchange(&x64_play_state, 4);
    while (InterlockedCompareExchange(&x64_play_calls, 0, 0)) Sleep(1);
    x64_play_api.request_stop(x64_play_host);
    result = x64_play_api.join(x64_play_host, 1500);
    if (!result) result = x64_play_api.destroy(x64_play_host);
    if (result) {
        /* Do NOT unload while any DLL/device thread may still execute. The
         * supervisor's process teardown is the final release for this failure. */
        InterlockedExchange(&x64_play_error, result);
        InterlockedExchange(&x64_play_state, 3);
        return 0;
    }
    x64_play_host = NULL;
    FreeLibrary(module);
    InterlockedExchange(&x64_play_state, 5);
    return 0;
windows_failure:
    InterlockedExchange(&x64_play_error, (LONG)GetLastError());
    InterlockedExchange(&x64_play_state, 3);
    return 0;
failure:
    FreeLibrary(module);
    InterlockedExchange(&x64_play_state, 3);
    return 0;
}
static int x64_play_absolute(const wchar_t *path)
{
    return (path[0] && path[1] && path[2] &&
        ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
        (path[0] == L'\\' && path[1] == L'\\' && path[2]);
}
static int x64_play_copy_path(wchar_t *destination, size_t capacity, const wchar_t *path)
{
    size_t i, length = wcslen(path);
    if (!x64_play_absolute(path) || length >= capacity) return 0;
    wcscpy(destination, path);
    /* DLL_LOAD_DIR dependency lookup requires native separators even though
     * the primary DLL itself can load with '/'. Do not rewrite the explicitly
     * verbatim/device namespaces: their spelling has different Win32 rules. */
    if (!(length >= 4 && path[0] == L'\\' && path[1] == L'\\' &&
        (path[2] == L'?' || path[2] == L'.') && path[3] == L'\\')) {
        for (i = 0; i < length; ++i) if (destination[i] == L'/') destination[i] = L'\\';
    }
    return 1;
}
#endif

void x64_native_audio_host_prepare(int *argc, char **argv, uint64_t runtime,
    uint64_t epoch, uint64_t generation, int paused)
{
    int input, output = 1, requested = 0, valid = 1;
#ifdef WINDOWS_COMPILE
    HMODULE shell = NULL;
    wchar_t **wide = NULL;
    int wide_count = 0;
    typedef wchar_t **(WINAPI *command_line_fn)(const wchar_t *, int *);
    command_line_fn parse = NULL;
    for (input = 1; input < *argc; ++input) {
        if (!strcmp(argv[input], "-x64-audio-host")) {
            FARPROC address;
            if (++requested > 1 || input + 1 >= *argc) { valid = 0; if (input + 1 < *argc) ++input; continue; }
            shell = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!shell) { valid = 0; ++input; continue; }
            address = GetProcAddress(shell, "CommandLineToArgvW");
            if (address) memcpy(&parse, &address, sizeof(parse));
            if (parse) wide = parse(GetCommandLineW(), &wide_count);
            if (!wide || wide_count != *argc || !x64_play_copy_path(x64_play_path,
                sizeof(x64_play_path) / sizeof(*x64_play_path), wide[input + 1])) valid = 0;
            if (wide) { LocalFree(wide); wide = NULL; }
            FreeLibrary(shell); shell = NULL;
            ++input;
        } else argv[output++] = argv[input];
    }
#else
    for (input = 1; input < *argc; ++input) {
        if (!strcmp(argv[input], "-x64-audio-host")) { ++requested; if (input + 1 < *argc) ++input; }
        else argv[output++] = argv[input];
    }
    (void)runtime; (void)epoch; (void)generation; (void)paused; (void)valid;
#endif
    argv[output] = NULL; *argc = output;
    if (!requested) return;
#ifdef WINDOWS_COMPILE
    if (!valid || !runtime) {
        InterlockedExchange(&x64_play_error, ERROR_INVALID_PARAMETER); InterlockedExchange(&x64_play_state, 3); return;
    }
    x64_play_runtime = runtime; x64_play_epoch = epoch; x64_play_apply = generation; x64_play_paused = !!paused;
    x64_play_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!x64_play_stop) { InterlockedExchange(&x64_play_error, (LONG)GetLastError()); InterlockedExchange(&x64_play_state, 3); return; }
    InterlockedExchange(&x64_play_state, 1);
    x64_play_thread = CreateThread(NULL, 0, x64_play_lifecycle, NULL, 0, NULL);
    if (!x64_play_thread) { InterlockedExchange(&x64_play_error, (LONG)GetLastError()); InterlockedExchange(&x64_play_state, 3); }
    atexit(x64_native_audio_host_request_stop);
#endif
}
int x64_native_audio_host_available(void)
{
#ifdef WINDOWS_COMPILE
    return InterlockedCompareExchange(&x64_play_state, 0, 0) == 2;
#else
    return 0;
#endif
}
void x64_native_audio_host_context(uint64_t epoch, uint64_t generation, int paused, int invalidate)
{
#ifdef WINDOWS_COMPILE
    if (x64_play_epoch != epoch || x64_play_apply != generation || x64_play_paused != !!paused || invalidate) {
        x64_play_epoch = epoch; x64_play_apply = generation; x64_play_paused = !!paused;
        if (x64_play_generation == UINT64_MAX) { x64_native_audio_host_request_stop(); return; }
        ++x64_play_generation;
        x64_play_synced = 0;
        x64_native_audio_host_poll();
    }
#else
    (void)epoch; (void)generation; (void)paused; (void)invalidate;
#endif
}
void x64_native_audio_host_poll(void)
{
#ifdef WINDOWS_COMPILE
    int warp = x64_play_resources_ready ? !!vice_web_warp_enabled() : 0;
    if (x64_play_resources_ready && x64_play_warp != warp) {
        x64_play_warp = !!warp; x64_native_audio_host_reset();
    }
    if (x64_native_audio_host_available() && !x64_play_synced) {
        x64_play_call_control(X64_AUDIO_CONTROL_RUNNING, x64_play_running && !x64_play_paused, 0.0f);
        x64_play_call_control(X64_AUDIO_CONTROL_SUSPEND, x64_play_suspended || x64_play_warp, 0.0f);
        x64_play_call_control(X64_AUDIO_CONTROL_FLUSH, 0, 0.0f);
        x64_play_synced = 1;
    }
#endif
}
void x64_native_audio_host_safe_point(void)
{
#ifdef WINDOWS_COMPILE
    /* prepare() precedes core initialization; sound-enable reset there must
     * not observe retained core metrics until the first real safe point. */
    x64_play_resources_ready = 1;
#endif
    x64_native_audio_host_poll();
}
void x64_native_audio_host_reset(void)
{
#ifdef WINDOWS_COMPILE
    x64_native_audio_host_context(x64_play_epoch, x64_play_apply, x64_play_paused, 1);
    x64_native_audio_host_poll();
#endif
}
void x64_native_audio_host_submit(const int16_t *samples, size_t count, uint32_t rate, uint32_t channels)
{
#ifdef WINDOWS_COMPILE
    X64AudioHostInput input;
    size_t frames, offset = 0;
    x64_native_audio_host_safe_point();
    if (!samples || !count || !rate || channels < 1 || channels > 2 || count % channels ||
        x64_play_paused || x64_play_warp || x64_play_suspended || !x64_play_running || !x64_play_enter()) return;
    memset(&input, 0, sizeof(input)); input.abi_version = 1; input.struct_bytes = sizeof(input);
    input.runtime_id = x64_play_runtime; input.epoch = x64_play_epoch; input.application_generation = x64_play_apply;
    input.ring_generation = x64_play_generation; input.observed_core_clock = (uint64_t)maincpu_clk;
    input.sample_rate = rate; input.channels = channels;
    frames = count / channels;
    while (offset < frames) {
        size_t part = frames - offset;
        if (part > x64_play_api.max_submit_frames) part = x64_play_api.max_submit_frames;
        input.frame_count = (uint32_t)part;
        (void)x64_play_api.try_submit(x64_play_host, &input, samples + offset * channels);
        offset += part;
    }
    x64_play_leave();
#else
    (void)samples; (void)count; (void)rate; (void)channels;
#endif
}
int x64_native_audio_host_control(uint32_t operation, uint32_t value, uint32_t volume_q16)
{
#ifdef WINDOWS_COMPILE
    if (!x64_native_audio_host_available()) return X64_AUDIO_HOST_PENDING;
    if (operation < 1 || operation > 7 || volume_q16 > 65536 ||
        (operation != 3 && volume_q16) ||
        ((operation == 1 || operation == 2 || operation == 6) && value > 1) ||
        ((operation == 3 || operation == 4 || operation == 5) && value) ||
        (operation == 7 && (value < 10 || value > 80 || value % 10))) return X64_AUDIO_HOST_INVALID;
    if (operation == 1) { x64_play_running = !!value; x64_play_synced = 0; }
    if (operation == 6) { x64_play_suspended = !!value; x64_play_synced = 0; }
    if (operation == 4) { x64_native_audio_host_reset(); return X64_AUDIO_HOST_OK; }
    if (operation == 1 || operation == 6) { x64_native_audio_host_reset(); return X64_AUDIO_HOST_OK; }
    return x64_play_call_control(operation, value, (float)volume_q16 / 65536.0f);
#else
    (void)operation; (void)value; (void)volume_q16;
    return X64_AUDIO_HOST_CLOSED;
#endif
}
void x64_native_audio_host_status(uint32_t *loader_state, int32_t *loader_error, unsigned char status[176])
{
    memset(status, 0, 176);
#ifdef WINDOWS_COMPILE
    X64AudioHostStatus snapshot;
    int result;
    *loader_state = (uint32_t)InterlockedCompareExchange(&x64_play_state, 0, 0);
    *loader_error = (int32_t)InterlockedCompareExchange(&x64_play_error, 0, 0);
    if (!x64_play_enter()) return;
    memset(&snapshot, 0, sizeof(snapshot)); snapshot.abi_version = 1; snapshot.struct_bytes = sizeof(snapshot);
    result = x64_play_api.status(x64_play_host, &snapshot);
    x64_play_leave();
    if (result || snapshot.abi_version != 1 || snapshot.struct_bytes != sizeof(snapshot)) { *loader_error = result ? result : X64_AUDIO_HOST_ABI; return; }
    /* Windows x64/arm64 are LE; fixed-width ABI is independently sizeof-checked.
     * No pointer, allocator state, padding or private object crosses this copy. */
    memcpy(status, &snapshot, 176);
#else
    *loader_state = 0; *loader_error = -4;
#endif
}
void x64_native_audio_host_request_stop(void)
{
#ifdef WINDOWS_COMPILE
    if (x64_play_stop) SetEvent(x64_play_stop);
#endif
}
int x64_native_audio_host_shutdown_complete(void)
{
#ifdef WINDOWS_COMPILE
    return !x64_play_thread || WaitForSingleObject(x64_play_thread, 0) == WAIT_OBJECT_0;
#else
    return 1;
#endif
}
