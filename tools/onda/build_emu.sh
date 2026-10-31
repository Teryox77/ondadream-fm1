#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# OndaDream: the browser emulator (web/emu) built with clang + the WASI sysroot instead of Emscripten.
#   tools/onda/build_emu.sh   ->  build/emu/{index.html, worklet.js, felucca.wasm, fonts/}
# Needs build/gen (./build.sh, or the generators alone: tools/onda/gen.sh), clang and wasm-ld (lld).
# WASI_SDK_DIR: where the sysroot and the wasm32 builtins are (downloaded once from WebAssembly/wasi-sdk 24).
set -e
cd "$(dirname "$0")/../.."
W="${WASI_SDK_DIR:-$HOME/.wasi}"
CLANG="${CLANG:-clang}"
if [ ! -d "$W/wasi-sysroot-24.0" ]; then
    mkdir -p "$W"
    for f in wasi-sysroot-24.0.tar.gz libclang_rt.builtins-wasm32-wasi-24.0.tar.gz; do
        curl -fsSL -o "$W/$f" "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-24/$f"
        tar xzf "$W/$f" -C "$W"
    done
fi
RES="$($CLANG -print-resource-dir)"
if [ ! -f "$RES/lib/wasi/libclang_rt.builtins-wasm32.a" ]; then
    SUDO=""; [ -w "$RES" ] || SUDO=sudo
    $SUDO mkdir -p "$RES/lib/wasi"
    $SUDO cp "$W/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a" "$RES/lib/wasi/"
fi
STUB="$W/stub"
mkdir -p "$STUB/sys"
cat > "$STUB/sys/wait.h" <<'H'
#pragma once
/* the host tests' process helpers (tests/hostsim.c): never called in the browser */
static inline int pipe(int *f) { (void)f; return -1; }
static inline int fork(void) { return -1; }
static inline int waitpid(int p, int *s, int o) { (void)p; (void)s; (void)o; return -1; }
#define WIFEXITED(s) 1
#define WEXITSTATUS(s) 0
H
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c | head -n 1)
ONDA=$(sed -n 's/^#define ONDA_VERSION "\([^"]*\)".*/\1/p' firmware/src/ui_draw.c | head -n 1)
mkdir -p build/emu/fonts
$CLANG --target=wasm32-wasi --sysroot="$W/wasi-sysroot-24.0" -O2 -ffp-contract=off -std=gnu99 -w -I"$STUB" \
    "-DFELUCCA_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 \
    -mexec-model=reactor -Wl,--export-dynamic -Wl,--gc-sections -Wl,--initial-memory=33554432 -Wl,-z,stack-size=1048576 \
    -o build/emu/felucca.wasm web/emu/felucca_web.c
sed -e "s/@VERSION@/OndaDream $ONDA · Felucca $VERSION/g" -e "s/@PREVIEW@//g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
echo "emu: build/emu ($(wc -c < build/emu/felucca.wasm | tr -d ' ') B wasm, OndaDream $ONDA)"
