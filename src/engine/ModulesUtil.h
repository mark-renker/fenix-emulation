// Fénix I emulation — VCAs, mixers, CV mixers, portamento, slew limiter, S&H,
// comparator (brief §4.6), ring modulators (§4.7), noise (§4.8), external
// signal processors and stereo output (§4.9), and the software rear I/O.
#pragma once
#include "Core.h"
#include "ModulesVCF.h" // otaSat

namespace fenix {

// --------------------------------------------------------------- VCA x4 ----
// Linear OTA VCA: gain = clamp(CV/8, 0, 1), OTA tanh on the signal path,
// mild CV feed-through, tiny leak at CV = 0.
class VCA : public Module
{
public:
    int jIn, jCV, jOut;
    VCA() { jIn = addIn ("In"); jCV = addIn ("CV"); jOut = addOut ("Out", true); }
    void tick() override
    {
        const Tunables& t = *T;
        const float cv = in (jCV);
        const float g = clampf (cv / t.vca_cv_full_v, 0.0f, 1.0f);
        const float x = in (jIn);
        float y = otaSat (x * g, t.vca_vt_eff_mv, t.vca_atten_ratio);
        y += x * dbToLin (t.vca_offset_db);
        y += cv * dbToLin (t.vca_feedthrough_db);
        out (jOut, clip (y + noiseFloor()));
    }
};

// --------------------------------------------------------------- Mixer1 ----
// Op-amp summing: A (lin), B (inverting attenuverter), X unity. DC-coupled.
class Mixer1 : public Module
{
public:
    int cA, cB, jA, jB, jX, jOut;
    Mixer1()
    {
        cA = addPot ("A", Law::Lin, 1.0f); cB = addPot ("B -/+", Law::Lin, 0.5f);
        jA = addIn ("A"); jB = addIn ("B"); jX = addIn ("X"); jOut = addOut ("Out", true);
    }
    void tick() override
    {
        const float b = T->mix1_b_bipolar > 0.5f ? (pot (cB) - 0.5f) * 2.0f : pot (cB);
        out (jOut, clip (in (jA) * pot (cA) + in (jB) * b + in (jX) + noiseFloor()));
    }
};

// ----------------------------------------------------------- Mixer2 / 3 ----
// A–B crossfade with an S-taper pot (constant-power-ish feel), X unity.
class Mixer23 : public Module
{
public:
    int cBal, jA, jB, jX, jOut;
    Mixer23()
    {
        cBal = addPot ("A-B balance", Law::S, 0.5f);
        jA = addIn ("A"); jB = addIn ("B"); jX = addIn ("X"); jOut = addOut ("Out", true);
    }
    void tick() override
    {
        const float s = pot (cBal);
        out (jOut, clip (in (jA) * (1.0f - s) + in (jB) * s + in (jX) + noiseFloor()));
    }
};

// --------------------------------------------------------------- CV Mix1 ----
class CVMix1 : public Module
{
public:
    int cGain, cOffset, jIn, jX, jOut;
    CVMix1()
    {
        cGain = addPot ("Gain", Law::Lin, 1.0f); cOffset = addPot ("Offset", Law::Lin, 0.5f);
        jIn = addIn ("In"); jX = addIn ("X"); jOut = addOut ("Out", false);
    }
    void tick() override
    {
        const Tunables& t = *T;
        out (jOut, clip (in (jIn) * pot (cGain) * t.cvmix1_gain_max + (pot (cOffset) - 0.5f) * 2.0f * t.cvmix1_offset_v + in (jX)));
    }
};

// --------------------------------------------------------------- CV Mix2 ----
// Octave switch in exact 1.000 V steps (the designer's calibration source).
class CVMix2 : public Module
{
public:
    int cOct, cCV, jCV, jIn, jOut;
    CVMix2()
    {
        cOct = addSwitch ("Octave -1/0/+1/+2", 4, 1); cCV = addPot ("CV", Law::Lin, 1.0f);
        jCV = addIn ("CV"); jIn = addIn ("In"); jOut = addOut ("Out", false);
    }
    void tick() override
    {
        const float oct = (float) (sw (cOct) - 1);
        out (jOut, clip (in (jIn) + in (jCV) * pot (cCV) + oct));
    }
};

// ------------------------------------------------------------ Portamento ----
// ONE input, two independent unity-gain RC lags; Enable gate bypasses both.
class Portamento : public Module
{
public:
    int cT1, cT2, jIn, jEnable, jOut1, jOut2;
    Portamento()
    {
        cT1 = addPot ("Time 1", Law::Log, 0.3f); cT2 = addPot ("Time 2", Law::Log, 0.5f);
        jIn = addIn ("In"); jEnable = addIn ("Enable"); jOut1 = addOut ("Out 1", false); jOut2 = addOut ("Out 2", false);
    }
    void reset() override { y1 = y2 = 0.0f; en = {}; }
    void tick() override
    {
        const Tunables& t = *T;
        const float x = in (jIn);
        en.update (in (jEnable), t);
        if (connected (jEnable) && en.high) { y1 = y2 = x; }
        else
        {
            const float tau1 = expoMap (pot (cT1), t.port_time_min_ms * 1e-3f, t.port_time_max_s);
            const float tau2 = expoMap (pot (cT2), t.port_time_min_ms * 1e-3f, t.port_time_max_s);
            y1 += (x - y1) * (1.0f - std::exp (-dt / tau1));
            y2 += (x - y2) * (1.0f - std::exp (-dt / tau2));
        }
        y1 = flushDenorm (y1); y2 = flushDenorm (y2);
        out (jOut1, clip (y1)); out (jOut2, clip (y2));
    }
private:
    float y1 = 0.0f, y2 = 0.0f; GateIn en;
};

// ---------------------------------------------------------- Slew limiter ----
// Diode-steered current-limited integrator: separate rise/fall rates. Usable
// as an AR envelope. 'No defined unity gain' -> small static gain error.
class Slew : public Module
{
public:
    int cRise, cFall, jIn, jOut;
    Slew()
    {
        cRise = addPot ("Rise", Law::Log, 0.3f); cFall = addPot ("Fall", Law::Log, 0.3f);
        jIn = addIn ("In"); jOut = addOut ("Out", false);
    }
    void reset() override { y = 0.0f; }
    void tick() override
    {
        const Tunables& t = *T;
        auto rate = [&] (int c) { return expoMap (1.0f - pot (c), t.slew_rate_min_v_s, t.slew_rate_max_v_s); };
        const float x = in (jIn);
        const float up = rate (cRise) * dt, dn = rate (cFall) * dt;
        const float d = x - y;
        y += clampf (d, -dn, up);
        y = flushDenorm (y);
        out (jOut, clip (y * (1.0f + t.slew_gain_err_pct * 0.01f)));
    }
private:
    float y = 0.0f;
};

// ---------------------------------------------------------------- S&H --------
class SampleHold : public Module
{
public:
    int jIn, jTrig, jOut;
    SampleHold() { jIn = addIn ("In"); jTrig = addIn ("Trigger"); jOut = addOut ("Out", false); }
    void reset() override { held = 0.0f; trig = {}; }
    void tick() override
    {
        const Tunables& t = *T;
        if (trig.update (in (jTrig), t)) held = in (jIn) + t.sh_aperture_glitch_mv * 1e-3f;
        held -= (double) t.sh_droop_v_per_s * dt;   // 'may gradually decrease'
        out (jOut, clip ((float) held));
    }
private:
    double held = 0.0; GateIn trig;
};

// ------------------------------------------------------------ Comparator ----
class Comparator : public Module
{
public:
    int cButton, jPos, jNeg, jGate;
    Comparator()
    {
        cButton = addButton ("Push");
        jPos = addIn ("+"); jNeg = addIn ("-"); jGate = addOut ("Gate out", false);
    }
    void reset() override { high = false; }
    void tick() override
    {
        const Tunables& t = *T;
        const float d = in (jPos) - in (jNeg);
        const float h = t.cmp_hysteresis_mv * 1e-3f * 0.5f;
        if (! high && d > h) high = true;
        else if (high && d < -h) high = false;
        const bool on = high || sw (cButton) == 1;
        out (jGate, on ? t.sys_gate_out_v : 0.0f);
    }
private:
    bool high = false;
};

// ------------------------------------------------------- Ring modulators ----
// Four-quadrant multiplier (OTA) with trimmed residual carrier leaks. RM1 adds
// the 'am' input to B (DC offset there acts as 'cleanfeed') and has an
// AC-coupled output as well as the DC one.
class RingMod : public Module
{
public:
    int jA, jB, jAm = -1, jOutDC, jOutAC = -1;
    const bool isRM1;
    explicit RingMod (bool rm1) : isRM1 (rm1)
    {
        jA = addIn ("A"); jB = addIn ("B");
        if (rm1) jAm = addIn ("am");
        jOutDC = addOut (rm1 ? "Out DC" : "Out", true);
        if (rm1) jOutAC = addOut ("Out AC", true);
    }
    void reset() override { hpX = hpY = 0.0f; }
    void tick() override
    {
        const Tunables& t = *T;
        const float a = in (jA);
        const float b = in (jB) + (isRM1 ? in (jAm) * t.rm_am_gain : 0.0f);
        float y = a * b * t.rm_scale + a * dbToLin (t.rm_leak_a_db) + b * dbToLin (t.rm_leak_b_db);
        y = clip (y);
        out (jOutDC, y);
        if (isRM1)
        {
            const float c = std::exp (-kTwoPi * t.sys_ac_hpf_hz * dt);
            const float hp = c * (hpY + y - hpX);
            hpX = y; hpY = flushDenorm (hp);
            out (jOutAC, hp);
        }
    }
private:
    float hpX = 0.0f, hpY = 0.0f;
};

// ---------------------------------------------------------------- Noise ----
class Noise : public Module
{
public:
    int cTimbre, cCV2, cAnalog, cDigital, cVLF;
    int jTimbreCV, jAnalog, jDigital, jVLF;
    Noise()
    {
        cTimbre = addPot ("Timbre", Law::Lin, 0.8f); cCV2 = addPot ("CV2", Law::Lin, 0.0f);
        cAnalog = addPot ("Analog level", Law::Log, 1.0f); cDigital = addPot ("Digital level", Law::Log, 1.0f);
        cVLF = addPot ("VLF level", Law::Log, 1.0f);
        jTimbreCV = addIn ("Timbre CV");
        jAnalog = addOut ("Analog", true); jDigital = addOut ("Digital", true); jVLF = addOut ("VLF", false);
    }
    void prepare (double s) override
    {
        Module::prepare (s);
        const Tunables& t = *T;
        const float fc = std::max (0.1f, t.noise_vlf_hz);
        // 2-pole Butterworth LP (TPT SVF, k = sqrt 2)
        vg = std::tan (kPi * fc / sr);
        // white sigma -> VLF sigma: var_out = sigma^2 * 2 * 1.11 * fc / fs
        vlfGain = (t.noise_vlf_amp_v / 3.0f) / std::max (1e-6f, std::sqrt (2.22f * fc / sr));
        lfsrBits = (int) clampf (t.noise_lfsr_bits, 41.0f, 45.0f);
    }
    void reset() override { lfsr = 0x1234567ull | 1ull; clk = 0.0f; bit = 1.0f; b0 = b1 = b2 = 0.0f; v1 = v2 = 0.0f; nrng = 0x9E3779B97F4A7C15ull; }

    void tick() override
    {
        const Tunables& t = *T;
        // --- analog: Gaussian white (sum of 4 uniforms) with a tilt ---
        float w = 0.0f;
        for (int i = 0; i < 4; ++i) w += uni() - 0.5f;   // var = 4/12 -> sigma 0.577
        w *= 1.732f;                                       // sigma 1
        // tilt: blend white with a 3-stage pink approximation; -3 dB/oct = full pink
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        const float pink = (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
        const float m = clampf (-t.noise_analog_tilt_db / 3.0f, 0.0f, 1.0f);
        const float an = (w * (1.0f - m) + pink * m) * (t.noise_analog_amp_v * 0.5f);
        out (jAnalog, clip (an * pot (cAnalog)));

        // --- VLF: analog noise -> 2-pole LP at ~2 Hz ---
        {
            const float g = vg, k = 1.41421f;
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float x = w;
            const float v3 = x - v2;
            const float vb = a1 * v1 + a2 * v3;
            const float vl = v2 + a2 * v1 + a3 * v3;
            v1 = flushDenorm (2.0f * vb - v1); v2 = flushDenorm (2.0f * vl - v2);
            out (jVLF, clip (vl * vlfGain * pot (cVLF)));
        }

        // --- digital: maximal LFSR clocked by an expo VCO (Timbre pot + CV) ---
        const float octaves = std::log2 (std::max (2.0f, t.noise_clock_max_hz / std::max (0.1f, t.noise_clock_min_hz)));
        const float x01 = clampf (pot (cTimbre) + pot (cCV2) * in (jTimbreCV) / kCVFullV, 0.0f, 1.2f);
        const float fclk = t.noise_clock_min_hz * std::exp2 (x01 * octaves);
        clk += fclk * dt;
        int steps = 0;
        while (clk >= 1.0f && steps < 64) { clk -= 1.0f; stepLfsr(); ++steps; }
        if (clk >= 1.0f) clk = 0.0f;
        out (jDigital, clip (bit * t.noise_digital_amp_v * pot (cDigital)));
    }

private:
    inline float uni() { nrng ^= nrng << 13; nrng ^= nrng >> 7; nrng ^= nrng << 17; return (float) ((nrng >> 11) & 0x1FFFFF) / 2097152.0f; }
    inline void stepLfsr()
    {
        // taps for maximal-length registers (Xilinx XAPP052 table), 1-based bit numbers
        uint64_t fb;
        switch (lfsrBits)
        {
            case 41: fb = (lfsr >> 40) ^ (lfsr >> 37); break;
            case 42: fb = (lfsr >> 41) ^ (lfsr >> 40) ^ (lfsr >> 19) ^ (lfsr >> 18); break;
            case 44: fb = (lfsr >> 43) ^ (lfsr >> 42) ^ (lfsr >> 17) ^ (lfsr >> 16); break;
            case 45: fb = (lfsr >> 44) ^ (lfsr >> 43) ^ (lfsr >> 41) ^ (lfsr >> 40); break;
            default: fb = (lfsr >> 42) ^ (lfsr >> 41) ^ (lfsr >> 37) ^ (lfsr >> 36); break; // 43
        }
        lfsr = ((lfsr << 1) | (fb & 1ull)) & ((1ull << lfsrBits) - 1ull);
        if (lfsr == 0) lfsr = 1;
        bit = (lfsr & 1ull) ? 1.0f : -1.0f;
    }
    uint64_t lfsr = 0x1234567ull, nrng = 0x9E3779B97F4A7C15ull;
    int lfsrBits = 43;
    float clk = 0.0f, bit = 1.0f, b0 = 0, b1 = 0, b2 = 0, v1 = 0, v2 = 0, vg = 0.0001f, vlfGain = 1.0f;
};

// ----------------------------------------------- External signal processors ----
// Audio-in 1: Gain -> Audio out, Level CV (envelope follower), Trigger (fires
// on an abrupt positive level change). Audio-in 2: Gain, Threshold -> Audio
// out, Envelope, Gate (comparator on envelope vs threshold, with hysteresis).
class ESP : public Module
{
public:
    int cGain, cThresh = -1;
    int jAudio, jEnv, jTrig = -1, jGate = -1;
    const bool isOne;
    float extIn = 0.0f;   // set by the host layer (volts, already at engine rate)

    explicit ESP (bool one) : isOne (one)
    {
        cGain = addPot ("Gain", Law::Lin, 0.3f);
        if (! one) cThresh = addPot ("Threshold", Law::Lin, 0.3f);
        jAudio = addOut ("Audio out", true);
        jEnv = addOut (one ? "Level CV" : "Envelope", false);
        if (one) jTrig = addOut ("Trigger", false); else jGate = addOut ("Gate", false);
    }
    void reset() override { env = 0.0f; hpX = hpY = 0.0f; trigT = 0.0f; gateHi = false; prevEnv = 0.0f; }
    void tick() override
    {
        const Tunables& t = *T;
        const float gain = dbToLin (t.esp_gain_min_db + (t.esp_gain_max_db - t.esp_gain_min_db) * pot (cGain));
        // AC-coupled input (20 Hz HPF)
        const float c = std::exp (-kTwoPi * t.sys_ac_hpf_hz * dt);
        const float x = extIn * gain;
        const float hp = c * (hpY + x - hpX); hpX = x; hpY = flushDenorm (hp);
        const float y = clip (hp);
        out (jAudio, y);
        // envelope follower: precision rectifier + fast attack / slow release
        const float r = std::fabs (y);
        const float ca = 1.0f - std::exp (-dt / (t.esp_env_attack_ms * 1e-3f));
        const float cr = 1.0f - std::exp (-dt / (t.esp_env_release_ms * 1e-3f));
        env += (r - env) * (r > env ? ca : cr);
        env = flushDenorm (env);
        const float envV = clip (env * t.esp_env_gain);
        out (jEnv, envV);
        if (isOne)
        {
            const float slope = (envV - prevEnv) / (dt * 1e3f); // V per ms
            prevEnv = envV;
            if (slope > t.esp_trig_slope_v_ms && trigT <= 0.0f) trigT = t.esp_trig_len_ms * 1e-3f;
            if (trigT > 0.0f) trigT -= dt;
            out (jTrig, trigT > 0.0f ? t.sys_gate_out_v : 0.0f);
        }
        else
        {
            const float thr = pot (cThresh) * kCVFullV, h = t.esp_gate_hyst_mv * 1e-3f;
            if (! gateHi && envV > thr + h) gateHi = true;
            else if (gateHi && envV < thr - h) gateHi = false;
            out (jGate, gateHi ? t.sys_gate_out_v : 0.0f);
        }
    }
private:
    float env = 0.0f, hpX = 0.0f, hpY = 0.0f, trigT = 0.0f, prevEnv = 0.0f; bool gateHi = false;
};

// ------------------------------------------------------------ Stereo out ----
// Tip (L) / Ring (R) attenuators, clean buffer with clip at ±13.5 V. The host
// layer reads outL/outR (volts) and scales to the DAW.
class StereoOut : public Module
{
public:
    int cL, cR, jL, jR;
    float outL = 0.0f, outR = 0.0f;
    StereoOut()
    {
        cL = addPot ("Tip (L)", Law::Lin, 0.8f); cR = addPot ("Ring (R)", Law::Lin, 0.8f);
        jL = addIn ("L"); jR = addIn ("R");
    }
    void tick() override
    {
        const Tunables& t = *T;
        outL = opampClip (in (jL) * pot (cL), t.out_clip_v, t.sys_clip_knee_v);
        outR = opampClip (in (jR) * pot (cR), t.out_clip_v, t.sys_clip_knee_v);
    }
};

// ------------------------------------------------------------- Rear I/O ----
// The six rear 3.5 mm jacks (2 CV, 2 Gate, 2 Aux) are hard-wired to front
// bananas; here they are plugin inputs. Plus the MIDI-to-CV adaptor (1 V/oct,
// gate, velocity) — the real instrument has no MIDI.
class RearIO : public Module
{
public:
    int jPitch, jGate, jVel, jMod, jCVA, jCVB, jGateA, jGateB, jAuxA, jAuxB;
    float pitchV = 0.0f, gateV = 0.0f, velV = 0.0f, modV = 0.0f;
    float rear[6] = { 0, 0, 0, 0, 0, 0 };
    RearIO()
    {
        jPitch = addOut ("MIDI 1V/oct", false); jGate = addOut ("MIDI gate", false); jVel = addOut ("MIDI velocity", false); jMod = addOut ("MIDI mod wheel", false);
        jCVA = addOut ("Rear CV 1", false); jCVB = addOut ("Rear CV 2", false);
        jGateA = addOut ("Rear gate 1", false); jGateB = addOut ("Rear gate 2", false);
        jAuxA = addOut ("Rear aux 1", false); jAuxB = addOut ("Rear aux 2", false);
    }
    void tick() override
    {
        out (jPitch, pitchV); out (jGate, gateV); out (jVel, velV); out (jMod, modV);
        out (jCVA, rear[0]); out (jCVB, rear[1]); out (jGateA, rear[2]); out (jGateB, rear[3]); out (jAuxA, rear[4]); out (jAuxB, rear[5]);
    }
};

} // namespace fenix
