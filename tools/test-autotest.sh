#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$root/tools/env.sh"
export BALABALA_GENERATED="$root/.build/generated"
autotest_output=${BALABALA_AUTOTEST_OUT:-"$root/.build/autotest-phase${BALABALA_AUTOTEST_PHASE:-1}"}
export CARGO_TARGET_DIR="$autotest_output/rust-target-host"
# 使用固定IDF真实reset enum；此宿主配置不启用任何板级功能。
mkdir -p "$autotest_output/host-include"
printf '#pragma once\n' > "$autotest_output/host-include/sdkconfig.h"
rustup run "$RUST_VERSION" cargo build --locked --manifest-path "$root/.build/native/Cargo.toml" --release --features autotest
cc -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" -I "$autotest_output/host-include" \
  -I "$IDF_PATH/components/esp_system/include" -I "$IDF_PATH/components/esp_common/include" \
  "$root/tools/test-autotest.c" "$root/firmware/main/balabala_autotest.c" \
  "$root/firmware/main/balabala_storage.c" "$root/firmware/main/balabala_runtime.c" \
  "$root/firmware/main/balabala_input.c" \
  "$root/firmware/main/balabala_network.c" \
  "$CARGO_TARGET_DIR/release/libbalabala_probe.a" \
  -lpthread -ldl -lm -o "$autotest_output/test-autotest"
cd "$root"
"$autotest_output/test-autotest"

cc -std=c11 -D_POSIX_C_SOURCE=200809L -DBALABALA_AUTOTEST_PHASE2=1 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" -I "$autotest_output/host-include" -I "$root/tools" \
  -I "$IDF_PATH/components/esp_system/include" -I "$IDF_PATH/components/esp_common/include" \
  "$root/tools/test-autotest-phase2.c" "$root/firmware/main/balabala_autotest.c" \
  "$root/firmware/main/balabala_alloc_diag.c" \
  "$root/firmware/main/balabala_storage.c" "$root/firmware/main/balabala_runtime.c" \
  "$root/firmware/main/balabala_input.c" "$root/firmware/main/balabala_network.c" \
  "$CARGO_TARGET_DIR/release/libbalabala_probe.a" \
  -lpthread -ldl -lm -o "$autotest_output/test-autotest-phase2"
"$autotest_output/test-autotest-phase2"
