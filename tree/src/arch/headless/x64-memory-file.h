/* x64-owned bounded resident media and stdio adapter. GPL-2.0-or-later.
 * All calls belong to the VICE core thread. No native pointer enters the wire.
 */
#ifndef X64_MEMORY_FILE_H
#define X64_MEMORY_FILE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define X64_MEMORY_FILE_MAX_BLOB (16u * 1024u * 1024u)
#define X64_MEMORY_FILE_BUDGET (128u * 1024u * 1024u)
#define X64_MEMORY_FILE_MAX_BLOBS 64u
#define X64_MEMORY_FILE_MAX_STREAMS 64u
#define X64_MEMORY_FILE_MAX_CHUNK (256u * 1024u)
#define X64_MEMORY_FILE_PATH_BYTES 32u

enum {
    X64_MEMORY_D64 = 1, X64_MEMORY_CRT = 2, X64_MEMORY_REU = 3
};
enum {
    X64_MEMORY_OK = 0, X64_MEMORY_INVALID = -1,
    X64_MEMORY_NOT_FOUND = -2, X64_MEMORY_LIMIT = -3,
    X64_MEMORY_CRC = -4, X64_MEMORY_BUSY = -5
};
/* Explicitly serialize fields; this native struct is NOT the 28-byte wire. */
typedef struct {
    uint64_t id;
    uint32_t length, crc32, kind, sealed, received;
} x64_memory_blob_info;

int x64_memory_blob_begin(uint32_t kind, uint32_t length, uint32_t crc32, uint64_t *out_id);
int x64_memory_blob_chunk(uint64_t id, uint32_t offset, const void *data, uint32_t length);
int x64_memory_blob_seal(uint64_t id);
int x64_memory_blob_describe(uint64_t id, x64_memory_blob_info *out);
int x64_memory_blob_read(uint64_t id, uint32_t offset, void *out, uint32_t length);
int x64_memory_blob_drop(uint64_t id);
/* A clone shares an immutable baseline; its first write takes a private copy.
 * Clone accepts sealed immutable inputs only. Capture creates such an input
 * from current private bytes; output creates an empty private export target. */
int x64_memory_blob_clone(uint64_t id, uint64_t *out_private_id);
int x64_memory_blob_capture(uint64_t id, uint64_t *out_sealed_id);
int x64_memory_blob_output(uint32_t kind, uint64_t *out_private_id);
int x64_memory_blob_pin(uint64_t id);
int x64_memory_blob_unpin(uint64_t id);
int x64_memory_blob_path(uint64_t id, char *out, size_t capacity);
size_t x64_memory_blob_allocated(void);

/* Reserved names never fall back to host I/O, including malformed/unknown IDs.
 * A valid name is exactly x64mem:<16 lowercase hexadecimal ID>.<d64|crt|reu>.
 * File operations require sealed entries; immutable uploads cannot be written.
 * Paths remain registered independently of FILE lifetime. Pins and open streams
 * prevent drop of that entry. Drop of an original upload keeps a shared clone's
 * immutable baseline alive. There is no automatic path creation or file growth
 * beyond MAX_BLOB; a write beyond EOF zero-fills its gap. */
int x64_memory_file_reserved(const char *path);
int x64_memory_file_name_valid(const char *path);
int x64_memory_file_equal(const char *left, const char *right);
int x64_memory_file_stat(const char *path, size_t *length, unsigned int *isdir);
int x64_memory_file_access(const char *path, int mode);
int x64_memory_file_remove(const char *path);
/* Recognizes both live and closed resident FILE tokens. Never pass one to libc. */
int x64_memory_file_stream(FILE *stream);
int x64_memory_file_seek64(FILE *stream, int64_t offset, int whence);
int64_t x64_memory_file_tell64(FILE *stream);
int64_t x64_memory_file_size(FILE *stream);
FILE *x64_memory_file_fopen(const char *path, const char *mode);
int x64_memory_file_fclose(FILE *stream);
size_t x64_memory_file_fread(void *out, size_t size, size_t count, FILE *stream);
size_t x64_memory_file_fwrite(const void *data, size_t size, size_t count, FILE *stream);
int x64_memory_file_fseek(FILE *stream, long offset, int whence);
long x64_memory_file_ftell(FILE *stream);
void x64_memory_file_rewind(FILE *stream);
int x64_memory_file_feof(FILE *stream);
int x64_memory_file_ferror(FILE *stream);
void x64_memory_file_clearerr(FILE *stream);
int x64_memory_file_fflush(FILE *stream);
int x64_memory_file_fgetc(FILE *stream);
int x64_memory_file_ungetc(int value, FILE *stream);
char *x64_memory_file_fgets(char *out, int capacity, FILE *stream);
int x64_memory_file_fputc(int value, FILE *stream);
int x64_memory_file_fputs(const char *value, FILE *stream);
#endif

/* Opt in only AFTER all system/library headers in selected I/O translation
 * units. Main includes declarations/implementation without these aliases. */
#if defined(WEB64_HOST_NATIVE) && defined(X64_MEMORY_FILE_STDIO) && !defined(X64_MEMORY_FILE_STDIO_INSTALLED)
#define X64_MEMORY_FILE_STDIO_INSTALLED
#undef getc
#undef putc
#define fopen x64_memory_file_fopen
#define fclose x64_memory_file_fclose
#define fread x64_memory_file_fread
#define fwrite x64_memory_file_fwrite
#define fseek x64_memory_file_fseek
#define ftell x64_memory_file_ftell
#define rewind x64_memory_file_rewind
#define feof x64_memory_file_feof
#define ferror x64_memory_file_ferror
#define clearerr x64_memory_file_clearerr
#define fflush x64_memory_file_fflush
#define fgetc x64_memory_file_fgetc
#define getc x64_memory_file_fgetc
#define ungetc x64_memory_file_ungetc
#define fgets x64_memory_file_fgets
#define fputc x64_memory_file_fputc
#define putc x64_memory_file_fputc
#define fputs x64_memory_file_fputs
#endif
