/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The core in one translation unit: the synth, the drawing, the UI. */
#include "od.h"
#include "od_tables.h"
static int od_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
#include "synth.c"
#include "gfx.c"
#include "ui.c"
