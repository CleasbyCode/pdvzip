#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CXX="${CXX:-g++}"
OPT_LEVEL="${OPT_LEVEL:--O3}"
OUTPUT="${PDVZIP_OUTPUT-pdvzip}"
FORTIFY_LEVEL="${PDVZIP_FORTIFY_LEVEL:-3}"

if [[ -z "$OUTPUT" ]]; then
  echo "PDVZIP_OUTPUT must not be empty." >&2
  exit 1
fi

OUTPUT_DIR="$(dirname -- "$OUTPUT")"
OUTPUT_NAME="$(basename -- "$OUTPUT")"
if [[ ! -d "$OUTPUT_DIR" ]]; then
  echo "Output directory does not exist: $OUTPUT_DIR" >&2
  exit 1
fi
if [[ -d "$OUTPUT" ]]; then
  echo "Output path is a directory: $OUTPUT" >&2
  exit 1
fi

case "$OPT_LEVEL" in
  -O0|-O1|-O2|-O3|-Og|-Os|-Oz|-Ofast) ;;
  *)
    echo "Unsupported OPT_LEVEL: $OPT_LEVEL" >&2
    echo "Expected one of: -O0 -O1 -O2 -O3 -Og -Os -Oz -Ofast" >&2
    exit 1
    ;;
esac

case "$FORTIFY_LEVEL" in
  2|3) ;;
  *)
    echo "Unsupported PDVZIP_FORTIFY_LEVEL: $FORTIFY_LEVEL (expected 2 or 3)." >&2
    exit 1
    ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
  echo "Missing required build tool: cmake" >&2
  exit 1
fi
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "Missing required C++ compiler: $CXX" >&2
  exit 1
fi
if ! command -v flock >/dev/null 2>&1; then
  echo "Missing required build lock tool: flock (provided by util-linux)." >&2
  exit 1
fi

CXX_PATH="$(command -v "$CXX")"
COMPILER_NAME="$(basename -- "$CXX")"
BUILD_KEY="${COMPILER_NAME}-${OPT_LEVEL#-}"
BUILD_KEY="${BUILD_KEY//[^[:alnum:]._-]/_}"

if [[ -n "${PDVZIP_BUILD_DIR:-}" ]]; then
  BUILD_DIR="$PDVZIP_BUILD_DIR"
else
  BUILD_DIR="$SCRIPT_DIR/build/$BUILD_KEY"
fi

JOBS="${PDVZIP_JOBS:-}"
if [[ -z "$JOBS" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS=4
  fi
  if (( JOBS > 8 )); then
    JOBS=8
  fi
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "PDVZIP_JOBS must be a positive integer." >&2
  exit 1
fi

cmake -E make_directory "$BUILD_DIR"
exec {BUILD_LOCK_FD}>"$BUILD_DIR/.pdvzip-build.lock"
flock "$BUILD_LOCK_FD"

GENERATOR_ARGS=()
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
  elif command -v make >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G "Unix Makefiles")
  else
    echo "Missing required build backend: install Ninja or Make." >&2
    exit 1
  fi
fi

configure_build() {
  local fortify_level="$1"
  cmake \
    "${GENERATOR_ARGS[@]}" \
    -S "$SCRIPT_DIR" \
    -B "$BUILD_DIR" \
    "-DCMAKE_CXX_COMPILER=$CXX_PATH" \
    "-DPDVZIP_OPT_LEVEL=$OPT_LEVEL" \
    "-DPDVZIP_FORTIFY_LEVEL=$fortify_level" \
    -DPDVZIP_ENABLE_LTO=ON
}

build_target() {
  cmake --build "$BUILD_DIR" --parallel "$JOBS" --target pdvzip
}

echo "Compiling pdvzip (parallel incremental hardened build, ${OPT_LEVEL}, ${JOBS} jobs)..."
configure_build "$FORTIFY_LEVEL"
if ! build_target; then
  if [[ "$FORTIFY_LEVEL" != 3 ]]; then
    exit 1
  fi
  echo "Retrying with -D_FORTIFY_SOURCE=2 for compatibility..."
  FORTIFY_LEVEL=2
  configure_build "$FORTIFY_LEVEL"
  build_target
fi

BUILT_BINARY="$BUILD_DIR/pdvzip"
if [[ ! -x "$BUILT_BINARY" ]]; then
  echo "Build Error: CMake did not produce an executable pdvzip binary." >&2
  exit 1
fi

if [[ ! -f "$OUTPUT" ]] || ! cmp -s -- "$BUILT_BINARY" "$OUTPUT"; then
  # Publish beside the destination only after the linker has produced a complete
  # binary. A failed build therefore cannot truncate the last usable executable.
  TEMP_OUTPUT="$(mktemp "$OUTPUT_DIR/.${OUTPUT_NAME}.tmp.XXXXXX")"
  trap 'rm -f -- "$TEMP_OUTPUT"' EXIT
  cp -- "$BUILT_BINARY" "$TEMP_OUTPUT"
  chmod --reference="$BUILT_BINARY" "$TEMP_OUTPUT"
  mv -f -- "$TEMP_OUTPUT" "$OUTPUT"
  trap - EXIT
fi
echo "Compilation successful. Executable '$OUTPUT' is up to date."
