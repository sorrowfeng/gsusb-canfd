#!/usr/bin/env bash
# build_windows.sh - one-shot Windows build for gsusb-canfd.
#
# Tested with the MinGW-w64 toolchain shipped with Qt (C:/Qt/Tools/mingw1310_64)
# plus Ninja, driven from Git Bash. Nothing outside this repository is required
# except a compiler, CMake, Ninja (or mingw32-make) and 7-Zip.
#
# Usage:
#   ./scripts/build_windows.sh
#
# Environment overrides (all optional):
#   LIBUSB_VER      libusb release to download            (default: 1.0.30)
#   MINGW_DIR       MinGW toolchain root, e.g. C:/Qt/Tools/mingw1310_64
#   NINJA_DIR       directory containing ninja.exe
#   CMAKE_BIN       full path to cmake.exe
#   SEVENZIP        full path to 7z.exe
#   HTTP_PROXY_URL  proxy for the download, e.g. http://127.0.0.1:7890
#   BUILD_DIR       build directory                       (default: <repo>/build)
#
# libusb is unpacked into <repo>/.build/libusb; the build uses
# -DCANFD_STATIC_RUNTIME=ON so the executables depend on libusb-1.0.dll only
# (that DLL is copied next to them automatically).

set -euo pipefail

LIBUSB_VER="${LIBUSB_VER:-1.0.30}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="$ROOT/.build"
DL="$CACHE/dl"
TP="$CACHE/libusb"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

# --------------------------------------------------------------- environment
# Git Bash does not always have coreutils on PATH, and bash needs POSIX style
# paths for PATH entries while the native tools need Windows style ones.
to_posix() { cygpath -u "$1" 2>/dev/null || printf '%s' "$1"; }
to_windows() { cygpath -w "$1" 2>/dev/null || printf '%s' "$1"; }

first_existing() {
  local candidate
  for candidate in "$@"; do
    if [[ -n "$candidate" && -e "$candidate" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done
  return 1
}

# A nested bash can lose TMP/TEMP; MinGW's ld then falls back to C:\Windows and
# fails with "Cannot create temporary file in C:\Windows\: Permission denied".
TMP_DIR="${TMPDIR:-${TEMP:-${TMP:-}}}"
if [[ -z "$TMP_DIR" || ! -d "$TMP_DIR" ]]; then
  TMP_DIR="$(to_windows "${LOCALAPPDATA:-$HOME/AppData/Local}/Temp")"
fi
[[ -d "$TMP_DIR" ]] || { echo "error: no usable temporary directory" >&2; exit 1; }
export TMPDIR="$TMP_DIR" TMP="$TMP_DIR" TEMP="$TMP_DIR"

CMAKE_BIN="${CMAKE_BIN:-$(first_existing \
  "$(command -v cmake 2>/dev/null || true)" \
  "$(command -v cmake.exe 2>/dev/null || true)" \
  "C:/Program Files/CMake/bin/cmake.exe" \
  "C:/Program Files (x86)/CMake/bin/cmake.exe" || true)}"
[[ -n "$CMAKE_BIN" ]] || { echo "error: cmake not found; set CMAKE_BIN" >&2; exit 1; }

CXX_BIN="${CXX_BIN:-$(first_existing \
  "${MINGW_DIR:+$MINGW_DIR/bin/g++.exe}" \
  "$(command -v g++ 2>/dev/null || true)" \
  "C:/Qt/Tools/mingw1310_64/bin/g++.exe" || true)}"
[[ -n "$CXX_BIN" ]] || {
  echo "error: g++ not found; set MINGW_DIR or CXX_BIN" >&2; exit 1; }
MINGW_BIN_DIR="$(dirname "$CXX_BIN")"

NINJA_BIN="$(first_existing \
  "${NINJA_DIR:+$NINJA_DIR/ninja.exe}" \
  "$(command -v ninja 2>/dev/null || true)" \
  "C:/Qt/Tools/Ninja/ninja.exe" || true)"
MAKE_BIN="$(first_existing \
  "$MINGW_BIN_DIR/mingw32-make.exe" \
  "$(command -v mingw32-make 2>/dev/null || true)" || true)"

if [[ -n "$NINJA_BIN" ]]; then
  GENERATOR=(-G Ninja)
else
  [[ -n "$MAKE_BIN" ]] || {
    echo "error: neither ninja nor mingw32-make found; set NINJA_DIR" >&2; exit 1; }
  GENERATOR=(-G "MinGW Makefiles" "-DCMAKE_MAKE_PROGRAM=$(to_windows "$MAKE_BIN")")
fi

SEVENZIP="${SEVENZIP:-$(first_existing \
  "C:/Program Files/7-Zip/7z.exe" \
  "C:/Program Files (x86)/7-Zip/7z.exe" \
  "$(command -v 7z 2>/dev/null || true)" || true)}"

# The MinGW bin directory has to be on PATH as a POSIX path, otherwise bash
# cannot see the directory at all and g++ fails to locate cc1plus.
export PATH="$(to_posix "$MINGW_BIN_DIR"):$(to_posix "$(dirname "$CMAKE_BIN")"):$PATH"
[[ -n "$NINJA_BIN" ]] && export PATH="$(to_posix "$(dirname "$NINJA_BIN")"):$PATH"

echo "==> repo       : $ROOT"
echo "==> compiler   : $CXX_BIN"
echo "==> cmake      : $CMAKE_BIN"
echo "==> generator  : ${GENERATOR[0]} ${GENERATOR[1]:-}"
echo "==> temp dir   : $TMP_DIR"

mkdir -p "$DL" "$TP/include/libusb-1.0" "$TP/lib" "$TP/bin" "$BUILD_DIR"

# ------------------------------------------------------------------ 1. libusb
if [[ ! -f "$TP/include/libusb-1.0/libusb.h" || ! -f "$TP/bin/libusb-1.0.dll" ]]; then
  echo "==> fetching libusb $LIBUSB_VER"
  [[ -n "$SEVENZIP" ]] || { echo "error: 7z not found; set SEVENZIP" >&2; exit 1; }

  ARCHIVE="$DL/libusb-$LIBUSB_VER.7z"
  URL="https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.7z"

  # Drop any inherited proxy settings that may be dead; use HTTP_PROXY_URL if set.
  unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy

  if [[ ! -f "$ARCHIVE" ]]; then
    if [[ -n "${HTTP_PROXY_URL:-}" ]]; then
      curl -fSL --retry 3 -x "$HTTP_PROXY_URL" -o "$ARCHIVE" "$URL"
    else
      curl -fSL --retry 3 -o "$ARCHIVE" "$URL"
    fi
  fi

  echo "==> unpacking"
  "$SEVENZIP" x -y -o"$DL" "$ARCHIVE" >/dev/null
  cp "$DL/include/libusb.h"                "$TP/include/libusb-1.0/"
  cp "$DL/MinGW64/dll/libusb-1.0.dll"      "$TP/bin/"
  cp "$DL/MinGW64/static/libusb-1.0.dll.a" "$TP/lib/"
else
  echo "==> libusb already cached, skipping download"
fi

# --------------------------------------------------------------- 2. configure
echo "==> configuring"
# CMake is a native Windows binary: it cannot resolve Git Bash's /c/... paths,
# so every path handed to it (or to ctest) must be converted first. ROOT comes
# from `pwd` and is therefore POSIX; BUILD_DIR may be given either way.
"$CMAKE_BIN" -S "$(to_windows "$ROOT")" -B "$(to_windows "$BUILD_DIR")" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$(to_windows "$CXX_BIN")" \
  -DCANFD_LIBUSB_INCLUDE_DIR="$(to_windows "$TP/include/libusb-1.0")" \
  -DCANFD_LIBUSB_LIBRARY="$(to_windows "$TP/lib/libusb-1.0.dll.a")" \
  -DCANFD_STATIC_RUNTIME=ON

# ------------------------------------------------------------------- 3. build
echo "==> building"
"$CMAKE_BIN" --build "$(to_windows "$BUILD_DIR")" -j

# ------------------------------------------------------------- 4. deploy dll
cp "$TP/bin/libusb-1.0.dll" "$BUILD_DIR/"
echo "==> copied libusb-1.0.dll into $BUILD_DIR"

# --------------------------------------------------------------- 5. self-test
echo "==> unit tests"
ctest --test-dir "$(to_windows "$BUILD_DIR")" --output-on-failure

echo "==> attached adapters"
"$(to_windows "$BUILD_DIR")\\canfd.exe" list || true

cat <<EOF

Build finished. Artifacts in $BUILD_DIR:
  canfd.exe              CLI (list / send / monitor)
  canfd_term.exe         interactive terminal
  canfd_demo.exe         hardware demo (trigger 0x501 -> receive 0x481)
  test_bit_timing.exe    unit test
  test_frame.exe         unit test

The executables need libusb-1.0.dll beside them (copied automatically). Under
Windows the adapter must be bound to WinUSB; candleLight-compatible adapters
usually ship that way, so Zadig is normally not needed.
EOF
