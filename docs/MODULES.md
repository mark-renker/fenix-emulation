# Fénix I emulation — module reference

Every module is a node in `src/engine/System.h` with the designer's port names.
Sockets: **black** = input, **green** = audio output, **blue** = CV output. Pot laws
follow the designer's spec page: lin (A), log/audio taper (B, 10 % at mid), reverse-log
(C), S-curve. Parameters marked *tunable* live in `resources/topology_params.json`.

Electrical contract (brief §2): audio ±2 V nominal, CV −4…+4 V or 0…+8 V, gates
0 / +8 V (inputs read high above +5 V, low below +1 V), op-amp stages clip at ±13.5 V
with a ≤ 0.5 V soft knee, every output is buffered, fan-out is free, tied outputs
resolve by the current-limited node rule (`sys.rout_ohm`, `sys.ilimit_ma`, `sys.isink_ratio`).

## VCO 1 (full) — `src/engine/ModulesOsc.h`

| | |
|---|---|
| Controls | Frequency (lin, 14 octaves from 0.5 Hz), Tune (±0.5 oct), CV2 amount (attenuverter), PW, PWM depth, Sync level |
| Inputs | CV1 (1 V/oct), CV2, Lin FM, PWM, Sync in, Sync level CV, Sub-phase CV |
| Outputs | Sine, Saw, Pulse, Sub1 (−1 oct), Sub2 (−2 oct) |

Model: expo converter `f = f_min · 2^(pot·oct + tune + CV1·(1+err) + CV2·att)`, HF droop
term `f/(1 + droop·f/1000)` (zero when calibrated), linear FM added as current
(`vco.fm_hz_per_v`, stalls at 0 Hz unless `vco.thru_zero_fm`). Sawtooth core at 4× the
host rate with reset-time amplitude shrink (`vco.reset_time_us`), 4-point polyBLEP on
every discontinuity, half-band decimation to the engine rate. Pulse = comparator on
the saw against `PW + PWM·CV` (±4 V sweeps the full window at PW = 12 o'clock; the
pulse vanishes when the threshold leaves the ramp). Triangle = full-wave rectified
saw with a rounded rectifier corner (`vco.tri_fold_corner`) and polyBLAMP; sine =
triangle through the differential-pair shaper (`vco.sine_shape_mix`, `vco.sine_asym`).
Sync: window 0 (off) → 1 (hard), continuous, from pot + CV with `vco.sync_window_curve`;
reset only when the slave is within the window of its own end-of-cycle; positive
edges only. Sub-oscillators: flip-flop ÷2/÷4 driven by a *delayed copy* of the
comparator edge; delay = Sub-phase CV / 8 V × one period (`vco.subphase_law`), so the
sub pitch shifts by d(phase)/dt while the CV moves and returns exactly when it stops.
Heater LED cosmetic (`vintage.warmup_s`).

## VCO 2 / VCO 3 (basic)

Frequency, Tune, CV2 amount (0→max); CV1, CV2, Lin FM, PWM; Saw, Pulse. Same core,
PW fixed at 50 % with the PWM input modulating it.

## LFO 1 / LFO 2 / LFO 3 — `ModulesOsc.h`

Integrator + Schmitt relaxation oscillator; Symmetry splits the charge/discharge
rates (3–97 %), so rate changes and resets are phase-continuous. LFO1: Freq (lin,
0.02 Hz → audio), CV2, Symmetry; Trigger input (`lfo1.trig_mode`: 0 = slam the
integrator to the low rail, 1 = hold while high); Triangle 0…+8 V and Pulse 0/+8 V.
LFO2: Gate input runs the LFO while high (`lfo2.gate_mode` 1 = reset while low);
Triangle ±4 V, double-frequency triangle 0…+8 V (full-wave rectified), Pulse.
LFO3: Freq (log) only; Sine (diode-shaped, `lfo3.sine_thd`), Triangle, Pulse, ±4 V.
Pulse edges are BLEP-corrected because LFO1 reaches the audio range.

## VCF 1 / VCF 2 — 2-pole OTA state-variable — `ModulesVCF.h`

Frequency (16 Hz → 16 kHz, 1 V/oct on CV1, CV2 attenuverter), Resonance; Audio in;
LP / BP / HP out. Zero-delay-feedback (TPT) SVF with the OTA tanh on both integrator
inputs (`vcf12.vt_eff_mv`, `vcf12.atten_ratio`), damping `k = 2(1 − res·vcf12.res_overdrive)`
so the pot maximum sits just beyond self-oscillation, integrator swing bounded by
`vcf12.osc_limit_v` — the self-oscillation level is therefore flat across a sweep,
the designer's calibration criterion. Overload LED (`vcf12.led_thresh_v`).

## VCF 3 — ladder with 2-pole tap and 3-input mixer

Frequency, Resonance, CV2 (attenuverter), Level 1/2/3 (log); CV1, CV2, Audio 1–3;
'12 dB' (stage-2 tap) and '24 dB' (5-pole output, ≈30 dB/oct). Huovilainen-style
ladder with tanh on every stage at 4× the host rate; feedback from stage 5 blended
toward stage 4 by the voicing (`vcf3.early_fb_comp`, `vcf3.late_fb_comp`) with
separate input drives (`vcf3.early_drive`, `vcf3.late_drive`); the **early/late**
switch defaults to late ('Moog-ish' retune). Resonance pot → loop gain so that
oscillation starts at `vcf3.selfosc_pot_pos` (designer: 10 o'clock) and reaches
`vcf3.k_max_ratio` × that at maximum; the threshold is computed analytically for the
chosen feedback topology and corrected for the discrete model. Passband drops with
resonance unless `vcf3.passband_comp` > 0. Output level depends on drive and input
mixing, as the designer notes.

## EG 1 / EG 2 / EG 3 — `ModulesEG.h`

EG1: Attack (2M2 log), Peak time (log), Decay, Sustain, Release, Accent; Gate,
Decay-time CV, Peak-level CV, Release-time CV, Accent-time CV, Accent-level CV;
CV out, Accent out. EG2: Attack, Peak time, Decay/Release, Accent, D/R switch; Gate,
D/R-time CV, Accent-level CV; CV out, Accent out, Mix out (50/50). EG3: Delay,
Decay; Gate; CV out, Delayed gate +, Delayed gate −.
Attack = RC charge toward `eg.overshoot_target` × peak (ends 63–90 % up the curve);
peak-hold = timer; decay/release = exponential, time × 2^(CV·`eg.cv_oct_per_v`);
range `eg.time_min_ms` … `eg.time_max_s`; retrigger continues from the current level.
Accent = instantaneous attack to the level CV, exponential VC decay.

## VCA 1–4, mixers, utilities — `ModulesUtil.h`

* **VCA** — gain = clamp(CV/8 V), 0 → closed, 8 V → unity; OTA tanh (`vca.vt_eff_mv`,
  `vca.atten_ratio`), CV feed-through (`vca.feedthrough_db`), leak (`vca.offset_db`).
* **Mixer 1** — A (lin), B inverting attenuverter, X unity; DC-coupled; clips at the rails.
* **Mixer 2 / 3** — A–B crossfade (S-law), X unity.
* **CV Mix 1** — In × Gain + Offset (±`cvmix1.offset_v`) + X.
* **CV Mix 2** — In + CV × att + octave switch in exact 1.000 V steps.
* **Portamento** — one input, two independent unity lags (Time 1/2), Enable gate bypasses both.
* **Slew limiter** — separate rise/fall rate limits (diode steered), `slew.gain_err_pct`.
* **S&H** — samples on the trigger's rising edge, droops (`sh.droop_v_per_s`), optional aperture glitch.
* **Comparator** — Gate high when (+) > (−) with `cmp.hysteresis_mv`; push button forces high.
* **RM 1 / RM 2** — out = A·B_eff·`rm.scale` + leaks (`rm.leak_a_db`, `rm.leak_b_db`); RM1
  B_eff = B + am·`rm.am_gain`, DC and AC (20 Hz HPF) outputs.
* **Noise** — Analog: Gaussian with `noise.analog_tilt_db` per octave; Digital: maximal
  LFSR (`noise.lfsr_bits`, period > 10^12 states) clocked by an expo VCO from Timbre
  pot/CV (`noise.clock_min_hz` … `noise.clock_max_hz`), raw two-level stream; VLF:
  analog noise through a 2-pole LP at `noise.vlf_hz`, ±4 V.
* **Audio in 1 / 2** — gain (`esp.gain_min_db`…`esp.gain_max_db`), AC-coupled; Level CV /
  Envelope follower; Trigger on abrupt level rise (in 1) or Gate vs Threshold (in 2).
* **Stereo out** — Tip (L) / Ring (R) attenuators, clean buffer, clip at `out.clip_v`;
  `out.full_scale_v` volts = 0 dBFS at the host.
* **Rear I/O + MIDI** (software) — MIDI 1 V/oct (C4 = 0 V, ±2 semitone bend), gate,
  velocity, mod wheel; Rear CV 1/2, Gate 1/2, Aux 1/2 from the FX variant's sidechain bus.
