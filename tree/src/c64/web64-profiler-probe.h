/* Web64 P0 timing/cost probe. Experimental, not the stable capture ABI.
 * Included only by the cycle-exact x64sc translation unit. GPL-2.0-or-later,
 * under the same terms as VICE. Observes native clocks; never clocks the CPU.
 * No allocations or extra memory reads on the disabled path.
 */
#if defined(VICE_WEB) && defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"
#include "viciitypes.h"

typedef struct {
    uint64_t start, end;
    uint32_t pc, bytes, regs, mapping, raster_start, raster_end, kind, aux;
} web64_probe_record_t;
typedef struct {
    uint64_t cpu, stolen;
    uint32_t hits;
} web64_probe_pc_t;

static unsigned int web64_probe_state; /* 0 idle, 1 armed, 2 active, 3 done, 4 incomplete */
static unsigned int web64_probe_mode, web64_probe_count, web64_probe_capacity;
static unsigned int web64_probe_reason, web64_probe_calls;
static CLOCK web64_probe_first, web64_probe_last, web64_probe_limit;
static CLOCK web64_probe_cpu, web64_probe_stolen, web64_probe_current_stolen;
static web64_probe_record_t web64_probe_current;
static web64_probe_record_t *web64_probe_records;
static web64_probe_pc_t *web64_probe_pcs;
static uint32_t web64_probe_info[24];

static unsigned int web64_probe_raster(void)
{
    /* At the first cycle of a frame, raw raster_line still contains the
     * previous frame's last line. start_of_frame distinguishes this from the
     * real first cycle of that last line. Do not guess using raster_line alone. */
    return ((vicii.start_of_frame ? 0 : vicii.raster_line) << 8) | vicii.raster_cycle;
}

/* Exact bytes are fetched by the CPU, not peeked by this probe. For JSR the
 * late high operand is supplied at the actual JSR hook below. */
static void web64_probe_begin(unsigned int pc, unsigned int regs, unsigned int kind)
{
    if (web64_probe_state == 1) {
        web64_probe_first = maincpu_clk;
        web64_probe_last = maincpu_clk;
        web64_probe_limit += maincpu_clk;
        web64_probe_state = 2;
    }
    web64_probe_current.start = maincpu_clk;
    web64_probe_current.pc = pc & 0xffff;
    web64_probe_current.bytes = 0;
    web64_probe_current.regs = regs;
    web64_probe_current.mapping = mem_get_current_bank_config();
    web64_probe_current.raster_start = web64_probe_raster();
    web64_probe_current.kind = kind;
    web64_probe_current.aux = 0;
    web64_probe_current_stolen = 0;
}

static void web64_probe_append(const web64_probe_record_t *r)
{
    if (web64_probe_mode != 2) return;
    if (web64_probe_count == web64_probe_capacity) {
        web64_probe_state = 4;
        web64_probe_reason = 1; /* bounded storage exhausted, never a silent drop */
        return;
    }
    web64_probe_records[web64_probe_count++] = *r;
}

static void web64_probe_finish(unsigned int next_pc)
{
    CLOCK elapsed;
    if (maincpu_clk < web64_probe_current.start) {
        web64_probe_state = 4;
        web64_probe_reason = 2; /* reset/clock discontinuity */
        return;
    }
    elapsed = maincpu_clk - web64_probe_current.start;
    if (!elapsed) return; /* pending IRQ checks and host pauses cost no guest time */
    if (elapsed < web64_probe_current_stolen) {
        web64_probe_state = 4;
        web64_probe_reason = 3;
        return;
    }
    web64_probe_current.end = maincpu_clk;
    web64_probe_current.raster_end = web64_probe_raster();
    web64_probe_current.aux |= next_pc & 0xffff;
    web64_probe_cpu += elapsed - web64_probe_current_stolen;
    web64_probe_stolen += web64_probe_current_stolen;
    web64_probe_pcs[web64_probe_current.pc].cpu += elapsed - web64_probe_current_stolen;
    web64_probe_pcs[web64_probe_current.pc].stolen += web64_probe_current_stolen;
    web64_probe_pcs[web64_probe_current.pc].hits++;
    web64_probe_calls++;
    web64_probe_last = maincpu_clk;
    web64_probe_append(&web64_probe_current);
    if (web64_probe_state == 2 && maincpu_clk >= web64_probe_limit) web64_probe_state = 3;
}

static void web64_probe_steal(CLOCK start, unsigned int raster, unsigned int mask)
{
    web64_probe_record_t r = {0};
    r.start = start;
    r.end = maincpu_clk;
    r.pc = web64_probe_current.pc;
    r.raster_start = raster;
    r.raster_end = web64_probe_raster();
    r.kind = 2;
    r.aux = mask; /* request mask, NOT claimed per-cycle VIC causal attribution */
    web64_probe_current_stolen += maincpu_clk - start;
    web64_probe_append(&r);
}

EMSCRIPTEN_KEEPALIVE int vice_web_profiler_probe_control(int mode, int clocks, int records)
{
    if (mode == 0) {
        if (web64_probe_state == 1 || web64_probe_state == 2) {
            web64_probe_state = 4;
            web64_probe_reason = 4; /* explicitly cancelled */
        }
        return 1;
    }
    if (mode == 3) {
        free(web64_probe_records); web64_probe_records = NULL;
        free(web64_probe_pcs); web64_probe_pcs = NULL;
        web64_probe_state = web64_probe_capacity = web64_probe_count = 0;
        return 1;
    }
    if ((mode != 1 && mode != 2) || clocks < 1 || clocks > 100000000
        || records < 1 || records > 196608 || web64_probe_state == 1 || web64_probe_state == 2) return 0;
    free(web64_probe_records); web64_probe_records = NULL;
    free(web64_probe_pcs); web64_probe_pcs = NULL;
    web64_probe_state = 0;
    web64_probe_count = web64_probe_calls = web64_probe_reason = 0;
    web64_probe_first = web64_probe_last = web64_probe_cpu = web64_probe_stolen = 0;
    web64_probe_mode = mode;
    web64_probe_capacity = mode == 2 ? records : 0;
    if (mode == 2) web64_probe_records = calloc(records, sizeof(*web64_probe_records));
    web64_probe_pcs = calloc(65536, sizeof(*web64_probe_pcs));
    if (!web64_probe_pcs || (mode == 2 && !web64_probe_records)) {
        free(web64_probe_records); web64_probe_records = NULL;
        free(web64_probe_pcs); web64_probe_pcs = NULL;
        web64_probe_state = 4; web64_probe_reason = 5;
        return 0;
    }
    web64_probe_limit = clocks;
    web64_probe_state = 1;
    return 1;
}

EMSCRIPTEN_KEEPALIVE const uint32_t *vice_web_profiler_probe_info(void)
{
    web64_probe_info[0] = 0x30525057; /* WPR0: experimental P0, not public v1 */
    web64_probe_info[1] = web64_probe_state;
    web64_probe_info[2] = web64_probe_reason;
    web64_probe_info[3] = web64_probe_count;
    web64_probe_info[4] = sizeof(web64_probe_record_t);
    web64_probe_info[5] = (uint32_t)(uintptr_t)web64_probe_records;
    web64_probe_info[6] = web64_probe_first;
    web64_probe_info[7] = web64_probe_first >> 32;
    web64_probe_info[8] = web64_probe_last;
    web64_probe_info[9] = web64_probe_last >> 32;
    web64_probe_info[10] = web64_probe_cpu;
    web64_probe_info[11] = web64_probe_cpu >> 32;
    web64_probe_info[12] = web64_probe_stolen;
    web64_probe_info[13] = web64_probe_stolen >> 32;
    web64_probe_info[14] = vicii.cycles_per_line;
    web64_probe_info[15] = vicii.screen_height;
    web64_probe_info[16] = web64_probe_calls;
    web64_probe_info[17] = web64_probe_capacity * sizeof(web64_probe_record_t)
        + (web64_probe_pcs ? 65536 * sizeof(web64_probe_pc_t) : 0);
    web64_probe_info[18] = maincpu_clk;
    web64_probe_info[19] = maincpu_clk >> 32;
    web64_probe_info[20] = web64_probe_raster();
    web64_probe_info[21] = (vicii.raster_line << 8) | vicii.raster_cycle;
    web64_probe_info[22] = vicii.start_of_frame;
    web64_probe_info[23] = vicii.color_latency;
    return web64_probe_info;
}

#define WEB64_PROBE_ENABLED (web64_probe_state == 1 || web64_probe_state == 2)
#define WEB64_PROBE_BEGIN(pc, regs, kind) do { if (WEB64_PROBE_ENABLED) web64_probe_begin(pc, regs, kind); } while (0)
#define WEB64_PROBE_FINISH(pc) do { if (web64_probe_state == 2) web64_probe_finish(pc); } while (0)
#define WEB64_PROBE_FETCH(a, b, c) do { if (web64_probe_state == 2) web64_probe_current.bytes = (a) | ((b) << 8) | ((c) << 16); } while (0)
#define WEB64_PROBE_JSR(dst) do { if (web64_probe_state == 2) web64_probe_current.bytes = (web64_probe_current.bytes & 0xffff) | (((dst) >> 8) << 16); } while (0)
#define WEB64_PROBE_INTERRUPT(vector) do { if (web64_probe_state == 2) web64_probe_current.aux = (uint32_t)(vector) << 16; } while (0)
#endif
