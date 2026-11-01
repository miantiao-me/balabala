#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
python3 "$root/tools/apply-idf-patches.py"
python3 "$root/tools/fetch-components.py"
python3 "$root/tools/prepare-network-event-test.py"
python3 "$root/tools/extract-network-manager-test.py" --event-loop
# IDF的int64_t格式使用目标32位ABI；宿主64位long布局不同，只关闭format告警。
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-format -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/.build/mock-event" -I "$root/tools" -I "$root/firmware/main" \
  -I "$root/.tools/esp-idf/components/protocomm/include/common" \
  -I "$root/.tools/esp-idf/components/protocomm/include/security" \
  -I "$root/.tools/esp-idf/components/esp_event/include" \
  -I "$root/.tools/esp-idf/components/esp_event/private_include" \
  "$root/tools/test-network-event-loop.c" "$root/firmware/main/balabala_network.c" \
  -lpthread -o "$root/.build/test-network-event-loop"
python3 - "$root/.build/test-network-event-loop" <<'PY'
import subprocess, sys
subprocess.run([sys.argv[1]], timeout=30, check=True)
PY
