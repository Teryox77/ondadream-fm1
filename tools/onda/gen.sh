#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# OndaDream: build/gen (tables, fonts, icons, samples) without the JieLi toolchain: enough for the browser
# emulator (tools/onda/build_emu.sh) and the host UI renders (tests/ui_render.c)
set -e
cd "$(dirname "$0")/../.."
python3 -c 'import sys; sys.argv = ["build.py"]; sys.path.insert(0, "tools"); import build; build.generate()'
