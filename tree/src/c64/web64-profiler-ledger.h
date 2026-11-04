/* Bounded observer ledger, shared by x64sc and fast native-host tests.
 * GPL-2.0-or-later, like VICE. No guest memory, CPU execution or wall clocks.
 * The caller supplies actual instruction boundaries and blocked-bus intervals.
 */
#ifndef WEB64_PROFILER_LEDGER_H
#define WEB64_PROFILER_LEDGER_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef W64P_CALLOC
#define W64P_CALLOC calloc
#define W64P_FREE free
#endif
#define W64P_DETAIL_BYTES (8u * 1024u * 1024u)
#define W64P_SUMMARY_BYTES (8u * 1024u * 1024u)
#define W64P_MAX_FRAMES 16u
#define W64P_MAX_LINES 512u
#define W64P_MAX_PCS 16384u
#define W64P_HASH_SLOTS (W64P_MAX_PCS * 2u)
#define W64P_MAX_PROBES 64u
#define W64P_PENDING_STALLS 256u

enum { W64P_ARMED = 1, W64P_ACTIVE = 2, W64P_DONE = 3, W64P_INCOMPLETE = 4 };
enum { W64P_OK, W64P_OVERFLOW, W64P_CLOCK, W64P_ACCOUNTING, W64P_CANCEL,
       W64P_ALLOCATION, W64P_DICTIONARY, W64P_RESET, W64P_MODEL, W64P_JAM };
enum { W64P_BADLINE = 1, W64P_SPRITE = 2, W64P_REU = 4, W64P_UNKNOWN = 8 };
enum { W64P_INSTRUCTION = 0, W64P_INTERRUPT = 1, W64P_STALL = 2 };
#define W64P_EDGE_START 0x100u
#define W64P_EDGE_END 0x200u

typedef struct {
    uint64_t start, end;
    uint16_t pc, next_pc;
    uint32_t bytes, regs, identity, kind, context, stolen, aux;
} w64p_record_t; /* native ABI stride 48; not the exported file envelope */
typedef struct {
    uint64_t cpu, stolen;
    uint32_t pc, mapping, bytes, generation, hits, kind, first_record, flags;
} w64p_pc_t;
typedef struct {
    uint32_t elapsed, stolen, badline, sprite, reu, mixed, irq_entry, flags;
} w64p_stats_t;
typedef struct { uint64_t start, end; uint32_t cause; } w64p_stall_t;
typedef struct {
    uint32_t state, reason, mode, frames, lines, cycles, count, capacity;
    uint32_t pc_count, pending_count, has_clock, mapping, owned;
    uint32_t generation, identity_flags, unknown_serial;
    uint64_t start, limit, last, observed, pending_stolen, last_stall_end;
    w64p_record_t current;
    w64p_stall_t pending[W64P_PENDING_STALLS];
    w64p_record_t *records;
    w64p_pc_t *pcs;
    uint32_t *slots;
    w64p_stats_t *frame, *line;
} w64p_ledger_t;

static int w64p_running(const w64p_ledger_t *c)
{
    return c && (c->state == W64P_ARMED || c->state == W64P_ACTIVE);
}

static void w64p_abort(w64p_ledger_t *c, unsigned int reason)
{
    if (w64p_running(c)) {
        c->state = W64P_INCOMPLETE;
        c->reason = reason;
        /* Pending instructions never modify published tables or counts. */
        c->pending_count = 0;
    }
}

static void w64p_destroy(w64p_ledger_t *c)
{
    if (!c) return;
    W64P_FREE(c->records);
    W64P_FREE(c->pcs);
    W64P_FREE(c->slots);
    W64P_FREE(c->frame);
    W64P_FREE(c->line);
    W64P_FREE(c);
}

static w64p_ledger_t *w64p_create(unsigned int mode, unsigned int frames,
    unsigned int lines, unsigned int cycles, uint64_t start, unsigned int capacity)
{
    w64p_ledger_t *c;
    uint64_t span;
    uint32_t owned;
    if ((mode != 1 && mode != 2) || frames < 1 || frames > W64P_MAX_FRAMES ||
        lines < 1 || lines > W64P_MAX_LINES || cycles < 1 || cycles > 128 ||
        (mode == 2 && (!capacity || capacity > W64P_DETAIL_BYTES / sizeof(w64p_record_t)))) return NULL;
    span = (uint64_t)frames * lines * cycles;
    if (start > UINT64_MAX - span) return NULL;
    owned = (uint32_t)(sizeof(*c) + W64P_MAX_PCS * sizeof(w64p_pc_t) +
        W64P_HASH_SLOTS * sizeof(uint32_t) + frames * (lines + 1) * sizeof(w64p_stats_t));
    if (owned > W64P_SUMMARY_BYTES) return NULL;
    c = W64P_CALLOC(1, sizeof(*c));
    if (!c) return NULL;
    c->pcs = W64P_CALLOC(W64P_MAX_PCS, sizeof(*c->pcs));
    c->slots = W64P_CALLOC(W64P_HASH_SLOTS, sizeof(*c->slots));
    c->frame = W64P_CALLOC(frames, sizeof(*c->frame));
    c->line = W64P_CALLOC(frames * lines, sizeof(*c->line));
    if (mode == 2) c->records = W64P_CALLOC(capacity, sizeof(*c->records));
    if (!c->pcs || !c->slots || !c->frame || !c->line || (mode == 2 && !c->records)) {
        w64p_destroy(c);
        return NULL;
    }
    c->mode = mode;
    c->frames = frames;
    c->lines = lines;
    c->cycles = cycles;
    c->start = c->last = start;
    c->limit = start + span;
    c->capacity = mode == 2 ? capacity : 0;
    c->owned = owned + c->capacity * (uint32_t)sizeof(*c->records);
    c->state = W64P_ARMED;
    return c;
}

static void w64p_begin(w64p_ledger_t *c, uint64_t clock, uint32_t pc,
    uint32_t regs, uint32_t mapping, uint32_t kind)
{
    if (!w64p_running(c)) return;
    if (c->has_clock && clock != c->observed) {
        w64p_abort(c, W64P_CLOCK);
        return;
    }
    c->has_clock = 1;
    c->observed = clock;
    c->mapping = mapping;
    c->generation = 0;
    c->identity_flags = 1; /* callers without memory evidence remain raw-only */
    memset(&c->current, 0, sizeof(c->current));
    c->current.start = clock;
    c->current.pc = (uint16_t)pc;
    c->current.regs = regs;
    c->current.kind = kind;
    c->pending_count = 0;
    c->pending_stolen = 0;
    c->last_stall_end = clock;
    if (clock >= c->start) c->state = W64P_ACTIVE;
}

static void w64p_stall(w64p_ledger_t *c, uint64_t start, uint64_t end, uint32_t cause)
{
    w64p_stall_t *s;
    if (!w64p_running(c) || start == end) return;
    if (end < start || start < c->last_stall_end ||
        UINT64_MAX - c->pending_stolen < end - start) {
        w64p_abort(c, W64P_ACCOUNTING);
        return;
    }
    c->pending_stolen += end - start;
    c->last_stall_end = end;
    /* Only requested-window detail needs staging, but retain the full scalar
     * stolen count for instruction-cost decoding across capture edges. */
    if (end <= c->start || start >= c->limit) return;
    if (start < c->start) start = c->start;
    if (end > c->limit) end = c->limit;
    if (!cause) cause = W64P_UNKNOWN;
    if (c->pending_count) {
        s = &c->pending[c->pending_count - 1];
        if (s->end == start && s->cause == cause) { s->end = end; return; }
    }
    if (c->pending_count == W64P_PENDING_STALLS) {
        w64p_abort(c, W64P_OVERFLOW);
        return;
    }
    s = &c->pending[c->pending_count++];
    s->start = start;
    s->end = end;
    s->cause = cause;
}

/* Capture-local physical evidence, never source/routine claims. Unknown memory
 * observations cannot pretend that one raw PLA value identifies a stable bank. */
static w64p_pc_t *w64p_lookup(w64p_ledger_t *c, uint32_t *identity)
{
    uint32_t hash = ((uint32_t)c->current.pc * 2654435761u) ^
        (c->mapping * 2246822519u) ^ (c->current.bytes * 3266489917u) ^ c->current.kind ^
        (c->generation * 668265263u) ^ (c->identity_flags * 374761393u);
    uint32_t n, slot = hash & (W64P_HASH_SLOTS - 1);
    for (n = 0; n < W64P_MAX_PROBES; n++, slot = (slot + 1) & (W64P_HASH_SLOTS - 1)) {
        uint32_t id = c->slots[slot];
        w64p_pc_t *p;
        if (!id) {
            if (c->pc_count == W64P_MAX_PCS) return NULL;
            id = ++c->pc_count;
            c->slots[slot] = id;
            p = &c->pcs[id - 1];
            p->pc = c->current.pc;
            p->mapping = c->mapping;
            p->bytes = c->current.bytes;
            p->generation = c->generation;
            p->kind = c->current.kind;
            p->first_record = c->count;
            p->flags = c->identity_flags;
        } else p = &c->pcs[id - 1];
        if (p->pc == c->current.pc && p->mapping == c->mapping &&
            p->bytes == c->current.bytes && p->kind == c->current.kind &&
            p->generation == c->generation && p->flags == c->identity_flags) {
            *identity = id;
            return p;
        }
    }
    return NULL;
}

static void w64p_add_stats(w64p_stats_t *s, uint32_t count, uint32_t cause, int irq)
{
    if (!cause) {
        s->elapsed += count;
        if (irq) s->irq_entry += count;
    } else {
        s->stolen += count;
        if (cause == W64P_BADLINE) s->badline += count;
        else if (cause == W64P_SPRITE) s->sprite += count;
        else if (cause == W64P_REU) s->reu += count;
        else s->mixed += count; /* compound/unknown, counted only once */
    }
}

static void w64p_range(w64p_ledger_t *c, uint64_t start, uint64_t end, uint32_t cause, int irq)
{
    while (start < end) {
        uint32_t line = (uint32_t)((start - c->start) / c->cycles);
        uint32_t offset = (uint32_t)((start - c->start) % c->cycles);
        uint32_t count = c->cycles - offset;
        if (end - start < count) count = (uint32_t)(end - start);
        w64p_add_stats(&c->line[line], count, cause, irq);
        w64p_add_stats(&c->frame[line / c->lines], count, cause, irq);
        start += count;
    }
}

static void w64p_finish(w64p_ledger_t *c, uint64_t end, uint32_t next_pc)
{
    uint64_t start, stop, clipped_stolen = 0;
    uint32_t n, identity;
    int irq;
    w64p_pc_t *p;
    if (!w64p_running(c)) return;
    start = c->current.start;
    if (end < start) { w64p_abort(c, W64P_CLOCK); return; }
    if (c->pending_stolen > end - start || c->pending_stolen > UINT32_MAX) {
        w64p_abort(c, W64P_ACCOUNTING); return;
    }
    c->observed = end;
    if (end == start || end <= c->start) return;
    stop = end < c->limit ? end : c->limit;
    if (start < c->start) start = c->start;
    if (start != c->last || stop < start) { w64p_abort(c, W64P_CLOCK); return; }
    for (n = 0; n < c->pending_count; n++) {
        if (c->pending[n].start < start || c->pending[n].end > stop) {
            w64p_abort(c, W64P_ACCOUNTING); return;
        }
        clipped_stolen += c->pending[n].end - c->pending[n].start;
    }
    if (clipped_stolen > stop - start) { w64p_abort(c, W64P_ACCOUNTING); return; }
    if (c->mode == 2 && c->capacity - c->count < 1 + c->pending_count) {
        w64p_abort(c, W64P_OVERFLOW); return;
    }
    p = w64p_lookup(c, &identity);
    if (!p) { w64p_abort(c, W64P_DICTIONARY); return; }
    /* Everything above is preflight. No partially committed instruction can
     * leak into a cancelled/overflow result or a host-yield snapshot. */
    p->cpu += stop - start - clipped_stolen;
    p->stolen += clipped_stolen;
    p->hits++;
    irq = c->current.kind == W64P_INTERRUPT || (c->current.bytes & 255) == 0;
    w64p_range(c, start, stop, 0, irq);
    for (n = 0; n < c->pending_count; n++) {
        const w64p_stall_t *s = &c->pending[n];
        w64p_range(c, s->start, s->end, s->cause, 0);
    }
    if (c->mode == 2) {
        w64p_record_t *r = &c->records[c->count++];
        *r = c->current;
        r->end = end; /* preserve full instruction span, frame tables are clipped */
        r->next_pc = (uint16_t)next_pc;
        r->identity = identity;
        r->stolen = (uint32_t)c->pending_stolen;
        if (r->start < c->start) r->kind |= W64P_EDGE_START;
        if (end > c->limit) r->kind |= W64P_EDGE_END;
        for (n = 0; n < c->pending_count; n++) {
            const w64p_stall_t *s = &c->pending[n];
            r = &c->records[c->count++];
            memset(r, 0, sizeof(*r));
            r->start = s->start;
            r->end = s->end;
            r->pc = c->current.pc;
            r->identity = identity;
            r->kind = W64P_STALL;
            r->aux = s->cause;
        }
    }
    c->last = stop;
    c->pending_count = 0;
    if (stop == c->limit) c->state = W64P_DONE;
    else c->state = W64P_ACTIVE;
}
#endif
