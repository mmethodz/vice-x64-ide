/** \file   main.c
 * \brief   Headless UI startup
 *
 * \author  David Hogan <david.q.hogan@gmail.com>
 */

/*
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>

#include "attach.h"
#include "autostart.h"
#include "cartridge.h"
#include "c64meminit.h"
#include "drive.h"
#include "interrupt.h"
#include "joyport.h"
#include "joystick.h"
#include "keyboard.h"
#include "keymap.h"
#include "kbdbuf.h"
#include "log.h"
#include "machine.h"
#include "main.h"
#include "maincpu.h"
#include "mem.h"
#include "mon_breakpoint.h"
#include "montypes.h"
#include "mos6510.h"
#include "resources.h"
#include "reu.h"
#include "sid.h"
#include "sound.h"
#include "ui.h"
#include "video.h"
#include "web64-frame-step.h"
#include "vsync.h"
#include "vsyncapi.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

/* For the ugly hack below */
#ifdef WINDOWS_COMPILE
# include "windows.h"
#endif

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE int vice_web_boot(void);
EMSCRIPTEN_KEEPALIVE void vice_web_reset(int mode);
EMSCRIPTEN_KEEPALIVE int vice_web_pause(void);
EMSCRIPTEN_KEEPALIVE int vice_web_resume(void);
EMSCRIPTEN_KEEPALIVE int vice_web_pause_status(void);
EMSCRIPTEN_KEEPALIVE int vice_web_boot_status(void);
EMSCRIPTEN_KEEPALIVE int vice_web_set_resource(const char *name, const char *value);
EMSCRIPTEN_KEEPALIVE const char *vice_web_get_resource(const char *name);
EMSCRIPTEN_KEEPALIVE int vice_web_reu_export(const char *filename);
EMSCRIPTEN_KEEPALIVE int vice_web_attach_disk(int unit, const char *filename);
EMSCRIPTEN_KEEPALIVE int vice_web_autostart_prg(const char *filename, int autorun);
EMSCRIPTEN_KEEPALIVE int vice_web_attach_cartridge(const char *filename);
EMSCRIPTEN_KEEPALIVE int vice_web_detach_cartridge(void);
EMSCRIPTEN_KEEPALIVE int vice_web_cartridge_id(void);
EMSCRIPTEN_KEEPALIVE int vice_web_load_psid(const char *filename, int tune);
EMSCRIPTEN_KEEPALIVE int vice_web_set_warp_enabled(int enabled);
EMSCRIPTEN_KEEPALIVE void vice_web_detach_disk(int unit);
EMSCRIPTEN_KEEPALIVE const char *vice_web_disk_name(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_led(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_led2(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_track(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_half_track(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_sector(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_side(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_drive_image_attached(int unit);
EMSCRIPTEN_KEEPALIVE int vice_web_save_snapshot(const char *filename);
EMSCRIPTEN_KEEPALIVE int vice_web_load_snapshot(const char *filename);
EMSCRIPTEN_KEEPALIVE int vice_web_snapshot_pending(void);
EMSCRIPTEN_KEEPALIVE int vice_web_snapshot_result(void);
EMSCRIPTEN_KEEPALIVE int vice_web_paste_text(const char *text);
EMSCRIPTEN_KEEPALIVE int vice_web_write_input(const char *type, const char *code, int pressed, int port, const char *value);
EMSCRIPTEN_KEEPALIVE int vice_web_joystick_value(int port);
EMSCRIPTEN_KEEPALIVE int vice_web_keyboard_row(int row);
EMSCRIPTEN_KEEPALIVE double vice_web_emulated_fps(void);
EMSCRIPTEN_KEEPALIVE double vice_web_cpu_percent(void);
EMSCRIPTEN_KEEPALIVE int vice_web_warp_enabled(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_register_get(int reg_id);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_register_set(int reg_id, int value);
EMSCRIPTEN_KEEPALIVE int vice_web_run_program(int load_address, int start_address, const unsigned char *bytes, int length);
EMSCRIPTEN_KEEPALIVE int vice_web_run_program_pending(void);
EMSCRIPTEN_KEEPALIVE int vice_web_run_program_result(void);
EMSCRIPTEN_KEEPALIVE unsigned char *vice_web_monitor_memory_read(int address, int length);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_debugger_version(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_cpu_mapping(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_memory_write(int address, const unsigned char *bytes, int length);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_mutation_generation(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_ram_visible(int address, int length);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_mutate_memory(int address, const unsigned char *expected,
    const unsigned char *next, int length, int mapping, int generation);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_mutate_register(int reg_id, int expected, int next,
    int mapping, int generation);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_set(int address, int enabled);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_clear(int address);
void vice_web_monitor_breakpoint_hit(int memspace, int address);
int vice_web_monitor_breakpoint_guard_match(int memspace, int address);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_guard_set(int address, int memspace, int mapping, const unsigned char *bytes, int length);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_guard_clear(int guard_id);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_set(int address, int memspace, int mapping,
    const unsigned char *bytes, int byte_length, const int *program, int program_length);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_clear(int owner_id);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_match_count(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_match_id(int index);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_unavailable_count(int owner_id);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_step_past(int address);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_step_instruction(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_version(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_epoch(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_arm(int pc, int mapping, int epoch);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_status(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_epoch(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_pc(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_mapping(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_line(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_cycle(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_clock_lo(void);
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_clock_hi(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_pending(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_address(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_memspace(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_mapping(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_kind(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_checkpoint_id(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_guard_id(void);
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_clear(void);
EMSCRIPTEN_KEEPALIVE const char *vice_web_monitor_disassemble(int address, int count);

extern unsigned int headless_statusbar_drive_led_pwm1(unsigned int drive_number);
extern unsigned int headless_statusbar_drive_led_pwm2(unsigned int drive_number);
extern unsigned int headless_statusbar_drive_half_track(unsigned int drive_number);
extern unsigned int headless_statusbar_drive_side(unsigned int drive_number);
extern unsigned int headless_statusbar_drive_image_attached(unsigned int drive_number);
#endif


/** \brief  Program driver
 *
 * \param[in]   argc    argument count
 * \param[in]   argv    argument vector
 *
 * \return  0 on success, any other value on failure
 *
 * \note    This should return either EXIT_SUCCESS on success or EXIT_FAILURE
 *          on failure. Unfortunately, there are a lot of exit(1)/exit(-1)
 *          calls, so don't expect to get a meaningful exit status.
 */
int main(int argc, char **argv)
{
    /* printf("%s\n", __func__); */

    return main_program(argc, argv);
}

#ifdef __EMSCRIPTEN__
static int vice_web_started = 0;
static int vice_web_exit_code = 0;
static int vice_web_snapshot_pending_flag = 0;
static int vice_web_snapshot_result_code = 0;
static int vice_web_snapshot_operation = 0;
static int vice_web_run_program_pending_flag = 0;
static int vice_web_run_program_result_code = 0;
static int vice_web_run_program_load_address = 0;
static int vice_web_run_program_start_address = 0;
static int vice_web_run_program_length = 0;
static unsigned char vice_web_run_program_bytes[65536];
static char vice_web_snapshot_filename[1024];
static char vice_web_sid_engine_model_arg[16] = "257";
static char vice_web_resid_sampling_arg[16] = "0";
static char *vice_web_argv[] = {
    (char *)"x64sc",
    (char *)"-sidenginemodel", vice_web_sid_engine_model_arg,
    (char *)"-residsamp", vice_web_resid_sampling_arg,
    NULL
};
static char vice_web_resource_value[128];
static int vice_web_joystick_configured = 0;
static int vice_web_left_shift_pressed = 0;
static int vice_web_right_shift_pressed = 0;
static int vice_web_ctrl_pressed = 0;
static int vice_web_cbm_pressed = 0;
static int vice_web_virtual_shift_count = 0;
static int vice_web_virtual_ctrl_count = 0;
static int vice_web_virtual_cbm_count = 0;
static int vice_web_deshift_count = 0;
static int vice_web_monitor_breakpoint_hit_pending_flag = 0;
static int vice_web_monitor_breakpoint_hit_address_value = 0;
static int vice_web_monitor_breakpoint_hit_memspace_value = -1;
static int vice_web_monitor_breakpoint_hit_mapping_value = -1;
static int vice_web_monitor_breakpoint_hit_kind_value = 0;
static int vice_web_monitor_breakpoint_hit_checkpoint_id_value = 0;
static int vice_web_monitor_breakpoint_hit_guard_id_value = 0;
static int vice_web_monitor_matched_guard_id = 0;
static int vice_web_monitor_matched_owner_count = 0;
static int vice_web_monitor_step_past_address = -1;
static int vice_web_monitor_instruction_step_pending = 0;
static int vice_web_monitor_rearm_address = -1;
/* 0 idle, 1 armed, 2 VIC transition observed, 3 stopped, 4 cancelled. */
int vice_web_frame_step_state = 0;
static uint32_t vice_web_frame_epoch = 0;
static uint32_t vice_web_frame_armed_epoch = 0;
static uint32_t vice_web_frame_stop_epoch = 0;
static int vice_web_frame_stop_pc = -1;
static int vice_web_frame_stop_mapping = -1;
static int vice_web_frame_stop_line = -1;
static int vice_web_frame_stop_cycle = -1;
static CLOCK vice_web_frame_stop_clock = 0;
#define VICE_WEB_MONITOR_GUARDS 128
#define VICE_WEB_MONITOR_GUARD_BYTES 8
#define VICE_WEB_MONITOR_OWNERS 128
#define VICE_WEB_MONITOR_PROGRAM_LIMIT 128
#define VICE_WEB_MONITOR_STACK_LIMIT 32

/* Small, validated postfix filter. Arguments are signed i32 words; results are
 * u32. Forward JZ/JNZ leave a normalized Boolean on the taken path and pop it
 * on the fallthrough path. They give &&/|| real short-circuit semantics. */
enum vice_web_monitor_opcode {
    VICE_WEB_PUSH = 1, VICE_WEB_REG = 2, VICE_WEB_READ8 = 3,
    VICE_WEB_READ16 = 4, VICE_WEB_READ32 = 5, VICE_WEB_EQ = 10,
    VICE_WEB_NE = 11, VICE_WEB_LT = 12, VICE_WEB_LE = 13,
    VICE_WEB_GT = 14, VICE_WEB_GE = 15, VICE_WEB_AND = 16,
    VICE_WEB_OR = 17, VICE_WEB_XOR = 18, VICE_WEB_NOT = 19,
    VICE_WEB_BITNOT = 20, VICE_WEB_JZ = 21, VICE_WEB_JNZ = 22,
    VICE_WEB_BOOL = 23
};

typedef struct vice_web_monitor_owner_s {
    int active, address, mapping, byte_length, program_length, next;
    int unavailable_count;
    unsigned char bytes[VICE_WEB_MONITOR_GUARD_BYTES];
    int program[VICE_WEB_MONITOR_PROGRAM_LIMIT * 2];
} vice_web_monitor_owner_t;
static vice_web_monitor_owner_t vice_web_monitor_owners[VICE_WEB_MONITOR_OWNERS];
static int vice_web_monitor_owner_head[65536]; /* slot + 1; zero is empty */
static unsigned char vice_web_monitor_owner_address[65536];
static int vice_web_monitor_matched_owner_ids[VICE_WEB_MONITOR_OWNERS];
typedef struct vice_web_monitor_guard_s {
    int active, address, memspace, mapping, length;
    unsigned char bytes[VICE_WEB_MONITOR_GUARD_BYTES];
} vice_web_monitor_guard_t;
static vice_web_monitor_guard_t vice_web_monitor_guards[VICE_WEB_MONITOR_GUARDS];
static unsigned char vice_web_monitor_guarded_address[65536];
static unsigned char vice_web_monitor_raw_address[65536];
static int vice_web_monitor_checkpoint_id[65536];
static unsigned char vice_web_monitor_memory_scratch[4096];
static char vice_web_monitor_disassembly_scratch[256];
static unsigned int vice_web_monitor_mutation_serial = 0;

#define VICE_WEB_SNAPSHOT_SAVE 1
#define VICE_WEB_SNAPSHOT_LOAD 2

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_register_get(int reg_id)
{
#ifndef C64DTV
    switch (reg_id) {
        case 0:
            return (int)(MOS6510_REGS_GET_PC(&maincpu_regs) & 0xffff);
        case 1:
            return (int)(MOS6510_REGS_GET_A(&maincpu_regs) & 0xff);
        case 2:
            return (int)(MOS6510_REGS_GET_X(&maincpu_regs) & 0xff);
        case 3:
            return (int)(MOS6510_REGS_GET_Y(&maincpu_regs) & 0xff);
        case 4:
            return (int)(MOS6510_REGS_GET_SP(&maincpu_regs) & 0xff);
        case 5:
            return (int)(MOS6510_REGS_GET_STATUS(&maincpu_regs) & 0xff);
        default:
            return 0;
    }
#else
    return reg_id == 0 ? (int)(reg_pc & 0xffff) : 0;
#endif
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_register_set(int reg_id, int value)
{
#ifndef C64DTV
    switch (reg_id) {
        case 0:
            MOS6510_REGS_SET_PC(&maincpu_regs, value & 0xffff);
            return 0;
        case 1:
            MOS6510_REGS_SET_A(&maincpu_regs, value & 0xff);
            return 0;
        case 2:
            MOS6510_REGS_SET_X(&maincpu_regs, value & 0xff);
            return 0;
        case 3:
            MOS6510_REGS_SET_Y(&maincpu_regs, value & 0xff);
            return 0;
        case 4:
            MOS6510_REGS_SET_SP(&maincpu_regs, value & 0xff);
            return 0;
        case 5:
            MOS6510_REGS_SET_STATUS(&maincpu_regs, value & 0xff);
            return 0;
        default:
            return -1;
    }
#else
    if (reg_id == 0) {
        reg_pc = (unsigned int)(value & 0xffff);
        return 0;
    }
    return -1;
#endif
}

static void vice_web_run_program_trap(uint16_t addr, void *data)
{
    int i;

    (void)addr;
    (void)data;

    for (i = 0; i < vice_web_run_program_length; i++) {
        mem_inject((uint32_t)(vice_web_run_program_load_address + i), vice_web_run_program_bytes[i]);
    }
#ifndef C64DTV
    maincpu_set_pc(vice_web_run_program_start_address & 0xffff);
    reg_pc = (unsigned int)(vice_web_run_program_start_address & 0xffff);
    maincpu_resync_limits();
#else
    reg_pc = (unsigned int)(vice_web_run_program_start_address & 0xffff);
#endif
    vice_web_run_program_result_code = 0;
    vice_web_run_program_pending_flag = 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_run_program(int load_address, int start_address, const unsigned char *bytes, int length)
{
    if (bytes == NULL || length < 0 || load_address < 0 || load_address > 0xffff
        || start_address < 0 || start_address > 0xffff
        || length > 65536 - load_address || vice_web_run_program_pending_flag) {
        return -1;
    }

    vice_web_frame_step_cancel();
    memcpy(vice_web_run_program_bytes, bytes, (size_t)length);
    vice_web_run_program_load_address = load_address;
    vice_web_run_program_start_address = start_address;
    vice_web_run_program_length = length;
    ui_pause_disable();
    vice_web_run_program_pending_flag = 1;
    vice_web_run_program_result_code = -1;
    interrupt_maincpu_trigger_trap(vice_web_run_program_trap, NULL);
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_run_program_pending(void)
{
    return vice_web_run_program_pending_flag;
}

EMSCRIPTEN_KEEPALIVE int vice_web_run_program_result(void)
{
    return vice_web_run_program_result_code;
}

EMSCRIPTEN_KEEPALIVE unsigned char *vice_web_monitor_memory_read(int address, int length)
{
    int i;
    int size = length;
    if (size < 0) {
        size = 0;
    }
    if (size > (int)sizeof(vice_web_monitor_memory_scratch)) {
        size = (int)sizeof(vice_web_monitor_memory_scratch);
    }
    for (i = 0; i < size; i++) {
        /* The monitor's ordinary mem_read can acknowledge I/O and retrigger
         * checkpoints. Peek the CPU-visible mapping without bus side effects. */
        vice_web_monitor_memory_scratch[i] = mem_peek_with_config(mem_get_current_bank_config(),
            (uint16_t)((address + i) & 0xffff), NULL);
    }
    return vice_web_monitor_memory_scratch;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_debugger_version(void)
{
    return 5;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_cpu_mapping(void)
{
    return mem_get_current_bank_config();
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_memory_write(int address, const unsigned char *bytes, int length)
{
    int i;
    if (bytes == NULL || length < 0) {
        return -1;
    }
    for (i = 0; i < length; i++) {
        mem_inject((uint32_t)((address + i) & 0xffff), bytes[i]);
    }
    return length;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_mutation_generation(void)
{
    return (int)vice_web_monitor_mutation_serial;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_ram_visible(int address, int length)
{
    int i, config, memory_hack = -1;
    if (!ui_pause_trap_active() || address < 0 || length < 1 || length > 4
        || address > 0x10000 - length || resources_get_int("MemoryHack", &memory_hack) < 0
        || memory_hack != 0) return 0;
    config = mem_get_current_bank_config();
    for (i = 0; i < length; i++) {
        if (!vice_web_c64_plain_ram(config, (unsigned int)(address + i))) return 0;
    }
    return 1;
}

/* Return 0 on success. Every accepted debugger action advances the serial,
 * including a write whose bytes equal the previous value. */
EMSCRIPTEN_KEEPALIVE int vice_web_monitor_mutate_memory(int address, const unsigned char *expected,
    const unsigned char *next, int length, int mapping, int generation)
{
    int i, config;
    if (expected == NULL || next == NULL || length < 1 || length > 4) return -1;
    if (!ui_pause_trap_active() || (unsigned int)generation != vice_web_monitor_mutation_serial) return -2;
    config = mem_get_current_bank_config();
    if (mapping != config) return -3;
    if (!vice_web_monitor_ram_visible(address, length)) return -4;
    for (i = 0; i < length; i++) {
        if (mem_peek_with_config(config, (uint16_t)(address + i), NULL) != expected[i]) return -5;
    }
    for (i = 0; i < length; i++) mem_inject((uint32_t)(address + i), next[i]);
    vice_web_monitor_mutation_serial++;
    for (i = 0; i < length; i++) {
        if (mem_peek_with_config(config, (uint16_t)(address + i), NULL) != next[i]) return -6;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_mutate_register(int reg_id, int expected, int next,
    int mapping, int generation)
{
    int width, actual;
    if (!ui_pause_trap_active() || (unsigned int)generation != vice_web_monitor_mutation_serial) return -2;
    if (mapping != mem_get_current_bank_config()) return -3;
    if (reg_id < 0 || reg_id > 5) return -1;
    width = reg_id == 0 ? 0xffff : 0xff;
    if (next < 0 || next > width || expected < 0 || expected > width) return -1;
    actual = vice_web_monitor_register_get(reg_id);
    if (actual != expected) return -5;
    if (reg_id == 5 && ((next ^ actual) & 0x30)) return -1;
    if (vice_web_monitor_register_set(reg_id, next) != 0) return -1;
    if (reg_id == 0) {
        reg_pc = (unsigned int)next;
        maincpu_resync_limits();
    }
    vice_web_monitor_mutation_serial++;
    if (vice_web_monitor_register_get(reg_id) != next) return -6;
    return 0;
}

static int vice_web_monitor_checkpoint_reconcile(int address)
{
    const int needed = vice_web_monitor_raw_address[address] || vice_web_monitor_guarded_address[address]
        || vice_web_monitor_owner_address[address];
    int id = vice_web_monitor_checkpoint_id[address];
    if (needed && !id) {
        MON_ADDR location = new_addr(e_comp_space, (unsigned int)address);
        id = mon_breakpoint_add_checkpoint(location, location, TRUE, e_exec, FALSE, FALSE);
        if (id < 1) return -1;
        vice_web_monitor_checkpoint_id[address] = id;
    } else if (!needed && id) {
        mon_breakpoint_delete_checkpoint(id);
        vice_web_monitor_checkpoint_id[address] = 0;
        if (vice_web_monitor_step_past_address == address) vice_web_monitor_step_past_address = -1;
        if (vice_web_monitor_rearm_address == address) vice_web_monitor_rearm_address = -1;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_set(int address, int enabled)
{
    if (address < 0 || address > 0xffff) return -1;
    vice_web_monitor_raw_address[address] = enabled ? 1 : 0;
    if (vice_web_monitor_checkpoint_reconcile(address) < 0) return -1;
    return enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_clear(int address)
{
    if (address < 0 || address > 0xffff) {
        return -1;
    }
    vice_web_monitor_raw_address[address] = 0;
    if (vice_web_monitor_checkpoint_reconcile(address) < 0) return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_guard_set(int address, int memspace, int mapping,
    const unsigned char *bytes, int length)
{
    int i;
    if (address < 0 || address > 0xffff || memspace != e_comp_space ||
        length < 1 || length > VICE_WEB_MONITOR_GUARD_BYTES || bytes == NULL) {
        return -1;
    }
    for (i = 0; i < VICE_WEB_MONITOR_GUARDS; i++) {
        if (!vice_web_monitor_guards[i].active) {
            vice_web_monitor_guard_t *guard = &vice_web_monitor_guards[i];
            guard->active = 1;
            guard->address = address;
            guard->memspace = memspace;
            guard->mapping = mapping;
            guard->length = length;
            memcpy(guard->bytes, bytes, (size_t)length);
            vice_web_monitor_guarded_address[address] = 1;
            if (vice_web_monitor_checkpoint_reconcile(address) < 0) {
                guard->active = 0;
                vice_web_monitor_guarded_address[address] = 0;
                return -2;
            }
            return i + 1;
        }
    }
    return -2;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_guard_clear(int guard_id)
{
    int i, address;
    if (guard_id < 1 || guard_id > VICE_WEB_MONITOR_GUARDS || !vice_web_monitor_guards[guard_id - 1].active) {
        return -1;
    }
    address = vice_web_monitor_guards[guard_id - 1].address;
    vice_web_monitor_guards[guard_id - 1].active = 0;
    vice_web_monitor_guarded_address[address] = 0;
    for (i = 0; i < VICE_WEB_MONITOR_GUARDS; i++) {
        if (vice_web_monitor_guards[i].active && vice_web_monitor_guards[i].address == address) {
            vice_web_monitor_guarded_address[address] = 1;
            break;
        }
    }
    if (vice_web_monitor_checkpoint_reconcile(address) < 0) return -1;
    return 0;
}

static int vice_web_monitor_program_valid(const int *program, int length)
{
    int depth[VICE_WEB_MONITOR_PROGRAM_LIMIT + 1];
    int pc, reads = 0, bytes = 0;
    if (length == 0) return 1;
    if (program == NULL || length < 0 || length > VICE_WEB_MONITOR_PROGRAM_LIMIT) return 0;
    for (pc = 0; pc <= length; pc++) depth[pc] = -1;
    depth[0] = 0;
    for (pc = 0; pc < length; pc++) {
        int op, arg, current, following, target = -1, target_depth = -1;
        if (depth[pc] < 0) continue;
        op = program[pc * 2];
        arg = program[pc * 2 + 1];
        current = depth[pc];
        following = current;
        switch (op) {
            case VICE_WEB_PUSH: following++; break;
            case VICE_WEB_REG:
                if (arg < 0 || arg > 11) return 0;
                following++;
                break;
            case VICE_WEB_READ8: case VICE_WEB_READ16: case VICE_WEB_READ32:
                if (current < 1 || arg != 0) return 0;
                reads++;
                bytes += op == VICE_WEB_READ8 ? 1 : op == VICE_WEB_READ16 ? 2 : 4;
                break;
            case VICE_WEB_NOT: case VICE_WEB_BITNOT: case VICE_WEB_BOOL:
                if (current < 1 || arg != 0) return 0;
                break;
            case VICE_WEB_JZ: case VICE_WEB_JNZ:
                if (current < 1 || arg <= pc || arg > length) return 0;
                target = arg;
                target_depth = current;
                following--;
                break;
            case VICE_WEB_EQ: case VICE_WEB_NE: case VICE_WEB_LT:
            case VICE_WEB_LE: case VICE_WEB_GT: case VICE_WEB_GE:
            case VICE_WEB_AND: case VICE_WEB_OR: case VICE_WEB_XOR:
                if (current < 2 || arg != 0) return 0;
                following--;
                break;
            default: return 0;
        }
        if (reads > 8 || bytes > 16 || following > VICE_WEB_MONITOR_STACK_LIMIT) return 0;
        if (depth[pc + 1] >= 0 && depth[pc + 1] != following) return 0;
        depth[pc + 1] = following;
        if (target >= 0) {
            if (depth[target] >= 0 && depth[target] != target_depth) return 0;
            depth[target] = target_depth;
        }
    }
    return depth[length] == 1;
}

static int vice_web_monitor_filter(const vice_web_monitor_owner_t *owner)
{
    uint32_t stack[VICE_WEB_MONITOR_STACK_LIMIT];
    int pc = 0, depth = 0, reads = 0, bytes = 0;
    if (!owner->program_length) return 1;
    while (pc < owner->program_length) {
        const int op = owner->program[pc * 2];
        const int arg = owner->program[pc * 2 + 1];
        uint32_t value, rhs, address;
        pc++;
        switch (op) {
            case VICE_WEB_PUSH: stack[depth++] = (uint32_t)arg; break;
            case VICE_WEB_REG:
                if (arg <= 5) value = (uint32_t)vice_web_monitor_register_get(arg);
                else {
                    static const int flag_bits[] = { 0, 1, 2, 3, 6, 7 };
                    value = ((uint32_t)vice_web_monitor_register_get(5) >> flag_bits[arg - 6]) & 1u;
                }
                stack[depth++] = value;
                break;
            case VICE_WEB_READ8: case VICE_WEB_READ16: case VICE_WEB_READ32: {
                const int width = op == VICE_WEB_READ8 ? 1 : op == VICE_WEB_READ16 ? 2 : 4;
                int i;
                address = stack[depth - 1];
                reads++;
                bytes += width;
                if (reads > 8 || bytes > 16 || address > (uint32_t)(65536 - width)) return -1;
                value = 0;
                for (i = 0; i < width; i++) {
                    value |= (uint32_t)mem_peek_with_config(mem_get_current_bank_config(),
                        (uint16_t)(address + (uint32_t)i), NULL) << (i * 8);
                }
                stack[depth - 1] = value;
                break;
            }
            case VICE_WEB_NOT: stack[depth - 1] = !stack[depth - 1]; break;
            case VICE_WEB_BITNOT: stack[depth - 1] = ~stack[depth - 1]; break;
            case VICE_WEB_BOOL: stack[depth - 1] = !!stack[depth - 1]; break;
            case VICE_WEB_JZ:
                value = !!stack[depth - 1];
                if (!value) { stack[depth - 1] = 0; pc = arg; }
                else depth--;
                break;
            case VICE_WEB_JNZ:
                value = !!stack[depth - 1];
                if (value) { stack[depth - 1] = 1; pc = arg; }
                else depth--;
                break;
            default:
                rhs = stack[--depth];
                value = stack[depth - 1];
                switch (op) {
                    case VICE_WEB_EQ: value = value == rhs; break;
                    case VICE_WEB_NE: value = value != rhs; break;
                    case VICE_WEB_LT: value = value < rhs; break;
                    case VICE_WEB_LE: value = value <= rhs; break;
                    case VICE_WEB_GT: value = value > rhs; break;
                    case VICE_WEB_GE: value = value >= rhs; break;
                    case VICE_WEB_AND: value &= rhs; break;
                    case VICE_WEB_OR: value |= rhs; break;
                    case VICE_WEB_XOR: value ^= rhs; break;
                    default: return -1; /* Validated installation should make this unreachable. */
                }
                stack[depth - 1] = value;
                break;
        }
    }
    return stack[0] != 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_set(int address, int memspace, int mapping,
    const unsigned char *bytes, int byte_length, const int *program, int program_length)
{
    int slot;
    vice_web_monitor_owner_t *owner;
    if (address < 0 || address > 0xffff || memspace != e_comp_space ||
        mapping < -1 || mapping > 31 || byte_length < 0 || byte_length > VICE_WEB_MONITOR_GUARD_BYTES ||
        address + byte_length > 65536 || (byte_length && bytes == NULL) ||
        !vice_web_monitor_program_valid(program, program_length)) return -1;
    for (slot = 0; slot < VICE_WEB_MONITOR_OWNERS; slot++) {
        if (!vice_web_monitor_owners[slot].active) break;
    }
    if (slot == VICE_WEB_MONITOR_OWNERS || vice_web_monitor_owner_address[address] == 255) return -2;
    owner = &vice_web_monitor_owners[slot];
    owner->active = 1;
    owner->address = address;
    owner->mapping = mapping;
    owner->byte_length = byte_length;
    owner->program_length = program_length;
    owner->unavailable_count = 0;
    if (byte_length) memcpy(owner->bytes, bytes, (size_t)byte_length);
    if (program_length) memcpy(owner->program, program, (size_t)program_length * 2 * sizeof(int));
    owner->next = vice_web_monitor_owner_head[address];
    vice_web_monitor_owner_head[address] = slot + 1;
    vice_web_monitor_owner_address[address]++;
    if (vice_web_monitor_checkpoint_reconcile(address) < 0) {
        vice_web_monitor_owner_head[address] = owner->next;
        vice_web_monitor_owner_address[address]--;
        owner->active = 0;
        return -2;
    }
    return slot + 1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_clear(int owner_id)
{
    vice_web_monitor_owner_t *owner;
    int *link;
    if (owner_id < 1 || owner_id > VICE_WEB_MONITOR_OWNERS ||
        !vice_web_monitor_owners[owner_id - 1].active) return -1;
    owner = &vice_web_monitor_owners[owner_id - 1];
    link = &vice_web_monitor_owner_head[owner->address];
    while (*link && *link != owner_id) link = &vice_web_monitor_owners[*link - 1].next;
    if (!*link) return -1;
    *link = owner->next;
    vice_web_monitor_owner_address[owner->address]--;
    owner->active = 0;
    if (vice_web_monitor_checkpoint_reconcile(owner->address) < 0) return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_match_count(void)
{
    return vice_web_monitor_matched_owner_count;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_match_id(int index)
{
    if (index < 0 || index >= vice_web_monitor_matched_owner_count) return -1;
    return vice_web_monitor_matched_owner_ids[index];
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_owner_unavailable_count(int owner_id)
{
    if (owner_id < 1 || owner_id > VICE_WEB_MONITOR_OWNERS ||
        !vice_web_monitor_owners[owner_id - 1].active) return -1;
    return vice_web_monitor_owners[owner_id - 1].unavailable_count;
}

int vice_web_monitor_breakpoint_guard_match(int memspace, int address)
{
    int i, j, matched = 0, native_match = 0, owner_id;
    address &= 0xffff;
    vice_web_monitor_matched_guard_id = 0;
    vice_web_monitor_matched_owner_count = 0;
    if (memspace != e_comp_space) return 1;
    if (!vice_web_monitor_raw_address[address] && !vice_web_monitor_guarded_address[address]
        && !vice_web_monitor_owner_address[address]) return 1;
    if (vice_web_monitor_raw_address[address]) matched = 1;
    else for (i = 0; i < VICE_WEB_MONITOR_GUARDS; i++) {
        const vice_web_monitor_guard_t *guard = &vice_web_monitor_guards[i];
        if (!guard->active || guard->address != address || guard->memspace != memspace ||
            (guard->mapping >= 0 && guard->mapping != mem_get_current_bank_config())) {
            continue;
        }
        for (j = 0; j < guard->length; j++) {
            if (mem_peek_with_config(mem_get_current_bank_config(),
                    (uint16_t)((address + j) & 0xffff), NULL) != guard->bytes[j]) {
                break;
            }
        }
        if (j == guard->length) {
            matched = matched ? -2 : i + 1;
        }
    }
    if (vice_web_monitor_raw_address[address]) vice_web_monitor_matched_guard_id = 0;
    else vice_web_monitor_matched_guard_id = matched;
    owner_id = vice_web_monitor_owner_head[address];
    while (owner_id) {
        vice_web_monitor_owner_t *owner = &vice_web_monitor_owners[owner_id - 1];
        const int next = owner->next;
        int result = 0;
        if (owner->mapping < 0 || owner->mapping == mem_get_current_bank_config()) {
            for (j = 0; j < owner->byte_length; j++) {
                if (mem_peek_with_config(mem_get_current_bank_config(),
                    (uint16_t)(address + j), NULL) != owner->bytes[j]) break;
            }
            if (j == owner->byte_length) result = vice_web_monitor_filter(owner);
        }
        if (result > 0) {
            vice_web_monitor_matched_owner_ids[vice_web_monitor_matched_owner_count++] = owner_id;
            native_match = 1;
        } else if (result < 0 && owner->unavailable_count < 0x7fffffff) owner->unavailable_count++;
        owner_id = next;
    }
    return matched != 0 || native_match;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_step_past(int address)
{
    if (address < 0 || address > 0xffff || !vice_web_monitor_breakpoint_hit_pending_flag ||
        vice_web_monitor_breakpoint_hit_address_value != address ||
        vice_web_monitor_breakpoint_hit_memspace_value != e_comp_space ||
        vice_web_monitor_step_past_address >= 0 || !vice_web_monitor_checkpoint_id[address] ||
        vice_web_monitor_breakpoint_hit_checkpoint_id_value != vice_web_monitor_checkpoint_id[address]) {
        return -1;
    }
    mon_breakpoint_switch_checkpoint(e_OFF, vice_web_monitor_checkpoint_id[address]);
    vice_web_monitor_rearm_address = -1;
    vice_web_monitor_step_past_address = address;
    vice_web_monitor_breakpoint_hit_pending_flag = 0;
    vice_web_monitor_matched_owner_count = 0;
    /* The paused trap resumes directly into the current instruction. The
     * monitor's next boundary follows exactly one completed instruction. */
    mon_instructions_step(1);
    ui_pause_disable();
    return 1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_step_instruction(void)
{
    if (!ui_pause_trap_active() || vice_web_monitor_step_past_address >= 0 ||
        vice_web_monitor_instruction_step_pending ||
        (vice_web_monitor_breakpoint_hit_pending_flag && vice_web_monitor_breakpoint_hit_kind_value == 1)) {
        return -1;
    }
    vice_web_monitor_breakpoint_hit_pending_flag = 0;
    vice_web_monitor_matched_owner_count = 0;
    vice_web_monitor_instruction_step_pending = 1;
    /* The monitor stop now waits at its actual instruction boundary. One
     * subsequent boundary proves exactly one instruction completed. */
    mon_instructions_step(1);
    ui_pause_disable();
    return 1;
}

void vice_web_frame_step_vic_start(void)
{
    ++vice_web_frame_epoch;
    if (vice_web_frame_step_state == 1 && vice_web_frame_epoch != vice_web_frame_armed_epoch) {
        vice_web_frame_step_state = 2;
    }
}

void vice_web_frame_step_cancel(void)
{
    if (vice_web_frame_step_state == 1 || vice_web_frame_step_state == 2) {
        vice_web_frame_step_state = 4;
    }
}

void vice_web_frame_step_cpu_boundary(int pc)
{
    if (vice_web_frame_step_state != 2) return;
    vice_web_frame_stop_epoch = vice_web_frame_epoch;
    vice_web_frame_stop_pc = pc & 0xffff;
    vice_web_frame_stop_mapping = mem_get_current_bank_config();
    vice_web_c64_vicii_position(&vice_web_frame_stop_line, &vice_web_frame_stop_cycle);
    vice_web_frame_stop_clock = maincpu_clk;
    vice_web_frame_step_state = 3;
    ui_pause_at_monitor_boundary();
}

EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_version(void)
{
    return machine_class == VICE_MACHINE_C64SC ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_epoch(void) { return (int)vice_web_frame_epoch; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_status(void) { return vice_web_frame_step_state; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_epoch(void) { return (int)vice_web_frame_stop_epoch; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_pc(void) { return vice_web_frame_stop_pc; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_mapping(void) { return vice_web_frame_stop_mapping; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_line(void) { return vice_web_frame_stop_line; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_cycle(void) { return vice_web_frame_stop_cycle; }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_clock_lo(void) { return (int)((uint64_t)vice_web_frame_stop_clock & 0xffffffffu); }
EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_stop_clock_hi(void) { return (int)((uint64_t)vice_web_frame_stop_clock >> 32); }

EMSCRIPTEN_KEEPALIVE int vice_web_frame_step_arm(int pc, int mapping, int epoch)
{
    if (machine_class != VICE_MACHINE_C64SC || !ui_pause_trap_active() ||
        (vice_web_frame_step_state == 1 || vice_web_frame_step_state == 2) ||
        vice_web_monitor_instruction_step_pending || vice_web_monitor_step_past_address >= 0 ||
        pc < 0 || pc > 0xffff || mapping != mem_get_current_bank_config() ||
        pc != (vice_web_monitor_register_get(0) & 0xffff) ||
        (uint32_t)epoch != vice_web_frame_epoch) return -1;
    vice_web_frame_armed_epoch = vice_web_frame_epoch;
    vice_web_frame_step_state = 1;
    vice_web_monitor_breakpoint_hit_pending_flag = 0;
    vice_web_monitor_matched_owner_count = 0;
    ui_pause_disable();
    return 1;
}

void vice_web_monitor_breakpoint_hit(int memspace, int address)
{
    vice_web_frame_step_cancel();
    if (vice_web_monitor_step_past_address >= 0) {
        /* Keep this checkpoint disabled until the completed-step stop has
         * entered the pause trap. Rearming here can immediately overwrite the
         * stop with another hit in a tight same-PC loop. */
        vice_web_monitor_rearm_address = vice_web_monitor_step_past_address;
        vice_web_monitor_step_past_address = -1;
        vice_web_monitor_breakpoint_hit_kind_value = 2;
        vice_web_monitor_breakpoint_hit_checkpoint_id_value = vice_web_monitor_checkpoint_id[vice_web_monitor_rearm_address];
        vice_web_monitor_breakpoint_hit_guard_id_value = 0;
        vice_web_monitor_matched_owner_count = 0;
    } else if (vice_web_monitor_instruction_step_pending) {
        vice_web_monitor_instruction_step_pending = 0;
        vice_web_monitor_breakpoint_hit_kind_value = 2;
        vice_web_monitor_breakpoint_hit_checkpoint_id_value = 0;
        vice_web_monitor_breakpoint_hit_guard_id_value = 0;
        vice_web_monitor_matched_owner_count = 0;
    } else {
        vice_web_monitor_breakpoint_hit_kind_value = 1;
        vice_web_monitor_breakpoint_hit_checkpoint_id_value = memspace == e_comp_space
            ? vice_web_monitor_checkpoint_id[address & 0xffff] : 0;
        vice_web_monitor_breakpoint_hit_guard_id_value = vice_web_monitor_matched_guard_id;
    }
    vice_web_monitor_breakpoint_hit_address_value = address & 0xffff;
    vice_web_monitor_breakpoint_hit_memspace_value = memspace;
    vice_web_monitor_breakpoint_hit_mapping_value = memspace == e_comp_space ? mem_get_current_bank_config() : -1;
    vice_web_monitor_breakpoint_hit_pending_flag = 1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_pending(void)
{
    return vice_web_monitor_breakpoint_hit_pending_flag;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_address(void)
{
    return vice_web_monitor_breakpoint_hit_address_value & 0xffff;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_memspace(void)
{
    return vice_web_monitor_breakpoint_hit_memspace_value;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_mapping(void)
{
    return vice_web_monitor_breakpoint_hit_mapping_value;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_kind(void)
{
    return vice_web_monitor_breakpoint_hit_kind_value;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_checkpoint_id(void)
{
    return vice_web_monitor_breakpoint_hit_checkpoint_id_value;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_guard_id(void)
{
    return vice_web_monitor_breakpoint_hit_guard_id_value;
}

EMSCRIPTEN_KEEPALIVE int vice_web_monitor_breakpoint_hit_clear(void)
{
    vice_web_monitor_breakpoint_hit_pending_flag = 0;
    vice_web_monitor_matched_owner_count = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE const char *vice_web_monitor_disassemble(int address, int count)
{
    snprintf(vice_web_monitor_disassembly_scratch, sizeof(vice_web_monitor_disassembly_scratch), "$%04x  ; %d instruction(s)", address & 0xffff, count < 0 ? 0 : count);
    return vice_web_monitor_disassembly_scratch;
}

static int vice_web_key_name_is(const char *code, const char *value, const char *name)
{
    return (code != NULL && !strcmp(code, name)) || (value != NULL && !strcmp(value, name));
}

typedef struct vice_web_key_entry_s {
    const char *name;
    int sym;
    int row;
    int column;
    int shift;
} vice_web_key_entry_t;

#define VICE_WEB_KEY_BASE 0x770000
#define VICE_WEB_KEY_RETURN (VICE_WEB_KEY_BASE + 1)
#define VICE_WEB_KEY_RUNSTOP (VICE_WEB_KEY_BASE + 2)
#define VICE_WEB_KEY_CURSOR_RIGHT (VICE_WEB_KEY_BASE + 3)
#define VICE_WEB_KEY_CURSOR_LEFT (VICE_WEB_KEY_BASE + 4)
#define VICE_WEB_KEY_CURSOR_DOWN (VICE_WEB_KEY_BASE + 5)
#define VICE_WEB_KEY_CURSOR_UP (VICE_WEB_KEY_BASE + 6)
#define VICE_WEB_KEY_DEL (VICE_WEB_KEY_BASE + 7)
#define VICE_WEB_KEY_HOME (VICE_WEB_KEY_BASE + 8)
#define VICE_WEB_KEY_SHIFT_LEFT (VICE_WEB_KEY_BASE + 9)
#define VICE_WEB_KEY_SHIFT_RIGHT (VICE_WEB_KEY_BASE + 10)
#define VICE_WEB_KEY_CTRL (VICE_WEB_KEY_BASE + 11)
#define VICE_WEB_KEY_CBM (VICE_WEB_KEY_BASE + 12)
#define VICE_WEB_KEY_F1 (VICE_WEB_KEY_BASE + 13)
#define VICE_WEB_KEY_F2 (VICE_WEB_KEY_BASE + 14)
#define VICE_WEB_KEY_F3 (VICE_WEB_KEY_BASE + 15)
#define VICE_WEB_KEY_F4 (VICE_WEB_KEY_BASE + 16)
#define VICE_WEB_KEY_F5 (VICE_WEB_KEY_BASE + 17)
#define VICE_WEB_KEY_F6 (VICE_WEB_KEY_BASE + 18)
#define VICE_WEB_KEY_F7 (VICE_WEB_KEY_BASE + 19)
#define VICE_WEB_KEY_F8 (VICE_WEB_KEY_BASE + 20)

static const vice_web_key_entry_t vice_web_key_entries[] = {
    { "SPACE", ' ', 7, 4, ALLOW_SHIFT },
    { "Space", ' ', 7, 4, ALLOW_SHIFT },
    { "RETURN", VICE_WEB_KEY_RETURN, 0, 1, ALLOW_SHIFT },
    { "Enter", VICE_WEB_KEY_RETURN, 0, 1, ALLOW_SHIFT },
    { "RUNSTOP", VICE_WEB_KEY_RUNSTOP, 7, 7, ALLOW_SHIFT },
    { "RunStop", VICE_WEB_KEY_RUNSTOP, 7, 7, ALLOW_SHIFT },
    { "Escape", VICE_WEB_KEY_RUNSTOP, 7, 7, ALLOW_SHIFT },
    { "Esc", VICE_WEB_KEY_RUNSTOP, 7, 7, ALLOW_SHIFT },
    { "DEL", VICE_WEB_KEY_DEL, 0, 0, ALLOW_SHIFT },
    { "Backspace", VICE_WEB_KEY_DEL, 0, 0, ALLOW_SHIFT },
    { "Delete", VICE_WEB_KEY_DEL, 0, 0, ALLOW_SHIFT },
    { "CURSOR_RIGHT", VICE_WEB_KEY_CURSOR_RIGHT, 0, 2, ALLOW_SHIFT },
    { "CRSR_RIGHT", VICE_WEB_KEY_CURSOR_RIGHT, 0, 2, ALLOW_SHIFT },
    { "ArrowRight", VICE_WEB_KEY_CURSOR_RIGHT, 0, 2, ALLOW_SHIFT },
    { "CURSOR_LEFT", VICE_WEB_KEY_CURSOR_LEFT, 0, 2, VIRTUAL_SHIFT },
    { "CRSR_LEFT", VICE_WEB_KEY_CURSOR_LEFT, 0, 2, VIRTUAL_SHIFT },
    { "ArrowLeft", VICE_WEB_KEY_CURSOR_LEFT, 0, 2, VIRTUAL_SHIFT },
    { "CURSOR_DOWN", VICE_WEB_KEY_CURSOR_DOWN, 0, 7, ALLOW_SHIFT },
    { "CRSR_DOWN", VICE_WEB_KEY_CURSOR_DOWN, 0, 7, ALLOW_SHIFT },
    { "ArrowDown", VICE_WEB_KEY_CURSOR_DOWN, 0, 7, ALLOW_SHIFT },
    { "CURSOR_UP", VICE_WEB_KEY_CURSOR_UP, 0, 7, VIRTUAL_SHIFT },
    { "CRSR_UP", VICE_WEB_KEY_CURSOR_UP, 0, 7, VIRTUAL_SHIFT },
    { "ArrowUp", VICE_WEB_KEY_CURSOR_UP, 0, 7, VIRTUAL_SHIFT },
    { "HOME", VICE_WEB_KEY_HOME, 6, 3, ALLOW_SHIFT },
    { "Home", VICE_WEB_KEY_HOME, 6, 3, ALLOW_SHIFT },
    { "SHIFT_LEFT", VICE_WEB_KEY_SHIFT_LEFT, 1, 7, LEFT_SHIFT },
    { "ShiftLeft", VICE_WEB_KEY_SHIFT_LEFT, 1, 7, LEFT_SHIFT },
    { "SHIFT_RIGHT", VICE_WEB_KEY_SHIFT_RIGHT, 6, 4, RIGHT_SHIFT },
    { "ShiftRight", VICE_WEB_KEY_SHIFT_RIGHT, 6, 4, RIGHT_SHIFT },
    { "CTRL", VICE_WEB_KEY_CTRL, 7, 2, LEFT_CTRL },
    { "ControlLeft", VICE_WEB_KEY_CTRL, 7, 2, LEFT_CTRL },
    { "ControlRight", VICE_WEB_KEY_CTRL, 7, 2, LEFT_CTRL },
    { "CBM", VICE_WEB_KEY_CBM, 7, 5, LEFT_CBM },
    { "Commodore", VICE_WEB_KEY_CBM, 7, 5, LEFT_CBM },
    { "F1", VICE_WEB_KEY_F1, 0, 4, ALLOW_SHIFT },
    { "F2", VICE_WEB_KEY_F2, 0, 4, VIRTUAL_SHIFT },
    { "F3", VICE_WEB_KEY_F3, 0, 5, ALLOW_SHIFT },
    { "F4", VICE_WEB_KEY_F4, 0, 5, VIRTUAL_SHIFT },
    { "F5", VICE_WEB_KEY_F5, 0, 6, ALLOW_SHIFT },
    { "F6", VICE_WEB_KEY_F6, 0, 6, VIRTUAL_SHIFT },
    { "F7", VICE_WEB_KEY_F7, 0, 3, ALLOW_SHIFT },
    { "F8", VICE_WEB_KEY_F8, 0, 3, VIRTUAL_SHIFT },
    { "0", '0', 4, 3, NO_SHIFT },
    { "1", '1', 7, 0, NO_SHIFT },
    { "2", '2', 7, 3, NO_SHIFT },
    { "3", '3', 1, 0, NO_SHIFT },
    { "4", '4', 1, 3, NO_SHIFT },
    { "5", '5', 2, 0, NO_SHIFT },
    { "6", '6', 2, 3, NO_SHIFT },
    { "7", '7', 3, 0, NO_SHIFT },
    { "8", '8', 3, 3, NO_SHIFT },
    { "9", '9', 4, 0, NO_SHIFT },
    { "A", 'A', 1, 2, ALLOW_SHIFT }, { "a", 'a', 1, 2, ALLOW_SHIFT },
    { "B", 'B', 3, 4, ALLOW_SHIFT }, { "b", 'b', 3, 4, ALLOW_SHIFT },
    { "C", 'C', 2, 4, ALLOW_SHIFT }, { "c", 'c', 2, 4, ALLOW_SHIFT },
    { "D", 'D', 2, 2, ALLOW_SHIFT }, { "d", 'd', 2, 2, ALLOW_SHIFT },
    { "E", 'E', 1, 6, ALLOW_SHIFT }, { "e", 'e', 1, 6, ALLOW_SHIFT },
    { "F", 'F', 2, 5, ALLOW_SHIFT }, { "f", 'f', 2, 5, ALLOW_SHIFT },
    { "G", 'G', 3, 2, ALLOW_SHIFT }, { "g", 'g', 3, 2, ALLOW_SHIFT },
    { "H", 'H', 3, 5, ALLOW_SHIFT }, { "h", 'h', 3, 5, ALLOW_SHIFT },
    { "I", 'I', 4, 1, ALLOW_SHIFT }, { "i", 'i', 4, 1, ALLOW_SHIFT },
    { "J", 'J', 4, 2, ALLOW_SHIFT }, { "j", 'j', 4, 2, ALLOW_SHIFT },
    { "K", 'K', 4, 5, ALLOW_SHIFT }, { "k", 'k', 4, 5, ALLOW_SHIFT },
    { "L", 'L', 5, 2, ALLOW_SHIFT }, { "l", 'l', 5, 2, ALLOW_SHIFT },
    { "M", 'M', 4, 4, ALLOW_SHIFT }, { "m", 'm', 4, 4, ALLOW_SHIFT },
    { "N", 'N', 4, 7, ALLOW_SHIFT }, { "n", 'n', 4, 7, ALLOW_SHIFT },
    { "O", 'O', 4, 6, ALLOW_SHIFT }, { "o", 'o', 4, 6, ALLOW_SHIFT },
    { "P", 'P', 5, 1, ALLOW_SHIFT }, { "p", 'p', 5, 1, ALLOW_SHIFT },
    { "Q", 'Q', 7, 6, ALLOW_SHIFT }, { "q", 'q', 7, 6, ALLOW_SHIFT },
    { "R", 'R', 2, 1, ALLOW_SHIFT }, { "r", 'r', 2, 1, ALLOW_SHIFT },
    { "S", 'S', 1, 5, ALLOW_SHIFT }, { "s", 's', 1, 5, ALLOW_SHIFT },
    { "T", 'T', 2, 6, ALLOW_SHIFT }, { "t", 't', 2, 6, ALLOW_SHIFT },
    { "U", 'U', 3, 6, ALLOW_SHIFT }, { "u", 'u', 3, 6, ALLOW_SHIFT },
    { "V", 'V', 3, 7, ALLOW_SHIFT }, { "v", 'v', 3, 7, ALLOW_SHIFT },
    { "W", 'W', 1, 1, ALLOW_SHIFT }, { "w", 'w', 1, 1, ALLOW_SHIFT },
    { "X", 'X', 2, 7, ALLOW_SHIFT }, { "x", 'x', 2, 7, ALLOW_SHIFT },
    { "Y", 'Y', 3, 1, ALLOW_SHIFT }, { "y", 'y', 3, 1, ALLOW_SHIFT },
    { "Z", 'Z', 1, 4, ALLOW_SHIFT }, { "z", 'z', 1, 4, ALLOW_SHIFT },
    { "!", '!', 7, 0, VIRTUAL_SHIFT },
    { "\"", '"', 7, 3, VIRTUAL_SHIFT },
    { "#", '#', 1, 0, VIRTUAL_SHIFT },
    { "$", '$', 1, 3, VIRTUAL_SHIFT },
    { "%", '%', 2, 0, VIRTUAL_SHIFT },
    { "&", '&', 2, 3, VIRTUAL_SHIFT },
    { "'", '\'', 3, 0, VIRTUAL_SHIFT },
    { "(", '(', 3, 3, VIRTUAL_SHIFT },
    { ")", ')', 4, 0, VIRTUAL_SHIFT },
    { "+", '+', 5, 0, DESHIFT_SHIFT },
    { "-", '-', 5, 3, NO_SHIFT },
    { "_", '_', 5, 6, DESHIFT_SHIFT | VIRTUAL_CBM },
    { "=", '=', 6, 5, NO_SHIFT },
    { "@", '@', 5, 6, DESHIFT_SHIFT },
    { "[", '[', 5, 5, VIRTUAL_SHIFT },
    { "]", ']', 6, 2, VIRTUAL_SHIFT },
    { ":", ':', 5, 5, DESHIFT_SHIFT },
    { ";", ';', 6, 2, NO_SHIFT },
    { "*", '*', 6, 1, DESHIFT_SHIFT },
    { ",", ',', 5, 7, NO_SHIFT },
    { "<", '<', 5, 7, VIRTUAL_SHIFT },
    { ".", '.', 5, 4, NO_SHIFT },
    { ">", '>', 5, 4, VIRTUAL_SHIFT },
    { "/", '/', 6, 7, NO_SHIFT },
    { "?", '?', 6, 7, VIRTUAL_SHIFT },
    { "\\", '\\', 6, 0, NO_SHIFT },
    { "|", '|', 6, 0, ALLOW_SHIFT },
    { "^", '^', 6, 6, DESHIFT_SHIFT },
    { "`", '`', 3, 0, VIRTUAL_SHIFT },
    { "~", '~', 7, 1, DESHIFT_SHIFT }
};

static int vice_web_printable_symbol(const char *code, const char *value)
{
    if (value != NULL && value[0] != '\0' && value[1] == '\0') {
        return (unsigned char)value[0];
    }
    if (code != NULL) {
        if (code[0] != '\0' && code[1] == '\0') {
            return (unsigned char)code[0];
        }
        if (!strncmp(code, "Key", 3) && code[3] != '\0' && code[4] == '\0') {
            return code[3];
        }
        if (!strncmp(code, "Digit", 5) && code[5] != '\0' && code[6] == '\0') {
            return code[5];
        }
        if (!strncmp(code, "Numpad", 6) && code[6] >= '0' && code[6] <= '9' && code[7] == '\0') {
            return code[6];
        }
    }
    return 0;
}

static const vice_web_key_entry_t *vice_web_lookup_key_entry(const char *code, const char *value)
{
    size_t i;
    int printable;

    for (i = 0; i < sizeof(vice_web_key_entries) / sizeof(vice_web_key_entries[0]); i++) {
        if (vice_web_key_entries[i].name != NULL && vice_web_key_name_is(code, value, vice_web_key_entries[i].name)) {
            return &vice_web_key_entries[i];
        }
    }

    printable = vice_web_printable_symbol(code, value);
    if (printable != 0) {
        for (i = 0; i < sizeof(vice_web_key_entries) / sizeof(vice_web_key_entries[0]); i++) {
            if (vice_web_key_entries[i].sym == printable) {
                return &vice_web_key_entries[i];
            }
        }
    }
    return NULL;
}

static int vice_web_valid_disk_unit(int unit)
{
    return unit >= 8 && unit <= 11;
}

static int vice_web_feed_key_value(const char *code, const char *value)
{
    (void)value;

    if (code == NULL || code[0] == '\0') {
        return 0;
    }
    if (!strcmp(code, "RETURN") || !strcmp(code, "Enter")) {
        return kbdbuf_feed_string("\n");
    }
    if (!strcmp(code, "DEL") || !strcmp(code, "Backspace")) {
        return kbdbuf_feed("\x14");
    }
    if (code[1] == '\0') {
        return kbdbuf_feed_string(code);
    }
    return 0;
}

static void vice_web_apply_keyboard_modifiers(void)
{
    int shifted = vice_web_deshift_count <= 0 &&
        (vice_web_left_shift_pressed || vice_web_right_shift_pressed || vice_web_virtual_shift_count > 0);

    keyboard_set_keyarr_any(1, 7, shifted && vice_web_left_shift_pressed ? 1 : 0);
    keyboard_set_keyarr_any(6, 4, shifted && (vice_web_right_shift_pressed || vice_web_virtual_shift_count > 0) ? 1 : 0);
    keyboard_set_keyarr_any(7, 2, (vice_web_ctrl_pressed || vice_web_virtual_ctrl_count > 0) ? 1 : 0);
    keyboard_set_keyarr_any(7, 5, (vice_web_cbm_pressed || vice_web_virtual_cbm_count > 0) ? 1 : 0);
}

static int vice_web_decrement_count(int count)
{
    return count > 0 ? count - 1 : 0;
}

static int vice_web_write_key_entry(const vice_web_key_entry_t *entry, int pressed)
{
    if (entry == NULL) {
        return 0;
    }

    if (entry->shift & LEFT_SHIFT) {
        vice_web_left_shift_pressed = pressed ? 1 : 0;
        vice_web_apply_keyboard_modifiers();
        return 1;
    }
    if (entry->shift & RIGHT_SHIFT) {
        vice_web_right_shift_pressed = pressed ? 1 : 0;
        vice_web_apply_keyboard_modifiers();
        return 1;
    }
    if (entry->shift & LEFT_CTRL) {
        vice_web_ctrl_pressed = pressed ? 1 : 0;
        vice_web_apply_keyboard_modifiers();
        return 1;
    }
    if (entry->shift & LEFT_CBM) {
        vice_web_cbm_pressed = pressed ? 1 : 0;
        vice_web_apply_keyboard_modifiers();
        return 1;
    }

    if (pressed) {
        if (entry->shift & DESHIFT_SHIFT) {
            vice_web_deshift_count++;
        }
        if (entry->shift & VIRTUAL_SHIFT) {
            vice_web_virtual_shift_count++;
        }
        if (entry->shift & VIRTUAL_CTRL) {
            vice_web_virtual_ctrl_count++;
        }
        if (entry->shift & VIRTUAL_CBM) {
            vice_web_virtual_cbm_count++;
        }
        vice_web_apply_keyboard_modifiers();
        keyboard_set_keyarr_any(entry->row, entry->column, 1);
    } else {
        keyboard_set_keyarr_any(entry->row, entry->column, 0);
        if (entry->shift & DESHIFT_SHIFT) {
            vice_web_deshift_count = vice_web_decrement_count(vice_web_deshift_count);
        }
        if (entry->shift & VIRTUAL_SHIFT) {
            vice_web_virtual_shift_count = vice_web_decrement_count(vice_web_virtual_shift_count);
        }
        if (entry->shift & VIRTUAL_CTRL) {
            vice_web_virtual_ctrl_count = vice_web_decrement_count(vice_web_virtual_ctrl_count);
        }
        if (entry->shift & VIRTUAL_CBM) {
            vice_web_virtual_cbm_count = vice_web_decrement_count(vice_web_virtual_cbm_count);
        }
        vice_web_apply_keyboard_modifiers();
    }
    return 1;
}

static int vice_web_write_key(const char *code, const char *value, int pressed)
{
    return vice_web_write_key_entry(vice_web_lookup_key_entry(code, value), pressed);
}

static uint16_t vice_web_joystick_mask(const char *code, const char *value)
{
    if (code == NULL || code[0] == '\0') {
        return 0;
    }
    if (!strcmp(code, "mask")) {
        return (uint16_t)strtoul(value != NULL ? value : "0", NULL, 0);
    }
    if (!strcmp(code, "up")) {
        return JOYPORT_UP;
    }
    if (!strcmp(code, "down")) {
        return JOYPORT_DOWN;
    }
    if (!strcmp(code, "left")) {
        return JOYPORT_LEFT;
    }
    if (!strcmp(code, "right")) {
        return JOYPORT_RIGHT;
    }
    if (!strcmp(code, "fire") || !strcmp(code, "button0")) {
        return JOYPORT_FIRE;
    }
    return 0;
}

static int vice_web_internal_joystick_port(int port)
{
    if (port == 1) {
        return JOYPORT_1;
    }
    if (port == 2) {
        return JOYPORT_2;
    }
    return -1;
}

static int vice_web_parse_int_value(const char *value, int *int_value)
{
    char *endptr;
    long parsed;

    if (value == NULL || value[0] == '\0' || int_value == NULL) {
        return -1;
    }

    parsed = strtol(value, &endptr, 0);
    if (*endptr != '\0') {
        return -1;
    }

    *int_value = (int)parsed;
    return 0;
}

static int vice_web_parse_sid_engine_value(const char *value, int *engine)
{
    if (value == NULL || engine == NULL) {
        return -1;
    }
    if (!strcmp(value, "fast") || !strcmp(value, "fastsid")) {
        *engine = 0;
        return 0;
    }
    if (!strcmp(value, "resid")) {
        *engine = 1;
        return 0;
    }
    return vice_web_parse_int_value(value, engine);
}

static int vice_web_set_sid_engine_model(int engine)
{
    int model = SID_MODEL_8580;

    switch (engine) {
        case SID_ENGINE_FASTSID:
        case SID_ENGINE_RESID:
            return sid_set_engine_model(engine, model);
        default:
            return -1;
    }
}

static int vice_web_set_boot_sid_engine_model(int engine)
{
    int engine_model;

    switch (engine) {
        case SID_ENGINE_FASTSID:
            engine_model = SID_FASTSID_8580;
            break;
        case SID_ENGINE_RESID:
            engine_model = SID_RESID_8580;
            break;
        default:
            return -1;
    }

    snprintf(vice_web_sid_engine_model_arg, sizeof(vice_web_sid_engine_model_arg), "%d", engine_model);
    return 0;
}

static int vice_web_set_boot_resource_arg(const char *name, const char *value)
{
    int int_value;

    if (name == NULL || value == NULL) {
        return -1;
    }

    if (!strcmp(name, "SidEngine")) {
        if (vice_web_parse_sid_engine_value(value, &int_value) < 0 || int_value < 0 || int_value > 1) {
            return -1;
        }
        return vice_web_set_boot_sid_engine_model(int_value);
    }

    if (!strcmp(name, "SidResidSampling")) {
        if (vice_web_parse_int_value(value, &int_value) < 0 || int_value < 0 || int_value > 3) {
            return -1;
        }
        snprintf(vice_web_resid_sampling_arg, sizeof(vice_web_resid_sampling_arg), "%d", int_value);
        return 0;
    }

    return -1;
}

static void vice_web_configure_joystick_ports(void)
{
    if (vice_web_joystick_configured) {
        return;
    }
    resources_set_int("JoyPort1Device", JOYPORT_ID_JOYSTICK);
    resources_set_int("JoyPort2Device", JOYPORT_ID_JOYSTICK);
    resources_set_int("JoyDevice1", JOYDEV_NONE);
    resources_set_int("JoyDevice2", JOYDEV_NONE);
    vice_web_joystick_configured = 1;
}

static int vice_web_write_joystick(const char *code, int pressed, int port, const char *value)
{
    uint16_t mask;
    int joyport;

    joyport = vice_web_internal_joystick_port(port);
    if (joyport < 0) {
        return -1;
    }

    mask = vice_web_joystick_mask(code, value);
    if (mask == 0) {
        return 0;
    }

    vice_web_configure_joystick_ports();

    if (code != NULL && !strcmp(code, "mask")) {
        joystick_set_value_absolute((unsigned int)joyport, mask);
        return 0;
    }

    if (pressed) {
        joystick_set_value_or((unsigned int)joyport, mask);
    } else {
        joystick_set_value_and((unsigned int)joyport, (uint16_t)~mask);
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_joystick_value(int port)
{
    int joyport = vice_web_internal_joystick_port(port);
    if (joyport < 0) {
        return -1;
    }
    return (int)get_joystick_value(joyport);
}

EMSCRIPTEN_KEEPALIVE int vice_web_keyboard_row(int row)
{
    if (row < 0 || row >= KBD_ROWS) {
        return -1;
    }
    return keyarr[row];
}

EMSCRIPTEN_KEEPALIVE double vice_web_emulated_fps(void)
{
    double cpu_percent;
    double emulated_fps;
    int warp_enabled;

    vsyncarch_get_metrics(&cpu_percent, &emulated_fps, &warp_enabled);
    return emulated_fps;
}

EMSCRIPTEN_KEEPALIVE double vice_web_cpu_percent(void)
{
    double cpu_percent;
    double emulated_fps;
    int warp_enabled;

    vsyncarch_get_metrics(&cpu_percent, &emulated_fps, &warp_enabled);
    return cpu_percent;
}

EMSCRIPTEN_KEEPALIVE int vice_web_warp_enabled(void)
{
    double cpu_percent;
    double emulated_fps;
    int warp_enabled;

    vsyncarch_get_metrics(&cpu_percent, &emulated_fps, &warp_enabled);
    return warp_enabled;
}

static void vice_web_boot_async(void *unused)
{
    (void)unused;
    vice_web_exit_code = main_program(5, vice_web_argv);
    vice_web_started = 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_boot(void)
{
    if (vice_web_started) {
        return vice_web_exit_code;
    }

    vice_web_started = 1;
    vice_web_exit_code = 0;
    emscripten_async_call(vice_web_boot_async, NULL, 0);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void vice_web_reset(int mode)
{
    vice_web_frame_step_cancel();
    ui_pause_disable();
    machine_trigger_reset(mode ? MACHINE_RESET_MODE_POWER_CYCLE : MACHINE_RESET_MODE_RESET_CPU);
}

EMSCRIPTEN_KEEPALIVE int vice_web_pause(void)
{
    vice_web_frame_step_cancel();
    ui_pause_enable();
    return 1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_resume(void)
{
    vice_web_frame_step_cancel();
    if (vice_web_monitor_rearm_address >= 0) {
        int id = vice_web_monitor_checkpoint_id[vice_web_monitor_rearm_address];
        if (id) mon_breakpoint_switch_checkpoint(e_ON, id);
        vice_web_monitor_rearm_address = -1;
    }
    ui_pause_disable();
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_pause_status(void)
{
    return ui_pause_trap_active() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_boot_status(void)
{
    return vice_web_started ? 1 : vice_web_exit_code;
}

EMSCRIPTEN_KEEPALIVE int vice_web_set_resource(const char *name, const char *value)
{
    int int_value;
    int resource_type;

    if (name == NULL || value == NULL) {
        return -1;
    }
    if (!strcmp(name, "MachineVideoStandard") || !strcmp(name, "VICIIModel")) {
        vice_web_frame_step_cancel();
    }
    resource_type = resources_query_type(name);
    switch (resource_type) {
        case RES_INTEGER:
            if (!strcmp(name, "SidEngine") && vice_web_parse_sid_engine_value(value, &int_value) == 0) {
                return vice_web_set_sid_engine_model(int_value);
            }
            return resources_set_int(name, atoi(value));
        case RES_STRING:
            return resources_set_string(name, value);
        default:
            return vice_web_set_boot_resource_arg(name, value);
    }
}

EMSCRIPTEN_KEEPALIVE const char *vice_web_get_resource(const char *name)
{
    int int_value;
    const char *string_value;

    if (name == NULL) {
        return "";
    }
    switch (resources_query_type(name)) {
        case RES_INTEGER:
            if (resources_get_int(name, &int_value) < 0) {
                return "";
            }
            snprintf(vice_web_resource_value, sizeof(vice_web_resource_value), "%d", int_value);
            return vice_web_resource_value;
        case RES_STRING:
            if (resources_get_string(name, &string_value) < 0 || string_value == NULL) {
                return "";
            }
            return string_value;
        default:
            return "";
    }
}

/* Export the live REU RAM without detaching it or altering guest registers. */
EMSCRIPTEN_KEEPALIVE int vice_web_reu_export(const char *filename)
{
    if (filename == NULL || filename[0] == '\0' || !reu_cart_enabled()) {
        return -1;
    }
    return reu_bin_save(filename);
}

EMSCRIPTEN_KEEPALIVE int vice_web_attach_disk(int unit, const char *filename)
{
    int drive_type = DRIVE_TYPE_NONE;

    if (!vice_web_valid_disk_unit(unit) || filename == NULL || filename[0] == '\0') {
        return -1;
    }
    vice_web_frame_step_cancel();
    if (resources_get_int_sprintf("Drive%dType", &drive_type, unit) < 0) {
        return -1;
    }
    if (drive_type == DRIVE_TYPE_NONE
        && resources_set_int_sprintf("Drive%dType", DRIVE_TYPE_1541II, unit) < 0) {
        return -1;
    }
    return file_system_attach_disk((unsigned int)unit, 0, filename);
}

EMSCRIPTEN_KEEPALIVE int vice_web_autostart_prg(const char *filename, int autorun)
{
    if (filename == NULL || filename[0] == '\0') {
        return -1;
    }
    vice_web_frame_step_cancel();
    return autostart_prg(filename, autorun ? AUTOSTART_MODE_RUN : AUTOSTART_MODE_LOAD);
}

EMSCRIPTEN_KEEPALIVE int vice_web_attach_cartridge(const char *filename)
{
    if (filename == NULL || filename[0] == '\0') {
        return -1;
    }
    vice_web_frame_step_cancel();
    return cartridge_attach_image(CARTRIDGE_CRT, filename);
}

EMSCRIPTEN_KEEPALIVE int vice_web_detach_cartridge(void)
{
    vice_web_frame_step_cancel();
    /* Detach only the C64 main slot; do not disturb drives or other media. */
    cartridge_detach_image(CARTRIDGE_CRT);
    return cartridge_get_id(0) == CARTRIDGE_NONE ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_cartridge_id(void)
{
    return cartridge_get_id(0);
}

EMSCRIPTEN_KEEPALIVE int vice_web_load_psid(const char *filename, int tune)
{
    if (filename == NULL || filename[0] == '\0') {
        return -1;
    }
    vice_web_frame_step_cancel();
    if (machine_autodetect_psid(filename) < 0) {
        return -1;
    }
    machine_play_psid(tune);
    machine_trigger_reset(MACHINE_RESET_MODE_RESET_CPU);
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_set_warp_enabled(int enabled)
{
    vsync_set_warp_mode(enabled ? 1 : 0);
    return enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void vice_web_detach_disk(int unit)
{
    if (vice_web_valid_disk_unit(unit)) {
        vice_web_frame_step_cancel();
        file_system_detach_disk((unsigned int)unit, 0);
    }
}

EMSCRIPTEN_KEEPALIVE const char *vice_web_disk_name(int unit)
{
    const char *name;

    if (!vice_web_valid_disk_unit(unit)) {
        return "";
    }
    name = file_system_get_disk_name((unsigned int)unit, 0);
    return name != NULL ? name : "";
}

static int vice_web_drive_number(int unit)
{
    return vice_web_valid_disk_unit(unit) ? unit - 8 : -1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_led(int unit)
{
    int drive_number = vice_web_drive_number(unit);

    return drive_number >= 0 ? (int)headless_statusbar_drive_led_pwm1((unsigned int)drive_number) : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_led2(int unit)
{
    int drive_number = vice_web_drive_number(unit);

    return drive_number >= 0 ? (int)headless_statusbar_drive_led_pwm2((unsigned int)drive_number) : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_half_track(int unit)
{
    int drive_number = vice_web_drive_number(unit);

    return drive_number >= 0 ? (int)headless_statusbar_drive_half_track((unsigned int)drive_number) : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_track(int unit)
{
    int half_track = vice_web_drive_half_track(unit);

    return half_track > 0 ? half_track / 2 : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_sector(int unit)
{
    return -1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_side(int unit)
{
    int drive_number = vice_web_drive_number(unit);

    return drive_number >= 0 ? (int)headless_statusbar_drive_side((unsigned int)drive_number) : 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_drive_image_attached(int unit)
{
    int drive_number = vice_web_drive_number(unit);

    return drive_number >= 0 ? (int)headless_statusbar_drive_image_attached((unsigned int)drive_number) : 0;
}

static void vice_web_snapshot_trap(uint16_t addr, void *data)
{
    (void)addr;
    (void)data;

    vsync_suspend_speed_eval();
    sound_suspend();

    switch (vice_web_snapshot_operation) {
        case VICE_WEB_SNAPSHOT_SAVE:
            vice_web_snapshot_result_code = machine_write_snapshot(vice_web_snapshot_filename, 1, 1, 0);
            break;
        case VICE_WEB_SNAPSHOT_LOAD:
            vice_web_snapshot_result_code = machine_read_snapshot(vice_web_snapshot_filename, 0);
            break;
        default:
            vice_web_snapshot_result_code = -1;
            break;
    }

    vice_web_snapshot_operation = 0;
    vice_web_snapshot_pending_flag = 0;
}

static int vice_web_snapshot_request(const char *filename, int operation)
{
    if (filename == NULL || filename[0] == '\0') {
        return -1;
    }
    if (vice_web_snapshot_pending_flag) {
        return -1;
    }

    snprintf(vice_web_snapshot_filename, sizeof(vice_web_snapshot_filename), "%s", filename);
    vice_web_snapshot_result_code = -1;
    vice_web_snapshot_operation = operation;
    vice_web_snapshot_pending_flag = 1;
    interrupt_maincpu_trigger_trap(vice_web_snapshot_trap, NULL);
    return 0;
}

EMSCRIPTEN_KEEPALIVE int vice_web_save_snapshot(const char *filename)
{
    return vice_web_snapshot_request(filename, VICE_WEB_SNAPSHOT_SAVE);
}

EMSCRIPTEN_KEEPALIVE int vice_web_load_snapshot(const char *filename)
{
    vice_web_frame_step_cancel();
    return vice_web_snapshot_request(filename, VICE_WEB_SNAPSHOT_LOAD);
}

EMSCRIPTEN_KEEPALIVE int vice_web_snapshot_pending(void)
{
    return vice_web_snapshot_pending_flag;
}

EMSCRIPTEN_KEEPALIVE int vice_web_snapshot_result(void)
{
    return vice_web_snapshot_result_code;
}

EMSCRIPTEN_KEEPALIVE int vice_web_paste_text(const char *text)
{
    return text != NULL ? kbdbuf_feed_string(text) : -1;
}

EMSCRIPTEN_KEEPALIVE int vice_web_write_input(const char *type, const char *code, int pressed, int port, const char *value)
{
    if (type == NULL) {
        return 0;
    }
    if (!strcmp(type, "joystick")) {
        return vice_web_write_joystick(code, pressed, port, value);
    }
    if (!pressed) {
        return !strcmp(type, "key") ? vice_web_write_key(code, value, pressed) : 0;
    }
    if (!strcmp(type, "paste") || !strcmp(type, "text")) {
        return value != NULL ? kbdbuf_feed_string(value) : 0;
    }
    if (!strcmp(type, "key")) {
        if (vice_web_write_key(code, value, pressed)) {
            return 0;
        }
        return vice_web_feed_key_value(code, value);
    }
    return 0;
}
#endif


/** \brief  Exit handler
 */
void main_exit(void)
{
    /* printf("%s\n", __func__); */

    log_message(LOG_DEFAULT, "\nExiting...");

    machine_shutdown();
}
