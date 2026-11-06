// Fénix I emulation — the "topology parameters" table (brief §0 deliverable 3,
// §6 "tunable constants file"). Every [MOD]/[LOW]/[UNK] constant from the brief
// lives here with its confidence tag, so retuning against hardware recordings
// is a JSON edit, not a recompile. The JSON file (resources/topology_params.json)
// is generated from this table by `fenix_harness --dump-params`.
//
// Field naming: <module>_<quantity>; JSON key: "<module>.<quantity>".
#pragma once
#include "TinyJson.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace fenix {

enum class Conf { HIGH, MOD, LOW, UNK };
inline const char* confName (Conf c)
{
    switch (c) { case Conf::HIGH: return "HIGH"; case Conf::MOD: return "MOD"; case Conf::LOW: return "LOW"; case Conf::UNK: break; }
    return "UNK";
}

// X(field, "json.key", default, Conf, "note")
#define FENIX_TUNABLES(X) \
/* ---- system electrical contract (§2, §5) ---- */ \
X(sys_rout_ohm,        "sys.rout_ohm",        100.0f,  MOD,  "Op-amp output series resistance used by the stacked-output rule (§2)") \
X(sys_ilimit_ma,       "sys.ilimit_ma",       20.0f,   MOD,  "Per-driver output source-current limit, mA (TL07x class)") \
X(sys_isink_ratio,     "sys.isink_ratio",     0.6f,    LOW,  "Sink-current limit as a fraction of the source limit (asymmetric output stage; makes stacked-output fights deterministic)") \
X(sys_clip_v,          "sys.clip_v",          13.5f,   HIGH, "Op-amp clip level on ±15 V rails") \
X(sys_clip_knee_v,     "sys.clip_knee_v",     0.5f,    MOD,  "Soft-knee width of the op-amp clip (§5 'soft knee <= 0.5 V')") \
X(sys_noise_floor_dbfs,"sys.noise_floor_dbfs",-100.0f, LOW,  "White noise added per op-amp output stage (cosmetic). <= -200 disables") \
X(sys_ctl_smooth_ms,   "sys.ctl_smooth_ms",   4.0f,    UNK,  "Pot smoothing time constant for host-automated controls (software only)") \
X(sys_ac_hpf_hz,       "sys.ac_hpf_hz",       20.0f,   LOW,  "HPF for the AC-coupled paths (RM1 AC out, ESP audio)") \
X(sys_gate_hi_v,       "sys.gate_hi_v",       5.0f,    MOD,  "Gate/trigger input reads high above this (II PP thresholds)") \
X(sys_gate_lo_v,       "sys.gate_lo_v",       1.0f,    MOD,  "Gate/trigger input reads low below this (hysteresis)") \
X(sys_gate_out_v,      "sys.gate_out_v",      8.0f,    MOD,  "Gate/trigger output high level") \
/* ---- VCO (§4.1) ---- */ \
X(vco_sine_shape_mix,  "vco.sine_shape_mix",  1.6f,    MOD,  "Strength of the discrete differential-pair shaper residual (0 = ideal sine, 1 = cubic, >1 harder); sets odd-harmonic THD ~1-3%") \
X(vco_sine_asym,       "vco.sine_asym",       0.003f,  MOD,  "Even-harmonic asymmetry of the sine shaper (0 = symmetric; 0.008 ~ 1 % H2)") \
X(vco_tri_fold_corner, "vco.tri_fold_corner", 0.01f,   MOD,  "Rounding of the full-wave-rectifier corner that makes the triangle (0..0.2 of amplitude)") \
X(vco_reset_time_us,   "vco.reset_time_us",   0.2f,    MOD,  "Comparator/JFET reset time; saw amplitude shrinks by f*t (100-300 ns in hardware)") \
X(vco_sync_window_curve,"vco.sync_window_curve",1.0f,  MOD,  "Exponent mapping the Sync level pot+CV to the soft-sync window (1 = linear)") \
X(vco_subphase_law,    "vco.subphase_law",    0.0f,    MOD,  "Sub-phase CV law: 0 = linear delay in CV, 1 = exponential") \
X(vco_fm_hz_per_v,     "vco.fm_hz_per_v",     250.0f,  UNK,  "Linear FM input scale (Hz per volt, additive current into the integrator)") \
X(vco_thru_zero_fm,    "vco.thru_zero_fm",    0.0f,    UNK,  "1 = allow through-zero linear FM; 0 = core stalls at 0 Hz (default per brief)") \
X(vco_saw_amp_v,       "vco.saw_amp_v",       2.0f,    HIGH, "Saw output amplitude (±V)") \
X(vco_pulse_amp_v,     "vco.pulse_amp_v",     2.0f,    HIGH, "Pulse / sub output amplitude (±V)") \
X(vco_sine_amp_v,      "vco.sine_amp_v",      2.0f,    HIGH, "Sine output amplitude (±V)") \
X(vco_hf_tracking_err, "vco.hf_tracking_err", 0.0f,    LOW,  "Bulk-resistance droop term r*I of the expo converter (0 = calibrated)") \
X(vco_freq_pot_octaves,"vco.freq_pot_octaves",14.0f,   MOD,  "Octaves spanned by the Frequency pot (sub-audio to above audio)") \
X(vco_freq_pot_min_hz, "vco.freq_pot_min_hz", 0.5f,    MOD,  "Frequency at pot fully CCW with 0 V CV") \
X(vco_tune_range_oct,  "vco.tune_range_oct",  0.5f,    UNK,  "Tune pot range (±octaves)") \
X(vco_pw_cv_full_v,    "vco.pw_cv_full_v",    4.0f,    HIGH, "PWM CV swing (±V) that sweeps the full pulse-width window at PW=12 o'clock") \
X(vco_cv2_bipolar,     "vco.cv2_bipolar",     1.0f,    UNK,  "VCO CV2 attenuator law: 1 = -/0/+ attenuverter, 0 = 0..max") \
/* ---- LFO (§4.2) ---- */ \
X(lfo_min_hz,          "lfo.min_hz",          0.02f,   MOD,  "LFO1/2 frequency at pot CCW") \
X(lfo_pot_octaves,     "lfo.pot_octaves",     16.0f,   MOD,  "Octaves spanned by LFO1/2 Freq pot ('reaches audible spectrum')") \
X(lfo3_min_hz,         "lfo3.min_hz",         0.03f,   MOD,  "LFO3 frequency at pot CCW (manual only)") \
X(lfo3_pot_octaves,    "lfo3.pot_octaves",    10.0f,   MOD,  "Octaves spanned by LFO3 Freq (log pot)") \
X(lfo_sym_min,         "lfo.sym_min",         0.03f,   MOD,  "Symmetry pot minimum duty (II PP documents 3..97 %)") \
X(lfo_sym_max,         "lfo.sym_max",         0.97f,   MOD,  "Symmetry pot maximum duty") \
X(lfo1_tri_hi_v,       "lfo1.tri_hi_v",       8.0f,    HIGH, "LFO1 triangle output: positive-going 0..this") \
X(lfo2_tri_amp_v,      "lfo2.tri_amp_v",      4.0f,    HIGH, "LFO2 triangle output amplitude (±V)") \
X(lfo2_gate_mode,      "lfo2.gate_mode",      0.0f,    MOD,  "LFO2 Gate input: 0 = hold integrator while low (run while high), 1 = reset while low") \
X(lfo1_trig_mode,      "lfo1.trig_mode",      0.0f,    MOD,  "LFO1 Trigger: 0 = force integrator to low rail (restart), 1 = hold while high") \
X(lfo3_amp_v,          "lfo3.amp_v",          4.0f,    MOD,  "LFO3 output amplitude (±V)") \
X(lfo3_sine_thd,       "lfo3.sine_thd",       0.5f,    MOD,  "LFO3 diode-shaper sine blend (0 = ideal sine, 1 = heavy triangle residue)") \
/* ---- VCF1/VCF2 (§4.3) ---- */ \
X(vcf12_vt_eff_mv,     "vcf12.vt_eff_mv",     90.0f,   MOD,  "Effective OTA tanh scale per integrator input (CA3280 + linearising diodes; 60-120 mV)") \
X(vcf12_res_overdrive, "vcf12.res_overdrive", 1.03f,   MOD,  "Resonance->damping law: res pot at max reaches (this) x self-oscillation") \
X(vcf12_led_thresh_v,  "vcf12.led_thresh_v",  4.0f,    LOW,  "Overload LED threshold (cosmetic)") \
X(vcf12_osc_limit_v,   "vcf12.osc_limit_v",   3.0f,    MOD,  "Integrator output soft limit (OTA output swing); sets the self-oscillation amplitude, which is then flat across the sweep") \
X(vcf12_pot_min_hz,    "vcf12.pot_min_hz",    16.0f,   MOD,  "Cutoff at Frequency pot CCW") \
X(vcf12_pot_octaves,   "vcf12.pot_octaves",   10.0f,   MOD,  "Octaves spanned by the Frequency pot") \
X(vcf12_atten_ratio,   "vcf12.atten_ratio",   20.0f,   MOD,  "Input attenuation before the OTA integrators (signal /ratio, output x ratio); sets how hard 4 Vpp drives Vt_eff") \
/* ---- VCF3 ladder (§4.4) ---- */ \
X(vcf3_vt_mv,          "vcf3.vt_mv",          26.0f,   MOD,  "Thermal voltage of the ladder stages (BJT; 26 mV at 300 K)") \
X(vcf3_input_scale,    "vcf3.input_scale",    0.25f,   MOD,  "Volts-to-ladder scaling (how hard 4 Vpp drives the first stage)") \
X(vcf3_k_osc_scale,    "vcf3.k_osc_scale",    1.0f,    MOD,  "Multiplier on the analytic self-oscillation loop gain of the chosen feedback topology (1 = exact)") \
X(vcf3_selfosc_pot_pos,"vcf3.selfosc_pot_pos",0.30f,   HIGH, "Resonance pot position where oscillation 'just starts' with no CV (designer: 10 o'clock)") \
X(vcf3_k_max_ratio,    "vcf3.k_max_ratio",    1.6f,    MOD,  "Feedback gain at Resonance pot max, as a multiple of the self-oscillation gain") \
X(vcf3_voicing,        "vcf3.voicing",        1.0f,    MOD,  "0 = early (bright, raw ladder), 1 = late ('Moog-ish' retune after owner feedback)") \
X(vcf3_early_drive,    "vcf3.early_drive",    1.0f,    MOD,  "Input drive multiplier for the early voicing") \
X(vcf3_late_drive,     "vcf3.late_drive",     1.6f,    MOD,  "Input drive multiplier for the late voicing") \
X(vcf3_early_fb_comp,  "vcf3.early_fb_comp",  0.0f,    MOD,  "Early voicing: fraction of feedback taken from stage 4 instead of stage 5 (0 = pure 5-pole feedback)") \
X(vcf3_late_fb_comp,   "vcf3.late_fb_comp",   0.5f,    MOD,  "Late voicing: fraction of feedback taken from stage 4 (closer to a 4-pole Moog ladder)") \
X(vcf3_passband_comp,  "vcf3.passband_comp",  0.0f,    UNK,  "Resonance passband-loss compensation 0..1 (0 = classic ladder bass loss)") \
X(vcf3_pot_min_hz,     "vcf3.pot_min_hz",     16.0f,   MOD,  "Cutoff at Frequency pot CCW") \
X(vcf3_pot_octaves,    "vcf3.pot_octaves",    10.0f,   MOD,  "Octaves spanned by the Frequency pot") \
X(vcf3_tap12_gain,     "vcf3.tap12_gain",     1.0f,    MOD,  "Output make-up gain of the '12 dB' stage-2 tap") \
X(vcf3_tap24_gain,     "vcf3.tap24_gain",     1.0f,    MOD,  "Output make-up gain of the '24 dB' (5-pole) output") \
/* ---- Envelope generators (§4.5) ---- */ \
X(eg_overshoot_target, "eg.overshoot_target", 1.4f,    MOD,  "Attack RC charges toward (this x full-scale) so the stage ends at ~63-90% of the way") \
X(eg_time_min_ms,      "eg.time_min_ms",      1.0f,    MOD,  "Fastest attack/decay/release time constant (ms) at pot CCW") \
X(eg_time_max_s,       "eg.time_max_s",       50.0f,   MOD,  "Slowest time constant (s) at pot CW (designer: '<1 ms to >50 s')") \
X(eg_cv_oct_per_v,     "eg.cv_oct_per_v",     1.0f,    LOW,  "Decay/Release time CV scaling: +1 V -> x2 time (II PP: 'higher CV = longer time')") \
X(eg_out_v,            "eg.out_v",            8.0f,    HIGH, "Envelope output full scale (V)") \
X(eg_retrigger_from_level,"eg.retrigger_from_level",1.0f,MOD, "1 = retrigger continues from current level (no reset to zero)") \
X(eg_peak_min_ms,      "eg.peak_min_ms",      0.1f,    MOD,  "Peak-hold monostable minimum (ms) at pot CCW") \
X(eg_peak_max_s,       "eg.peak_max_s",       10.0f,   MOD,  "Peak-hold monostable maximum (s) at pot CW") \
X(eg_accent_attack_ms, "eg.accent_attack_ms", 0.05f,   MOD,  "'Accent' envelope attack ('instantaneous')") \
X(eg3_gate_delay_min_ms,"eg3.gate_delay_min_ms",0.1f,  MOD,  "EG3 delayed-gate timer minimum (ms)") \
X(eg3_gate_delay_max_s,"eg3.gate_delay_max_s",10.0f,   MOD,  "EG3 delayed-gate timer maximum (s)") \
X(eg_level_pot_law,    "eg.level_pot_law",    1.0f,    UNK,  "Sustain/Accent-level 10k C pots: 0 = literal reverse-log (90% at mid), 1 = wired reversed = audio taper (10% at mid)") \
/* ---- VCAs, mixers, utilities (§4.6) ---- */ \
X(vca_vt_eff_mv,       "vca.vt_eff_mv",       60.0f,   MOD,  "OTA tanh scale on the VCA signal path (after internal attenuation)") \
X(vca_atten_ratio,     "vca.atten_ratio",     100.0f,  MOD,  "Internal attenuation before the OTA (signal /ratio, output x ratio)") \
X(vca_cv_full_v,       "vca.cv_full_v",       8.0f,    HIGH, "CV at which the linear VCA reaches unity gain") \
X(vca_feedthrough_db,  "vca.feedthrough_db",  -60.0f,  MOD,  "CV feed-through to output (mild)") \
X(vca_offset_db,       "vca.offset_db",       -90.0f,  LOW,  "Signal leak with CV at 0 (OTA offset)") \
X(mix1_b_bipolar,      "mix1.b_bipolar",      1.0f,    HIGH, "Mixer1 B is a -/+ attenuverter (inverting)") \
X(cvmix1_offset_v,     "cvmix1.offset_v",     4.0f,    LOW,  "CV Mix1 Offset pot range (±V)") \
X(cvmix1_gain_max,     "cvmix1.gain_max",     1.0f,    LOW,  "CV Mix1 Gain pot at max (x)") \
X(port_time_min_ms,    "port.time_min_ms",    0.5f,    MOD,  "Portamento lag time constant at pot CCW (ms)") \
X(port_time_max_s,     "port.time_max_s",     10.0f,   MOD,  "Portamento lag time constant at pot CW (s)") \
X(slew_rate_min_v_s,   "slew.rate_min_v_s",   0.8f,    MOD,  "Slew limiter slowest rate (V/s) at pot CW") \
X(slew_rate_max_v_s,   "slew.rate_max_v_s",   20000.0f,MOD,  "Slew limiter fastest rate (V/s) at pot CCW") \
X(slew_gain_err_pct,   "slew.gain_err_pct",   0.0f,    MOD,  "'No defined unity gain' - small static gain error (±2 %)") \
X(sh_droop_v_per_s,    "sh.droop_v_per_s",    0.01f,   MOD,  "S&H hold-cap droop ('may gradually decrease', tens of mV/s)") \
X(sh_aperture_glitch_mv,"sh.aperture_glitch_mv",0.0f,  LOW,  "S&H JFET switch charge-injection step at sample (mV)") \
X(cmp_hysteresis_mv,   "cmp.hysteresis_mv",   20.0f,   MOD,  "LM311 comparator hysteresis") \
/* ---- Ring modulators (§4.7) ---- */ \
X(rm_scale,            "rm.scale",            0.25f,   MOD,  "Four-quadrant multiplier scaling: out = A*B*scale (II PP: A*B/4)") \
X(rm_leak_a_db,        "rm.leak_a_db",        -40.0f,  MOD,  "Carrier A leak into the product (trimmed residual)") \
X(rm_leak_b_db,        "rm.leak_b_db",        -40.0f,  MOD,  "Carrier B leak into the product (trimmed residual)") \
X(rm_am_gain,          "rm.am_gain",          0.5f,    MOD,  "RM1 'am' input: B_eff = B + am*am_gain (SOS: inputs 1+3 give the same product at lower amplitude)") \
/* ---- Noise (§4.8) ---- */ \
X(noise_lfsr_bits,     "noise.lfsr_bits",     43.0f,   MOD,  "Maximal LFSR length (two/three CD4006s; 41..45 bits)") \
X(noise_clock_min_hz,  "noise.clock_min_hz",  2.0f,    MOD,  "Digital noise clock VCO at Timbre pot CCW") \
X(noise_clock_max_hz,  "noise.clock_max_hz",  80000.0f,MOD,  "Digital noise clock VCO at Timbre pot CW ('>50 kHz blue noise')") \
X(noise_digital_amp_v, "noise.digital_amp_v", 2.0f,    HIGH, "Digital noise two-level output amplitude (±V)") \
X(noise_analog_tilt_db,"noise.analog_tilt_db",-1.5f,   MOD,  "Analog noise spectral tilt per octave (0 = white, -3 = pink)") \
X(noise_analog_amp_v,  "noise.analog_amp_v",  2.0f,    MOD,  "Analog noise RMS-ish amplitude at level max (V)") \
X(noise_vlf_hz,        "noise.vlf_hz",        2.0f,    MOD,  "VLF random: 2-pole low-pass cutoff on the analog noise (1-3 Hz)") \
X(noise_vlf_amp_v,     "noise.vlf_amp_v",     4.0f,    HIGH, "VLF output amplitude (±V)") \
/* ---- External signal processors (§4.9) ---- */ \
X(esp_gain_min_db,     "esp.gain_min_db",     -6.0f,   LOW,  "Audio-in Gain pot at CCW (II PP module)") \
X(esp_gain_max_db,     "esp.gain_max_db",     20.0f,   LOW,  "Audio-in Gain pot at CW") \
X(esp_env_attack_ms,   "esp.env_attack_ms",   2.0f,    LOW,  "Envelope follower attack") \
X(esp_env_release_ms,  "esp.env_release_ms",  50.0f,   LOW,  "Envelope follower release") \
X(esp_trig_slope_v_ms, "esp.trig_slope_v_ms", 2.0f,    LOW,  "Audio-in 1 Trigger fires when d(env)/dt exceeds this (V/ms)") \
X(esp_trig_len_ms,     "esp.trig_len_ms",     5.0f,    LOW,  "Trigger one-shot length") \
X(esp_gate_hyst_mv,    "esp.gate_hyst_mv",    100.0f,  LOW,  "Audio-in 2 Gate comparator hysteresis") \
X(esp_env_gain,        "esp.env_gain",        4.0f,    LOW,  "Envelope follower output scale (V per V peak)") \
X(out_clip_v,          "out.clip_v",          13.5f,   HIGH, "Stereo out buffer clip level") \
X(out_full_scale_v,    "out.full_scale_v",    8.0f,    MOD,  "Volts mapped to 0 dBFS at the host output (software scaling)") \
X(in_full_scale_v,     "in.full_scale_v",     2.0f,    MOD,  "Host 0 dBFS maps to this many volts at the 1/4-inch inputs") \
/* ---- 'vintage' macro (§5 Calibration-perfect by default) ---- */ \
X(vintage_on,          "vintage.on",          0.0f,    HIGH, "0 = calibrated instrument (default); 1 = apply small 1 V/oct errors + HF droop") \
X(vintage_track_err_pct,"vintage.track_err_pct",0.25f, MOD,  "1 V/oct scale error per VCO when vintage is on (<= 0.3 %)") \
X(vintage_hf_droop,    "vintage.hf_droop",    0.002f,  MOD,  "HF droop term per VCO when vintage is on") \
X(vintage_warmup_s,    "vintage.warmup_s",    60.0f,   HIGH, "Heater LED red->green time (cosmetic)")

struct Tunables
{
#define X(field, key, def, conf, note) float field = def;
    FENIX_TUNABLES(X)
#undef X

    struct Entry { const char* key; float* ptr; float def; Conf conf; const char* note; };

    std::vector<Entry> entries()
    {
        std::vector<Entry> v;
#define X(field, key, def, conf, note) v.push_back ({ key, &field, def, Conf::conf, note });
        FENIX_TUNABLES(X)
#undef X
        return v;
    }

    void reset()
    {
#define X(field, key, def, conf, note) field = def;
        FENIX_TUNABLES(X)
#undef X
    }

    // Accepts either  "vco.sine_thd": 0.02   or  "vco.sine_thd": { "value": 0.02, ... }
    // Unknown keys are reported (not fatal). Returns number of keys applied.
    int loadJsonText (const std::string& text, std::string* err = nullptr, std::vector<std::string>* unknown = nullptr)
    {
        JsonValue root;
        if (! JsonParser::parse (text, root, err)) return -1;
        if (! root.isObject()) { if (err) *err = "root is not an object"; return -1; }
        auto ents = entries();
        int applied = 0;
        for (auto& kv : root.obj)
        {
            if (! kv.first.empty() && kv.first[0] == '_') continue; // metadata
            const JsonValue* v = &kv.second;
            if (v->isObject()) v = v->get ("value");
            if (v == nullptr || ! v->isNumber()) continue;
            bool found = false;
            for (auto& e : ents)
                if (kv.first == e.key) { *e.ptr = (float) v->num; found = true; ++applied; break; }
            if (! found && unknown) unknown->push_back (kv.first);
        }
        return applied;
    }

    bool loadJsonFile (const std::string& path, std::string* err = nullptr)
    {
        std::ifstream f (path);
        if (! f) { if (err) *err = "cannot open " + path; return false; }
        std::stringstream ss; ss << f.rdbuf();
        return loadJsonText (ss.str(), err) >= 0;
    }

    std::string toJson (bool withMeta = true)
    {
        std::string o = "{\n";
        o += "  \"_about\": \"Synton Fenix I emulation - topology parameters. Edit values; keys are <module>.<quantity>. "
             "conf = evidence level from the engineering brief (HIGH/MOD/LOW/UNK). Loaded at plugin start from the bundle "
             "and overridden by ~/Library/Application Support/FenixI/topology_params.json if present.\",\n";
        auto ents = entries();
        for (size_t i = 0; i < ents.size(); ++i)
        {
            auto& e = ents[i];
            o += "  \"" + std::string (e.key) + "\": ";
            if (withMeta)
                o += "{ \"value\": " + jsonNumber (*e.ptr) + ", \"default\": " + jsonNumber (e.def)
                   + ", \"conf\": \"" + confName (e.conf) + "\", \"note\": \"" + jsonEscape (e.note) + "\" }";
            else
                o += jsonNumber (*e.ptr);
            o += (i + 1 < ents.size()) ? ",\n" : "\n";
        }
        o += "}\n";
        return o;
    }
};

} // namespace fenix
