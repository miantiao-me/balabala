#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$root/tools/versions.sh"
command -v shasum >/dev/null 2>&1 || { echo 'Missing prerequisite: shasum' >&2; exit 1; }
unset IDF_PYTHON_ENV_PATH
export IDF_PATH="$root/.tools/esp-idf" IDF_TOOLS_PATH="$root/.tools/idf-tools"
export CARGO_HOME="$root/.tools/cargo" RUSTUP_HOME="$root/.tools/rustup"
export BUN_INSTALL_CACHE_DIR="$root/.tools/bun-cache"
mkdir -p "$root/.tools/downloads" "$root/.build"

archive="$root/.tools/downloads/pocketjs.tar.gz"
if [ ! -f "$archive" ]; then
  curl -fL --retry 2 "https://codeload.github.com/pocket-nexus/pocketjs/tar.gz/$POCKETJS_COMMIT" -o "$archive"
fi
actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
[ "$actual" = "$POCKETJS_SHA256" ] || { echo 'PocketJS SHA256 mismatch' >&2; exit 1; }
if [ ! -f "$root/.tools/pocketjs/.balabala-version" ]; then
  mkdir -p "$root/.tools/pocketjs"
  tar xzf "$archive" -C "$root/.tools/pocketjs" --strip-components=1
  printf '%s\n' "$POCKETJS_COMMIT" > "$root/.tools/pocketjs/.balabala-version"
fi
[ "$(cat "$root/.tools/pocketjs/.balabala-version")" = "$POCKETJS_COMMIT" ]

python3 "$root/tools/apply-patches.py"
python3 "$root/tools/fetch-components.py"

# rustup 本身须预装；隔离安装，不改变用户的默认工具链。
rustup toolchain install "$RUST_VERSION" --profile minimal --no-self-update
rustup target add --toolchain "$RUST_VERSION" riscv32imc-unknown-none-elf

if [ ! -x "$root/.tools/bun/bun" ]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) bun_platform=darwin-aarch64 ;;
    Linux-aarch64) bun_platform=linux-aarch64 ;;
    Linux-x86_64) bun_platform=linux-x64 ;;
    *) echo 'Bun bootstrap supports macOS arm64 / Linux arm64 or x64' >&2; exit 1 ;;
  esac
  curl -fL --retry 2 "https://github.com/oven-sh/bun/releases/download/bun-v$BUN_VERSION/bun-$bun_platform.zip" -o "$root/.tools/downloads/bun.zip"
  mkdir -p "$root/.tools/bun"
  unzip -oq "$root/.tools/downloads/bun.zip" -d "$root/.tools/bun"
  cp "$root/.tools/bun/bun-$bun_platform/bun" "$root/.tools/bun/bun"
fi
[ "$("$root/.tools/bun/bun" --version)" = "$BUN_VERSION" ]
(cd "$root/.tools/pocketjs" && "$root/.tools/bun/bun" install --frozen-lockfile --production --ignore-scripts)

if [ ! -d "$IDF_PATH/.git" ]; then
  git clone --branch "v$IDF_VERSION" --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git "$IDF_PATH"
else
  python3 "$root/tools/apply-idf-patches.py"
fi
[ "$(git -C "$IDF_PATH" rev-parse HEAD)" = "$IDF_COMMIT" ]
git -C "$IDF_PATH" submodule update --init --recursive --depth 1
python3 "$root/tools/apply-idf-patches.py"
python3 "$IDF_PATH/tools/idf_tools.py" install --targets esp32c3
python3 "$IDF_PATH/tools/idf_tools.py" install-python-env
printf 'Setup complete: PocketJS %s, IDF %s, Rust %s, Bun %s\n' "$POCKETJS_COMMIT" "$IDF_VERSION" "$RUST_VERSION" "$BUN_VERSION"
