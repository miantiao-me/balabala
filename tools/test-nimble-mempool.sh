#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$root/tools/versions.sh"
python3 "$root/tools/apply-idf-patches.py"
idf="$root/.tools/esp-idf"
[ "$(git -C "$idf" rev-parse HEAD)" = "$IDF_COMMIT" ]
# 完整编译固定IDF的mempool实现与原始结构/配置映射，只替换OS和heap边界。
mock="$root/.build/mock-nimble-mempool"
mkdir -p "$mock/os" "$mock/modlog"
cat > "$mock/sdkconfig.h" <<'EOF'
#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC 1
#define CONFIG_BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC 1
#define CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY 1
EOF
cat > "$mock/os/os.h" <<'EOF'
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"
#include "syscfg/syscfg.h"
#include <sys/queue.h>
#define BIT(n) (1U << (n))
#define OS_ALIGNMENT 4
#define OS_ALIGN(n, a) (((n) + (a) - 1) & ~((a) - 1))
typedef uint32_t os_sr_t;
void mempool_test_enter(void);
void mempool_test_exit(void);
#define OS_ENTER_CRITICAL(sr) do { (sr) = 0; mempool_test_enter(); } while (0)
#define OS_EXIT_CRITICAL(sr) do { (void)(sr); mempool_test_exit(); } while (0)
#include "os/os_error.h"
#include "os/os_mempool.h"
void os_mempool_module_init(void);
void os_mempool_deinit(bool is_controller);
EOF
# 此实现没有日志调用；不引入无关的IDF日志/FreeRTOS依赖。
printf '#pragma once\n' > "$mock/modlog/modlog.h"
# STAILQ/SLIST与IDF一样使用sys/queue.h；上游关闭trace的空函数保留未用参数。
cc -std=c11 -DESP_PLATFORM -Wall -Wextra -Werror -Wno-unused-parameter -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$mock" -I "$idf/components/bt/porting/include" \
  -I "$idf/components/bt/host/nimble/port/include" \
  -I "$idf/components/bt/host/nimble/nimble/porting/nimble/include" \
  "$root/tools/test-nimble-mempool.c" "$idf/components/bt/porting/mem/os_mempool.c" \
  -o "$root/.build/test-nimble-mempool"
"$root/.build/test-nimble-mempool"
