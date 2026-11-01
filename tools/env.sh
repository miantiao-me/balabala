# 由 build.sh source，root 已指向项目根目录。
. "$root/tools/versions.sh"
export CARGO_HOME="$root/.tools/cargo" RUSTUP_HOME="$root/.tools/rustup"
export CARGO_TARGET_DIR="$root/.build/rust-target"
export BUN_INSTALL_CACHE_DIR="$root/.tools/bun-cache"
command -v shasum >/dev/null 2>&1 || { echo 'Missing prerequisite: shasum' >&2; exit 1; }
unset IDF_PYTHON_ENV_PATH
export IDF_PATH="$root/.tools/esp-idf" IDF_TOOLS_PATH="$root/.tools/idf-tools"
export IDF_TARGET=esp32c3 ESP_IDF_VERSION="$IDF_VERSION"
export PATH="$root/.tools/bun:$PATH"
python3 "$root/tools/apply-idf-patches.py" || return 1
# IDF 自己解析工具及 Python 环境，避免写死宿主的 Python 小版本路径。
if balabala_idf_exports=$(python3 "$IDF_PATH/tools/idf_tools.py" export --format shell); then
  eval "$balabala_idf_exports"
  unset balabala_idf_exports
else
  echo 'ESP-IDF environment export failed' >&2
  return 1
fi
