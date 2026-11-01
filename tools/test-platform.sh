#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$root/tools/env.sh"
export BALABALA_GENERATED="$root/.build/generated"
# build.sh已生成同一套AOT/字体；用真实Rust静态库验证C服务。
rustup run "$RUST_VERSION" cargo build --locked --manifest-path "$root/.build/native/Cargo.toml" --release
cc -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" \
  "$root/tools/test-platform.c" \
  "$root/firmware/main/balabala_input.c" \
  "$root/firmware/main/balabala_storage.c" \
  "$root/.build/rust-target/release/libbalabala_probe.a" \
  -lpthread -ldl -lm -o "$root/.build/test-platform"
cd "$root"
"$root/.build/test-platform"
cc -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" \
  "$root/tools/test-runtime.c" \
  "$root/firmware/main/balabala_input.c" \
  "$root/firmware/main/balabala_display.c" \
  "$root/firmware/main/balabala_runtime.c" \
  "$root/firmware/main/balabala_storage.c" \
  "$root/firmware/main/balabala_network.c" \
  "$root/.build/rust-target/release/libbalabala_probe.a" \
  -lpthread -ldl -lm -o "$root/.build/test-runtime"
"$root/.build/test-runtime"

cc -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" \
  "$root/tools/test-network.c" "$root/firmware/main/balabala_network.c" \
  -lpthread -o "$root/.build/test-network"
"$root/.build/test-network"

cc -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" -I "$root/tools" \
  "$root/tools/test-network-idf.c" "$root/firmware/main/balabala_network.c" \
  -o "$root/.build/test-network-idf"
"$root/.build/test-network-idf"

python3 "$root/tools/extract-network-manager-test.py"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/firmware/main" \
  "$root/tools/test-network-manager.c" "$root/firmware/main/balabala_network.c" -lpthread -o "$root/.build/test-network-manager"
"$root/.build/test-network-manager"

python3 "$root/tools/extract-protocomm-test.py"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/.tools/esp-idf/components/protocomm/proto-c" \
  -I "$root/.tools/esp-idf/components/protobuf-c/protobuf-c" \
  "$root/tools/test-protocomm-notifications.c" -lpthread -o "$root/.build/test-protocomm-notifications"
"$root/.build/test-protocomm-notifications"

python3 "$root/tools/extract-advertising-test.py"
cc -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$root/tools/test-advertising.c" -o "$root/.build/test-advertising"
"$root/.build/test-advertising"

# 注册/派发/删除使用完整固定IDF实现，覆盖instance生命周期与回调重开。
sh "$root/tools/test-network-event-loop.sh"

# 固定真实NimBLE mempool，验证按需分配/归还与重复初始化。
sh "$root/tools/test-nimble-mempool.sh"
