/* Included only by the actual x64sc c64memsc.c (or the focused host fixture).
 * Normal CPU STORE/PUSH and normal RAM handlers are unchanged. At capture arm,
 * replace just the three stock RAM handlers; restore them on every disarm.
 * No guest reads. The supported EasyFlash MMU query is a side-effect-free
 * flash READ-state check, not a read/peek of the emulated bus.
 * GPL-2.0-or-later. */
#include "web64-profiler-memory.h"
#ifndef W64P_MEMORY_HOST_TEST
#define CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64cartsystem.h"
#undef CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "easyflash.h"
extern int mem_cartridge_type;
extern uint32_t easyflash_web_profiler_generation;
#endif

typedef struct {
    uint32_t ticket, active, valid, stock, serial, byte_generation[65536];
    uint32_t cart_profile, cart_bytes, flash_generation, flash_pending;
    uint8_t *cart_baseline;
    uint8_t baseline[65536];
} w64p_memory_t;
static w64p_memory_t *web64_memory;

static void web64_memory_write(uint16_t addr)
{
    w64p_memory_t *m = web64_memory;
    if (!m || !m->active) return;
    if (m->serial == UINT32_MAX) { m->valid = 0; return; }
    /* x64: exact byte generations keep unrelated data writes on a code
     * page from invalidating instructions. Same-value writes and restored
     * operands still advance identity; no guest reads are added. */
    m->byte_generation[addr] = ++m->serial;
}

static void web64_memory_ram_store(uint16_t addr, uint8_t value)
{
    web64_memory_write(addr);
    ram_store(addr, value);
}
static void web64_memory_hi_store(uint16_t addr, uint8_t value)
{
    web64_memory_write(addr);
    ram_hi_store(addr, value);
}
static void web64_memory_zero_store(uint16_t addr, uint8_t value)
{
    web64_memory_write(addr & 255);
    zero_store(addr, value);
}

/* These dispatchers can write RAM underneath a mapped cartridge without
 * re-entering mem_write_tab. Conservatively record even a discarded write. */
#define W64P_CART_STORES(X) X(roml_store) X(romh_store) X(roml_no_ultimax_store) \
    X(raml_no_ultimax_store) X(romh_no_ultimax_store) X(ramh_no_ultimax_store)
#define W64P_CART_WRAP(name) static void web64_memory_##name(uint16_t addr, uint8_t value) \
    { web64_memory_write(addr); name(addr, value); }
W64P_CART_STORES(W64P_CART_WRAP)
#undef W64P_CART_WRAP

static store_func_ptr_t web64_memory_wrap(store_func_ptr_t f)
{
    if (f == ram_store) return web64_memory_ram_store;
    if (f == ram_hi_store) return web64_memory_hi_store;
    if (f == zero_store) return web64_memory_zero_store;
#define W64P_CART_WRAP(name) if (f == name) return web64_memory_##name;
    W64P_CART_STORES(W64P_CART_WRAP)
#undef W64P_CART_WRAP
    return f;
}
static store_func_ptr_t web64_memory_unwrap(store_func_ptr_t f)
{
    if (f == web64_memory_ram_store) return ram_store;
    if (f == web64_memory_hi_store) return ram_hi_store;
    if (f == web64_memory_zero_store) return zero_store;
#define W64P_CART_WRAP(name) if (f == web64_memory_##name) return name;
    W64P_CART_STORES(W64P_CART_WRAP)
#undef W64P_CART_WRAP
    return f;
}
#undef W64P_CART_STORES

void mem_web_profiler_disarm(void)
{
    unsigned int config, page;
    if (!web64_memory || !web64_memory->active) return;
    web64_memory->active = 0;
    for (config = 0; config < NUM_CONFIGS; config++)
        for (page = 0; page <= 256; page++)
            mem_write_tab[config][page] = web64_memory_unwrap(mem_write_tab[config][page]);
}
void mem_web_profiler_clear(void)
{
    mem_web_profiler_disarm();
    if (web64_memory) free(web64_memory->cart_baseline);
    free(web64_memory);
    web64_memory = NULL;
}

static int web64_memory_stock_profile(void)
{
    cartridge_info_t *info;
    unsigned int count;
    if (c64_256k_enabled || plus256k_enabled || plus60k_enabled) return 0;
    /* REU writes are routed through mem_dma_store, including its zero-page
     * bypass. Other expansion/secondary-CPU writers are not certified here. */
    info = cartridge_get_info_list();
    if (!info) return 0;
    for (count = 0; count < 512 && info[count].name; count++) {
        int type = info[count].crtid;
        if (type != CARTRIDGE_REU && type != CARTRIDGE_GENERIC_8KB && type != CARTRIDGE_GENERIC_16KB &&
            type != CARTRIDGE_MAGIC_DESK && type != CARTRIDGE_EASYFLASH && cartridge_type_enabled(type)) return 0;
    }
    return count < 512;
}

uint32_t mem_web_profiler_start(uint32_t ticket)
{
    unsigned int config, page;
    if (!ticket || web64_memory) return 0;
    web64_memory = calloc(1, sizeof(*web64_memory));
    if (!web64_memory) return 0;
    web64_memory->ticket = ticket;
    web64_memory->active = web64_memory->valid = 1;
    web64_memory->stock = web64_memory_stock_profile();
    switch (mem_cartridge_type) {
        case CARTRIDGE_NONE: break;
        case CARTRIDGE_GENERIC_8KB: web64_memory->cart_profile = 1; break;
        case CARTRIDGE_GENERIC_16KB: web64_memory->cart_profile = 2; break;
        case CARTRIDGE_MAGIC_DESK: web64_memory->cart_profile = 3; break;
        case CARTRIDGE_EASYFLASH: web64_memory->cart_profile = 4; break;
        default: web64_memory->stock = 0; break;
    }
    if (web64_memory->stock && web64_memory->cart_profile) {
        uint32_t half = web64_memory->cart_profile <= 2 ? 8192 : 64 * 8192;
        uint8_t *base;
        int start, limit;
        web64_memory->cart_bytes = 2 * half;
        web64_memory->cart_baseline = malloc(2 * half);
        if (!web64_memory->cart_baseline) { free(web64_memory); web64_memory = NULL; return 0; }
        memcpy(web64_memory->cart_baseline, roml_banks, half);
        memcpy(web64_memory->cart_baseline + half, romh_banks, half);
        web64_memory->flash_generation = easyflash_web_profiler_generation;
        if (web64_memory->cart_profile == 4) {
            easyflash_mmu_translate(0x8000, &base, &start, &limit);
            web64_memory->flash_pending = !base;
            easyflash_mmu_translate(0xa000, &base, &start, &limit);
            web64_memory->flash_pending |= !base;
        }
    }
    memcpy(web64_memory->baseline, mem_ram, sizeof(web64_memory->baseline));
    for (config = 0; config < NUM_CONFIGS; config++)
        for (page = 0; page <= 256; page++)
            mem_write_tab[config][page] = web64_memory_wrap(mem_write_tab[config][page]);
    return (uint32_t)sizeof(*web64_memory) + web64_memory->cart_bytes;
}

const uint8_t *mem_web_profiler_baseline(void)
{
    return web64_memory ? web64_memory->baseline : NULL;
}

const uint8_t *mem_web_profiler_cartridge_baseline(void)
{
    return web64_memory ? web64_memory->cart_baseline : NULL;
}
uint32_t mem_web_profiler_cartridge_bytes(void)
{
    return web64_memory ? web64_memory->cart_bytes : 0;
}

static void web64_memory_invalidate(void)
{
    /* A mapping-table rebuild is cold and may change physical ownership.
     * Restore old hooks BEFORE it changes tables; prior evidence stays frozen. */
    if (web64_memory) web64_memory->valid = 0;
    mem_web_profiler_disarm();
}

static uint32_t web64_memory_space(uint16_t addr)
{
    uintptr_t base = (uintptr_t)_mem_read_base_tab_ptr[addr >> 8];
    if (addr < 2) return 0; /* CPU port, not RAM, even if the table base is RAM */
    if (base == (uintptr_t)mem_ram) return W64P_SPACE_RAM;
    if (addr >= 0xa000 && addr < 0xc000 && base + 0xa000 == (uintptr_t)c64memrom_basic64_rom) return W64P_SPACE_BASIC;
    if (addr >= 0xe000 && base + 0xe000 == (uintptr_t)c64memrom_kernal64_trap_rom) return W64P_SPACE_KERNAL;
    if (addr >= 0xd000 && addr < 0xe000 && base + 0xd000 == (uintptr_t)mem_chargen_rom) return W64P_SPACE_CHARGEN;
    if (web64_memory->cart_profile) {
        read_func_ptr_t read = _mem_read_tab_ptr[addr >> 8];
        uint32_t window = 0, bank = 0;
        uint8_t *flash_base;
        int start, limit;
        if (addr >= 0x8000 && addr < 0xa000 && read == roml_read) { window = 1; bank = roml_bank; }
        if (addr >= 0xa000 && addr < 0xc000 && read == romh_read) { window = 2; bank = romh_bank; }
        if (addr >= 0xe000 && read == ultimax_romh_read_hirom) { window = 3; bank = romh_bank; }
        if (!window || bank >= (web64_memory->cart_profile <= 2 ? 1u : 64u)) return 0;
        if (web64_memory->cart_profile == 4) {
            easyflash_mmu_translate(addr, &flash_base, &start, &limit);
            /* Also verify that the chip's READ pointer agrees with the actual
             * ROM bank latch. No identity is inferred from the CPU PC alone. */
            if (!flash_base || (uintptr_t)flash_base + (addr & 0xe000) !=
                (uintptr_t)(window == 1 ? roml_banks : romh_banks) + bank * 8192) return 0;
        }
        /* WPR1 mapping v3: PLA[7:0], space[11:8], CPU window[15:12],
         * bank[21:16], supported profile[27:24]. */
        return W64P_SPACE_CARTRIDGE | (window << 4) | (bank << 8) | (web64_memory->cart_profile << 16);
    }
    return 0;
}

w64p_memory_observation_t mem_web_profiler_observe(uint16_t pc)
{
    w64p_memory_observation_t o = { (uint32_t)mem_config, 0, W64P_MEMORY_UNKNOWN | W64P_GENERATION_UNKNOWN };
    w64p_memory_t *m = web64_memory;
    uint32_t space, generation, tail;
    if (!m || !m->active || !m->valid || !m->stock || !_mem_read_base_tab_ptr) return o;
    space = web64_memory_space(pc);
    /* Conservative three-byte fetch span. Crossing different memory spaces,
     * wrapping through the CPU port, or mid-instruction remapping stays unknown. */
    if (!space || space != web64_memory_space((uint16_t)(pc + 1)) ||
        space != web64_memory_space((uint16_t)(pc + 2))) return o;
    o.mapping |= space << 8;
    o.flags = W64P_GENERATION_UNKNOWN;
    if ((space & 15) == W64P_SPACE_CARTRIDGE) {
        /* Flash commands conservatively make the entire cartridge generation
         * unproven for this capture, including pending erase operations. A new
         * capture can bind its new arm-time bytes once flash is READ again. */
        if (m->cart_profile != 4 || (!m->flash_pending && easyflash_web_profiler_generation != UINT32_MAX &&
            m->flash_generation == easyflash_web_profiler_generation)) o.flags = 0;
        return o;
    }
    if (space != W64P_SPACE_RAM) return o; /* ROM trap/patch generations unproven */
    generation = m->byte_generation[pc];
    tail = m->byte_generation[(uint16_t)(pc + 1)];
    if (tail > generation) generation = tail;
    tail = m->byte_generation[(uint16_t)(pc + 2)];
    o.generation = generation > tail ? generation : tail;
    o.flags = 0;
    return o;
}
