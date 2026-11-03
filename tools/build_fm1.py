#!/usr/bin/env python3
# OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
"""Build the FM-1 package: build/fm1/ondadream-VER.fwsc (the app + the BSP's update loader).

  JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/ac79 tools/build_fm1.py [--version 1.0]

The JieLi toolchain (clang for pi32v2) and three Apache-2.0 files of the JieLi AC79 SDK (that fm1pkg.py puts in
the package) come from JieLi. The update loader is felucca-bsp's (bsp/loader, MPL-2.0)."""
import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

import lz4.block

ROOT = Path(__file__).resolve().parent.parent
BSP = ROOT / "bsp"
OUT = ROOT / "build" / "fm1"
TC = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain"))
CC = [str(TC / "pi32v2/bin/clang"), "-target", "pi32v2"]
INC = [f"-I{BSP / 'chip'}", f"-I{BSP / 'boards/fm1'}"]
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"            # the file name the SPL looks for
APP_BASE = 0x02000120


def run(*cmd, capture=False):
    cmd = [str(c) for c in cmd]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        if os.environ.get("GITHUB_ACTIONS"):          # (the logs need a login; annotations do not)
            msg = (r.stdout + r.stderr).replace("%", "%25").replace("\r", "").replace("\n", "%0A")[:4000]
            print(f"::error title={Path(cmd[0]).name}::{msg}")
        raise SystemExit(f"failed: {' '.join(cmd[:3])} ...")
    if r.stderr.strip() and not capture:
        sys.stdout.write(r.stderr)
    return r.stdout


def sym(syms, name):
    m = re.search(rf"^([0-9a-f]+) .*\s{re.escape(name)}$", syms, re.M)
    return int(m.group(1), 16) if m else None


def build_app(product):
    o = OUT / "app"
    o.mkdir(parents=True, exist_ok=True)
    for s in ("crt0.S", "fm1_vec.S", "fm1_isr.S"):
        run(*CC, "-c", BSP / "chip" / s, "-o", o / (s[:-2] + ".o"))
    # -mcpu=r3: the FPU, as the JieLi SDK and other FM-1 firmwares use it
    run(*CC, "-mcpu=r3", "-O2", "-ffunction-sections", "-fno-builtin", "-fno-stack-protector", "-Wall", "-Wno-unused-function",
        *INC, f"-I{ROOT / 'src/core'}", f"-I{ROOT / 'src/gen'}", f"-I{ROOT / 'src/fm1'}", f'-DOD_PRODUCT="{product}"',
        "-c", ROOT / "src/fm1/main.c", "-o", o / "main.o")
    objs = [o / "crt0.o", o / "fm1_vec.o", o / "fm1_isr.o", o / "main.o"]
    elf = o / "ondadream.elf"
    libs = [p for p in os.environ.get("OD_LIBS", "").split(":") if p]
    run(TC / "pi32v2/bin/ld", "-T", ROOT / "src/fm1/app.ld", *objs, *libs,
        "-o", elf)
    for sect, name in ((".text", "text.bin"), (".data", "data.bin"), (".ram_text", "rt.bin")):
        run(TC / "common/bin/objcopy", "-O", "binary", "-j", sect, elf, o / name)
    syms = run(TC / "common/bin/objdump", "-t", elf, capture=True)
    (o / "ondadream.sym").write_text(syms)
    (o / "ondadream.dis").write_text(run(TC / "common/bin/objdump", "-d", elf, capture=True))
    dis = (o / "ondadream.dis").read_text()
    calls = sorted(set(re.findall(r"call\b[^<\n]*<(__\w+)", dis)))
    note = f"app: helper calls {' '.join(calls) or 'none'}; f-ops {' '.join(sorted(set(re.findall(r'\t(f[a-z.0-9]+)\b', dis)))[:30])}"
    print(note)
    if os.environ.get("GITHUB_ACTIONS"):
        print(f"::warning title=app::{note}")
    assert sym(syms, "_start") == APP_BASE, "the entry is not at the start of the app slot"
    img = bytearray((o / "text.bin").read_bytes())
    for at, part in sorted([(sym(syms, "_rt_load"), (o / "rt.bin").read_bytes()),
                            (sym(syms, "_data_load"), (o / "data.bin").read_bytes())]):
        if part:
            assert at - APP_BASE >= len(img), "sections overlap"
            img += b"\xff" * (at - APP_BASE - len(img)) + part
    img += b"\xff" * (-len(img) % 4)
    bss_end, pool_end = sym(syms, "_bss_end"), sym(syms, "_pool_end")
    where = " ".join(f"{n}={sym(syms, n) or 0:#x}" for n in ("rowbuf", "abuf", "ep1rx", "fbuf", "dl_l", "fm1_crash", "od_guard",
                                                           "od_diag", "fm1_crash_ctx", "bootguard", "fm1_led", "fm1_led_mid", "fm1_led_dim"))
    note = f"app: {len(img)} B in flash; .bss to {bss_end:#x} (< 0x1c1a000), pool to {pool_end:#x} (< 0x1c74000); {where}"
    print(note)
    if os.environ.get("GITHUB_ACTIONS"):
        print(f"::warning title=memory::{note}")
    if bss_end > 0x01C1A000 or pool_end > 0x01C74000:
        raise SystemExit("app: RAM overflows")
    (OUT / "app.bin").write_bytes(img)
    return bytes(img)


def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def jlfs_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def build_loader():
    o = OUT / "loader"
    o.mkdir(parents=True, exist_ok=True)
    run(*CC, "-c", BSP / "loader/crt0_ldr.S", "-o", o / "crt0_ldr.o")
    run(*CC, "-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function", *INC,
        "-c", BSP / "loader/loader.c", "-o", o / "loader.o")
    elf = o / "loader.elf"
    run(TC / "pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", BSP / "loader/loader.ld",
        o / "crt0_ldr.o", o / "loader.o", "-o", elf)
    run(TC / "common/bin/objcopy", "-O", "binary", "-j", ".text", elf, o / "loader.bin")
    for ln in run(TC / "common/bin/objdump", "-h", elf, capture=True).splitlines():
        p = ln.split()                          # the loader rewrites the flash: nothing may run from XIP
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} is outside RAM")
    image = (o / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    blocks = bytearray()                        # [clen u32][dlen u32][LZ4 block] per 4 KiB
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4.block.compress(raw, mode="high_compression", compression=12, store_size=False)
        if len(c) > 4096 or lz4.block.decompress(c, uncompressed_size=len(raw)) != raw:
            raise SystemExit("loader: a block does not compress below 4 KiB")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = jlfs_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    ota = jlfs_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body
    (OUT / "ota.bin").write_bytes(ota)
    print(f"loader: {len(image)} B, ota.bin {len(ota)} B")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="1.0")
    a = ap.parse_args()
    m = re.fullmatch(r"(\d)\.(\d)", a.version)
    if not m:
        raise SystemExit("--version X.Y")
    product = f"FM-1_9{m[1]}{m[2]}"
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, ROOT / "tools/gen_tables.py", ROOT / "src/gen/od_tables.h"], check=True)   # (the fonts: src/gen/od_fonts.h, made by tools/gen_fonts.js)
    build_app(product)
    build_loader()
    pkg = OUT / f"ondadream-{a.version}.fwsc"
    subprocess.run([sys.executable, BSP / "tools/fm1pkg.py", OUT / "app.bin", OUT / "ota.bin", pkg,
                    "--product", product], check=True)
    print(f"package: {pkg} ({product})")


if __name__ == "__main__":
    main()
