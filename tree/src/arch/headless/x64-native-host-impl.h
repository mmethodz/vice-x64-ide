/* Native transport adapter for the retained Web64 control implementation.
 * GPL-2.0-or-later. Included once, after the retained APIs in headless/main.c.
 * Wire bytes are LE values or owned copies, NEVER native pointers/structs.
 * Input is read only at frame safe points and inside the real CPU pause trap.
 * Injection still uses the original private buffer + interrupt trap; publication
 * occurs in its successful trap callback, before any following instruction.
 */
#include <errno.h>
#include <limits.h>
#include "arch/shared/sounddrv/x64-audio-native.h"
#include "x64-native-launch-permit.h"
#include "x64-native-audio-host-impl.h"
#include "x64-native-delivery-impl.h"
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
#include "c64/x64-profiler-native.h"
int vice_web_profiler_control(int command, int frames, int capacity, uint32_t epoch);
#endif
#ifdef WINDOWS_COMPILE
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <poll.h>
#endif

uint8_t *vice_web_frame_pixels(void);
int vice_web_frame_width(void), vice_web_frame_height(void), vice_web_frame_number(void);
int vice_web_frame_ready(void);

#define X64_NATIVE_HEADER 40
#define X64_NATIVE_MAX_INPUT (1024 * 1024)
#define X64_NATIVE_MAX_OUTPUT (32 * 1024 * 1024)
static FILE *x64_native_output;
static unsigned char x64_native_input[X64_NATIVE_HEADER + X64_NATIVE_MAX_INPUT];
static size_t x64_native_received;
static int x64_native_enabled, x64_native_pending;
static uint16_t x64_native_pending_op;
static uint64_t x64_native_pending_request, x64_native_pending_generation;
static uint64_t x64_native_pending_permit;
static uint64_t x64_native_epoch = 1, x64_native_generation;
static uint64_t x64_native_stop_counter, x64_native_stop;
static int x64_native_stop_mapping;
static uint32_t x64_native_stop_mutation;
static int x64_native_snapshot_done, x64_native_snapshot_result;
static const char *x64_native_snapshot_root;
static int x64_native_frame_revoked, x64_native_frame_floor;

static uint32_t x64_get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t x64_get64(const unsigned char *p)
{
    return x64_get32(p) | ((uint64_t)x64_get32(p + 4) << 32);
}
static void x64_put32(unsigned char *p, uint32_t v)
{
    int i; for (i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (i * 8));
}
static void x64_put64(unsigned char *p, uint64_t v)
{
    x64_put32(p, (uint32_t)v); x64_put32(p + 4, (uint32_t)(v >> 32));
}
static void x64_native_reply(uint16_t op, uint64_t request, int status, const void *data, size_t length)
{
    unsigned char h[X64_NATIVE_HEADER] = { 'X', '6', '4', 'V', 2, 0 };
    if (length > X64_NATIVE_MAX_OUTPUT) exit(EXIT_FAILURE);
    h[6] = (unsigned char)op; h[7] = (unsigned char)(op >> 8);
    x64_put32(h + 8, (uint32_t)length); x64_put32(h + 12, (uint32_t)status);
    x64_put64(h + 16, request); x64_put64(h + 24, x64_native_epoch);
    x64_put64(h + 32, x64_native_generation);
    if (fwrite(h, 1, sizeof(h), x64_native_output) != sizeof(h) ||
        (length && fwrite(data, 1, length, x64_native_output) != length) ||
        fflush(x64_native_output)) exit(EXIT_FAILURE);
}
static void x64_native_snapshot_bytes(unsigned char b[72])
{
    uint32_t v[18]; int i;
    v[0] = (uint32_t)(ui_pause_active() && ui_pause_trap_active());
    for (i = 0; i < 6; i++) v[1 + i] = (uint32_t)vice_web_monitor_register_get(i);
    v[7] = (uint32_t)vice_web_monitor_cpu_mapping();
    v[8] = (uint32_t)vice_web_frame_step_epoch();
    v[9] = (uint32_t)vice_web_frame_step_status();
    v[10] = (uint32_t)vice_web_frame_step_stop_line();
    v[11] = (uint32_t)vice_web_frame_step_stop_cycle();
    v[12] = (uint32_t)maincpu_clk; v[13] = (uint32_t)(maincpu_clk >> 32);
    v[14] = (uint32_t)vice_web_monitor_mutation_generation();
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
    v[15] = 1;
#else
    v[15] = 0;
#endif
    v[16] = (uint32_t)x64_native_stop; v[17] = (uint32_t)(x64_native_stop >> 32);
    for (i = 0; i < 18; i++) x64_put32(b + i * 4, v[i]);
}
static void x64_native_status(uint16_t op, uint64_t request)
{
    unsigned char b[72];
    x64_native_snapshot_bytes(b);
    x64_native_reply(op, request, 0, b, sizeof(b));
}
static void x64_native_result(uint16_t op, uint64_t request, int result)
{
    unsigned char b[76];
    x64_put32(b, (uint32_t)result); x64_native_snapshot_bytes(b + 4);
    x64_native_reply(op, request, 0, b, sizeof(b));
}
static int x64_native_is_stopped(void)
{
    return ui_pause_active() && ui_pause_trap_active();
}
void x64_native_stop_entered(void)
{
    if (!x64_native_enabled) return;
    if (x64_native_stop_counter == UINT64_MAX) exit(EXIT_FAILURE);
    x64_native_stop = ++x64_native_stop_counter;
    x64_native_stop_mapping = vice_web_monitor_cpu_mapping();
    x64_native_stop_mutation = (uint32_t)vice_web_monitor_mutation_generation();
    x64_native_delivery_context(x64_native_epoch, x64_native_generation, 1, 0);
}
void x64_native_stop_left(void)
{
    x64_native_stop = 0;
    x64_native_delivery_context(x64_native_epoch, x64_native_generation, 0, 0);
}
static int x64_native_stop_matches(const unsigned char *p)
{
    if (!x64_native_is_stopped() || !x64_native_stop) return 0;
    if (x64_native_stop_mapping != vice_web_monitor_cpu_mapping() ||
        x64_native_stop_mutation != (uint32_t)vice_web_monitor_mutation_generation()) {
        x64_native_stop_left(); return 0;
    }
    return x64_get64(p) == x64_native_stop &&
        x64_get32(p + 8) == (uint32_t)x64_native_stop_mapping &&
        x64_get32(p + 12) == x64_native_stop_mutation;
}
static void x64_native_invalidate_profile(void)
{
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
    uint32_t info[46];
    x64_profiler_info(info);
    vice_web_profiler_control(4, 0, 0, info[6]);
#endif
}
void x64_native_reset_applied(void)
{
    /* Called after the real CPU reset vector/JUMP/DMA sequence, with exported
     * registers. Dispatch and machine_trigger_reset() are merely requests. */
    if (!x64_native_enabled) return;
    if (x64_native_epoch == UINT64_MAX) exit(EXIT_FAILURE);
    ++x64_native_epoch; x64_native_generation = 0;
    x64_native_stop_left(); x64_native_invalidate_profile();
    x64_audio_native_reset();
    x64_native_frame_floor = vice_web_frame_number(); x64_native_frame_revoked = 1;
    x64_native_delivery_context(x64_native_epoch, x64_native_generation, x64_native_is_stopped(), 1);
    if (x64_native_pending && x64_native_pending_op == 37) {
        x64_native_pending = 0;
        x64_native_status(37, x64_native_pending_request);
    }
}
void x64_native_snapshot_applied(int operation, int result)
{
    if (!x64_native_enabled || !x64_native_pending ||
        (x64_native_pending_op != 34 && x64_native_pending_op != 35)) return;
    if (operation == VICE_WEB_SNAPSHOT_LOAD) {
        /* An entered read can fail after changing machine state. Revoke the old
         * launch even on failure; do not pair it with uncertain snapshot RAM. */
        if (x64_native_epoch == UINT64_MAX) exit(EXIT_FAILURE);
        ++x64_native_epoch; x64_native_generation = 0;
        x64_native_invalidate_profile();
        x64_native_delivery_context(x64_native_epoch, x64_native_generation, x64_native_is_stopped(), 1);
    }
    x64_native_snapshot_result = result;
    x64_native_snapshot_done = 1;
    /* The retained instruction-step monitor was armed before leaving the held
     * pause. It owns the stop after this trap. Queuing a second pause here lets
     * a restored instruction slip before that new trap is entered. */
}
void x64_native_prepare(int *argc, char **argv)
{
    const char *enabled = getenv("X64_NATIVE_PIPE");
    int descriptor;
    if (!enabled || strcmp(enabled, "1")) return;
    x64_native_enabled = 1;
    x64_native_launch_permit_prepare();
    x64_native_delivery_prepare(x64_native_epoch, x64_native_generation, x64_native_is_stopped());
    x64_native_audio_host_prepare(argc, argv, x64_native_delivery_runtime(),
        x64_native_epoch, x64_native_generation, x64_native_is_stopped());
    x64_native_snapshot_root = getenv("X64_NATIVE_SNAPSHOT_ROOT");
#ifdef WINDOWS_COMPILE
    descriptor = _dup(_fileno(stdout));
    if (descriptor < 0 || _setmode(descriptor, _O_BINARY) < 0 ||
        _setmode(_fileno(stdin), _O_BINARY) < 0) exit(EXIT_FAILURE);
    x64_native_output = _fdopen(descriptor, "wb");
    if (_dup2(_fileno(stderr), _fileno(stdout)) < 0) exit(EXIT_FAILURE);
#else
    descriptor = dup(fileno(stdout));
    x64_native_output = fdopen(descriptor, "wb");
    if (dup2(fileno(stderr), fileno(stdout)) < 0) exit(EXIT_FAILURE);
#endif
    if (!x64_native_output) exit(EXIT_FAILURE);
    vice_web_started = 1;
    x64_audio_native_set_enabled(1);
}
int x64_native_program_may_apply(void)
{
    if (!x64_native_enabled || !x64_native_pending || x64_native_pending_op != 41) return 1;
    return x64_native_launch_permit_claim(x64_native_pending_permit);
}
void x64_native_program_rejected(void)
{
    if (!x64_native_enabled || !x64_native_pending || x64_native_pending_op != 41) return;
    x64_native_pending = 0;
    x64_native_reply(41, x64_native_pending_request, -7, NULL, 0);
}
void x64_native_program_applied(void)
{
    if (!x64_native_enabled || !x64_native_pending ||
        (x64_native_pending_op != 4 && x64_native_pending_op != 41)) return;
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
    {
        uint32_t info[46];
        x64_profiler_info(info);
        /* The Web64 host invalidates capture ownership on program takeover.
         * Do so only after successful injection: an old capture must never be
         * returned with this new launch generation's response identity. */
        vice_web_profiler_control(4, 0, 0, info[6]);
    }
#endif
    x64_native_epoch++;
    x64_native_generation = x64_native_pending_generation;
    x64_native_delivery_context(x64_native_epoch, x64_native_generation, x64_native_is_stopped(), 1);
    x64_native_pending = 0;
    x64_native_status(x64_native_pending_op, x64_native_pending_request);
}

#include "x64-native-media-impl.h"

static void x64_native_command(const unsigned char *h, const unsigned char *p, uint32_t n)
{
    uint16_t op = (uint16_t)(h[6] | (h[7] << 8));
    uint64_t request = x64_get64(h + 16), expected = x64_get64(h + 24), generation = x64_get64(h + 32);
    unsigned char small[184]; int result = 0;
    /* Hello alone negotiates the process-local epoch; every later command is
     * conditional on the last acknowledged epoch. No stale launch may mutate. */
    if (op != 0 && (expected != x64_native_epoch ||
        (op != 4 && op != 41 && generation != x64_native_generation))) { result = -2; goto failed; }
    if (x64_native_media_command(op, request, p, n)) return;
    switch (op) {
        case 0: case 1: case 7:
            if (n) break;
            x64_native_status(op, request); return;
        case 2:
            if (n) break;
            if (vice_web_pause_status()) { x64_native_status(op, request); return; }
            vice_web_pause(); goto deferred;
        case 3:
            if (n) break;
            x64_native_stop_left();
            vice_web_resume(); x64_native_status(op, request); return;
        case 4:
            if (n < 8 || generation <= x64_native_generation || x64_native_epoch == UINT64_MAX ||
                x64_get32(p) > 65535 || x64_get32(p + 4) > 65535 || n - 8 > 65536 - x64_get32(p)) break;
            if (vice_web_run_program((int)x64_get32(p), (int)x64_get32(p + 4), p + 8, (int)n - 8)) break;
            x64_native_stop_left();
            goto deferred;
        case 41:
            if (n < 16 || generation <= x64_native_generation || x64_native_epoch == UINT64_MAX ||
                x64_get32(p) > 65535 || x64_get32(p + 4) > 65535 || n - 16 > 65536 - x64_get32(p)) break;
            if (!x64_native_launch_permit_available()) { result = -4; goto failed; }
            x64_native_pending_permit = x64_get64(p + 8);
            if (!x64_native_pending_permit || x64_native_pending_permit > (UINT64_MAX >> 2)) break;
            if (!x64_native_launch_permit_armed(x64_native_pending_permit)) { result = -7; goto failed; }
            if (vice_web_run_program((int)x64_get32(p), (int)x64_get32(p + 4), p + 16, (int)n - 16)) break;
            x64_native_stop_left();
            goto deferred;
        case 42: {
            uint64_t features = UINT64_C(127), bitmap;
            int delivery = x64_native_delivery_available();
            int guard = x64_native_launch_permit_available();
            int snapshots = x64_native_snapshot_root && *x64_native_snapshot_root;
            unsigned int i;
            if (n) break;
            /* Report compiled AND currently granted process capabilities,
             * rather than advertising an unavailable optional adapter. */
            bitmap = 0;
            for (i = 0; i <= 42; ++i) {
                if (i == 11 || i == 12 || i == 13 || i == 14 || i == 34 || i == 35 || i == 41) continue;
                bitmap |= UINT64_C(1) << i;
            }
            for (i = 49; i <= 63; ++i) bitmap |= UINT64_C(1) << i;
            if (guard) { features |= UINT64_C(1) << 7; bitmap |= UINT64_C(1) << 41; }
            if (snapshots) { features |= UINT64_C(1) << 8; bitmap |= (UINT64_C(1) << 34) | (UINT64_C(1) << 35); }
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
            features |= UINT64_C(1) << 9;
            bitmap |= (UINT64_C(1) << 12) | (UINT64_C(1) << 13) | (UINT64_C(1) << 14);
#endif
            if (delivery) features |= (UINT64_C(1) << 10) | (UINT64_C(1) << 11);
            if (x64_native_audio_host_available()) {
                features |= UINT64_C(1) << 12; bitmap |= UINT64_C(1) << 43;
            }
            features |= (UINT64_C(1) << 13) | (UINT64_C(1) << 14) | (UINT64_C(1) << 15);
            bitmap |= (UINT64_C(1) << 44) | (UINT64_C(1) << 45) | (UINT64_C(1) << 46) |
                (UINT64_C(1) << 47) | (UINT64_C(1) << 48);
            memset(small, 0, 96);
            x64_put32(small, 1); x64_put32(small + 4, 96); x64_put32(small + 8, 2);
            x64_put64(small + 16, features);
            x64_put32(small + 24, 5); x64_put32(small + 28, 1);
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
            x64_put32(small + 32, 3);
#endif
            x64_put32(small + 36, 1024); x64_put32(small + 40, 1024);
            x64_put32(small + 44, delivery ? 3 : 0); x64_put32(small + 48, delivery ? 16384 : 0);
            x64_put32(small + 52, 2); x64_put32(small + 56, 16 * 1024 * 1024);
            x64_put64(small + 64, bitmap);
            x64_put32(small + 80, X64_NATIVE_MAX_INPUT); x64_put32(small + 84, X64_NATIVE_MAX_OUTPUT);
            x64_native_reply(op, request, 0, small, 96); return;
        }
        case 43: case 44: {
            unsigned char status[192] = {0};
            uint32_t loader; int32_t error; int control_result = 0;
            if (op == 43) {
                if (n != 32 || x64_get32(p) != 1 || x64_get32(p + 4) != 32 ||
                    x64_get32(p + 20) || x64_get64(p + 24)) break;
                if (!x64_native_audio_host_available()) { result = -4; goto failed; }
                control_result = x64_native_audio_host_control(x64_get32(p + 8),
                    x64_get32(p + 12), x64_get32(p + 16));
            } else if (n) break;
            x64_put32(status, 1); x64_put32(status + 4, 192);
            x64_native_audio_host_status(&loader, &error, status + 16);
            x64_put32(status + 8, loader); x64_put32(status + 12, (uint32_t)error);
            if (op == 43) {
                unsigned char response[196];
                x64_put32(response, (uint32_t)control_result); memcpy(response + 4, status, sizeof(status));
                x64_native_reply(op, request, 0, response, sizeof(response));
            } else x64_native_reply(op, request, 0, status, sizeof(status));
            return;
        }
        case 45: {
            uint32_t geometry[8]; uint64_t frame, clock; unsigned int i;
            if (n) break;
            if (!x64_native_frame_geometry(geometry, &frame, &clock)) { result = -3; goto failed; }
            memset(small, 0, 64); x64_put32(small, 1); x64_put32(small + 4, 64);
            for (i = 0; i < 8; ++i) x64_put32(small + 8 + i * 4, geometry[i]);
            x64_put64(small + 40, frame); x64_put64(small + 48, clock);
            x64_native_reply(op, request, 0, small, 64); return;
        }
        case 46:
            if (n != 4 || x64_get32(p) > 1) break;
            vice_web_set_warp_enabled((int)x64_get32(p));
            x64_native_audio_host_safe_point();
            x64_native_status(op, request); return;
        case 47: {
            double metric; uint64_t bits; unsigned int i;
            typedef char x64_native_binary64_size[(sizeof(double) == sizeof(uint64_t)) ? 1 : -1];
            (void)sizeof(x64_native_binary64_size);
            if (n) break;
            memset(small, 0, 160);
            x64_put32(small, 1); x64_put32(small + 4, 160);
            metric = vice_web_cpu_percent(); memcpy(&bits, &metric, sizeof(bits));
            x64_put64(small + 8, bits);
            metric = vice_web_emulated_fps(); memcpy(&bits, &metric, sizeof(bits));
            x64_put64(small + 16, bits);
            x64_put32(small + 24, (uint32_t)vice_web_warp_enabled());
            for (i = 0; i < 4; ++i) {
                int unit = (int)i + 8; unsigned char *drive = small + 32 + i * 32;
                x64_put32(drive, (uint32_t)unit);
                x64_put32(drive + 4, (uint32_t)vice_web_drive_image_attached(unit));
                x64_put32(drive + 8, (uint32_t)vice_web_drive_led(unit));
                x64_put32(drive + 12, (uint32_t)vice_web_drive_led2(unit));
                x64_put32(drive + 16, (uint32_t)vice_web_drive_track(unit));
                x64_put32(drive + 20, (uint32_t)vice_web_drive_half_track(unit));
                x64_put32(drive + 24, (uint32_t)vice_web_drive_sector(unit));
                x64_put32(drive + 28, (uint32_t)vice_web_drive_side(unit));
            }
            x64_native_reply(op, request, 0, small, 160); return;
        }
        case 48: {
            const char *name; size_t length;
            if (n != 4 || x64_get32(p) < 8 || x64_get32(p) > 11) break;
            name = vice_web_disk_name((int)x64_get32(p));
            for (length = 0; length <= 4096 && name[length]; ++length) {}
            if (length > 4096) break;
            x64_native_reply(op, request, 0, name, length); return;
        }
        case 49:
            if(n!=24)break;
            if(!x64_native_stop_matches(p)){result=-3;goto failed;}
            x64_put32(small,mem_x64_debug_mapping(x64_get32(p+16),x64_get32(p+20)));
            x64_native_reply(op,request,0,small,4);return;
        case 50: {
            uint32_t action,i,count=0;unsigned char *buffer;
            if(n<4)break;action=x64_get32(p);
            if(action==0){
                vice_web_monitor_owner_t *owner;uint32_t id;
                if(n!=20)break;id=x64_get32(p+4);
                if(id<1 || id>VICE_WEB_MONITOR_OWNERS || !vice_web_monitor_owners[id-1].active ||
                  !x64_get32(p+8) || !x64_get32(p+12) || x64_get32(p+16)>1)break;
                owner=&vice_web_monitor_owners[id-1];owner->hit_after=x64_get32(p+8);
                owner->hit_every=x64_get32(p+12);owner->log_only=x64_get32(p+16);
                x64_native_reply(op,request,0,NULL,0);return;
            }
            if(action==2){
                uint32_t id;if(n!=12)break;id=x64_get32(p+4);
                if(id<1 || id>VICE_WEB_MONITOR_OWNERS || !vice_web_monitor_owners[id-1].active || x64_get32(p+8)>1)break;
                vice_web_monitor_owners[id-1].suspended=x64_get32(p+8);
                x64_native_reply(op,request,0,NULL,0);return;
            }
            if(action!=1 || n!=4)break;
            for(i=0;i<VICE_WEB_MONITOR_OWNERS;i++)if(vice_web_monitor_owners[i].active)count++;
            buffer=malloc(12+count*12+x64_debug_log_count*36);if(!buffer){result=-5;goto failed;}
            x64_put32(buffer,count);x64_put32(buffer+4,x64_debug_log_count);x64_put32(buffer+8,x64_debug_log_dropped);
            count=12;
            for(i=0;i<VICE_WEB_MONITOR_OWNERS;i++)if(vice_web_monitor_owners[i].active){
                x64_put32(buffer+count,i+1);x64_put64(buffer+count+4,vice_web_monitor_owners[i].hits);count+=12;
            }
            for(i=0;i<x64_debug_log_count;i++){
                struct x64_debug_log *entry=&x64_debug_logs[i];unsigned int j;
                x64_put32(buffer+count,entry->owner);x64_put32(buffer+count+4,entry->address);
                x64_put64(buffer+count+8,entry->hit);
                for(j=0;j<5;j++)x64_put32(buffer+count+16+j*4,entry->regs[j]);count+=36;
            }
            x64_debug_log_count=x64_debug_log_dropped=0;
            x64_native_reply(op,request,0,buffer,count);free(buffer);return;
        }
        case 5:
            if (n || vice_web_frame_step_arm(vice_web_monitor_register_get(0),
                vice_web_monitor_cpu_mapping(), vice_web_frame_step_epoch()) < 0) break;
            x64_native_stop_left();
            goto deferred;
        case 6: {
            unsigned char *bytes;
            if (n != 8 || x64_get32(p) > 65535 || x64_get32(p + 4) > 4096 ||
                x64_get32(p + 4) > 65536 - x64_get32(p)) break;
            bytes = vice_web_monitor_memory_read((int)x64_get32(p), (int)x64_get32(p + 4));
            if (!bytes) break;
            x64_native_reply(op, request, 0, bytes, x64_get32(p + 4)); return;
        }
        case 8: {
            int w = vice_web_frame_width(), height = vice_web_frame_height();
            size_t size; unsigned char *copy;
            if (x64_native_frame_revoked) {
                if (vice_web_frame_number() == x64_native_frame_floor) { result = -3; goto failed; }
                x64_native_frame_revoked = 0;
            }
            if (n || !vice_web_frame_ready() || w < 1 || height < 1 || w > 2048 || height > 2048) break;
            size = (size_t)w * height * 4;
            copy = malloc(size + 16); if (!copy) { result = -5; goto failed; }
            x64_put32(copy, w); x64_put32(copy + 4, height);
            x64_put32(copy + 8, (uint32_t)vice_web_frame_number()); x64_put32(copy + 12, (uint32_t)size);
            memcpy(copy + 16, vice_web_frame_pixels(), size);
            x64_native_reply(op, request, 0, copy, size + 16); free(copy); return;
        }
        case 9: {
            char name[128]; const char *value;
            if (!n || n >= sizeof(name) || memchr(p, 0, n)) break;
            memcpy(name, p, n); name[n] = 0; value = vice_web_get_resource(name);
            if (!value) break;
            x64_native_reply(op, request, 0, value, strlen(value)); return;
        }
        case 10: {
            char name[128], value[256]; unsigned int a, b;
            if (n < 4) break;
            a = p[0] | (p[1] << 8); b = p[2] | (p[3] << 8);
            if (!a || a >= sizeof(name) || b >= sizeof(value) || n != 4 + a + b || memchr(p + 4, 0, a + b)) break;
            memcpy(name, p + 4, a); name[a] = 0; memcpy(value, p + 4 + a, b); value[b] = 0;
            if (vice_web_set_resource(name, value)) break;
            x64_native_status(op, request); return;
        }
        case 12:
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
            if (n != 16) break;
            result = vice_web_profiler_control((int)x64_get32(p), (int)x64_get32(p + 4), (int)x64_get32(p + 8), x64_get32(p + 12));
            x64_put32(small, (uint32_t)result); x64_native_reply(op, request, 0, small, 4); return;
#else
            result = -4; goto failed;
#endif
        case 13:
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
        {
            uint32_t info[46]; int i;
            if (n || !x64_profiler_info(info)) break;
            for (i = 0; i < 46; i++) x64_put32(small + i * 4, info[i]);
            x64_native_reply(op, request, 0, small, 184); return;
        }
#else
            result = -4; goto failed;
#endif
        case 14:
#if defined(ENABLE_PROFILER) && ENABLE_PROFILER
        {
            const void *bytes; size_t length = 0;
            if (n != 4) break;
            bytes = x64_profiler_buffer(x64_get32(p), &length);
            if (length > X64_NATIVE_MAX_OUTPUT ||
                (!bytes && !x64_profiler_copy(x64_get32(p), 0, NULL, 0))) break;
            x64_native_reply(op, request, 0, bytes, length); return;
        }
#else
            result = -4; goto failed;
#endif
        case 15:
            if (n) break;
            x64_native_audio_host_request_stop(); goto deferred;
        case 16: {
            size_t samples, i; int16_t *pcm; unsigned char *copy;
            if (n) break;
            samples = x64_audio_native_queued();
            if (samples > 48000 * 2) break;
            copy = malloc(24 + samples * 2); pcm = malloc(samples * sizeof(*pcm) + 1);
            if (!copy || !pcm) { free(copy); free(pcm); result = -5; goto failed; }
            samples = x64_audio_native_copy(pcm, samples);
            x64_put32(copy, x64_audio_native_sample_rate()); x64_put32(copy + 4, x64_audio_native_channels());
            x64_put32(copy + 8, (uint32_t)samples); x64_put32(copy + 12, 0);
            x64_put64(copy + 16, x64_audio_native_overruns());
            for (i = 0; i < samples; i++) { copy[24 + i * 2] = (unsigned char)pcm[i]; copy[25 + i * 2] = (unsigned char)((uint16_t)pcm[i] >> 8); }
            x64_native_reply(op, request, 0, copy, 24 + samples * 2); free(pcm); free(copy); return;
        }
        case 36:
            if (n) break;
            x64_audio_native_reset();
            x64_native_reply(op, request, 0, NULL, 0); return;
        case 37:
            if (n != 4 || x64_get32(p) > 1 || x64_native_epoch == UINT64_MAX) break;
            x64_native_stop_left();
            vice_web_reset((int)x64_get32(p));
            goto deferred;
        case 38: {
            uint32_t a, b, c; char *strings; const char *type, *code, *value;
            if (n < 20) break;
            a = x64_get32(p); b = x64_get32(p + 4); c = x64_get32(p + 8);
            if (a > 64 || b > 256 || c > 65536 || n != 20 + a + b + c || x64_get32(p + 12) > 1) break;
            strings = malloc((size_t)a + b + c + 3);
            if (!strings) { result = -5; goto failed; }
            type = strings; code = strings + a + 1; value = strings + a + b + 2;
            memcpy(strings, p + 20, a); strings[a] = 0;
            memcpy(strings + a + 1, p + 20 + a, b); strings[a + b + 1] = 0;
            memcpy(strings + a + b + 2, p + 20 + a + b, c); strings[a + b + c + 2] = 0;
            result = vice_web_write_input(type, code, (int)x64_get32(p + 12),
                (int)(int32_t)x64_get32(p + 16), value);
            free(strings); goto integer_result;
        }
        case 39: {
            char *text;
            if (n > 65536) break;
            text = malloc((size_t)n + 1); if (!text) { result = -5; goto failed; }
            memcpy(text, p, n); text[n] = 0;
            result = vice_web_paste_text(text); free(text); goto integer_result;
        }
        case 40: {
            int i;
            if (n) break;
            for (i = 0; i < 16; ++i) x64_put32(small + i * 4, (uint32_t)vice_web_keyboard_row(i));
            x64_put32(small + 64, (uint32_t)vice_web_joystick_value(1));
            x64_put32(small + 68, (uint32_t)vice_web_joystick_value(2));
            x64_native_reply(op, request, 0, small, 72); return;
        }
        case 17:
            if (n || vice_web_monitor_step_instruction() < 0) break;
            x64_native_stop_left();
            goto deferred;
        case 18:
            if (n || !x64_native_is_stopped()) { result = -3; goto failed; }
            if (!x64_native_stop || x64_native_stop_mapping != vice_web_monitor_cpu_mapping() ||
                x64_native_stop_mutation != (uint32_t)vice_web_monitor_mutation_generation())
                x64_native_stop_entered();
            x64_native_status(op, request); return;
        case 19: {
            unsigned char bytes[4096]; uint32_t ranges, i, total = 0;
            if (n < 20 || !x64_native_stop_matches(p)) { result = -3; goto failed; }
            ranges = x64_get32(p + 16);
            if (ranges < 1 || ranges > 64 || n != 20 + ranges * 8) break;
            for (i = 0; i < ranges; ++i) {
                uint32_t address = x64_get32(p + 20 + i * 8), length = x64_get32(p + 24 + i * 8);
                if (address > 65535 || !length || length > 4096 - total || length > 65536 - address) goto failed;
                total += length;
            }
            total = 0;
            for (i = 0; i < ranges; ++i) {
                uint32_t address = x64_get32(p + 20 + i * 8), length = x64_get32(p + 24 + i * 8);
                memcpy(bytes + total, vice_web_monitor_memory_read((int)address, (int)length), length);
                total += length;
            }
            x64_native_reply(op, request, 0, bytes, total); return;
        }
        case 20: {
            uint32_t address, length, indirect, cell;
            if (n != 44) break;
            if (!x64_native_stop_matches(p)) { result = -3; goto failed; }
            address = x64_get32(p + 16); length = x64_get32(p + 20);
            indirect = x64_get32(p + 24); cell = x64_get32(p + 28);
            if (length < 1 || length > 4 || address > 65536 - length || indirect > 1 || p[42] || p[43]) break;
            /* Match the Web64 runtime's preflight: no commit boundary entered
             * for unproven RAM or a changed pointer cell. Keep that stop valid. */
            if (!vice_web_monitor_ram_visible((int)address, (int)length)) result = -4;
            else if (indirect && (cell > 65534 || !vice_web_monitor_ram_visible((int)cell, 2))) result = -4;
            else if (indirect && memcmp(vice_web_monitor_memory_read((int)cell, 2), p + 40, 2)) result = -5;
            else {
                result = vice_web_monitor_mutate_memory((int)address, p + 32, p + 36, (int)length,
                    (int)x64_get32(p + 8), (int)x64_get32(p + 12));
                /* Every ENTERED native boundary revokes observation/capture,
                 * including a rejection or uncertain readback result (-6). */
                x64_native_stop_left(); x64_native_invalidate_profile();
            }
            x64_native_result(op, request, result); return;
        }
        case 21:
            if (n != 28) break;
            if (!x64_native_stop_matches(p)) { result = -3; goto failed; }
            result = vice_web_monitor_mutate_register((int)x64_get32(p + 16), (int)x64_get32(p + 20),
                (int)x64_get32(p + 24), (int)x64_get32(p + 8), (int)x64_get32(p + 12));
            x64_native_stop_left(); x64_native_invalidate_profile();
            x64_native_result(op, request, result); return;
        case 22:
            if (n != 8 || x64_get32(p) > 65535 || x64_get32(p + 4) > 1) break;
            result = vice_web_monitor_breakpoint_set((int)x64_get32(p), (int)x64_get32(p + 4)); goto integer_result;
        case 23:
            if (n != 4 || x64_get32(p) > 65535) break;
            result = vice_web_monitor_breakpoint_clear((int)x64_get32(p)); goto integer_result;
        case 24: {
            uint32_t length;
            if (n < 16) break;
            length = x64_get32(p + 12);
            if (length < 1 || length > 8 || n != 16 + length) break;
            result = vice_web_monitor_breakpoint_guard_set((int)x64_get32(p), (int)x64_get32(p + 4),
                (int)x64_get32(p + 8), p + 16, (int)length); goto integer_result;
        }
        case 25:
            if (n != 4) break;
            result = vice_web_monitor_breakpoint_guard_clear((int)x64_get32(p)); goto integer_result;
        case 26: {
            int program[VICE_WEB_MONITOR_PROGRAM_LIMIT * 2]; uint32_t bytes, count, i;
            if (n < 20) break;
            bytes = x64_get32(p + 12); count = x64_get32(p + 16);
            if (bytes > 8 || count > VICE_WEB_MONITOR_PROGRAM_LIMIT || n != 20 + bytes + count * 8) break;
            for (i = 0; i < count * 2; ++i) program[i] = (int)(int32_t)x64_get32(p + 20 + bytes + i * 4);
            result = vice_web_monitor_owner_set((int)x64_get32(p), (int)x64_get32(p + 4),
                (int)x64_get32(p + 8), p + 20, (int)bytes, program, (int)count); goto integer_result;
        }
        case 27:
            if (n != 4) break;
            result = vice_web_monitor_owner_clear((int)x64_get32(p)); goto integer_result;
        case 28: {
            unsigned char info[32 + VICE_WEB_MONITOR_OWNERS * 4]; int count, i;
            if (n) break;
            x64_put32(info, (uint32_t)vice_web_monitor_breakpoint_hit_pending());
            x64_put32(info + 4, (uint32_t)vice_web_monitor_breakpoint_hit_address());
            x64_put32(info + 8, (uint32_t)vice_web_monitor_breakpoint_hit_memspace());
            x64_put32(info + 12, (uint32_t)vice_web_monitor_breakpoint_hit_mapping());
            x64_put32(info + 16, (uint32_t)vice_web_monitor_breakpoint_hit_kind());
            x64_put32(info + 20, (uint32_t)vice_web_monitor_breakpoint_hit_checkpoint_id());
            x64_put32(info + 24, (uint32_t)vice_web_monitor_breakpoint_hit_guard_id());
            count = vice_web_monitor_owner_match_count();
            if (count < 0 || count > VICE_WEB_MONITOR_OWNERS) break;
            x64_put32(info + 28, (uint32_t)count);
            for (i = 0; i < count; ++i) x64_put32(info + 32 + i * 4, (uint32_t)vice_web_monitor_owner_match_id(i));
            x64_native_reply(op, request, 0, info, 32 + count * 4); return;
        }
        case 29:
            if (n != 4) break;
            result = vice_web_monitor_owner_unavailable_count((int)x64_get32(p)); goto integer_result;
        case 30:
            if (n != 20) break;
            if (!x64_native_stop_matches(p)) { result = -3; goto failed; }
            if (x64_get32(p + 16) > 65535 || vice_web_monitor_breakpoint_step_past((int)x64_get32(p + 16)) < 0) break;
            x64_native_stop_left(); goto deferred;
        case 31: {
            const char *text;
            if (n != 24) break;
            if (!x64_native_stop_matches(p)) { result = -3; goto failed; }
            if (x64_get32(p + 16) > 65535 || x64_get32(p + 20) > INT_MAX) break;
            text = vice_web_monitor_disassemble((int)x64_get32(p + 16), (int)x64_get32(p + 20));
            x64_native_reply(op, request, 0, text, strlen(text)); return;
        }
        case 32:
            if (n != 24) break;
            if (!x64_native_stop_matches(p)) { result = -3; goto failed; }
            result = vice_web_monitor_ram_visible((int)x64_get32(p + 16), (int)x64_get32(p + 20)); goto integer_result;
        case 33:
            if (n) break;
            result = vice_web_monitor_breakpoint_hit_clear(); goto integer_result;
        case 34: case 35: {
            char filename[1024]; uint32_t i; int size;
            if (!x64_native_snapshot_root || !*x64_native_snapshot_root) { result = -4; goto failed; }
            if (!x64_native_is_stopped()) { result = -3; goto failed; }
            if (vice_web_snapshot_pending() || vice_web_monitor_step_past_address >= 0 ||
                vice_web_monitor_instruction_step_pending ||
                (vice_web_monitor_breakpoint_hit_pending_flag && vice_web_monitor_breakpoint_hit_kind_value == 1)) break;
            if (!n || n > 128 || p[0] == '.') break;
            for (i = 0; i < n; ++i) {
                unsigned char c = p[i];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) break;
            }
            if (i != n || x64_native_epoch == UINT64_MAX) break;
#ifdef WINDOWS_COMPILE
            size = snprintf(filename, sizeof(filename), "%s\\%.*s", x64_native_snapshot_root, (int)n, (const char *)p);
#else
            size = snprintf(filename, sizeof(filename), "%s/%.*s", x64_native_snapshot_root, (int)n, (const char *)p);
#endif
            if (size < 0 || size >= (int)sizeof(filename)) break;
            result = op == 34 ? vice_web_save_snapshot(filename) : vice_web_load_snapshot(filename);
            if (result < 0) { x64_native_result(op, request, result); return; }
            x64_native_snapshot_done = 0; x64_native_stop_left();
            /* Match the retained Source harness: the instruction-step request
             * releases the old pause so the snapshot trap can run, then stops
             * at its monitor boundary. A load restores the serialized boundary;
             * it must not execute an extra instruction before acknowledgment. */
            if (vice_web_monitor_step_instruction() < 0) exit(EXIT_FAILURE);
            goto deferred;
        }
        default: result = -4; goto failed;
    }
    result = -1;
failed:
    if (!result) result = -1;
    x64_native_reply(op, request, result, NULL, 0); return;
integer_result:
    x64_put32(small, (uint32_t)result); x64_native_reply(op, request, 0, small, 4); return;
deferred:
    x64_native_pending = 1; x64_native_pending_op = op;
    x64_native_pending_request = request; x64_native_pending_generation = generation;
}

void x64_native_pump(void)
{
    size_t wanted; int count;
    if (!x64_native_enabled) return;
    x64_native_delivery_poll();
    if (x64_native_pending) {
        if (x64_native_pending_op == 15) {
            if (!x64_native_audio_host_shutdown_complete()) return;
            x64_native_pending = 0;
            x64_native_reply(15, x64_native_pending_request, 0, NULL, 0); exit(EXIT_SUCCESS);
        } else if ((x64_native_pending_op == 34 || x64_native_pending_op == 35) &&
            x64_native_snapshot_done && x64_native_is_stopped()) {
            x64_native_pending = 0;
            x64_native_result(x64_native_pending_op, x64_native_pending_request, x64_native_snapshot_result);
        } else if (x64_native_pending_op != 4 && x64_native_pending_op != 41 && x64_native_pending_op != 37 && x64_native_pending_op != 34 &&
            x64_native_pending_op != 35 && x64_native_is_stopped()) {
            x64_native_pending = 0; x64_native_status(x64_native_pending_op, x64_native_pending_request);
        } else return;
    }
    wanted = x64_native_received < X64_NATIVE_HEADER ? X64_NATIVE_HEADER :
        X64_NATIVE_HEADER + x64_get32(x64_native_input + 8);
    if (wanted > sizeof(x64_native_input)) exit(EXIT_FAILURE);
    if (wanted > x64_native_received) {
#ifdef WINDOWS_COMPILE
        DWORD available = 0; HANDLE input = (HANDLE)_get_osfhandle(_fileno(stdin));
        if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL)) exit(EXIT_FAILURE);
        if (!available) return;
        if (available > wanted - x64_native_received) available = (DWORD)(wanted - x64_native_received);
        count = _read(_fileno(stdin), x64_native_input + x64_native_received, available);
#else
        struct pollfd input = { 0, POLLIN, 0 };
        if (poll(&input, 1, 0) <= 0) return;
        count = (int)read(0, x64_native_input + x64_native_received, wanted - x64_native_received);
#endif
        if (count <= 0) exit(EXIT_FAILURE);
        x64_native_received += (size_t)count;
        if (x64_native_received < X64_NATIVE_HEADER) return;
        if (memcmp(x64_native_input, "X64V\2\0", 6) || x64_get32(x64_native_input + 12) != 0 ||
            x64_get32(x64_native_input + 8) > X64_NATIVE_MAX_INPUT) exit(EXIT_FAILURE);
        wanted = X64_NATIVE_HEADER + x64_get32(x64_native_input + 8);
        if (x64_native_received < wanted) return;
    }
    x64_native_command(x64_native_input, x64_native_input + X64_NATIVE_HEADER, (uint32_t)(wanted - X64_NATIVE_HEADER));
    x64_native_received = 0;
}
