/* x64-owned resident-media commands for the retained Web64 VICE APIs.
 * GPL-2.0-or-later. Included after x64-native-host's helpers, before dispatch.
 * No pathname enters the wire; every VICE filename below belongs to the
 * bounded memory-file registry. Uploads remain immutable, while mounted media
 * use separately pinned private working copies. All work is core-thread only.
 */
#ifndef X64_NATIVE_MEDIA_IMPL_H
#define X64_NATIVE_MEDIA_IMPL_H
#include "x64-memory-file.h"
#include "crt.h"
#include "lib.h"

static uint64_t x64_native_media_disks[4];
static uint64_t x64_native_media_cartridge;
static uint64_t x64_native_media_reu;
enum { X64_MEDIA_CART_OWNER = 4, X64_MEDIA_REU_OWNER = 5 };
/* One owned pin per identity. A failed retained rollback can leave either
 * filename authoritative, so keep both identities indexed until an explicit
 * successful cleanup of their owning device proves they can be retired. */
static struct {
    uint64_t id;
    uint32_t owners;
} x64_native_media_quarantine[X64_MEMORY_FILE_MAX_BLOBS];

static void x64_native_media_descriptor(unsigned char out[28], const x64_memory_blob_info *info)
{
    x64_put64(out, info->id);
    x64_put32(out + 8, info->length);
    x64_put32(out + 12, info->crc32);
    x64_put32(out + 16, info->kind);
    x64_put32(out + 20, info->sealed);
    x64_put32(out + 24, info->received);
}

static int x64_native_media_describe(uint64_t id, unsigned char out[28])
{
    x64_memory_blob_info info;
    int result = x64_memory_blob_describe(id, &info);
    if (!result) x64_native_media_descriptor(out, &info);
    return result;
}

static int x64_native_media_kind(uint64_t id, uint32_t kind, x64_memory_blob_info *info)
{
    int result = x64_memory_blob_describe(id, info);
    if (result) return result;
    if (!info->sealed || !info->length || info->received != info->length || info->kind != kind)
        return X64_MEMORY_INVALID;
    return 0;
}

static int x64_native_media_path_matches(uint64_t id, const char *actual)
{
    char path[X64_MEMORY_FILE_PATH_BYTES];
    return id && actual && !x64_memory_blob_path(id, path, sizeof(path)) &&
        x64_memory_file_equal(path, actual);
}

static void x64_native_media_hold(uint64_t id, unsigned int owner)
{
    unsigned int i, empty = X64_MEMORY_FILE_MAX_BLOBS;
    if (!id) return;
    for (i = 0; i < X64_MEMORY_FILE_MAX_BLOBS; i++) {
        if (x64_native_media_quarantine[i].id == id) {
            x64_native_media_quarantine[i].owners |= 1u << owner;
            return;
        }
        if (!x64_native_media_quarantine[i].id) empty = i;
    }
    /* Every indexed identity owns a pin in the same max-64-entry registry.
     * A new live identity cannot coexist with 64 other pinned identities. */
    if (empty == X64_MEMORY_FILE_MAX_BLOBS) exit(EXIT_FAILURE);
    x64_native_media_quarantine[empty].id = id;
    x64_native_media_quarantine[empty].owners = 1u << owner;
}

static void x64_native_media_forget(uint64_t id)
{
    unsigned int i;
    for (i = 0; i < X64_MEMORY_FILE_MAX_BLOBS; i++) {
        if (x64_native_media_quarantine[i].id == id) {
            x64_native_media_quarantine[i].id = 0;
            x64_native_media_quarantine[i].owners = 0;
        }
    }
}

static uint64_t x64_native_media_binding(uint64_t primary, const char *actual)
{
    unsigned int i;
    if (x64_native_media_path_matches(primary, actual)) return primary;
    for (i = 0; i < X64_MEMORY_FILE_MAX_BLOBS; i++) {
        uint64_t id = x64_native_media_quarantine[i].id;
        if (x64_native_media_path_matches(id, actual)) return id;
    }
    return 0;
}

static int x64_native_media_referenced(uint64_t id)
{
    unsigned int unit;
    for (unit = 8; unit <= 11; unit++)
        if (x64_native_media_path_matches(id, file_system_get_disk_name(unit, 0))) return 1;
    return x64_native_media_path_matches(id, cartridge_get_filename_by_slot(0)) ||
        x64_native_media_path_matches(id, reu_get_file_name());
}

/* Retire only after the retained detach/save path has completed. Restore the
 * owned pin if an unexpected stream prevents drop, and retain its owner tag. */
static int x64_native_media_release(uint64_t id, unsigned int owner)
{
    int result;
    if (!id) return 0;
    result = x64_memory_blob_unpin(id);
    if (result == X64_MEMORY_NOT_FOUND) { x64_native_media_forget(id); return 0; }
    if (!result) result = x64_memory_blob_drop(id);
    if (!result) { x64_native_media_forget(id); return 0; }
    /* A failed unpin means the owned-pin invariant is uncertain. Re-establish
     * a pin before retaining the identity; never forward it to the filesystem. */
    if (x64_memory_blob_pin(id)) exit(EXIT_FAILURE);
    x64_native_media_hold(id, owner);
    return result;
}

/* The caller must first prove successful cleanup of this device. Merely
 * observing absent names after an uncertain attach is not that proof. Other
 * device names and retained stream references still veto release. */
static int x64_native_media_collect(unsigned int owner)
{
    unsigned int i;
    int result = 0;
    for (i = 0; i < X64_MEMORY_FILE_MAX_BLOBS; i++) {
        uint64_t id = x64_native_media_quarantine[i].id;
        if (!id || !(x64_native_media_quarantine[i].owners & (1u << owner))) continue;
        if (x64_native_media_referenced(id) || x64_native_media_release(id, owner)) result = -6;
    }
    return result;
}

static int x64_native_media_clone(uint64_t input, uint32_t kind, uint64_t *out,
                                  char path[X64_MEMORY_FILE_PATH_BYTES])
{
    x64_memory_blob_info info;
    uint64_t id = 0;
    int result = x64_native_media_kind(input, kind, &info);
    if (!result) result = x64_memory_blob_clone(input, &id);
    if (!result) result = x64_memory_blob_pin(id);
    if (!result) result = x64_memory_blob_path(id, path, X64_MEMORY_FILE_PATH_BYTES);
    if (result) {
        if (id) {
            x64_memory_blob_unpin(id);
            x64_memory_blob_drop(id);
        }
        return result;
    }
    *out = id;
    return 0;
}

static int x64_native_media_reu_capacity(uint32_t kib)
{
    return kib == 128 || kib == 256 || kib == 512 || kib == 1024 || kib == 2048 ||
        kib == 4096 || kib == 8192 || kib == 16384;
}

/* Uses the Source's nonmutating live-RAM export, into a private memory-backed
 * output. Publication creates a separate complete immutable registry entry. */
static int x64_native_media_capture_reu(uint64_t *out)
{
    uint64_t temporary = 0, captured = 0;
    char path[X64_MEMORY_FILE_PATH_BYTES];
    x64_memory_blob_info info;
    int capacity = 0, result;
    if (!reu_cart_enabled() || resources_get_int("REUsize", &capacity) < 0 ||
        !x64_native_media_reu_capacity((uint32_t)capacity)) return -1;
    result = x64_memory_blob_output(X64_MEMORY_REU, &temporary);
    if (!result) result = x64_memory_blob_path(temporary, path, sizeof(path));
    if (!result) result = vice_web_reu_export(path);
    if (!result) result = x64_memory_blob_describe(temporary, &info);
    if (!result && info.length != (uint32_t)capacity * 1024u) result = -1;
    if (!result) result = x64_memory_blob_capture(temporary, &captured);
    if (temporary) {
        int released = x64_memory_blob_drop(temporary);
        if (!result && released) result = released;
    }
    if (result) {
        if (captured) x64_memory_blob_drop(captured);
        return result;
    }
    *out = captured;
    return 0;
}

static int x64_native_media_same_bytes(uint64_t left, uint64_t right)
{
    x64_memory_blob_info a, b;
    unsigned char first[4096], second[4096];
    uint32_t offset = 0;
    if (x64_memory_blob_describe(left, &a) || x64_memory_blob_describe(right, &b) ||
        a.length != b.length) return 0;
    while (offset < a.length) {
        uint32_t size = a.length - offset;
        if (size > sizeof(first)) size = sizeof(first);
        if (x64_memory_blob_read(left, offset, first, size) ||
            x64_memory_blob_read(right, offset, second, size) || memcmp(first, second, size)) return 0;
        offset += size;
    }
    return 1;
}

static int x64_native_media_disk_attach(uint32_t unit, uint64_t input, int *entered)
{
    uint64_t candidate = 0;
    uint64_t primary = x64_native_media_disks[unit - 8];
    uint64_t previous = x64_native_media_binding(primary, file_system_get_disk_name(unit, 0));
    char path[X64_MEMORY_FILE_PATH_BYTES], old_path[X64_MEMORY_FILE_PATH_BYTES];
    int result = x64_native_media_clone(input, X64_MEMORY_D64, &candidate, path);
    if (result) return result;
    *entered = 1;
    x64_native_stop_left();
    result = vice_web_attach_disk((int)unit, path);
    if (!result && x64_native_media_path_matches(candidate, file_system_get_disk_name(unit, 0))) {
        x64_native_media_disks[unit - 8] = candidate;
        if (primary && primary != previous) x64_native_media_hold(primary, unit - 8);
        if (x64_native_media_release(previous, unit - 8)) return -6;
        return 0;
    }
    /* The Source probes before replacing, but a later drive-attach failure can
     * already have detached the previous image. Restore only an owned backing. */
    if (previous && !x64_native_media_path_matches(previous, file_system_get_disk_name(unit, 0))) {
        if (x64_memory_blob_path(previous, old_path, sizeof(old_path)) ||
            vice_web_attach_disk((int)unit, old_path) ||
            !x64_native_media_path_matches(previous, file_system_get_disk_name(unit, 0))) {
            x64_native_media_hold(primary, unit - 8);
            x64_native_media_hold(previous, unit - 8);
            x64_native_media_disks[unit - 8] = candidate;
            return -6;
        }
    }
    if (x64_native_media_path_matches(candidate, file_system_get_disk_name(unit, 0))) {
        x64_native_media_hold(primary, unit - 8);
        x64_native_media_hold(previous, unit - 8);
        x64_native_media_disks[unit - 8] = candidate;
        return -6;
    }
    if (x64_native_media_release(candidate, unit - 8)) return -6;
    return result ? result : -1;
}

static int x64_native_media_disk_detach(uint32_t unit)
{
    uint64_t previous = x64_native_media_disks[unit - 8];
    x64_native_stop_left();
    vice_web_detach_disk((int)unit);
    if ((file_system_get_disk_name(unit, 0) && *file_system_get_disk_name(unit, 0)) ||
        vice_web_drive_image_attached((int)unit)) return -6;
    x64_native_media_disks[unit - 8] = 0;
    if (x64_native_media_referenced(previous)) x64_native_media_hold(previous, unit - 8);
    else if (x64_native_media_release(previous, unit - 8)) return -6;
    return x64_native_media_collect(unit - 8);
}

static int x64_native_media_capture_disk(uint32_t unit, uint64_t *out)
{
    uint64_t id = x64_native_media_binding(x64_native_media_disks[unit - 8], file_system_get_disk_name(unit, 0));
    uint64_t captured = 0;
    char path[X64_MEMORY_FILE_PATH_BYTES];
    int result;
    if (!x64_native_media_path_matches(id, file_system_get_disk_name(unit, 0)) ||
        x64_memory_blob_path(id, path, sizeof(path))) return -1;
    /* Match the Source fallback: detach writes dirty GCR sectors, then copy and
     * reattach on this same core-thread turn, with no intervening instruction. */
    x64_native_stop_left();
    vice_web_detach_disk((int)unit);
    if (file_system_get_disk_name(unit, 0) && *file_system_get_disk_name(unit, 0)) return -6;
    result = x64_memory_blob_capture(id, &captured);
    if (vice_web_attach_disk((int)unit, path) ||
        !x64_native_media_path_matches(id, file_system_get_disk_name(unit, 0))) {
        if (captured) x64_memory_blob_drop(captured);
        return -6;
    }
    if (result) return result;
    *out = captured;
    return 0;
}

static int x64_native_media_cartridge_attach(uint64_t input, int *entered)
{
    uint64_t candidate = 0, primary = x64_native_media_cartridge;
    uint64_t previous = x64_native_media_binding(primary, cartridge_get_filename_by_slot(0));
    char path[X64_MEMORY_FILE_PATH_BYTES], old_path[X64_MEMORY_FILE_PATH_BYTES];
    int previous_type = vice_web_cartridge_id();
    int header_type;
    int result = x64_native_media_clone(input, X64_MEMORY_CRT, &candidate, path);
    if (result) return result;
    /* Header validation uses the retained CRT parser; its chip loaders below
     * remain the semantic authority, including family-specific validation. */
    header_type = crt_getid(path);
    /* Admission matches the four frozen Web64 mastering profiles: generic
     * 8K/16K (header type 0), Magic Desk (19), and EasyFlash (32). Their
     * retained loaders are the qualified resident-FILE translation units. */
    if (header_type != 0 && header_type != 19 && header_type != 32) {
        x64_native_media_release(candidate, X64_MEDIA_CART_OWNER);
        return -1;
    }
    *entered = 1;
    x64_native_stop_left();
    x64_native_invalidate_profile();
    result = vice_web_attach_cartridge(path);
    if (!result && x64_native_media_path_matches(candidate, cartridge_get_filename_by_slot(0))) {
        x64_native_media_cartridge = candidate;
        if (primary && primary != previous) x64_native_media_hold(primary, X64_MEDIA_CART_OWNER);
        if (x64_native_media_release(previous, X64_MEDIA_CART_OWNER)) return -6;
        return 0;
    }
    /* Retained cartridge_attach_image may detach before reading all CHIP
     * records. Attempt a real retained reattach of the previous owned image. */
    if (previous) {
        if (x64_memory_blob_path(previous, old_path, sizeof(old_path)) ||
            vice_web_attach_cartridge(old_path) ||
            !x64_native_media_path_matches(previous, cartridge_get_filename_by_slot(0))) {
            x64_native_media_hold(primary, X64_MEDIA_CART_OWNER);
            x64_native_media_hold(previous, X64_MEDIA_CART_OWNER);
            x64_native_media_cartridge = candidate;
            return -6;
        }
    } else if (previous_type != CARTRIDGE_NONE) {
        x64_native_media_hold(primary, X64_MEDIA_CART_OWNER);
        x64_native_media_cartridge = candidate;
        return -6;
    } else if (vice_web_detach_cartridge() || vice_web_cartridge_id() != CARTRIDGE_NONE) {
        x64_native_media_hold(primary, X64_MEDIA_CART_OWNER);
        x64_native_media_cartridge = candidate;
        return -6;
    }
    if (x64_native_media_release(candidate, X64_MEDIA_CART_OWNER)) return -6;
    return result ? result : -1;
}

static int x64_native_media_cartridge_detach(void)
{
    uint64_t previous = x64_native_media_cartridge;
    int result;
    x64_native_stop_left();
    x64_native_invalidate_profile();
    result = vice_web_detach_cartridge();
    if (result || vice_web_cartridge_id() != CARTRIDGE_NONE) return -6;
    x64_native_media_cartridge = 0;
    if (x64_native_media_referenced(previous)) x64_native_media_hold(previous, X64_MEDIA_CART_OWNER);
    else if (x64_native_media_release(previous, X64_MEDIA_CART_OWNER)) return -6;
    return x64_native_media_collect(X64_MEDIA_CART_OWNER);
}

static int x64_native_media_restore_reu(const char *filename, int capacity, int write_image)
{
    const char *actual = NULL;
    int value = -1;
    if (resources_set_int("REUImageWrite", 0) < 0 || resources_set_int("REU", 0) < 0 ||
        reu_cart_enabled() || resources_set_string("REUfilename", filename) < 0 ||
        resources_set_int("REUsize", capacity) < 0 || resources_set_int("REUImageWrite", write_image) < 0 ||
        resources_get_string("REUfilename", &actual) < 0 || !actual || strcmp(actual, filename) ||
        resources_get_int("REUsize", &value) < 0 || value != capacity ||
        resources_get_int("REUImageWrite", &value) < 0 || value != write_image) return -6;
    return 0;
}

static int x64_native_media_reu_attach(uint32_t capacity, uint64_t input, int *entered)
{
    uint64_t candidate = 0, verification = 0, previous = x64_native_media_reu;
    char path[X64_MEMORY_FILE_PATH_BYTES];
    const char *old_name = NULL, *actual = NULL;
    char *previous_name;
    x64_memory_blob_info info;
    int old_capacity = 0, old_write = 0, value = 0, result;
    if (!x64_native_media_reu_capacity(capacity) || reu_cart_enabled()) return -1;
    result = x64_native_media_kind(input, X64_MEMORY_REU, &info);
    if (result || info.length != capacity * 1024u) return result ? result : -1;
    if (resources_get_int("REUsize", &old_capacity) < 0 || resources_get_int("REUImageWrite", &old_write) < 0 ||
        resources_get_string("REUfilename", &old_name) < 0 || !old_name) return -1;
    previous_name = lib_strdup(old_name);
    result = x64_native_media_clone(input, X64_MEMORY_REU, &candidate, path);
    if (result) { lib_free(previous_name); return result; }
    *entered = 1;
    x64_native_stop_left();
    x64_native_invalidate_profile();
    result = resources_set_int("REUImageWrite", 0);
    if (!result) result = resources_set_int("REUsize", (int)capacity);
    if (!result) result = resources_set_string("REUfilename", path);
    if (!result) result = resources_set_int("REU", 1);
    if (!result && (!reu_cart_enabled() || resources_get_int("REUsize", &value) < 0 || value != (int)capacity ||
        resources_get_int("REUImageWrite", &value) < 0 || value != 0 ||
        resources_get_string("REUfilename", &actual) < 0 || !actual || strcmp(actual, path))) result = -1;
    if (!result) result = x64_native_media_capture_reu(&verification);
    if (!result && !x64_native_media_same_bytes(input, verification)) result = -1;
    if (verification) x64_memory_blob_drop(verification);
    if (result) {
        if (x64_native_media_restore_reu(previous_name, old_capacity, old_write)) {
            /* Keep backing presence for any uncertain retained REU state. */
            x64_native_media_hold(previous, X64_MEDIA_REU_OWNER);
            x64_native_media_reu = candidate;
            lib_free(previous_name);
            return -6;
        }
        lib_free(previous_name);
        if (x64_native_media_release(candidate, X64_MEDIA_REU_OWNER)) return -6;
        return result;
    }
    lib_free(previous_name);
    x64_native_media_reu = candidate;
    return x64_native_media_release(previous, X64_MEDIA_REU_OWNER) ? -6 : 0;
}

static int x64_native_media_reu_detach(int discard, uint64_t *captured, int *entered)
{
    uint64_t previous = x64_native_media_reu;
    const char *actual = NULL;
    int result;
    if (!reu_cart_enabled()) {
        if (resources_get_string("REUfilename", &actual) < 0 || !actual) return -1;
        if (!*actual) {
            /* Explicit repeated cleanup can release an earlier BUSY/uncertain
             * owner once disabled + cleared filename has been proved. */
            *entered = 1;
            x64_native_media_reu = 0;
            if (x64_native_media_referenced(previous)) x64_native_media_hold(previous, X64_MEDIA_REU_OWNER);
            else if (x64_native_media_release(previous, X64_MEDIA_REU_OWNER)) return -6;
            return x64_native_media_collect(X64_MEDIA_REU_OWNER);
        }
        if (!discard) return -1; /* Cannot capture disabled RAM before discard. */
    } else if (!discard) {
        result = x64_native_media_capture_reu(captured);
        if (result) return result; /* A failed capture never authorizes detach. */
    }
    *entered = 1;
    x64_native_stop_left();
    x64_native_invalidate_profile();
    if (resources_set_int("REUImageWrite", 0) < 0 || resources_set_int("REU", 0) < 0 ||
        reu_cart_enabled() || resources_set_string("REUfilename", "") < 0 ||
        resources_get_string("REUfilename", &actual) < 0 || !actual || *actual) return -6;
    x64_native_media_reu = 0;
    if (x64_native_media_referenced(previous)) x64_native_media_hold(previous, X64_MEDIA_REU_OWNER);
    else if (x64_native_media_release(previous, X64_MEDIA_REU_OWNER)) return -6;
    return x64_native_media_collect(X64_MEDIA_REU_OWNER);
}

/* Returns one only for this packet's opcodes; every handled request emits one
 * response. Invalid wire/store requests are transport failures. Entered core
 * operations return MutationResult even on a retained failure/uncertain state. */
static int x64_native_media_command(uint16_t op, uint64_t request, const unsigned char *p, uint32_t n)
{
    unsigned char bytes[104] = {0};
    uint64_t id = 0;
    uint32_t unit, selector;
    int result = -1, entered = 0;
    if (op < 51 || op > 63) return 0;
    switch (op) {
        case 51:
            if (n != 12 || !x64_get32(p + 4)) break;
            result = x64_memory_blob_begin(x64_get32(p), x64_get32(p + 4), x64_get32(p + 8), &id);
            if (!result) result = x64_native_media_describe(id, bytes);
            if (result) break;
            x64_native_reply(op, request, 0, bytes, 28); return 1;
        case 52:
            if (n <= 12 || n - 12 > X64_MEMORY_FILE_MAX_CHUNK) break;
            id = x64_get64(p);
            result = x64_memory_blob_chunk(id, x64_get32(p + 8), p + 12, n - 12);
            if (!result) result = x64_native_media_describe(id, bytes);
            if (result) break;
            x64_native_reply(op, request, 0, bytes, 28); return 1;
        case 53:
            if (n != 8) break;
            id = x64_get64(p);
            result = x64_memory_blob_seal(id);
            if (!result) result = x64_native_media_describe(id, bytes);
            if (result) break;
            x64_native_reply(op, request, 0, bytes, 28); return 1;
        case 54: {
            unsigned char *data;
            uint32_t length;
            if (n != 16 || !(length = x64_get32(p + 12)) || length > X64_MEMORY_FILE_MAX_CHUNK) break;
            data = malloc(length);
            if (!data) { result = -5; break; }
            result = x64_memory_blob_read(x64_get64(p), x64_get32(p + 8), data, length);
            if (!result) x64_native_reply(op, request, 0, data, length);
            free(data);
            if (result) break;
            return 1;
        }
        case 55:
            if (n != 8) break;
            result = x64_memory_blob_drop(x64_get64(p));
            if (result) break;
            x64_native_reply(op, request, 0, NULL, 0); return 1;
        case 56:
            if (n != 12 || (unit = x64_get32(p)) < 8 || unit > 11) break;
            result = x64_native_media_disk_attach(unit, x64_get64(p + 4), &entered);
            if (result && !entered) break;
            x64_native_result(op, request, result); return 1;
        case 57:
            if (n != 4 || (unit = x64_get32(p)) < 8 || unit > 11) break;
            result = x64_native_media_disk_detach(unit);
            x64_native_result(op, request, result); return 1;
        case 58:
            if (n != 8) break;
            selector = x64_get32(p); unit = x64_get32(p + 4);
            if (selector == 0 && unit == 0) {
                int capacity = 0, i;
                if (resources_get_int("REUsize", &capacity) < 0) break;
                for (i = 0; i < 4; i++) {
                    const char *name = file_system_get_disk_name(i + 8, 0);
                    x64_put64(bytes + i * 8, x64_native_media_binding(x64_native_media_disks[i], name));
                    x64_put32(bytes + 60 + i * 4, name && *name ? 1 : 0);
                }
                x64_put64(bytes + 32, x64_native_media_binding(x64_native_media_cartridge,
                    cartridge_get_filename_by_slot(0)));
                x64_put64(bytes + 40, x64_native_media_binding(x64_native_media_reu, reu_get_file_name()));
                x64_put32(bytes + 48, (uint32_t)vice_web_cartridge_id());
                x64_put32(bytes + 52, reu_cart_enabled() ? 1 : 0);
                x64_put32(bytes + 56, (uint32_t)capacity);
                x64_native_reply(op, request, 0, bytes, 80); return 1;
            }
            if (selector != 1 || unit < 8 || unit > 11) break;
            result = x64_native_media_capture_disk(unit, &id);
            if (!result) result = x64_native_media_describe(id, bytes);
            if (result) break;
            x64_native_reply(op, request, 0, bytes, 28); return 1;
        case 59:
            if (n != 8) break;
            result = x64_native_media_cartridge_attach(x64_get64(p), &entered);
            if (result && !entered) break;
            x64_native_result(op, request, result); return 1;
        case 60:
            if (n) break;
            result = x64_native_media_cartridge_detach();
            x64_native_result(op, request, result); return 1;
        case 61:
            if (n != 12) break;
            result = x64_native_media_reu_attach(x64_get32(p), x64_get64(p + 4), &entered);
            if (result && !entered) break;
            x64_native_result(op, request, result); return 1;
        case 62:
            if (n != 4 || x64_get32(p) > 1) break;
            result = x64_native_media_reu_detach((int)x64_get32(p), &id, &entered);
            if (result && !entered) break;
            if (id && x64_native_media_describe(id, bytes)) exit(EXIT_FAILURE);
            x64_put32(bytes + 28, (uint32_t)result);
            x64_native_snapshot_bytes(bytes + 32);
            x64_native_reply(op, request, 0, bytes, 104); return 1;
        case 63:
            if (n) break;
            result = x64_native_media_capture_reu(&id);
            if (!result) result = x64_native_media_describe(id, bytes);
            if (result) break;
            x64_native_reply(op, request, 0, bytes, 28); return 1;
    }
    x64_native_reply(op, request, result ? result : -1, NULL, 0);
    return 1;
}
#endif
