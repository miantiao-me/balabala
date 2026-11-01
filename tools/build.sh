#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$root/tools/env.sh"
autotest_phase=${BALABALA_AUTOTEST_PHASE:-1}
case "$autotest_phase" in 1|2) ;; *) echo "invalid autotest phase" >&2; exit 1 ;; esac
autotest_output=${BALABALA_AUTOTEST_OUT:-"$root/.build/autotest-phase$autotest_phase"}
python3 "$root/tools/apply-patches.py"
python3 "$root/tools/fetch-components.py"
mkdir -p "$root/.build/generated" "$root/.build/native"
# 独立工作区及 lockfile 放在忽略目录，所有生成内容均不进入源码树。
cp "$root/native/Cargo.toml" "$root/.build/native/Cargo.toml"
lock="$root/.build/native/Cargo.lock"
if [ ! -f "$lock" ] || [ "$(shasum -a 256 "$lock" | cut -d ' ' -f 1)" != "$CARGO_LOCK_SHA256" ]; then
  # 沿用固定上游依赖版本；Cargo 仅补探针根包并裁去未使用条目。
  cp "$root/.tools/pocketjs/Cargo.lock" "$lock"
  rustup run "$RUST_VERSION" cargo fetch --manifest-path "$root/.build/native/Cargo.toml"
  [ "$(shasum -a 256 "$lock" | cut -d ' ' -f 1)" = "$CARGO_LOCK_SHA256" ] || {
    echo 'Resolved Cargo.lock differs from verified probe dependencies' >&2; exit 1;
  }
fi
mkdir -p "$root/.tools/fonts"
for weight in Regular:$NOTO_SHA256; do
  font="$root/.tools/fonts/NotoSansCJKsc-${weight%%:*}.otf"
  if [ ! -f "$font" ]; then
    curl -fL --retry 2 "https://raw.githubusercontent.com/notofonts/noto-cjk/$NOTO_COMMIT/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-${weight%%:*}.otf" -o "$font"
  fi
  [ "$(shasum -a 256 "$font" | cut -d ' ' -f 1)" = "${weight#*:}" ] || { echo "Noto ${weight%%:*} SHA256 mismatch" >&2; exit 1; }
done
sh "$root/tools/fetch-ui-font.sh"
# 插画 SVG 仅在构建期栅格化；依赖装在忽略目录，lockfile 的 integrity 校验包内容。
svg="$root/.tools/svg"
svg_lock=$(shasum -a 256 "$root/tools/svg/bun.lock" | cut -d ' ' -f 1)
if [ "$(cat "$svg/.balabala-lock" 2>/dev/null)" != "$svg_lock" ]; then
  mkdir -p "$svg"
  cp "$root/tools/svg/package.json" "$root/tools/svg/bun.lock" "$svg/"
  (cd "$svg" && bun install --frozen-lockfile --ignore-scripts)
  printf '%s\n' "$svg_lock" > "$svg/.balabala-lock"
fi
python3 "$root/tools/fetch-learning-data.py"
export BALABALA_GENERATED="$root/.build/generated"
bun "$root/tools/build-native.ts"
command=${1:-build}
case "$command" in
  test)
    shift
    rustup run "$RUST_VERSION" cargo test --locked --manifest-path "$root/.build/native/Cargo.toml" --lib "$@"
    sh "$root/tools/test-platform.sh"
    ;;
  build|build-autotest)
    # C3 为单核机器模式；portable-atomic 在 CAS 回退中保存/屏蔽/恢复中断。
    # 只设置交叉目标 flags，不把 RISC-V 假设带到宿主测试。
    export CARGO_TARGET_RISCV32IMC_UNKNOWN_NONE_ELF_RUSTFLAGS='--cfg portable_atomic_unsafe_assume_single_core -C panic=abort -C relocation-model=static'
    firmware_build="$root/.build/firmware"
    firmware_config="$root/.build/sdkconfig"
    autotest_rust=OFF
    rust_feature=
    if [ "$command" = build-autotest ]; then
      firmware_build="$autotest_output/firmware"
      firmware_config="$autotest_output/sdkconfig"
      export CARGO_TARGET_DIR="$autotest_output/rust-target"
      autotest_rust=ON
      rust_feature=--features=autotest
      mkdir -p "$autotest_output"
    fi
    # 显式覆盖已有配置；仅 defaults 不能覆盖已有 sdkconfig 的旧值。
    python3 "$root/tools/configure-firmware.py" "$root/.build/sdkconfig" "$firmware_config" "$autotest_rust" "$autotest_phase"
    rustup run "$RUST_VERSION" cargo build --locked --manifest-path "$root/.build/native/Cargo.toml" --target riscv32imc-unknown-none-elf --release $rust_feature
    "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" -C "$root/firmware" -B "$firmware_build" \
      -D "SDKCONFIG=$firmware_config" -D "BALABALA_AUTOTEST_RUST=$autotest_rust" \
      -D "RUST_ARCHIVE=$CARGO_TARGET_DIR/riscv32imc-unknown-none-elf/release/libbalabala_probe.a" build size
    python3 "$root/tools/check-autotest-build.py" "$firmware_build" "$firmware_config" "$autotest_rust" "$autotest_phase"
    ;;
  test-autotest)
    export CARGO_TARGET_DIR="$autotest_output/rust-target-host"
    rustup run "$RUST_VERSION" cargo test --locked --manifest-path "$root/.build/native/Cargo.toml" --features autotest --lib autotest::
    sh "$root/tools/test-autotest.sh"
    ;;
  *) echo 'usage: sh tools/build.sh test [cargo-test-args...] | test-autotest | build | build-autotest' >&2; exit 1 ;;
esac
