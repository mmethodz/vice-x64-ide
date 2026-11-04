/* Included once by headless/main.c, without X64_MEMORY_FILE_STDIO aliases.
 * GPL-2.0-or-later. Resident backing only; retained VICE parsers stay unchanged.
 */
#include "x64-memory-file.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    uint32_t references, length;
    unsigned char bytes[];
} x64_memory_data;
typedef struct {
    uint64_t id;
    x64_memory_data *base;
    unsigned char *working;
    uint32_t kind, expected_crc, running_crc, received;
    uint32_t working_length, working_capacity, pins, streams;
    int sealed, private_copy, has_working;
} x64_memory_blob;
typedef struct {
    /* An actual FILE member gives the opaque token correct FILE alignment.
     * Its fields are never read; even closed tokens remain recognizable. */
    FILE token;
    x64_memory_blob *blob;
    uint64_t position;
    int active, readable, writable, append, eof, error;
    int pushback, pushback_advance;
} x64_memory_stream;

static x64_memory_blob x64_memory_blobs[X64_MEMORY_FILE_MAX_BLOBS];
static x64_memory_stream x64_memory_streams[X64_MEMORY_FILE_MAX_STREAMS];
static uint64_t x64_memory_next_id;
static size_t x64_memory_owned;

static int x64_memory_fail(int code, int error)
{
    errno = error;
    return code;
}
static x64_memory_blob *x64_memory_find(uint64_t id)
{
    unsigned int i;
    if (!id) return NULL;
    for (i = 0; i < X64_MEMORY_FILE_MAX_BLOBS; i++)
        if (x64_memory_blobs[i].id == id) return &x64_memory_blobs[i];
    return NULL;
}
static x64_memory_blob *x64_memory_slot(void)
{
    unsigned int i;
    if (x64_memory_next_id == UINT64_MAX) return NULL;
    for (i = 0; i < X64_MEMORY_FILE_MAX_BLOBS; i++)
        if (!x64_memory_blobs[i].id) return &x64_memory_blobs[i];
    return NULL;
}
static x64_memory_data *x64_memory_data_create(uint32_t length)
{
    size_t bytes = sizeof(x64_memory_data) + (size_t)length;
    x64_memory_data *data;
    if (bytes > X64_MEMORY_FILE_BUDGET - x64_memory_owned) return NULL;
    data = malloc(bytes);
    if (!data) return NULL;
    data->references = 1;
    data->length = length;
    x64_memory_owned += bytes;
    return data;
}
static const unsigned char *x64_memory_bytes(const x64_memory_blob *blob)
{
    return blob->has_working ? blob->working : blob->base->bytes;
}
static uint32_t x64_memory_length(const x64_memory_blob *blob)
{
    return blob->has_working ? blob->working_length : blob->base->length;
}
static uint32_t x64_memory_crc(const unsigned char *bytes, uint32_t length)
{
    return (uint32_t)crc32(crc32(0L, Z_NULL, 0), bytes, length);
}
size_t x64_memory_blob_allocated(void) { return x64_memory_owned; }

int x64_memory_blob_begin(uint32_t kind, uint32_t length, uint32_t crc, uint64_t *out_id)
{
    x64_memory_blob *blob;
    x64_memory_data *data;
    if (out_id) *out_id = 0;
    if (!out_id || kind < X64_MEMORY_D64 || kind > X64_MEMORY_REU ||
        length > X64_MEMORY_FILE_MAX_BLOB) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    blob = x64_memory_slot();
    if (!blob) return x64_memory_fail(X64_MEMORY_LIMIT, ENOSPC);
    data = x64_memory_data_create(length);
    if (!data) return x64_memory_fail(X64_MEMORY_LIMIT, ENOMEM);
    memset(blob, 0, sizeof(*blob));
    blob->id = ++x64_memory_next_id;
    blob->base = data;
    blob->kind = kind;
    blob->expected_crc = crc;
    blob->running_crc = (uint32_t)crc32(0L, Z_NULL, 0);
    *out_id = blob->id;
    return X64_MEMORY_OK;
}
int x64_memory_blob_chunk(uint64_t id, uint32_t offset, const void *bytes, uint32_t length)
{
    x64_memory_blob *blob = x64_memory_find(id);
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (blob->sealed || offset != blob->received || length > X64_MEMORY_FILE_MAX_CHUNK ||
        length > blob->base->length - blob->received || (length && !bytes))
        return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (length) {
        memcpy(blob->base->bytes + offset, bytes, length);
        blob->running_crc = (uint32_t)crc32(blob->running_crc, bytes, length);
        blob->received += length;
    }
    return X64_MEMORY_OK;
}
int x64_memory_blob_seal(uint64_t id)
{
    x64_memory_blob *blob = x64_memory_find(id);
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (blob->sealed || blob->received != blob->base->length)
        return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (blob->running_crc != blob->expected_crc) return x64_memory_fail(X64_MEMORY_CRC, EIO);
    blob->sealed = 1;
    return X64_MEMORY_OK;
}
int x64_memory_blob_describe(uint64_t id, x64_memory_blob_info *out)
{
    x64_memory_blob *blob = x64_memory_find(id);
    if (out) memset(out, 0, sizeof(*out));
    if (!out) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    out->id = id;
    out->kind = blob->kind;
    out->sealed = blob->sealed != 0;
    out->length = x64_memory_length(blob);
    out->received = blob->private_copy ? out->length : blob->received;
    out->crc32 = blob->sealed ? x64_memory_crc(x64_memory_bytes(blob), out->length) : blob->expected_crc;
    return X64_MEMORY_OK;
}
int x64_memory_blob_read(uint64_t id, uint32_t offset, void *out, uint32_t length)
{
    x64_memory_blob *blob = x64_memory_find(id);
    uint32_t available;
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (!blob->sealed) return x64_memory_fail(X64_MEMORY_INVALID, EACCES);
    available = x64_memory_length(blob);
    if (offset > available || length > available - offset || (length && !out))
        return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (length) memcpy(out, x64_memory_bytes(blob) + offset, length);
    return X64_MEMORY_OK;
}
int x64_memory_blob_clone(uint64_t id, uint64_t *out_id)
{
    x64_memory_blob *source = x64_memory_find(id), *blob;
    if (out_id) *out_id = 0;
    if (!out_id) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (!source) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (!source->sealed || source->private_copy) return x64_memory_fail(X64_MEMORY_INVALID, EACCES);
    blob = x64_memory_slot();
    if (!blob) return x64_memory_fail(X64_MEMORY_LIMIT, ENOSPC);
    memset(blob, 0, sizeof(*blob));
    blob->id = ++x64_memory_next_id;
    blob->base = source->base;
    blob->base->references++;
    blob->kind = source->kind;
    blob->expected_crc = source->expected_crc;
    blob->received = source->received;
    blob->sealed = blob->private_copy = 1;
    *out_id = blob->id;
    return X64_MEMORY_OK;
}
int x64_memory_blob_capture(uint64_t id, uint64_t *out_id)
{
    x64_memory_blob *source = x64_memory_find(id), *blob;
    x64_memory_data *data;
    uint32_t length;
    if (out_id) *out_id = 0;
    if (!out_id) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (!source) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (!source->sealed) return x64_memory_fail(X64_MEMORY_INVALID, EACCES);
    blob = x64_memory_slot();
    if (!blob) return x64_memory_fail(X64_MEMORY_LIMIT, ENOSPC);
    length = x64_memory_length(source);
    data = x64_memory_data_create(length);
    if (!data) return x64_memory_fail(X64_MEMORY_LIMIT, ENOMEM);
    if (length) memcpy(data->bytes, x64_memory_bytes(source), length);
    memset(blob, 0, sizeof(*blob));
    blob->id = ++x64_memory_next_id;
    blob->base = data;
    blob->kind = source->kind;
    blob->expected_crc = x64_memory_crc(data->bytes, length);
    blob->running_crc = blob->expected_crc;
    blob->received = length;
    blob->sealed = 1;
    *out_id = blob->id;
    return X64_MEMORY_OK;
}
int x64_memory_blob_output(uint32_t kind, uint64_t *out_id)
{
    uint64_t id;
    int result;
    if (out_id) *out_id = 0;
    if (!out_id) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    result = x64_memory_blob_begin(kind, 0, 0, &id);
    if (result) return result;
    x64_memory_find(id)->sealed = x64_memory_find(id)->private_copy = 1;
    *out_id = id;
    return X64_MEMORY_OK;
}
int x64_memory_blob_pin(uint64_t id)
{
    x64_memory_blob *blob = x64_memory_find(id);
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (!blob->sealed || blob->pins == UINT32_MAX) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    blob->pins++;
    return X64_MEMORY_OK;
}
int x64_memory_blob_unpin(uint64_t id)
{
    x64_memory_blob *blob = x64_memory_find(id);
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (!blob->pins) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    blob->pins--;
    return X64_MEMORY_OK;
}
int x64_memory_blob_drop(uint64_t id)
{
    x64_memory_blob *blob = x64_memory_find(id);
    x64_memory_data *base;
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (blob->pins || blob->streams) return x64_memory_fail(X64_MEMORY_BUSY, EBUSY);
    base = blob->base;
    if (blob->working) {
        x64_memory_owned -= blob->working_capacity;
        free(blob->working);
    }
    if (!--base->references) {
        x64_memory_owned -= sizeof(*base) + (size_t)base->length;
        free(base);
    }
    memset(blob, 0, sizeof(*blob));
    return X64_MEMORY_OK;
}
static const char *x64_memory_extension(uint32_t kind)
{
    return kind == X64_MEMORY_D64 ? "d64" : kind == X64_MEMORY_CRT ? "crt" : "reu";
}
int x64_memory_blob_path(uint64_t id, char *out, size_t capacity)
{
    x64_memory_blob *blob = x64_memory_find(id);
    static const char hex[] = "0123456789abcdef";
    unsigned int i;
    if (out && capacity) out[0] = 0;
    if (!out || capacity < 28) return x64_memory_fail(X64_MEMORY_INVALID, EINVAL);
    if (!blob) return x64_memory_fail(X64_MEMORY_NOT_FOUND, ENOENT);
    if (!blob->sealed) return x64_memory_fail(X64_MEMORY_INVALID, EACCES);
    memcpy(out, "x64mem:", 7);
    for (i = 0; i < 16; i++) out[7 + i] = hex[(id >> ((15 - i) * 4)) & 15];
    out[23] = '.';
    memcpy(out + 24, x64_memory_extension(blob->kind), 4);
    return X64_MEMORY_OK;
}
int x64_memory_file_reserved(const char *path)
{
    static const char prefix[] = "x64mem:";
    unsigned int i;
    if (!path) return 0;
    for (i = 0; i < 7; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)prefix[i]) return 0;
    }
    return 1;
}
static x64_memory_blob *x64_memory_name(const char *path)
{
    uint64_t id = 0;
    unsigned int i;
    x64_memory_blob *blob;
    if (!path || strncmp(path, "x64mem:", 7) || strlen(path) != 27 || path[23] != '.') {
        errno = EINVAL; return NULL;
    }
    for (i = 7; i < 23; i++) {
        unsigned int c = (unsigned char)path[i], nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else { errno = EINVAL; return NULL; }
        id = (id << 4) | nibble;
    }
    blob = x64_memory_find(id);
    if (!blob) { errno = ENOENT; return NULL; }
    if (!blob->sealed || strcmp(path + 24, x64_memory_extension(blob->kind))) {
        errno = EACCES; return NULL;
    }
    return blob;
}
int x64_memory_file_name_valid(const char *path) { return x64_memory_name(path) != NULL; }
int x64_memory_file_equal(const char *left, const char *right)
{
    x64_memory_blob *a = x64_memory_name(left), *b = x64_memory_name(right);
    return a && b && a == b;
}
int x64_memory_file_stat(const char *path, size_t *length, unsigned int *isdir)
{
    x64_memory_blob *blob = x64_memory_name(path);
    if (length) *length = blob ? x64_memory_length(blob) : (size_t)-1;
    if (isdir) *isdir = 0;
    return blob ? 0 : -1;
}
int x64_memory_file_access(const char *path, int mode)
{
    x64_memory_blob *blob = x64_memory_name(path);
    if (!blob) return -1;
    if ((mode & ~7) || (mode & 1) || ((mode & 2) && !blob->private_copy)) {
        errno = EACCES; return -1;
    }
    return 0;
}
int x64_memory_file_remove(const char *path)
{
    x64_memory_blob *blob = x64_memory_name(path);
    return blob && !x64_memory_blob_drop(blob->id) ? 0 : -1;
}
static x64_memory_stream *x64_memory_stream_find(FILE *file)
{
    unsigned int i;
    for (i = 0; i < X64_MEMORY_FILE_MAX_STREAMS; i++)
        if (file == &x64_memory_streams[i].token) return &x64_memory_streams[i];
    return NULL;
}
int x64_memory_file_stream(FILE *file) { return x64_memory_stream_find(file) != NULL; }
static int x64_memory_stream_error(x64_memory_stream *stream, int error)
{
    if (stream) stream->error = 1;
    errno = error;
    return -1;
}
FILE *x64_memory_file_fopen(const char *path, const char *mode)
{
    x64_memory_blob *blob;
    x64_memory_stream *stream = NULL;
    unsigned int i;
    int plus = 0, truncate, append, readable, writable;
    if (!x64_memory_file_reserved(path)) return fopen(path, mode);
    blob = x64_memory_name(path);
    if (!blob) return NULL;
    if (!mode || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) { errno = EINVAL; return NULL; }
    for (i = 1; mode[i]; i++) {
        if (mode[i] == '+' && !plus) plus = 1;
        else if (mode[i] != 'b' && mode[i] != 't') { errno = EINVAL; return NULL; }
    }
    truncate = mode[0] == 'w'; append = mode[0] == 'a';
    readable = mode[0] == 'r' || plus;
    writable = mode[0] != 'r' || plus;
    if (writable && !blob->private_copy) { errno = EACCES; return NULL; }
    for (i = 0; i < X64_MEMORY_FILE_MAX_STREAMS; i++)
        if (!x64_memory_streams[i].active) { stream = &x64_memory_streams[i]; break; }
    if (!stream) { errno = EMFILE; return NULL; }
    if (truncate) { blob->has_working = 1; blob->working_length = 0; }
    memset(stream, 0, sizeof(*stream));
    stream->active = 1; stream->blob = blob;
    stream->readable = readable; stream->writable = writable; stream->append = append;
    stream->pushback = -1;
    stream->position = append ? x64_memory_length(blob) : 0;
    blob->streams++;
    return &stream->token;
}
int x64_memory_file_fclose(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream) return fclose(file);
    if (!stream->active) { errno = EBADF; return EOF; }
    stream->blob->streams--;
    stream->active = 0; stream->blob = NULL;
    return 0;
}
size_t x64_memory_file_fread(void *out, size_t size, size_t count, FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    size_t wanted, copied = 0, available, take;
    unsigned char *destination = out;
    uint32_t length;
    if (!stream) return fread(out, size, count, file);
    if (!stream->active || !stream->readable) { x64_memory_stream_error(stream, EBADF); return 0; }
    if (!size || !count) return 0;
    if (!out || count > SIZE_MAX / size) { x64_memory_stream_error(stream, EINVAL); return 0; }
    wanted = size * count;
    if (stream->pushback >= 0) {
        destination[copied++] = (unsigned char)stream->pushback;
        stream->pushback = -1;
        stream->position += stream->pushback_advance;
    }
    length = x64_memory_length(stream->blob);
    available = stream->position < length ? length - (size_t)stream->position : 0;
    take = wanted - copied < available ? wanted - copied : available;
    if (take) memcpy(destination + copied, x64_memory_bytes(stream->blob) + (size_t)stream->position, take);
    stream->position += take;
    copied += take;
    if (copied < wanted) stream->eof = 1;
    return copied / size;
}
static int x64_memory_working_reserve(x64_memory_blob *blob, uint32_t required)
{
    unsigned char *bytes;
    uint32_t capacity = blob->working_capacity;
    if (!blob->has_working && required < blob->base->length) required = blob->base->length;
    if (required > X64_MEMORY_FILE_MAX_BLOB) { errno = EFBIG; return -1; }
    if (required > capacity) {
        uint32_t grown = capacity <= X64_MEMORY_FILE_MAX_BLOB / 2 ? capacity * 2 : X64_MEMORY_FILE_MAX_BLOB;
        if (grown < required) grown = required;
        if ((size_t)(grown - capacity) > X64_MEMORY_FILE_BUDGET - x64_memory_owned) { errno = ENOSPC; return -1; }
        bytes = realloc(blob->working, grown);
        if (!bytes) { errno = ENOMEM; return -1; }
        blob->working = bytes;
        blob->working_capacity = grown;
        x64_memory_owned += grown - capacity;
    }
    if (!blob->has_working) {
        if (blob->base->length) memcpy(blob->working, blob->base->bytes, blob->base->length);
        blob->working_length = blob->base->length;
        blob->has_working = 1;
    }
    return 0;
}
size_t x64_memory_file_fwrite(const void *data, size_t size, size_t count, FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    size_t length;
    uint32_t previous, end;
    if (!stream) return fwrite(data, size, count, file);
    if (!stream->active || !stream->writable) { x64_memory_stream_error(stream, EBADF); return 0; }
    if (!size || !count) return 0;
    if (!data || count > SIZE_MAX / size) { x64_memory_stream_error(stream, EINVAL); return 0; }
    length = size * count;
    if (stream->append) stream->position = x64_memory_length(stream->blob);
    if (stream->position > X64_MEMORY_FILE_MAX_BLOB || length > X64_MEMORY_FILE_MAX_BLOB - stream->position) {
        x64_memory_stream_error(stream, EFBIG); return 0;
    }
    end = (uint32_t)(stream->position + length);
    if (x64_memory_working_reserve(stream->blob, end)) { stream->error = 1; return 0; }
    previous = stream->blob->working_length;
    if (stream->position > previous) memset(stream->blob->working + previous, 0, (size_t)stream->position - previous);
    memcpy(stream->blob->working + (size_t)stream->position, data, length);
    if (end > previous) stream->blob->working_length = end;
    stream->position = end;
    return count;
}
int x64_memory_file_seek64(FILE *file, int64_t offset, int whence)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    uint64_t base, target;
    if (!stream || !stream->active) { errno = EBADF; return -1; }
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = stream->position;
    else if (whence == SEEK_END) base = x64_memory_length(stream->blob);
    else { errno = EINVAL; return -1; }
    if (offset < 0) {
        uint64_t distance = (uint64_t)(-(offset + 1)) + 1;
        if (distance > base) { errno = EINVAL; return -1; }
        target = base - distance;
    } else {
        if ((uint64_t)offset > INT64_MAX - base) { errno = EOVERFLOW; return -1; }
        target = base + (uint64_t)offset;
    }
    stream->position = target; stream->eof = 0; stream->pushback = -1;
    return 0;
}
int64_t x64_memory_file_tell64(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream || !stream->active) { errno = EBADF; return -1; }
    return (int64_t)stream->position;
}
int64_t x64_memory_file_size(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream || !stream->active) { errno = EBADF; return -1; }
    return x64_memory_length(stream->blob);
}
int x64_memory_file_fseek(FILE *file, long offset, int whence)
{
    return x64_memory_file_stream(file) ? x64_memory_file_seek64(file, offset, whence) : fseek(file, offset, whence);
}
long x64_memory_file_ftell(FILE *file)
{
    int64_t position;
    if (!x64_memory_file_stream(file)) return ftell(file);
    position = x64_memory_file_tell64(file);
    if (position > LONG_MAX) { errno = EOVERFLOW; return -1; }
    return (long)position;
}
void x64_memory_file_rewind(FILE *file)
{
    if (!x64_memory_file_stream(file)) { rewind(file); return; }
    x64_memory_file_seek64(file, 0, SEEK_SET);
    x64_memory_file_clearerr(file);
}
int x64_memory_file_feof(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream) return feof(file);
    if (!stream->active) { errno = EBADF; return 0; }
    return stream->eof;
}
int x64_memory_file_ferror(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream) return ferror(file);
    if (!stream->active) { errno = EBADF; return 1; }
    return stream->error;
}
void x64_memory_file_clearerr(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream) { clearerr(file); return; }
    if (!stream->active) { errno = EBADF; return; }
    stream->eof = stream->error = 0;
}
int x64_memory_file_fflush(FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream) return fflush(file);
    if (!stream->active) { errno = EBADF; return EOF; }
    return 0;
}
int x64_memory_file_fgetc(FILE *file)
{
    unsigned char value;
    if (!x64_memory_file_stream(file)) return fgetc(file);
    return x64_memory_file_fread(&value, 1, 1, file) == 1 ? value : EOF;
}
int x64_memory_file_ungetc(int value, FILE *file)
{
    x64_memory_stream *stream = x64_memory_stream_find(file);
    if (!stream) return ungetc(value, file);
    if (!stream->active || !stream->readable || value == EOF || stream->pushback >= 0) return EOF;
    stream->pushback = (unsigned char)value;
    stream->pushback_advance = stream->position != 0;
    if (stream->pushback_advance) stream->position--;
    stream->eof = 0;
    return (unsigned char)value;
}
char *x64_memory_file_fgets(char *out, int capacity, FILE *file)
{
    int value, copied = 0;
    if (!x64_memory_file_stream(file)) return fgets(out, capacity, file);
    if (!out || capacity <= 0) { errno = EINVAL; return NULL; }
    while (copied < capacity - 1 && (value = x64_memory_file_fgetc(file)) != EOF) {
        out[copied++] = (char)value;
        if (value == '\n') break;
    }
    out[copied] = 0;
    return copied || capacity == 1 ? out : NULL;
}
int x64_memory_file_fputc(int value, FILE *file)
{
    unsigned char byte = (unsigned char)value;
    if (!x64_memory_file_stream(file)) return fputc(value, file);
    return x64_memory_file_fwrite(&byte, 1, 1, file) == 1 ? byte : EOF;
}
int x64_memory_file_fputs(const char *value, FILE *file)
{
    size_t length;
    if (!x64_memory_file_stream(file)) return fputs(value, file);
    if (!value) { errno = EINVAL; return EOF; }
    length = strlen(value);
    return x64_memory_file_fwrite(value, 1, length, file) == length ? 0 : EOF;
}
