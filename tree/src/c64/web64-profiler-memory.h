/* Capture-only x64sc memory evidence. GPL-2.0-or-later. */
#ifndef WEB64_PROFILER_MEMORY_H
#define WEB64_PROFILER_MEMORY_H
#include <stdint.h>

#define W64P_MEMORY_UNKNOWN 1u
#define W64P_GENERATION_UNKNOWN 2u
#define W64P_SPACE_RAM 1u
#define W64P_SPACE_BASIC 2u
#define W64P_SPACE_KERNAL 3u
#define W64P_SPACE_CHARGEN 4u
#define W64P_SPACE_CARTRIDGE 5u

typedef struct { uint32_t mapping, generation, flags; } w64p_memory_observation_t;
uint32_t mem_web_profiler_start(uint32_t ticket);
void mem_web_profiler_disarm(void);
void mem_web_profiler_clear(void);
const uint8_t *mem_web_profiler_baseline(void);
const uint8_t *mem_web_profiler_cartridge_baseline(void);
uint32_t mem_web_profiler_cartridge_bytes(void);
w64p_memory_observation_t mem_web_profiler_observe(uint16_t pc);
#endif
