// Fénix I emulation — envelope generators (brief §4.5).
// EG1: A–Peak(hold)–D–S–R + 'Accent' decay-only envelope.
// EG2: A–Peak–(D or R, switch) + Accent, with a 50/50 Mix output.
// EG3: Delay–then–AD (near-instant attack) plus delayed-gate outputs (+/−).
// Attack/hold are RC charges toward an overshoot target (so the attack ends at
// ~63–90 % of the curve); decay/release are exponential with a CV-steerable
// time constant (1 V ≈ ×2); the peak-hold is a timer, not a curve.
#pragma once
#include "Core.h"

namespace fenix {

// pot (0..1, with its law) -> time constant between min and max
inline float egTime (float law01, float minMs, float maxS)
{
    return expoMap (law01, minMs * 1e-3f, maxS);
}

// The time pots: 2M2 log (B) for attack/peak; 10k reverse-log (C) for
// decay/release but 'wired reversed' so the response in the clockwise
// direction equals B (1 - C(1-x) == B(x)). Both therefore use the B map.
inline float timePot (const Module& m, int c) { return potLaw (Law::Log, m.ctls[(size_t) c].s); }

// Level pots (Sustain, Accent level): see eg.level_pot_law
inline float levelPot (const Module& m, int c, const Tunables& t)
{
    const float x = m.ctls[(size_t) c].s;
    return t.eg_level_pot_law > 0.5f ? potLaw (Law::Log, x) : potLaw (Law::RevLog, x);
}

// Accent sub-envelope: instantaneous attack to a CV-set level, VC decay.
struct Accent
{
    float v = 0.0f;
    void trigger (float level) { v = std::max (v, level); }
    inline float step (float tauS, float dt)
    {
        v += (0.0f - v) * (1.0f - std::exp (-dt / std::max (tauS, 1e-5f)));
        return v = flushDenorm (v);
    }
};

class EG1 : public Module
{
public:
    enum Stage { Idle, Attack, Peak, Decay, Sustain, Release };
    int cA, cPeak, cD, cS, cR, cAcc;
    int jGate, jDecayCV, jPeakCV, jRelCV, jAccTimeCV, jAccLvlCV, jOut, jAccOut;

    EG1()
    {
        cA = addPot ("Attack", Law::Log, 0.1f);
        cPeak = addPot ("Peak time", Law::Log, 0.0f);
        cD = addPot ("Decay", Law::RevLog, 0.3f);
        cS = addPot ("Sustain", Law::RevLog, 0.6f);
        cR = addPot ("Release", Law::RevLog, 0.3f);
        cAcc = addPot ("Accent", Law::RevLog, 0.2f);
        jGate = addIn ("Gate"); jDecayCV = addIn ("Decay time CV"); jPeakCV = addIn ("Peak level CV");
        jRelCV = addIn ("Release time CV"); jAccTimeCV = addIn ("Accent time CV"); jAccLvlCV = addIn ("Accent level CV");
        jOut = addOut ("CV out", false); jAccOut = addOut ("Accent out", false);
        leds.assign (1, 0.0f);
    }
    void reset() override { v = 0.0f; stage = Idle; gate = {}; acc = {}; peakT = 0.0f; }
    int currentStage() const { return stage; }

    void tick() override
    {
        const Tunables& t = *T;
        const bool rise = gate.update (in (jGate), t);
        const float full = t.eg_out_v;
        const float peakLevel = connected (jPeakCV) ? clampf (full + in (jPeakCV), 0.5f, full) : full;

        if (rise)
        {
            if (t.eg_retrigger_from_level < 0.5f) v = 0.0f;
            stage = Attack;
            float lvl = connected (jAccLvlCV) ? clampf (in (jAccLvlCV) / kCVFullV, 0.0f, 1.0f) : 1.0f;
            acc.trigger (lvl * full);
        }
        if (! gate.high && stage != Idle && stage != Release) stage = Release;

        switch (stage)
        {
            case Attack:
            {
                const float tau = egTime (timePot (*this, cA), t.eg_time_min_ms, t.eg_time_max_s);
                const float target = peakLevel * t.eg_overshoot_target;
                v += (target - v) * (1.0f - std::exp (-dt / tau));
                if (v >= peakLevel) { v = peakLevel; stage = Peak; peakT = egTime (timePot (*this, cPeak), t.eg_peak_min_ms, t.eg_peak_max_s); }
                break;
            }
            case Peak:
                peakT -= dt;
                if (peakT <= 0.0f) stage = Decay;
                break;
            case Decay:
            {
                float tau = egTime (timePot (*this, cD), t.eg_time_min_ms, t.eg_time_max_s)
                          * std::exp2 (in (jDecayCV) * t.eg_cv_oct_per_v);
                const float sus = levelPot (*this, cS, t) * peakLevel;
                v += (sus - v) * (1.0f - std::exp (-dt / tau));
                if (std::fabs (v - sus) < 1e-3f) stage = Sustain;
                break;
            }
            case Sustain:
                v = levelPot (*this, cS, t) * peakLevel;
                break;
            case Release:
            {
                float tau = egTime (timePot (*this, cR), t.eg_time_min_ms, t.eg_time_max_s)
                          * std::exp2 (in (jRelCV) * t.eg_cv_oct_per_v);
                v += (0.0f - v) * (1.0f - std::exp (-dt / tau));
                if (v < 1e-4f) { v = 0.0f; stage = Idle; }
                break;
            }
            default: break;
        }
        v = flushDenorm (v);
        out (jOut, clip (v));

        float accTau = egTime (timePot (*this, cAcc), t.eg_time_min_ms, t.eg_time_max_s)
                     * std::exp2 (in (jAccTimeCV) * t.eg_cv_oct_per_v);
        out (jAccOut, clip (acc.step (accTau, dt)));
        leds[0] = v / full;
    }

private:
    float v = 0.0f, peakT = 0.0f; int stage = Idle;
    GateIn gate; Accent acc;
};

class EG2 : public Module
{
public:
    enum Stage { Idle, Attack, Peak, Decay, Hold, Release };
    int cA, cPeak, cDR, cAcc, cMode;
    int jGate, jDRCV, jAccLvlCV, jOut, jAccOut, jMix;

    EG2()
    {
        cA = addPot ("Attack", Law::Log, 0.05f);
        cPeak = addPot ("Peak time", Law::Log, 0.0f);
        cDR = addPot ("Decay/Release", Law::RevLog, 0.35f);
        cAcc = addPot ("Accent", Law::RevLog, 0.2f);
        cMode = addSwitch ("D/R", 2, 0);   // 0 = D (AD), 1 = R (ASR)
        jGate = addIn ("Gate"); jDRCV = addIn ("D/R time CV"); jAccLvlCV = addIn ("Accent level CV");
        jOut = addOut ("CV out", false); jAccOut = addOut ("Accent out", false); jMix = addOut ("Mix out", false);
        leds.assign (1, 0.0f);
    }
    void reset() override { v = 0.0f; stage = Idle; gate = {}; acc = {}; peakT = 0.0f; }

    void tick() override
    {
        const Tunables& t = *T;
        const bool rise = gate.update (in (jGate), t);
        const float full = t.eg_out_v;
        const bool rMode = sw (cMode) == 1;
        if (rise)
        {
            if (t.eg_retrigger_from_level < 0.5f) v = 0.0f;
            stage = Attack;
            float lvl = connected (jAccLvlCV) ? clampf (in (jAccLvlCV) / kCVFullV, 0.0f, 1.0f) : 1.0f;
            acc.trigger (lvl * full);
        }
        if (rMode && ! gate.high && (stage == Hold || stage == Attack || stage == Peak)) stage = Release;

        const float tauDR = egTime (timePot (*this, cDR), t.eg_time_min_ms, t.eg_time_max_s)
                          * std::exp2 (in (jDRCV) * t.eg_cv_oct_per_v);
        switch (stage)
        {
            case Attack:
            {
                const float tau = egTime (timePot (*this, cA), t.eg_time_min_ms, t.eg_time_max_s);
                v += (full * t.eg_overshoot_target - v) * (1.0f - std::exp (-dt / tau));
                if (v >= full) { v = full; stage = Peak; peakT = egTime (timePot (*this, cPeak), t.eg_peak_min_ms, t.eg_peak_max_s); }
                break;
            }
            case Peak:
                peakT -= dt;
                if (peakT <= 0.0f) stage = rMode ? (gate.high ? Hold : Release) : Decay;
                break;
            case Hold: v = full; break;
            case Decay: case Release:
                v += (0.0f - v) * (1.0f - std::exp (-dt / tauDR));
                if (v < 1e-4f) { v = 0.0f; stage = Idle; }
                break;
            default: break;
        }
        v = flushDenorm (v);
        const float a = acc.step (egTime (timePot (*this, cAcc), t.eg_time_min_ms, t.eg_time_max_s), dt);
        out (jOut, clip (v));
        out (jAccOut, clip (a));
        out (jMix, clip (0.5f * (v + a)));
        leds[0] = v / full;
    }

private:
    float v = 0.0f, peakT = 0.0f; int stage = Idle;
    GateIn gate; Accent acc;
};

class EG3 : public Module
{
public:
    enum Stage { Idle, Delay, Attack, Decay };
    int cDelay, cDecay;
    int jGate, jOut, jGatePos, jGateNeg;

    EG3()
    {
        cDelay = addPot ("Delay", Law::Log, 0.2f);
        cDecay = addPot ("Decay", Law::Log, 0.3f);
        jGate = addIn ("Gate");
        jOut = addOut ("CV out", false); jGatePos = addOut ("Delayed gate +", false); jGateNeg = addOut ("Delayed gate -", false);
        leds.assign (1, 0.0f);
    }
    void reset() override { v = 0.0f; stage = Idle; gate = {}; timer = 0.0f; dg = false; }

    void tick() override
    {
        const Tunables& t = *T;
        const bool rise = gate.update (in (jGate), t);
        const float full = t.eg_out_v;
        if (rise) { stage = Delay; timer = egTime (timePot (*this, cDelay), t.eg3_gate_delay_min_ms, t.eg3_gate_delay_max_s); dg = false; }
        if (! gate.high) dg = false;
        switch (stage)
        {
            case Delay:
                timer -= dt;
                if (timer <= 0.0f) { stage = Attack; if (gate.high) dg = true; }
                break;
            case Attack:
                v += (full * t.eg_overshoot_target - v) * (1.0f - std::exp (-dt / (t.eg_accent_attack_ms * 1e-3f * 10.0f)));
                if (v >= full) { v = full; stage = Decay; }
                break;
            case Decay:
            {
                const float tau = egTime (timePot (*this, cDecay), t.eg_time_min_ms, t.eg_time_max_s);
                v += (0.0f - v) * (1.0f - std::exp (-dt / tau));
                if (v < 1e-4f) { v = 0.0f; stage = Idle; }
                break;
            }
            default: break;
        }
        v = flushDenorm (v);
        out (jOut, clip (v));
        out (jGatePos, dg ? t.sys_gate_out_v : 0.0f);
        out (jGateNeg, dg ? 0.0f : t.sys_gate_out_v);
        leds[0] = v / full;
    }

private:
    float v = 0.0f, timer = 0.0f; int stage = Idle; bool dg = false;
    GateIn gate;
};

} // namespace fenix
