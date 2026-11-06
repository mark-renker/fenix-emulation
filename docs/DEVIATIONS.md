# Deviations, interpretations and open unknowns

The brief tags every claim HIGH / MOD / LOW / UNK and forbids silently upgrading a
MOD/LOW claim into a design assumption. This file lists every place the emulation
had to choose, what was chosen, and where it is exposed.

## Choices made on MOD / LOW / UNK claims

| Topic | Brief | Choice | Exposed as |
|---|---|---|---|
| Stacked outputs | MOD: weighted mean then per-driver current clip | KCL solved with asymmetric source/sink limits; a strong fight resolves to the sourcing driver minus the sink drop. Symmetric limits would make two drivers always average, so no 'unpredictable' behaviour would ever appear. | `sys.rout_ohm`, `sys.ilimit_ma`, `sys.isink_ratio` |
| Input stacking | HIGH 'fan-out is fine' | Several cables into one **input** sum cleanly (software-modular convention, and the harness criterion in §7). | — |
| VCO CV2 law | HIGH that two laws exist; UNK which modules use which | VCO1/VCF1-3 CV2 = −/0/+ attenuverter; VCO2/3 = 0→max | `vco.cv2_bipolar` |
| Linear FM | UNK | Additive current, stalls at 0 Hz; through-zero off | `vco.fm_hz_per_v`, `vco.thru_zero_fm` |
| Sync on Fénix I | MOD (II PP has continuous window; SOS says hard/soft/none) | Continuous window 0→1 from pot + CV; the three SOS positions are 0 / ~0.3 / 1 | `vco.sync_window_curve` |
| Sine shaper THD | MOD 1–3 % | 1.9 % at 1 kHz, H3 4.8 dB above H2 | `vco.sine_shape_mix`, `vco.sine_asym`, `vco.tri_fold_corner` |
| Sub-phase law | MOD | Linear in CV (0…8 V = 0…1 period) | `vco.subphase_law` |
| LFO2 gate | MOD 'likely holds' | Hold while low (default); reset mode available | `lfo2.gate_mode` |
| VCF1/2 self-oscillation bound | MOD 'bounded by OTA tanh' | A tanh on the integrator *inputs* alone bounds the slew, not the amplitude (the loop becomes a rail-to-rail relaxation oscillator beyond threshold). The OTA output swing is therefore also bounded, which gives the flat-level-across-sweep behaviour the calibration page describes. | `vcf12.osc_limit_v`, `vcf12.vt_eff_mv`, `vcf12.atten_ratio` |
| VCF3 topology | MOD BJT ladder vs OTA cascade | Transistor ladder (CA3046 heater + BJT parts list). 'Early' = pure stage-5 feedback, 'late' = half of the feedback taken from stage 4 plus more input drive. Awaiting the board overlay. | `vcf3.*` |
| VCF3 self-osc point | HIGH '10 o'clock' | Pot position 0.30 (7 o'clock = 0, 5 o'clock = 1). Most of the pot is therefore past oscillation, as the hardware is reported to be. | `vcf3.selfosc_pot_pos`, `vcf3.k_max_ratio` |
| VCF3 passband compensation | UNK | 0 (classic bass loss) | `vcf3.passband_comp` |
| EG time pots | MOD | 2M2 log attack = B taper; 10k C decay/release treated as wired reversed (1 − C(1 − x) = B(x)), so both favour short times as the designer states | — |
| EG level pots (Sustain, Accent) | UNK | Audio taper (10 % at mid), the 'wired reversed C' reading | `eg.level_pot_law` |
| EG attack end point | MOD 63–90 % | Target 1.4 × peak (ends at 71 %) | `eg.overshoot_target` |
| EG decay CV | LOW | +1 V = ×2 time | `eg.cv_oct_per_v` |
| Peak level CV (EG1) | MOD | peak = clamp(8 V + CV, 0.5, 8 V) | — |
| RM 'am' scaling | MOD | B_eff = B + 0.5·am, product ×¼ (II PP) | `rm.am_gain`, `rm.scale` |
| RM leak | MOD | −40 dB each carrier, trimmed residual | `rm.leak_a_db`, `rm.leak_b_db` |
| LFSR length/taps | UNK (41–45 bits) | 43 bits, XAPP052 taps; 41/42/44/45 selectable | `noise.lfsr_bits` |
| Analog noise colour | MOD 'pink-ish' | −1.5 dB/oct tilt (white↔pink blend) | `noise.analog_tilt_db` |
| S&H droop | MOD | 10 mV/s downward | `sh.droop_v_per_s` |
| Slew unity error | MOD ±2 % | 0 % | `slew.gain_err_pct` |
| ESP gain range, follower times | LOW (II PP) | −6…+20 dB, 2 ms / 50 ms | `esp.*` |
| Noise floor | cosmetic | −100 dBFS white per op-amp stage (also seeds filter self-oscillation) | `sys.noise_floor_dbfs` |
| Oversampling | §6 | 2× graph, 4× VCO + VCF3, 63-tap half-band host decimator (latency 16 samples) | — |

## Counts vs the designer's

Designer: 31 modules, 63 pots, 3 switches, 158 sockets (front bananas + rear). Here:
31 modules + rear node, 64 pots (the VCF3 early/late *voicing* is an added switch,
and Mixer 1's two pots are counted separately), 4 switches, 144 front sockets + 10
software rear/MIDI outputs. The one-pot difference is likely a module whose exact
control set is not on the designer's spec page (VCO2/3 'Tune' is modelled as its own pot).

## Open unknowns (brief §8) and how this build is prepared for them

1. **Board overlays (dutchsynth.eu/ServiceF1.html)** — will settle which OTA is in
   VCF1/2, whether VCF3 is a BJT ladder or OTA cascade, the LFSR length (count the
   4006s) and the EG discharge element. All four are JSON parameters or a switch,
   so none requires DSP changes unless VCF3 turns out to be an OTA cascade (then a
   second VCF3 model would be added behind the same ports).
2. **Owner recordings** — `fenix_harness --params` lets a retune be validated by the
   same measurements; the harness prints THD, slopes, k_osc, leak and timing numbers
   that can be fitted directly against recordings.
3. **Designer questions** worth asking: VCF3 topology and the 'Moog-ish' revision;
   RM1 cleanfeed structure; LFSR length; EG D/R current-control element; which CV2
   inputs are attenuverters.
4. **Fénix II PP manual** values borrowed as tie-breakers (and flagged above): sync
   window, PWM range, VCA/RM scaling, S&H droop, EG CV polarity, ESP gain range.
