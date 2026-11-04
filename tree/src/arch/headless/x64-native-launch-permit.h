/* x64-owned native launch authorization. GPL-2.0-or-later, like VICE.
 * This paging-file mapping contains no project bytes and creates no host file.
 * The core thread claims a ticket exactly once, immediately before injection.
 * Revocation after that claim cannot interrupt or partially apply the program.
 */
#ifndef X64_NATIVE_LAUNCH_PERMIT_H
#define X64_NATIVE_LAUNCH_PERMIT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef WINDOWS_COMPILE
#include <windows.h>
static unsigned char *x64_native_launch_permit_view;

static int x64_native_launch_permit_metadata(void)
{
    static const unsigned char prefix[24] = {
        'X','6','4','L','C','H','0','1', 1,0,0,0, 32,0,0,0,
        0,0,0,0,0,0,0,0
    };
    return x64_native_launch_permit_view &&
        !memcmp(x64_native_launch_permit_view, prefix, sizeof(prefix));
}
#endif

static void x64_native_launch_permit_prepare(void)
{
#ifdef WINDOWS_COMPILE
    const char *name = getenv("X64_NATIVE_LAUNCH_PERMIT");
    HANDLE mapping;
    const unsigned char *p;
    /* The same-release host uses an ASCII Local\\X64Launch-... name. Do not
     * silently reinterpret malformed input or fall back to an on-disk file. */
    if (!name || !*name) return;
    for (p = (const unsigned char *)name; *p; ++p) {
        if (*p < 32 || *p > 126) return;
    }
    mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!mapping) return;
    x64_native_launch_permit_view = (unsigned char *)MapViewOfFile(
        mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 32);
    CloseHandle(mapping);
    if (!x64_native_launch_permit_metadata()) {
        if (x64_native_launch_permit_view) UnmapViewOfFile(x64_native_launch_permit_view);
        x64_native_launch_permit_view = NULL;
    }
#endif
}

static int x64_native_launch_permit_available(void)
{
#ifdef WINDOWS_COMPILE
    return x64_native_launch_permit_metadata();
#else
    return 0;
#endif
}

static int x64_native_launch_permit_armed(uint64_t serial)
{
#ifdef WINDOWS_COMPILE
    volatile LONG64 *word;
    if (!serial || serial > (UINT64_MAX >> 2) ||
        !x64_native_launch_permit_available()) return 0;
    word = (volatile LONG64 *)(x64_native_launch_permit_view + 24);
    /* Admission can avoid known-stale dispatch side effects, but it does NOT
     * authorize application: the trap must still win the separate claim. */
    return (uint64_t)InterlockedCompareExchange64(word, 0, 0) == ((serial << 2) | 1);
#else
    (void)serial;
    return 0;
#endif
}

static int x64_native_launch_permit_claim(uint64_t serial)
{
#ifdef WINDOWS_COMPILE
    uint64_t armed, claimed;
    volatile LONG64 *word;
    if (!serial || serial > (UINT64_MAX >> 2) ||
        !x64_native_launch_permit_available()) return 0;
    word = (volatile LONG64 *)(x64_native_launch_permit_view + 24);
    armed = (serial << 2) | 1; claimed = (serial << 2) | 2;
    /* Windows mappings are page aligned, so the offset-24 atomic is aligned.
     * Interlocked CAS is the linearization point shared with host revocation. */
    return (uint64_t)InterlockedCompareExchange64(word, (LONG64)claimed,
        (LONG64)armed) == armed;
#else
    (void)serial;
    return 0; /* Guarded launch is not qualified on this platform. */
#endif
}
#endif
