# Sound against the preview

The preview's own DSP code (cut from the OndaDream HTML), rendered offline, against the firmware's core, with the same
notes for every preset: a chord (A3 C4 E4) held 1.2 s, then A2 for 0.6 s, 4.5 s in all.

    node ref_preview_exact.js ONDADREAM.html          # -> /tmp/fid/fref_N.f32   (random phases held at 0)
    gcc -O2 -DOD_TEST_ZERO_PHASE '-DOUTFMT="/tmp/fid/zc_%d.f32"' -I../../src/core -I../../src/gen fw_render.c -lm -o fw_render
    for i in $(seq 0 42); do ./fw_render $i; done
    python3 compare.py

`ref_preview_exact.js` leaves out one quirk of the preview's audio loop (an arpeggio's first step there ends that
audio buffer early, so the arpeggio starts one buffer, 23 ms, late) and holds the chorus LFO's phase where the
firmware's is; the rest is the preview's code as it is. Result (November 2026): every preset within 0.1 dB of level
and envelope and 0.5 dB of timbre (third-octave bands), on average 0.03 / 0.04 / 0.15 dB.
