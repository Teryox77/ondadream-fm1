#!/usr/bin/env python3
"""OndaDream presets -> Felucca library file (ANALOG engine), importable in Felucca's web editor."""
import json, math, re, sys, datetime
from pathlib import Path

SRC = Path("/home/claude/src/felucca/firmware/src")
core = (SRC / "core.h").read_text()
params = (SRC / "params.c").read_text()

# ---- per-track parameter ids, in enum order
body = core.split("enum {                          /* per-track parameters */", 1)[1].split("P_COUNT", 1)[0]
body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
ids = [x.strip() for x in body.replace("\n", " ").split(",") if x.strip()]
assert ids[0] == "P_LEVEL" and ids[-1] == "P_E7", ids[-3:]
PE0 = ids.index("P_E0")
PCOUNT = len(ids)

# ---- labels and defaults from TP[]
tp = params.split("static const param_desc_t TP[P_COUNT] = {", 1)[1].split("};", 1)[0]
desc = {}
for m in re.finditer(r"\[(P_\w+)\]\s*=\s*PD\(\"([^\"]+)\",\s*\w+,\s*(-?\d+),\s*(-?\w+),\s*(-?\d+)\)", tp):
    desc[m.group(1)] = (m.group(2), int(m.group(5)))
for m in re.finditer(r"\[(P_\w+)\]\s*=\s*PE\(\"([^\"]+)\",\s*\w+,\s*(-?\d+)\)", tp):
    desc[m.group(1)] = (m.group(2), int(m.group(3)))
for k in "1234":
    desc[f"P_M{k}SRC"] = (f"SRC{k}", 0); desc[f"P_M{k}DST"] = (f"DST{k}", 0); desc[f"P_M{k}AMT"] = (f"AMT{k}", 0)
    for s, l, d in (("ATK", "ATK", 0), ("DEC", "DEC", 0), ("SUS", "SUS", 127), ("REL", "REL", 0), ("LEVEL", "LVL", 127)):
        desc[f"P_FM{k}_{s}"] = (l, d)
for i, l in enumerate(["KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"]):
    desc[f"P_LN{i}"] = (l, 127)
missing = [i for i in ids[:PE0] if i not in desc]
assert not missing, missing

keys, seen = [], {}
for i in ids[:PE0]:
    l = desc[i][0]
    seen[l] = seen.get(l, 0) + 1
    keys.append(l if seen[l] == 1 else f"{l}#{seen[l]}")
keys += [f"E{k}" for k in range(8)]
DEF = [desc[i][1] for i in ids[:PE0]] + [0, 10, 64, 0, 90, 30, 0, 64]   # ANALOG's own defaults
I = {name: n for n, name in enumerate(ids)}

clamp = lambda x, lo, hi: max(lo, min(hi, int(round(x))))
def time_v(ms): return clamp(127 * math.log10(max(ms, 1.0)) / 4, 0, 127)          # 1 ms .. 10 s
def cut_v(hz): return clamp(127 * math.log(max(hz, 30) / 30) / math.log(16000 / 30), 0, 127)
def lfo_v(hz): return clamp(127 * math.log(max(hz, .05) / .05) / math.log(800), 0, 127)
STEP_OCT = math.log2((16000 / 30) ** (1 / 127))                                     # octaves per cutoff step

def name12(s):
    s = s.replace("À", "A").replace("È", "E").replace("É", "E").replace("Ù", "U").replace("Ò", "O").replace("Ì", "I")
    return s.upper()[:12]

def convert(pr):
    q = pr["p"]
    p = DEF[:]
    det, wave, mix = q["detune"], int(q["wave"]), q["oscMix"]
    notes = []
    # oscillators
    p[I["P_E0"]] = wave                                  # SAW SQR TRI SIN: same order
    p[I["P_E2"]] = clamp(mix * 127, 0, 127)
    if abs(det) <= 1.27:
        p[I["P_E1"]] = clamp(abs(det) * 100, 0, 127)     # DTN in cents
    elif abs(abs(det) - 12) < .1 and wave in (0, 1):
        p[I["P_E0"]] = 6                                 # SUB: a square an octave below a saw
        p[I["P_E1"]] = 0
        notes.append("ottava: onda SUB")
    elif abs(det - 7.02) < .1:
        p[I["P_E1"]] = 0
        p[I["P_CHRD"]] = 9                               # POW: root + fifth + octave
        notes.append("quinta: accordo POW")
    else:
        p[I["P_E1"]] = 7
        notes.append(f"intervallo {det} semitoni non riproducibile")
    # filter (OndaDream's key tracking is centred on C3 (48), Felucca's on C4 (60))
    kt = q["keyTrk"]
    p[I["P_E4"]] = cut_v(q["cutoff"] * 2 ** kt)
    Q = .5 + q["reso"] ** 2 * 17.5
    p[I["P_E5"]] = clamp((8192 - 4096 / Q) * 127 / 7600, 0, 127)
    p[I["P_E6"]] = clamp((q["drive"] - 1) * 20, 0, 127)
    # KTR: E7 / 256 of a cutoff step per semitone; full tracking would be 1 / (12 STEP_OCT) steps = ~299
    p[I["P_E7"]] = clamp(kt * 256 / (12 * STEP_OCT), 0, 127)
    # one envelope for amp and filter in Felucca: the filter's decay folds into its depth
    depth_oct = q["fEnvAmt"] * 5 * (.35 + .65 * (1 - q["s"]))
    p[I["P_ED_FLT"]] = clamp(depth_oct / STEP_OCT, -64, 63)
    p[I["P_ATK"]] = time_v(q["a"] * 1470)
    p[I["P_DEC"]] = time_v(q["d"] * 4600)
    p[I["P_SUS"]] = clamp(q["s"] * 127, 0, 127)
    p[I["P_REL"]] = time_v(q["r"] * 4600)
    # LFO: sine; pitch 3/16 semitone a step (OndaDream's few cents round to 0 or 1)
    p[I["P_LWAVE"]] = 0
    p[I["P_LRATE"]] = lfo_v(q["lfoRate"])
    p[I["P_LD_PIT"]] = 1 if q["lfoPitch"] >= 8 else 0
    p[I["P_LD_FLT"]] = clamp(q["lfoCut"] * 2 / STEP_OCT, -64, 63)
    # arpeggiator (16ths of the tempo)
    p[I["P_AMODE"]] = int(q["arpPattern"])               # OFF UP DN UPDN RND: same order
    p[I["P_ARATE"]] = 2
    p[I["P_AOCT"]] = clamp(q["arpOct"], 1, 4)
    p[I["P_AGATE"]] = clamp(q["arpGate"] * 127, 1, 127)
    if q["arpPattern"]:
        notes.append(f"arp a {q['arpBpm']} BPM nell'originale")
    # sends
    p[I["P_CHOR"]] = clamp(q["cho"] * 127, 0, 127)
    p[I["P_DLY"]] = clamp(q["eco"] * 127, 0, 127)
    p[I["P_REV"]] = clamp(q["rev"] * 127, 0, 127)
    p[I["P_VOICE"]] = 0                                  # POLY
    return p, notes

now = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
presets = json.load(open(sys.argv[1]))
patches, report = [], []
for pr in presets:
    if pr["name"] == "INIT":
        continue
    vals, notes = convert(pr)
    nm = name12(pr["name"])
    patches.append({"name": nm, "engine": 0, "engineName": "ANALOG", "params": vals, "pattern": None,
                    "tags": ["OndaDream"], "created": now, "modified": now})
    report.append((nm, notes))

lib = {"format": "felucca-library", "version": 1, "kind": "library", "firmware": "",
       "pCount": PCOUNT, "pE0": PE0, "paramLabels": keys,
       "engines": ["ANALOG"], "exported": now, "patches": patches}
Path(sys.argv[2]).write_text(json.dumps(lib, ensure_ascii=True, indent=1))
print(PCOUNT, PE0, len(patches))
for nm, n in report:
    if n:
        print(f"  {nm}: {'; '.join(n)}")
