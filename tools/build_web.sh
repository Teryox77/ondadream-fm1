#!/bin/sh
# OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
# The browser emulator: src/core in WebAssembly (clang + the WASI sysroot) and the page.
#   tools/build_web.sh   ->  build/web/{index.html, ondadream.wasm}
set -e
cd "$(dirname "$0")/.."
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
mkdir -p build/web
$CLANG --target=wasm32-wasi --sysroot="$W/wasi-sysroot-24.0" -O2 -std=gnu99 -Wall -Wno-unused-function -Isrc/gen \
    -mexec-model=reactor -Wl,--export-dynamic -Wl,--gc-sections -Wl,--initial-memory=4194304 \
    -o build/web/ondadream.wasm web/od_web.c
cp web/index.html build/web/index.html
echo "web: build/web ($(wc -c < build/web/ondadream.wasm | tr -d ' ') B wasm)"
