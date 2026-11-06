// Fénix I emulation — oscillators: VCO1 (full), VCO2/VCO3 (basic), LFO1-3.
// Brief §4.1 / §4.2. VCO cores run 2 sub-steps per engine sample (4x the host
// rate), use polyBLEP steps with a one-sub-sample latch so both sides of every
// discontinuity (wrap, sync reset, comparator edge, sub-divider toggle) are
// corrected, and are averaged back to the engine rate.
#pragma once
#include "Core.h"

namespace fenix {

// Four-point polyBLEP (integrated cubic B-spline) with a two-sample output
// latch: a step of height h that happened d (0..1] samples after the sample
// "h0" gets residual corrections on the two samples before and the two after.
// ramp() applies the matching two-point polyBLAMP for slope discontinuities.
struct BlepLatch
{
    float h0 = 0.0f, h1 = 0.0f;         // last two naive(+corrected) samples awaiting output
    float cur = 0.0f, nxt = 0.0f;       // pending corrections for the next two pushed samples
    static inline float S (float x)     // integral of the cubic B-spline, -2..2
    {
        if (x <= -2.0f) return 0.0f;
        if (x <= -1.0f) { const float u = x + 2.0f; return u * u * u * u / 24.0f; }
        if (x <= 0.0f)  return 0.5f + (4.0f * x - 2.0f * x * x * x - 0.75f * x * x * x * x) / 6.0f;
        if (x <= 1.0f)  return 0.5f + (4.0f * x - 2.0f * x * x * x + 0.75f * x * x * x * x) / 6.0f;
        if (x <= 2.0f)  { const float u = 2.0f - x; return 1.0f - u * u * u * u / 24.0f; }
        return 1.0f;
    }
    inline void step (float h, float d)
    {
        d = clampf (d, 0.0f, 1.0f);
        h1  += h * S (-1.0f - d);
        h0  += h * S (-d);
        cur += h * (S (1.0f - d) - 1.0f);
        nxt += h * (S (2.0f - d) - 1.0f);
    }
    // slope discontinuity of 'dh' (change in per-sample slope) at d: 2-point polyBLAMP
    inline void ramp (float dh, float d)
    {
        d = clampf (d, 0.0f, 1.0f);
        const float u = 1.0f - d;
        h0  += dh * u * u * u / 6.0f;
        cur += dh * d * d * d / 6.0f;
    }
    inline float push (float naive)
    {
        const float r = h1;
        h1 = h0;
        h0 = naive + cur;
        cur = nxt; nxt = 0.0f;
        return r;
    }
    void reset() { h0 = h1 = cur = nxt = 0.0f; }
};

// Rising-edge detector for sync/clock inputs that accept 'a wide range of
// voltages' (brief §4.1): fires when the input crosses +1 V upward.
struct EdgeIn
{
    bool high = false;
    inline bool rising (float v)
    {
        if (! high && v > 1.0f) { high = true; return true; }
        if (high && v < 0.2f) high = false;
        return false;
    }
};

// Sine shaper: triangle -> sine via a blend of the ideal map and the classic
// differential-pair cubic, plus an even-harmonic asymmetry term (brief §4.1).
inline float sineShaper (float x, float mix, float asym)
{
    x = clampf (x, -1.0f, 1.0f);
    const float ideal = std::sin (0.5f * kPi * x);
    const float cubic = x * (1.5f - 0.5f * x * x);
    float y = ideal + mix * (cubic - ideal);
    y += asym * 3.0f * (x * x - 1.0f / 3.0f);
    return y;
}

class VCO : public Module
{
public:
    // ctls
    int cFreq, cTune, cCV2, cPW = -1, cPWM = -1, cSync = -1;
    // jacks
    int jCV1, jCV2, jFM, jPWM, jSync = -1, jSyncCV = -1, jSubCV = -1;
    int jSine = -1, jSaw, jPulse, jSub1 = -1, jSub2 = -1;
    const bool full; const int number;

    VCO (bool fullFeatured, int num) : full (fullFeatured), number (num)
    {
        cFreq = addPot ("Frequency", Law::Lin, 0.5f);
        cTune = addPot ("Tune", Law::Lin, 0.5f);
        cCV2  = addPot ("CV2 amount", Law::Lin, full ? 0.5f : 0.0f);
        if (full)
        {
            cPW   = addPot ("PW", Law::Lin, 0.5f);
            cPWM  = addPot ("PWM depth", Law::Lin, 0.0f);
            cSync = addPot ("Sync level", Law::Lin, 0.0f);
        }
        jCV1 = addIn ("CV1"); jCV2 = addIn ("CV2"); jFM = addIn ("Lin FM"); jPWM = addIn ("PWM");
        if (full) { jSync = addIn ("Sync in"); jSyncCV = addIn ("Sync level CV"); jSubCV = addIn ("Sub-phase CV"); }
        if (full) jSine = addOut ("Sine", true);
        jSaw = addOut ("Saw", true); jPulse = addOut ("Pulse", true);
        if (full) { jSub1 = addOut ("Sub1", true); jSub2 = addOut ("Sub2", true); }
        leds.assign (1, 0.0f); // heater LED (0 = red, 1 = green)
    }

    void prepare (double s) override
    {
        Module::prepare (s);
        for (auto& d : dec) d.design (23, 8.0f);   // relaxed half-band: passband 24 k, stopband 72 k at the 4x rate
    }
    void reset() override
    {
        phase = 0.0f; pulseHi = false; sub1 = sub2 = false; subQueue.clear();
        bSaw.reset(); bPulse.reset(); bSub1.reset(); bSub2.reset(); bTri.reset(); sync = {}; warm = 0.0f;
        for (auto& d : dec) d.reset();
    }

    // current fundamental, Hz (for harness/UI)
    float currentHz() const { return lastHz; }

    void tick() override
    {
        const Tunables& t = *T;
        const float dtl = dt * 0.5f;                       // 4x sub-step

        // ---- pitch (per engine sample; fine at 96 kHz) ----
        float oct = pot (cFreq) * t.vco_freq_pot_octaves
                  + (pot (cTune) - 0.5f) * 2.0f * t.vco_tune_range_oct;
        float cv2 = in (jCV2);
        float att = t.vco_cv2_bipolar > 0.5f ? (pot (cCV2) - 0.5f) * 2.0f : pot (cCV2);
        float trackErr = 0.0f, droop = t.vco_hf_tracking_err;
        if (t.vintage_on > 0.5f)
        {
            static const float sgn[3] = { 1.0f, -0.6f, 0.3f };
            trackErr = t.vintage_track_err_pct * 0.01f * sgn[(number - 1) % 3];
            droop += t.vintage_hf_droop;
        }
        oct += in (jCV1) * (1.0f + trackErr) + cv2 * att;
        float f = t.vco_freq_pot_min_hz * std::exp2 (clampf (oct, -6.0f, 24.0f));
        f = f / (1.0f + droop * f * 0.001f);
        if (connected (jFM))
        {
            f += in (jFM) * t.vco_fm_hz_per_v;
            if (t.vco_thru_zero_fm < 0.5f && f < 0.0f) f = 0.0f;
        }
        f = clampf (f, -0.45f * sr * 2.0f, 0.45f * sr * 2.0f);
        lastHz = f;
        const float inc = f * dtl;
        // BLAMP weight at the rounded fold: once the rounding spans many sub-samples the corner is already smooth
        const float foldW = full ? 1.0f / (1.0f + t.vco_tri_fold_corner / (2.0f * std::max (std::fabs (inc), 1e-6f))) : 0.0f;
        const float sawAmp = t.vco_saw_amp_v * std::max (0.5f, 1.0f - std::fabs (f) * t.vco_reset_time_us * 1e-6f);

        // pulse threshold (volts on the ±2 V saw): PW pot + PWM depth * CV
        float thr = 0.0f;
        if (full)
            thr = (pot (cPW) - 0.5f) * 2.0f * t.vco_saw_amp_v + pot (cPWM) * in (jPWM) * (t.vco_saw_amp_v / t.vco_pw_cv_full_v);
        else
            thr = in (jPWM) * (t.vco_saw_amp_v / t.vco_pw_cv_full_v);
        const float pwPhase = clampf ((thr / t.vco_saw_amp_v + 1.0f) * 0.5f, -0.01f, 1.01f); // phase at which pulse rises

        // sync window 0 (off) .. 1 (hard)
        float window = 0.0f;
        if (full && connected (jSync))
        {
            float w = pot (cSync) + in (jSyncCV) / kCVFullV;
            window = std::pow (clampf (w, 0.0f, 1.0f), std::max (0.1f, t.vco_sync_window_curve));
        }
        const bool syncEdge = (full && connected (jSync)) ? sync.rising (in (jSync)) : false;

        // sub-phase delay (fraction of a period), linear or expo law
        float subDelay = 0.0f;
        if (full && connected (jSubCV))
        {
            float c = clampf (in (jSubCV) / kCVFullV, 0.0f, 1.0f);
            subDelay = t.vco_subphase_law > 0.5f ? (std::exp2 (c * 4.0f) - 1.0f) / 15.0f : c;
        }

        float sub[2][5];   // per sub-step: saw, pulse, sub1, sub2, sine (normalised)

        for (int s = 0; s < 2; ++s)
        {
            // --- advance core ---
            float pNew = phase + inc;
            bool wrapped = false; float dWrap = 0.0f;
            if (pNew >= 1.0f) { wrapped = true; dWrap = 1.0f - (pNew - 1.0f) / std::max (inc, 1e-9f); pNew -= 1.0f; }
            else if (pNew < 0.0f) { wrapped = true; dWrap = 1.0f - (-pNew) / std::max (-inc, 1e-9f); pNew += 1.0f; } // thru-zero

            // --- sync reset (assumed mid-interval, d = 0.5) ---
            if (syncEdge && s == 0 && window > 0.001f)
            {
                const float pAtEdge = phase + inc * 0.5f;
                if (window >= 0.999f || pAtEdge > 1.0f - window || wrapped)
                {
                    // saw jumps from its value at the edge down to -1 (normalised)
                    const float sawBefore = 2.0f * clampf (pAtEdge, 0.0f, 1.0f) - 1.0f;
                    bSaw.step (-1.0f - sawBefore, 0.5f);
                    if (full) bTri.step (triOf (0.0f, t.vco_tri_fold_corner) - triOf (sawBefore, t.vco_tri_fold_corner), 0.5f);
                    const bool pulseBefore = pAtEdge > pwPhase;
                    const bool pulseAfter  = (inc * 0.5f) > pwPhase;
                    if (pulseBefore != pulseAfter) { bPulse.step (pulseAfter ? 2.0f : -2.0f, 0.5f); }
                    if (pulseAfter && ! pulseBefore) scheduleSub (subDelay, f, 0.5f);
                    pNew = inc * 0.5f; wrapped = false; pulseHi = pulseAfter;
                    if (pulseHi != pulseBefore) {} // handled above
                }
            }

            if (wrapped)
            {
                bSaw.step (-2.0f, dWrap);
                if (full) bTri.ramp (-8.0f * inc * foldW, dWrap);          // triangle peak: slope +4inc -> -4inc
                if (pulseHi) { bPulse.step (-2.0f, dWrap); pulseHi = false; } // pulse falls at the ramp reset
            }
            if (full && phase < 0.5f && pNew >= 0.5f && ! wrapped)
                bTri.ramp (8.0f * inc * foldW, 1.0f - (pNew - 0.5f) / std::max (inc, 1e-9f)); // trough: -4inc -> +4inc
            // comparator rising edge when the ramp crosses the threshold
            const bool pulseNow = pNew > pwPhase;
            if (pulseNow && ! pulseHi)
            {
                float d = 1.0f - (pNew - pwPhase) / std::max (inc, 1e-9f);
                if (wrapped) d = (pNew - pwPhase) <= inc ? 1.0f - (pNew - pwPhase) / std::max (inc, 1e-9f) : 1.0f;
                bPulse.step (2.0f, d);
                scheduleSub (subDelay, f, d);
                pulseHi = true;
            }
            else if (! pulseNow && pulseHi && ! wrapped)
            {
                // threshold moved above the ramp (PWM) -> pulse drops
                bPulse.step (-2.0f, 0.5f); pulseHi = false;
            }
            phase = pNew;

            // --- sub-oscillator divider chain (delayed toggles) ---
            // queue entries are times measured from the previous sub-sample; fire inside this interval when <= 1
            while (! subQueue.empty() && subQueue.front() <= 1.0f)
            {
                const float d = std::max (0.0f, subQueue.front());
                subQueue.erase (subQueue.begin());
                sub1 = ! sub1;
                bSub1.step (sub1 ? 2.0f : -2.0f, d);
                if (sub1) { sub2 = ! sub2; bSub2.step (sub2 ? 2.0f : -2.0f, d); }
            }
            for (auto& q : subQueue) q -= 1.0f;

            // --- outputs (normalised ±1), BLEP latched ---
            const float sawN  = bSaw.push (2.0f * phase - 1.0f);
            const float pulN  = bPulse.push (pulseHi ? 1.0f : -1.0f);
            const float s1N   = bSub1.push (sub1 ? 1.0f : -1.0f);
            const float s2N   = bSub2.push (sub2 ? 1.0f : -1.0f);
            sub[s][0] = sawN; sub[s][1] = pulN; sub[s][2] = s1N; sub[s][3] = s2N; sub[s][4] = 0.0f;
            if (full)
            {
                // triangle = full-wave rectified (naive) saw with a rounded corner + BLAMP, then the sine shaper
                const float triN = bTri.push (triOf (2.0f * phase - 1.0f, t.vco_tri_fold_corner));
                sub[s][4] = sineShaper (triN, t.vco_sine_shape_mix, t.vco_sine_asym);
            }
        }
        // 4x -> 2x half-band decimation (keeps the 24 kHz band clean; the engine's own decimator does the rest)
        out (jSaw,   clip (dec[0].decimate (sub[0][0], sub[1][0]) * sawAmp));
        out (jPulse, clip (dec[1].decimate (sub[0][1], sub[1][1]) * t.vco_pulse_amp_v));
        if (full)
        {
            out (jSub1, clip (dec[2].decimate (sub[0][2], sub[1][2]) * t.vco_pulse_amp_v));
            out (jSub2, clip (dec[3].decimate (sub[0][3], sub[1][3]) * t.vco_pulse_amp_v));
            out (jSine, clip (dec[4].decimate (sub[0][4], sub[1][4]) * t.vco_sine_amp_v));
        }
        // heater LED (cosmetic)
        warm = std::min (1.0f, warm + dt / std::max (1.0f, t.vintage_warmup_s));
        leds[0] = warm;
    }

private:
    static inline float triOf (float sawN, float c)
    {
        return 2.0f * (std::sqrt (sawN * sawN + c * c) - c) / (std::sqrt (1.0f + c * c) - c) - 1.0f;
    }
    inline void scheduleSub (float delayFrac, float f, float dEdge)
    {
        // delayed copy of the comparator edge: delay = frac * period (in sub-samples)
        const float period = std::fabs (f) > 1e-6f ? 1.0f / (std::fabs (f) * dt * 0.5f) : 1e9f;
        const float when = dEdge + delayFrac * period;   // measured from the previous sub-sample
        if (subQueue.size() < 16) subQueue.push_back (when);
    }
    float phase = 0.0f, lastHz = 0.0f, warm = 0.0f;
    bool pulseHi = false, sub1 = false, sub2 = false;
    BlepLatch bSaw, bPulse, bSub1, bSub2, bTri;
    EdgeIn sync;
    std::vector<float> subQueue;
    HalfBandFast dec[5];
};

// ------------------------------------------------------------------ LFOs ----
// Integrator + Schmitt relaxation oscillator (brief §4.2): symmetry splits the
// charge/discharge rates, so CV-rate changes and resets are phase-continuous.
class LFO : public Module
{
public:
    enum Kind { One = 1, Two = 2, Three = 3 };
    int cFreq, cCV2 = -1, cSym = -1;
    int jCV2 = -1, jTrig = -1, jGate = -1;
    int jTri = -1, jTri2 = -1, jSine = -1, jPulse = -1;
    const int kind;

    explicit LFO (int k) : kind (k)
    {
        cFreq = addPot ("Freq", kind == Three ? Law::Log : Law::Lin, kind == Three ? 0.5f : 0.35f);
        if (kind != Three) { cCV2 = addPot ("CV2", Law::Lin, 0.0f); cSym = addPot ("Symmetry/PW", Law::Lin, 0.5f); }
        if (kind != Three) jCV2 = addIn ("CV2");
        if (kind == One) jTrig = addIn ("Trigger");
        if (kind == Two) jGate = addIn ("Gate");
        if (kind == Three) jSine = addOut ("Sine", false);
        jTri = addOut ("Triangle", false);
        if (kind == Two) jTri2 = addOut ("Double tri", false);
        jPulse = addOut ("Pulse", false);
    }
    void reset() override { x = 0.0f; dir = 1.0f; bP.reset(); trig = {}; gate = {}; }

    void tick() override
    {
        const Tunables& t = *T;
        float f;
        if (kind == Three) f = t.lfo3_min_hz * std::exp2 (pot (cFreq) * t.lfo3_pot_octaves);
        else
        {
            float oct = pot (cFreq) * t.lfo_pot_octaves + pot (cCV2) * in (jCV2);
            f = t.lfo_min_hz * std::exp2 (clampf (oct, -4.0f, 20.0f));
        }
        f = std::min (f, 0.45f * sr);
        float sym = kind == Three ? 0.5f : clampf (pot (cSym), t.lfo_sym_min, t.lfo_sym_max);

        bool hold = false, forceLow = false;
        if (kind == One && connected (jTrig))
        {
            float v = in (jTrig);
            bool edge = trig.update (v, t);
            if (t.lfo1_trig_mode < 0.5f) { if (edge) forceLow = true; }
            else hold = trig.high;
        }
        if (kind == Two && connected (jGate))
        {
            gate.update (in (jGate), t);
            if (t.lfo2_gate_mode < 0.5f) hold = ! gate.high;
            else forceLow = ! gate.high;
        }

        if (forceLow)
        {
            if (dir < 0 || x > 0.0f) { if (dir > 0 && x > 0.5f) {} }
            // integrator slammed to the low rail: pulse goes high (rising segment starts)
            if (dir < 0.0f) bP.step (2.0f, 0.5f);
            x = 0.0f; dir = 1.0f;
        }
        else if (! hold)
        {
            const float rate = dir > 0 ? f / sym : f / (1.0f - sym);
            float xn = x + dir * rate * dt;
            if (xn >= 1.0f)
            {
                const float d = 1.0f - (xn - 1.0f) / std::max (rate * dt, 1e-12f);
                xn = 2.0f - xn; dir = -1.0f; bP.step (-2.0f, d);
            }
            else if (xn <= 0.0f)
            {
                const float d = 1.0f - (-xn) / std::max (rate * dt, 1e-12f);
                xn = -xn; dir = 1.0f; bP.step (2.0f, d);
            }
            x = clampf (xn, 0.0f, 1.0f);
        }
        const float pulseN = bP.push (dir > 0 ? 1.0f : -1.0f);

        if (kind == One)
        {
            out (jTri,   clip (x * t.lfo1_tri_hi_v));
            out (jPulse, clip ((pulseN * 0.5f + 0.5f) * t.sys_gate_out_v));
        }
        else if (kind == Two)
        {
            const float bi = (2.0f * x - 1.0f) * t.lfo2_tri_amp_v;
            out (jTri,   clip (bi));
            out (jTri2,  clip (std::fabs (bi) * (kCVFullV / t.lfo2_tri_amp_v)));
            out (jPulse, clip ((pulseN * 0.5f + 0.5f) * t.sys_gate_out_v));
        }
        else
        {
            const float triN = 2.0f * x - 1.0f;
            out (jTri,   clip (triN * t.lfo3_amp_v));
            out (jSine,  clip (sineShaper (triN, t.lfo3_sine_thd, 0.0f) * t.lfo3_amp_v));
            out (jPulse, clip (pulseN * t.lfo3_amp_v));
        }
    }

private:
    float x = 0.0f, dir = 1.0f;
    BlepLatch bP;
    GateIn trig, gate;
};

} // namespace fenix
