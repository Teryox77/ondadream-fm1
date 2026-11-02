# The sequencer against the preview

- `mk_pv4.py`: the preview with the 4 tracks asked for (1-3 synth, D drums) and the firmware's English labels.
- `ref_seq.js`: the preview's own code in Chromium (Playwright), rendered offline: 23 scenarios (the default
  pattern; every rhythm with synth tracks, pans, sends, mutes, tempi; drums alone, synth tracks alone; every rhythm
  dry). The drums' noise is one xorshift per hit on both sides; the voices start at phase 0 (`OD_TEST_ZERO_PHASE`).
  The preview's loop ends a buffer early when an event falls on its first sample (the buffer comes out silent and
  the clock slips 1024 samples): the reference has that removed, the firmware does not have it.
- `c_seq.c`: the firmware's core, the same scenarios. `scmp.py`: level, envelope, timbre (1/3 octave) and
  waveform SNR per channel.

Results (v1.1): level within 0.11 dB, timbre within 0.3 dB, L/R balance within 0.1 dB in every scenario; the drums
alone 33-51 dB waveform SNR (every instrument of every rhythm); with the synth tracks 16-33 dB, as the keyboard's
own voices against the preview.

- `c_screen.c` / `shots.js`: the SEQUENCER and MIXER screens in the same fixed states on both sides.
