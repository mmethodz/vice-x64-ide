/* Pointer-safe native host view of the unchanged Web64 profiler ABI 3 ledger.
 * GPL-2.0-or-later. Available only in WEB64_HOST_NATIVE profiling builds.
 */
#ifndef X64_PROFILER_NATIVE_H
#define X64_PROFILER_NATIVE_H
#include <stddef.h>
#include <stdint.h>

#define X64_PROFILER_INFO_WORDS 46u
enum {
    X64_PROFILER_RECORDS = 1,
    X64_PROFILER_PCS = 2,
    X64_PROFILER_FRAMES = 3,
    X64_PROFILER_LINES = 4,
    X64_PROFILER_RAM = 5,
    X64_PROFILER_CARTRIDGE = 6
};

#ifdef __cplusplus
extern "C" {
#endif
/* All calls are core-thread-only. Control remains vice_web_profiler_control.
 * Metadata retains the 46 ABI 3 words, with Wasm pointer slots
 * 13/16/17/19/40/44 zero. Returns 1 on success, 0 for a null destination. */
int x64_profiler_info(uint32_t out[X64_PROFILER_INFO_WORDS]);

/* Borrowed ledger storage, with byte length in native size_t (never truncated
 * to a Wasm offset). Copy before advancing emulation or issuing any control,
 * reset or teardown operation; no cross-thread access is permitted. A null
 * return with zero length denotes an empty, absent or invalid descriptor.
 * Use bounded copy when a success/failure distinction is required. */
const void *x64_profiler_buffer(uint32_t id, size_t *length);

/* Copy a checked byte range into caller-owned, non-overlapping storage.
 * Returns 1 on success, including a zero-length range in a known empty buffer;
 * returns 0 for an unknown descriptor, invalid bounds or null nonempty output.
 * No partial copy is performed on failure. */
int x64_profiler_copy(uint32_t id, size_t offset, void *out, size_t length);
#ifdef __cplusplus
}
#endif
#endif
