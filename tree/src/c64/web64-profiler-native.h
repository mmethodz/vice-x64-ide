/* x64sc adapter for the bounded observer ledger. GPL-2.0-or-later.
 * Included by c64cpusc.c only. Native ABI 2 is deliberately distinct from the
 * portable W64P export envelope; the worker validates/copies these tables.
 */
/* Normal builds must omit the observer entirely, not branch around it. */
#if (defined(VICE_WEB) && defined(__EMSCRIPTEN__) && defined(ENABLE_PROFILER) && ENABLE_PROFILER) || defined(W64P_NATIVE_HOST_TEST)
#ifndef W64P_NATIVE_HOST_TEST
#include <emscripten/emscripten.h>
#include "mem.h"
#include "viciitypes.h"
#endif
#include "web64-profiler-ledger.h"
#include "web64-profiler-memory.h"

static w64p_ledger_t *web64_profiler;
static uint32_t web64_profiler_epoch = 1, web64_profiler_capture_epoch;
static uint32_t web64_profiler_ticket, web64_profiler_active_ticket, web64_profiler_observing;
static uint32_t web64_profiler_reu_ticket;
static CLOCK web64_profiler_reu_cursor;
static uint32_t web64_profiler_info[46];
static CLOCK web64_profiler_arm_clock;

EMSCRIPTEN_KEEPALIVE int vice_web_profiler_control(int command, int frames, int capacity, uint32_t epoch);
EMSCRIPTEN_KEEPALIVE const uint32_t *vice_web_profiler_info(void);

static uint32_t web64_profiler_phase(void)
{
    return (vicii.start_of_frame ? 0 : vicii.raster_line) * vicii.cycles_per_line + vicii.raster_cycle;
}

static void web64_profiler_disarm(void)
{
    web64_profiler_active_ticket = web64_profiler_observing = 0;
    web64_profiler_reu_ticket = 0;
    web64_profiler_reu_cursor = 0;
    vicii_web_cpu_stall_observer = NULL;
    mem_web_profiler_disarm();
}

static void web64_profiler_reset(void)
{
    w64p_abort(web64_profiler, W64P_RESET);
    web64_profiler_disarm();
    if (web64_profiler_epoch != UINT32_MAX) web64_profiler_epoch++;
}

static void web64_profiler_bus_cycle(CLOCK clock, unsigned int cause)
{
    if (web64_profiler_observing && web64_profiler_observing == web64_profiler_active_ticket) {
        if (web64_profiler_reu_ticket == web64_profiler_observing) {
            /* REU callbacks can yield the bus to the VIC inside a DMA transfer.
             * Emit the REU-only prefix, then the overlapping cycle once with
             * both cause bits. Never append the outer interval retrospectively. */
            w64p_stall(web64_profiler, web64_profiler_reu_cursor, clock, W64P_REU);
            web64_profiler_reu_cursor = clock + 1;
            cause |= W64P_REU;
        }
        w64p_stall(web64_profiler, clock, clock + 1, cause);
        if (!w64p_running(web64_profiler)) web64_profiler_disarm();
    }
}

/* Keep the cold observer outside the huge CPU switch, including when LLVM
 * inlining would otherwise inflate its register pressure/disabled path. */
static __attribute__((noinline)) void web64_profiler_begin(uint32_t pc, uint32_t regs, uint32_t kind)
{
    w64p_ledger_t *c = web64_profiler;
    uint32_t frame_clocks = c->lines * c->cycles;
    if (c->lines != vicii.screen_height || c->cycles != vicii.cycles_per_line) {
        w64p_abort(c, W64P_MODEL);
        web64_profiler_disarm();
        return;
    }
    if (!c->has_clock) {
        /* The command may arrive while an old instruction is suspended in a
         * host yield. Arm at the first NEW native instruction boundary, then
         * take the next complete frame. Never record that old continuation. */
        uint32_t phase = web64_profiler_phase();
        uint64_t skip;
        if (phase >= frame_clocks) { w64p_abort(c, W64P_MODEL); web64_profiler_disarm(); return; }
        skip = frame_clocks - phase;
        if (maincpu_clk > UINT64_MAX - skip - (uint64_t)c->frames * frame_clocks) {
            w64p_abort(c, W64P_CLOCK); web64_profiler_disarm(); return;
        }
        c->start = c->last = maincpu_clk + skip;
        c->limit = c->start + (uint64_t)c->frames * frame_clocks;
    }
    w64p_begin(c, maincpu_clk, pc, regs, (uint32_t)mem_get_current_bank_config(), kind);
    {
        w64p_memory_observation_t o = mem_web_profiler_observe((uint16_t)pc);
        c->mapping = o.mapping;
        c->generation = o.generation;
        c->identity_flags = o.flags;
    }
    if (w64p_running(c)) web64_profiler_observing = web64_profiler_active_ticket;
    else web64_profiler_disarm();
}

static __attribute__((noinline)) void web64_profiler_finish(uint32_t pc, uint32_t fetched, int jammed)
{
    w64p_ledger_t *c = web64_profiler;
    if (jammed) { w64p_abort(c, W64P_JAM); web64_profiler_disarm(); return; }
    if (c->lines != vicii.screen_height || c->cycles != vicii.cycles_per_line) {
        w64p_abort(c, W64P_MODEL); web64_profiler_disarm(); return;
    }
    if (c->current.kind == W64P_INSTRUCTION) {
        /* JSR's high operand was supplied by its actual late CPU fetch. No
         * additional read of guest RAM, ROM or I/O is performed by profiling. */
        if ((fetched & 255) == 0x20) fetched = (fetched & 0xffff) | (c->current.bytes & 0xff0000);
        c->current.bytes = fetched;
    }
    {
        w64p_memory_observation_t o = mem_web_profiler_observe(c->current.pc);
        if (c->mapping != o.mapping || c->identity_flags != o.flags || c->generation != o.generation) {
            /* DMA or the instruction itself changed a fetched page/mapping.
             * Actual bytes/timing survive, but a single fetch generation is
             * not proven. Never merge this edge with a following generation. */
            c->identity_flags |= W64P_GENERATION_UNKNOWN;
        }
        if (c->identity_flags) {
            if (c->unknown_serial == UINT32_MAX) {
                w64p_abort(c, W64P_DICTIONARY); web64_profiler_disarm(); return;
            }
            c->generation = ++c->unknown_serial;
        }
    }
    w64p_finish(c, maincpu_clk, pc);
    web64_profiler_observing = 0;
    if (!w64p_running(c)) web64_profiler_disarm();
}

static uint32_t web64_profiler_reu_begin(void)
{
    if (web64_profiler_observing && web64_profiler_observing == web64_profiler_active_ticket) {
        web64_profiler_reu_cursor = maincpu_clk;
        return web64_profiler_reu_ticket = web64_profiler_observing;
    }
    return 0;
}

static void web64_profiler_reu_finish(uint32_t ticket)
{
    /* A host command may cancel/replace the capture during a suspended DMA.
     * Its old continuation must neither touch nor disarm the new capture. */
    if (ticket && ticket == web64_profiler_active_ticket && ticket == web64_profiler_reu_ticket) {
        w64p_stall(web64_profiler, web64_profiler_reu_cursor, maincpu_clk, W64P_REU);
        web64_profiler_reu_ticket = 0;
        web64_profiler_reu_cursor = 0;
        if (!w64p_running(web64_profiler)) web64_profiler_disarm();
    }
}

EMSCRIPTEN_KEEPALIVE int vice_web_profiler_control(int command, int frames, int capacity, uint32_t epoch)
{
    uint32_t count;
    if (epoch != web64_profiler_epoch) return -1; /* stale command */
    if (command == 0) {
        w64p_abort(web64_profiler, W64P_CANCEL);
        web64_profiler_disarm();
        return 1;
    }
    if (command == 3 || command == 4) {
        web64_profiler_disarm();
        w64p_destroy(web64_profiler);
        web64_profiler = NULL;
        mem_web_profiler_clear();
        if (command == 4) web64_profiler_reset();
        return 1;
    }
    if ((command != 1 && command != 2) || frames < 1 || frames > 16 || capacity < 0) return -3;
    if (web64_profiler) return -2; /* explicit clear before replacement; no double allocation */
    if (web64_profiler_ticket == UINT32_MAX || web64_profiler_epoch == UINT32_MAX) return -3;
    count = capacity ? (uint32_t)capacity : (uint32_t)frames * 16384;
    if (!capacity && count > W64P_DETAIL_BYTES / sizeof(w64p_record_t)) count = W64P_DETAIL_BYTES / sizeof(w64p_record_t);
    web64_profiler = w64p_create((uint32_t)command, (uint32_t)frames, vicii.screen_height,
        vicii.cycles_per_line, 0, count);
    if (!web64_profiler) return -4; /* invalid geometry/cap or allocation failure */
    {
        uint32_t owned = mem_web_profiler_start(web64_profiler_ticket + 1);
        if (!owned || owned > W64P_SUMMARY_BYTES -
            (web64_profiler->owned - web64_profiler->capacity * (uint32_t)sizeof(w64p_record_t))) {
            mem_web_profiler_clear(); w64p_destroy(web64_profiler); web64_profiler = NULL; return -4;
        }
        web64_profiler->owned += owned;
    }
    web64_profiler_arm_clock = maincpu_clk;
    web64_profiler_capture_epoch = web64_profiler_epoch;
    web64_profiler_active_ticket = ++web64_profiler_ticket;
    web64_profiler_observing = 0;
    vicii_web_cpu_stall_observer = web64_profiler_bus_cycle;
    return 1;
}

EMSCRIPTEN_KEEPALIVE const uint32_t *vice_web_profiler_info(void)
{
    const w64p_ledger_t *c = web64_profiler;
    uint32_t *p = web64_profiler_info;
    memset(p, 0, sizeof(web64_profiler_info));
    p[0] = 0x31525057; /* WPR1 native transport, NOT W64P file magic */
    p[1] = 3;
    p[2] = 1; /* cycle-exact x64sc main CPU */
    p[3] = 0xff; /* aggregate/detail/frame/stall/IRQ/epoch + RAM + cartridge evidence */
    p[6] = web64_profiler_epoch;
    p[7] = web64_profiler_ticket;
    p[9] = c ? c->lines : vicii.screen_height;
    p[10] = c ? c->cycles : vicii.cycles_per_line;
    p[12] = sizeof(w64p_record_t);
    p[15] = sizeof(w64p_pc_t);
    p[18] = p[21] = sizeof(w64p_stats_t);
    p[30] = (uint32_t)maincpu_clk;
    p[31] = (uint32_t)(maincpu_clk >> 32);
    p[32] = web64_profiler_phase();
    p[35] = 3; /* PLA + space + cartridge profile/window/bank; flags gate proof */
    p[37] = vicii.screen_height;
    p[38] = vicii.cycles_per_line;
    p[39] = vicii.color_latency; /* chip-model light-pen subcycle offset */
    if (!c) return p;
    p[4] = c->state;
    p[5] = c->reason;
    p[8] = c->frames;
    p[11] = c->count;
    p[13] = (uint32_t)(uintptr_t)c->records;
    p[14] = c->pc_count;
    p[16] = (uint32_t)(uintptr_t)c->pcs;
    p[17] = (uint32_t)(uintptr_t)c->frame;
    p[19] = (uint32_t)(uintptr_t)c->line;
    p[20] = c->frames * c->lines;
    p[22] = c->owned;
    p[23] = c->capacity;
    p[24] = (uint32_t)c->start;
    p[25] = (uint32_t)(c->start >> 32);
    p[26] = (uint32_t)c->limit;
    p[27] = (uint32_t)(c->limit >> 32);
    p[28] = (uint32_t)c->last;
    p[29] = (uint32_t)(c->last >> 32);
    p[33] = c->has_clock;
    p[34] = web64_profiler_capture_epoch;
    p[36] = c->mode;
    p[40] = (uint32_t)(uintptr_t)mem_web_profiler_baseline();
    p[41] = 65536;
    p[42] = (uint32_t)web64_profiler_arm_clock;
    p[43] = (uint32_t)(web64_profiler_arm_clock >> 32);
    p[44] = (uint32_t)(uintptr_t)mem_web_profiler_cartridge_baseline();
    p[45] = mem_web_profiler_cartridge_bytes();
    return p;
}

/* One local ticket per native interval. Disabled: no fetch hook, allocations,
 * source work or transport. A host command during Asyncify suspension cannot
 * finish an old instruction into a newly started capture or freed buffer. */
#define WEB64_PROBE_ENABLED web64_profiler_active_ticket
#define WEB64_PROBE_BEGIN(pc, regs, kind) \
    uint32_t web64_interval_ticket = web64_profiler_active_ticket; \
    do { if (web64_interval_ticket) web64_profiler_begin(pc, regs, kind); } while (0)
#define WEB64_PROBE_VALID (web64_interval_ticket && web64_interval_ticket == web64_profiler_active_ticket)
/* The fast RAM fetch may speculatively read a third byte even when the CPU
 * does not fetch it. Mask it out; JSR supplies its own actual late fetch. */
#define WEB64_PROBE_FINISH(pc) do { if (WEB64_PROBE_VALID) web64_profiler_finish(pc, (p0) | ((p1) << 8) | (fetch_tab[p0] ? (((p2) & 0xff00) << 8) : 0), CPU_IS_JAMMED); } while (0)
#define WEB64_PROBE_FINISH_INTERRUPT(pc) do { if (WEB64_PROBE_VALID) web64_profiler_finish(pc, 0, 0); } while (0)
#define WEB64_PROBE_FETCH(a, b, c) do { } while (0)
#define WEB64_PROBE_JSR(dst) do { if (WEB64_PROBE_VALID) web64_profiler->current.bytes = ((uint32_t)(dst) & 0xff00) << 8; } while (0)
#define WEB64_PROBE_INTERRUPT(vector) do { if (WEB64_PROBE_VALID) web64_profiler->current.aux = (uint32_t)(vector); } while (0)
#define WEB64_PROBE_RESET() web64_profiler_reset()
#endif
