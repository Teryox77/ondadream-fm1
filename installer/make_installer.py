#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca web/make_site.py)
# Copyright (C) 2026 Teryox77 (changed for OndaDream: no editor, the OndaDream package and licences)
"""The installer page for the site (Felucca's web installer, GPL-3.0, like the files next to this one):

  installer/make_installer.py PACKAGE.fwsc VERSION SITE_DIR
    -> SITE_DIR/webapp/installer/index.html, SITE_DIR/firmware/ondadream-VERSION.fwsc + LICENSES/
"""
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not FM-1_9xx")
    if b"FELUCCA-LOADER-1" not in raw:              # the BSP's loader: no vendor files in the package
        raise SystemExit(f"{pkg}: no felucca-bsp loader in it")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js", "fm1backup.js"))
    name = f"ondadream-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, fw = out / "webapp" / "installer", out / "firmware"
    for d in (inst, fw / "LICENSES"):
        d.mkdir(parents=True, exist_ok=True)
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    lic = HERE.parent / "LICENSES"
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>OndaDream licences</title><h1>Licence texts</h1>'
        '<p>OndaDream: © 2026 Teryox77, all rights reserved (<a href="../LICENSE">LICENSE</a>, '
        '<a href="../NOTICE">NOTICE</a>).</p><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names) + "</ul>\n", encoding="utf-8")
    for doc in ("LICENSE", "NOTICE"):
        shutil.copy(HERE.parent / doc, fw / doc)
    print(f"installer: {inst} ({len(html)} B), firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
