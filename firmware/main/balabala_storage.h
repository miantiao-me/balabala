#ifndef BALABALA_STORAGE_H
#define BALABALA_STORAGE_H
#include <stdio.h>
#include "balabala_probe.h"
#define BALABALA_STORAGE_ROOT_BYTES 192
#define BALABALA_STORAGE_PATH_BYTES 256
#define BALABALA_STORAGE_VERIFY_BYTES 256

typedef enum {
    BALABALA_FILE_UNCHECKED, BALABALA_FILE_READ, BALABALA_FILE_ABSENT,
    BALABALA_FILE_TOO_LARGE, BALABALA_FILE_IO_ERROR, BALABALA_FILE_NO_MEMORY,
    BALABALA_FILE_CONTENT_REJECTED
} BalabalaFileStatus;
typedef enum {
    BALABALA_STORAGE_UNCHECKED, BALABALA_STORAGE_READY, BALABALA_STORAGE_UNAVAILABLE,
    BALABALA_STORAGE_FIRST_BOOT, BALABALA_STORAGE_RESTORED, BALABALA_STORAGE_DAMAGED,
    BALABALA_STORAGE_SAVED, BALABALA_STORAGE_SAVE_FAILED, BALABALA_STORAGE_CLEAN
} BalabalaStorageStatus;
/* 可选故障注入。NULL回调使用POSIX默认实现；close回调即使报错也须关闭流。
 * read/write返回字节数。context由调用方持有；同一任务串行使用。 */
typedef struct {
    FILE *(*open)(const char *path, const char *mode, void *context);
    size_t (*read)(void *out, size_t bytes, FILE *file, void *context);
    size_t (*write)(const void *data, size_t bytes, FILE *file, void *context);
    int (*flush)(FILE *file, void *context);
    int (*sync)(int fd, void *context);
    int (*close)(FILE *file, void *context);
    void *context;
} BalabalaStorageIo;
typedef struct {
    BalabalaStorageStatus status;
    BalabalaFileStatus decks[4], slots[2], course;
    int last_errno;
    const char *operation;
    int32_t app_result;
    size_t peak_buffer_bytes, deck_input_max_bytes;
} BalabalaStorageReport;
typedef struct {
    char root[BALABALA_STORAGE_ROOT_BYTES];
    BalabalaStorageIo io;
    BalabalaStorageReport report;
    size_t buffer_bytes;
} BalabalaStorage;
/* root为已挂载目录，只复制路径，不挂载、不创建root、不探测SD引脚。 */
int balabala_storage_init(BalabalaStorage *storage, const char *root, const BalabalaStorageIo *io);
/* 依次读取root/balabala/decks/{hanzi,pinyin,english,math}.json，再核验
 * curriculum/course.bin；完整课程存在时按需读取正文，目录留在Flash。
 * 每个旧包最多65536B；缺失保留内置/当前卡组，坏包保留旧内容。 */
int balabala_storage_import_decks(BalabalaStorage *storage, BalabalaApp *app);
/* import后、restore前呈现过大/IO/分配失败；CONTENT_REJECTED保留Rust具体错误。
 * 1存在读取失败并已通知应用；0无需通知；-1参数错。不把缺卡/无SD报成损坏。 */
int balabala_storage_show_import_error(BalabalaStorage *storage, BalabalaApp *app);
/* root/balabala/saves/slot{0,1}.bin各自限32768B。
 * 仅双ENOENT跳过ABI load；READ只代表IO完整，不表示CRC/语义有效。
 * root不可用静默保护保存；任一槽IO/内存错误锁住保存，不用部分读取解锁。
 * 每个槽顺序读取，两个有效输入缓冲须保留至现有双槽ABI返回后立即释放。 */
int balabala_storage_restore(BalabalaStorage *storage, BalabalaApp *app);
/* dirty时导出非活动槽，write/fflush/fsync/close，再以256B流式读回比较，
 * 全部成功才ack(1)，失败ack(0)。不写另一个槽，不清除失败后的dirty。
 * 返回0成功/无改动，1有可恢复问题，-1参数错误；诊断见report。 */
int balabala_storage_save(BalabalaStorage *storage, BalabalaApp *app);
#endif
