#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "balabala_storage.h"

static FILE *io_open(BalabalaStorage *s, const char *path, const char *mode) {
    return s->io.open ? s->io.open(path, mode, s->io.context) : fopen(path, mode);
}
static size_t io_read(BalabalaStorage *s, void *out, size_t bytes, FILE *f) {
    return s->io.read ? s->io.read(out, bytes, f, s->io.context) : fread(out, 1, bytes, f);
}
static size_t io_write(BalabalaStorage *s, const void *data, size_t bytes, FILE *f) {
    return s->io.write ? s->io.write(data, bytes, f, s->io.context) : fwrite(data, 1, bytes, f);
}
static int io_close(BalabalaStorage *s, FILE *f) {
    return s->io.close ? s->io.close(f, s->io.context) : fclose(f);
}
static void begin(BalabalaStorage *s) {
    s->report.last_errno = 0;
    s->report.operation = "idle";
    s->report.app_result = 0;
    s->report.peak_buffer_bytes = 0;
}
static bool available(BalabalaStorage *s) {
    s->report.operation = "root";
    struct stat st;
    int result = stat(s->root, &st);
    if (result == 0 && S_ISDIR(st.st_mode)) return true;
    s->report.last_errno = result == 0 ? ENOTDIR : errno;
    s->report.status = BALABALA_STORAGE_UNAVAILABLE;
    return false;
}
static int path_for(BalabalaStorage *s, char *path, const char *relative) {
    int n = snprintf(path, BALABALA_STORAGE_PATH_BYTES, "%s/balabala/%s", s->root, relative);
    return n >= 0 && n < BALABALA_STORAGE_PATH_BYTES ? 0 : -1;
}
static uint8_t *buffer_new(BalabalaStorage *s, size_t bytes) {
    uint8_t *data = malloc(bytes);
    if (data) {
        s->buffer_bytes += bytes;
        if (s->buffer_bytes > s->report.peak_buffer_bytes) s->report.peak_buffer_bytes = s->buffer_bytes;
    }
    return data;
}
static void buffer_drop(BalabalaStorage *s, uint8_t *data, size_t bytes) {
    if (data) { free(data); s->buffer_bytes -= bytes; }
}
static BalabalaFileStatus read_blob(BalabalaStorage *s, const char *path, size_t limit,
                                    uint8_t **data, size_t *length) {
    *data = NULL;
    *length = 0;
    errno = 0;
    FILE *f = io_open(s, path, "rb");
    if (!f) {
        if (errno == ENOENT) return BALABALA_FILE_ABSENT;
        s->report.last_errno = errno ? errno : EIO;
        return BALABALA_FILE_IO_ERROR;
    }
    struct stat st;
    BalabalaFileStatus result = BALABALA_FILE_READ;
    int error = 0;
    if (fstat(fileno(f), &st) != 0) {
        result = BALABALA_FILE_IO_ERROR;
        error = errno;
    } else if (!S_ISREG(st.st_mode) || st.st_size < 0) {
        result = BALABALA_FILE_IO_ERROR;
        error = EINVAL;
    } else if ((uint64_t)st.st_size > limit) {
        result = BALABALA_FILE_TOO_LARGE;
        error = EFBIG;
    } else if (st.st_size > 0) {
        *length = (size_t)st.st_size;
        *data = buffer_new(s, *length);
        if (!*data) { result = BALABALA_FILE_NO_MEMORY; error = ENOMEM; }
        else if (io_read(s, *data, *length, f) != *length) {
            result = BALABALA_FILE_IO_ERROR;
            error = errno ? errno : EIO;
        }
    }
    if (result == BALABALA_FILE_READ) {
        uint8_t extra;
        if (io_read(s, &extra, 1, f) != 0 || ferror(f) || !feof(f)) {
            result = BALABALA_FILE_IO_ERROR;
            error = errno ? errno : EIO;
        }
    }
    if (io_close(s, f) != 0) {
        error = errno ? errno : EIO;
        result = BALABALA_FILE_IO_ERROR;
    }
    if (error) s->report.last_errno = error;
    if (result != BALABALA_FILE_READ) {
        buffer_drop(s, *data, *length);
        *data = NULL;
        *length = 0;
    }
    return result;
}
int balabala_storage_init(BalabalaStorage *s, const char *root, const BalabalaStorageIo *io) {
    if (!s || !root || !root[0] || strlen(root) >= BALABALA_STORAGE_ROOT_BYTES) return -1;
    memset(s, 0, sizeof(*s));
    strcpy(s->root, root);
    size_t n = strlen(s->root);
    while (n > 1 && s->root[n - 1] == '/') s->root[--n] = 0;
    if (io) s->io = *io;
    return 0;
}
static uint32_t little_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static int32_t course_read(void *context, uint32_t offset, uint8_t *out, size_t length) {
    BalabalaStorage *s = context;
    char path[BALABALA_STORAGE_PATH_BYTES];
    if (path_for(s, path, "curriculum/course.bin") != 0) return -1;
    FILE *file = io_open(s, path, "rb");
    if (!file) return -1;
    bool ok = offset <= 0x7fffffff && fseek(file, (long)offset, SEEK_SET) == 0
        && io_read(s, out, length, file) == length && !ferror(file);
    if (io_close(s, file) != 0) ok = false;
    return ok ? 0 : -1;
}
static BalabalaFileStatus import_course(BalabalaStorage *s, BalabalaApp *app) {
    char path[BALABALA_STORAGE_PATH_BYTES];
    if (path_for(s, path, "curriculum/course.bin") != 0) return BALABALA_FILE_IO_ERROR;
    errno = 0;
    FILE *file = io_open(s, path, "rb");
    if (!file) {
        if (errno == ENOENT) return BALABALA_FILE_ABSENT;
        s->report.last_errno = errno ? errno : EIO;
        return BALABALA_FILE_IO_ERROR;
    }
    uint32_t bytes = 0, expected_crc = 0;
    int32_t info = balabala_app_curriculum_info(&bytes, &expected_crc);
    BalabalaFileStatus result = BALABALA_FILE_READ;
    struct stat st;
    uint8_t header[16];
    if (fstat(fileno(file), &st) != 0 || !S_ISREG(st.st_mode)) result = BALABALA_FILE_IO_ERROR;
    else if (info != 0 || bytes < sizeof(header) || (uint64_t)st.st_size != bytes) result = BALABALA_FILE_CONTENT_REJECTED;
    else if (io_read(s, header, sizeof(header), file) != sizeof(header) || ferror(file)) result = BALABALA_FILE_IO_ERROR;
    else if (memcmp(header, "BALC", 4) != 0 || little_u32(header + 4) != 1
        || little_u32(header + 8) != bytes - sizeof(header)
        || little_u32(header + 12) != expected_crc) result = BALABALA_FILE_CONTENT_REJECTED;
    uint8_t chunk[BALABALA_STORAGE_VERIFY_BYTES];
    uint32_t crc = UINT32_MAX;
    for (uint32_t offset = sizeof(header); result == BALABALA_FILE_READ && offset < bytes;) {
        size_t count = bytes - offset < sizeof(chunk) ? bytes - offset : sizeof(chunk);
        if (io_read(s, chunk, count, file) != count || ferror(file)) { result = BALABALA_FILE_IO_ERROR; break; }
        for (size_t i = 0; i < count; ++i) {
            crc ^= chunk[i];
            for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        offset += (uint32_t)count;
    }
    if (result == BALABALA_FILE_READ && (crc ^ UINT32_MAX) != expected_crc) result = BALABALA_FILE_CONTENT_REJECTED;
    if (result == BALABALA_FILE_READ && (io_read(s, chunk, 1, file) != 0 || ferror(file) || !feof(file))) result = BALABALA_FILE_IO_ERROR;
    int error = result == BALABALA_FILE_IO_ERROR ? (errno ? errno : EIO) : 0;
    if (io_close(s, file) != 0) { error = errno ? errno : EIO; result = BALABALA_FILE_IO_ERROR; }
    if (error) s->report.last_errno = error;
    if (result == BALABALA_FILE_READ) {
        s->report.app_result = balabala_app_curriculum_attach(app, course_read, s, bytes, expected_crc);
        if (s->report.app_result != 0) result = BALABALA_FILE_CONTENT_REJECTED;
    }
    if (result != BALABALA_FILE_READ) (void)balabala_app_save_protect(app);
    return result;
}
int balabala_storage_import_decks(BalabalaStorage *s, BalabalaApp *app) {
    if (!s || !app) return -1;
    begin(s);
    s->report.deck_input_max_bytes = 0;
    memset(s->report.decks, 0, sizeof(s->report.decks));
    s->report.course = BALABALA_FILE_UNCHECKED;
    if (!available(s)) return 1;
    static const char *files[] = {"decks/hanzi.json", "decks/pinyin.json", "decks/english.json", "decks/math.json"};
    int issues = 0;
    for (unsigned i = 0; i < 4; ++i) {
        char path[BALABALA_STORAGE_PATH_BYTES];
        if (path_for(s, path, files[i]) != 0) return -1;
        uint8_t *data;
        size_t length;
        s->report.decks[i] = read_blob(s, path, BALABALA_MAX_DECK_BYTES, &data, &length);
        if (s->report.decks[i] == BALABALA_FILE_READ) {
            if (length > s->report.deck_input_max_bytes) s->report.deck_input_max_bytes = length;
            int32_t result = balabala_app_import_deck(app, data, length);
            if (result != 0) {
                s->report.decks[i] = BALABALA_FILE_CONTENT_REJECTED;
                s->report.app_result = result;
            }
        }
        buffer_drop(s, data, length);
        if (s->report.decks[i] != BALABALA_FILE_READ && s->report.decks[i] != BALABALA_FILE_ABSENT) ++issues;
    }
    s->report.course = import_course(s, app);
    if (s->report.course != BALABALA_FILE_READ && s->report.course != BALABALA_FILE_ABSENT) ++issues;
    s->report.status = issues ? BALABALA_STORAGE_DAMAGED : BALABALA_STORAGE_READY;
    return issues ? 1 : 0;
}
int balabala_storage_show_import_error(BalabalaStorage *s, BalabalaApp *app) {
    if (!s || !app) return -1;
    for (unsigned i = 0; i < 4; ++i) {
        BalabalaFileStatus status = s->report.decks[i];
        if (status == BALABALA_FILE_TOO_LARGE || status == BALABALA_FILE_IO_ERROR ||
            status == BALABALA_FILE_NO_MEMORY) {
            return balabala_app_storage_error(app) == 0 ? 1 : -1;
        }
    }
    if (s->report.course != BALABALA_FILE_UNCHECKED && s->report.course != BALABALA_FILE_READ
        && s->report.course != BALABALA_FILE_ABSENT) {
        return balabala_app_storage_error(app) == 0 ? 1 : -1;
    }
    return 0;
}
int balabala_storage_restore(BalabalaStorage *s, BalabalaApp *app) {
    if (!s || !app) return -1;
    begin(s);
    memset(s->report.slots, 0, sizeof(s->report.slots));
    if (!available(s)) {
        (void)balabala_app_save_protect(app);
        return 1;
    }
    uint8_t *data[2] = {NULL, NULL};
    size_t length[2] = {0, 0};
    for (unsigned i = 0; i < 2; ++i) {
        char path[BALABALA_STORAGE_PATH_BYTES];
        (void)path_for(s, path, i == 0 ? "saves/slot0.bin" : "saves/slot1.bin");
        s->report.slots[i] = read_blob(s, path, BALABALA_MAX_SAVE_BYTES, &data[i], &length[i]);
    }
    bool first = s->report.slots[0] == BALABALA_FILE_ABSENT && s->report.slots[1] == BALABALA_FILE_ABSENT;
    bool uncertain = false;
    for (unsigned i = 0; i < 2; ++i) {
        if (s->report.slots[i] == BALABALA_FILE_IO_ERROR ||
            s->report.slots[i] == BALABALA_FILE_NO_MEMORY) uncertain = true;
    }
    // 未读槽可能比可读槽更新；仅确认无效的槽才允许另一槽降级恢复。
    if (uncertain) s->report.app_result = balabala_app_load(app, NULL, 0, NULL, 0);
    else if (!first) s->report.app_result = balabala_app_load(app, data[0], length[0], data[1], length[1]);
    for (unsigned i = 0; i < 2; ++i) buffer_drop(s, data[i], length[i]);
    s->report.status = first ? BALABALA_STORAGE_FIRST_BOOT :
        s->report.app_result == 0 ? BALABALA_STORAGE_RESTORED : BALABALA_STORAGE_DAMAGED;
    return first || s->report.app_result == 0 ? 0 : 1;
}
static bool directory(const char *path) {
    if (mkdir(path, 0775) == 0) return true;
    struct stat st;
    if (errno != EEXIST || stat(path, &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return false; }
    errno = 0;
    return true;
}
static bool save_directory(BalabalaStorage *s) {
    s->report.operation = "directory";
    char path[BALABALA_STORAGE_PATH_BYTES];
    (void)path_for(s, path, "");
    if (!directory(path)) return false;
    (void)path_for(s, path, "saves");
    return directory(path);
}
static bool close_record(BalabalaStorage *s, FILE *f, bool ok) {
    // fclose 仍须执行，但不能盖掉此前写入或校验失败的 errno。
    int error = ok ? 0 : (errno ? errno : EIO);
    if (io_close(s, f) != 0 && !error) {
        error = errno ? errno : EIO;
        s->report.operation = "close";
    }
    if (error) errno = error;
    return error == 0;
}
static bool write_record(BalabalaStorage *s, const char *path, const uint8_t *data, size_t length) {
    s->report.operation = "open-write";
    FILE *f = io_open(s, path, "wb");
    if (!f) return false;
    s->report.operation = "write";
    bool ok = io_write(s, data, length, f) == length && !ferror(f);
    if (ok) {
        s->report.operation = "flush";
        ok = (s->io.flush ? s->io.flush(f, s->io.context) : fflush(f)) == 0;
    }
    if (ok) {
        s->report.operation = "sync";
        ok = (s->io.sync ? s->io.sync(fileno(f), s->io.context) : fsync(fileno(f))) == 0;
    }
    return close_record(s, f, ok);
}
static bool verify_record(BalabalaStorage *s, const char *path, const uint8_t *data, size_t length) {
    s->report.operation = "verify-open";
    errno = 0;
    FILE *f = io_open(s, path, "rb");
    if (!f) return false;
    s->report.operation = "verify-read";
    uint8_t chunk[BALABALA_STORAGE_VERIFY_BYTES];
    bool ok = true;
    for (size_t offset = 0; offset < length && ok;) {
        size_t n = length - offset < sizeof(chunk) ? length - offset : sizeof(chunk);
        ok = io_read(s, chunk, n, f) == n && !ferror(f) && memcmp(chunk, data + offset, n) == 0;
        offset += n;
    }
    if (ok) ok = io_read(s, chunk, 1, f) == 0 && !ferror(f) && feof(f);
    return close_record(s, f, ok);
}
int balabala_storage_save(BalabalaStorage *s, BalabalaApp *app) {
    if (!s || !app) return -1;
    begin(s);
    if (!available(s)) return 1;
    if (!(balabala_app_status(app) & BALABALA_SAVE_DIRTY)) {
        s->report.status = BALABALA_STORAGE_CLEAN;
        return 0;
    }
    s->report.operation = "allocate";
    uint8_t *data = buffer_new(s, BALABALA_MAX_SAVE_BYTES);
    if (!data) {
        s->report.last_errno = ENOMEM;
        s->report.status = BALABALA_STORAGE_SAVE_FAILED;
        return 1;
    }
    uint32_t slot = 0;
    uint64_t generation = 0;
    s->report.operation = "export";
    int32_t length = balabala_app_save_export(app, data, BALABALA_MAX_SAVE_BYTES, &slot, &generation);
    bool ok = false;
    if (length > 0 && slot <= 1) {
        char path[BALABALA_STORAGE_PATH_BYTES];
        (void)path_for(s, path, slot == 0 ? "saves/slot0.bin" : "saves/slot1.bin");
        errno = 0;
        ok = save_directory(s) && write_record(s, path, data, (size_t)length) &&
             verify_record(s, path, data, (size_t)length);
        if (!ok) s->report.last_errno = errno ? errno : EIO;
        if (ok) s->report.operation = "ack";
        s->report.app_result = balabala_app_save_ack(app, generation, ok ? 1 : 0);
        ok = ok && s->report.app_result == 0;
    } else {
        s->report.app_result = length;
    }
    buffer_drop(s, data, BALABALA_MAX_SAVE_BYTES);
    s->report.status = ok ? BALABALA_STORAGE_SAVED : BALABALA_STORAGE_SAVE_FAILED;
    return ok ? 0 : 1;
}
