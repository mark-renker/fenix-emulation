// Fénix I emulation — filters. VCF1/VCF2: 2-pole OTA state-variable (brief §4.3),
// VCF3: 5-stage transistor ladder with a 2-pole tap and a 3-input mixer (§4.4).
#pragma once
#include "Core.h"
#include <complex>

namespace fenix {

// OTA saturation referred to the volt domain: the signal is attenuated by
// 'ratio' before the OTA (Vt_eff in mV) and re-amplified after it.
inline float otaSat (float v, float vtMv, float ratio)
{
    const float k = vtMv * 1e-3f * ratio;    // effective knee in volts
    return k * std::tanh (v / k);
}

// ---------------------------------------------------------- VCF1 / VCF2 ----
// Zero-delay-feedback (TPT) SVF; integrator inputs pass through the OTA tanh,
// solved with a short fixed-point iteration. Resonance = damping feedback from
// BP; 'max' is just beyond self-oscillation (calibration note in the brief).
class VCF12 : public Module
{
public:
    int cFreq, cRes, cCV2;
    int jCV1, jCV2, jAudio, jLP, jBP, jHP;

    VCF12()
    {
        cFreq = addPot ("Frequency", Law::Lin, 0.5f);
        cRes  = addPot ("Resonance", Law::Lin, 0.0f);
        cCV2  = addPot ("CV2", Law::Lin, 0.5f);
        jCV1 = addIn ("CV1"); jCV2 = addIn ("CV2"); jAudio = addIn ("Audio");
        jLP = addOut ("LP", true); jBP = addOut ("BP", true); jHP = addOut ("HP", true);
        leds.assign (2, 0.0f); // 0 heater, 1 overload
    }
    void reset() override { s1 = s2 = 0.0f; warm = 0.0f; }

    float currentCutoffHz() const { return lastFc; }

    void tick() override
    {
        const Tunables& t = *T;
        float oct = pot (cFreq) * t.vcf12_pot_octaves + in (jCV1) + (pot (cCV2) - 0.5f) * 2.0f * in (jCV2);
        float fc = t.vcf12_pot_min_hz * std::exp2 (clampf (oct, -4.0f, 16.0f));
        fc = std::min (fc, 0.40f * sr);
        lastFc = fc;
        const float g = std::tan (kPi * fc / sr);
        const float k = 2.0f * (1.0f - pot (cRes) * t.vcf12_res_overdrive);   // 2 = no resonance, <0 = beyond self-osc

        const float x = in (jAudio) + noiseFloor();
        const float vt = t.vcf12_vt_eff_mv, ratio = t.vcf12_atten_ratio;
        const float lim = std::max (0.5f, t.vcf12_osc_limit_v);

        // Exact linear zero-delay solution (Simper TPT), then the OTA tanh on both
        // integrator inputs. The linear core keeps the solve unconditionally
        // stable for any g; the saturation bounds amplitude (self-oscillation
        // limited by the tanh, not by the rails).
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        float hp = x - k * v1 - v2;
        const float u1 = otaSat (hp, vt, ratio);
        v1 = s1 + g * u1;
        const float u2 = otaSat (v1, vt, ratio);
        v2 = s2 + g * u2;
        hp = x - k * v1 - v2;
        // integrator output swing is bounded (OTA output stage): soft limit, linear below ~70 % of it
        s1 = opampClip (flushDenorm (v1 + g * u1), lim, lim * 0.3f);
        s2 = opampClip (flushDenorm (v2 + g * u2), lim, lim * 0.3f);

        out (jLP, clip (v2));
        out (jBP, clip (v1));
        out (jHP, clip (hp));

        warm = std::min (1.0f, warm + dt / std::max (1.0f, t.vintage_warmup_s));
        leds[0] = warm;
        const float ov = std::fabs (v2) > t.vcf12_led_thresh_v ? 1.0f : 0.0f;
        leds[1] = ov > ledOv ? ov : ledOv + (ov - ledOv) * 0.0005f; ledOv = leds[1];
    }

private:
    float s1 = 0.0f, s2 = 0.0f, lastFc = 0.0f, warm = 0.0f, ledOv = 0.0f;
};

// ------------------------------------------------------------------ VCF3 ----
// Huovilainen-style ladder, five stages, tanh on every stage, feedback taken
// from stage 5 (early voicing) blended toward stage 4 (late/'Moog-ish'
// voicing), 2 sub-steps per engine sample (4x host). The '12 dB' output is the
// stage-2 tap inside the feedback loop; '24 dB' is the 5-pole output.
class VCF3 : public Module
{
public:
    int cFreq, cRes, cCV2, cLvl1, cLvl2, cLvl3, cVoicing;
    int jCV1, jCV2, jIn1, jIn2, jIn3, jOut12, jOut24;

    VCF3()
    {
        cFreq = addPot ("Frequency", Law::Lin, 0.5f);
        cRes  = addPot ("Resonance", Law::Lin, 0.0f);
        cCV2  = addPot ("CV2", Law::Lin, 0.5f);
        cLvl1 = addPot ("Level 1", Law::Log, 0.75f);
        cLvl2 = addPot ("Level 2", Law::Log, 0.0f);
        cLvl3 = addPot ("Level 3", Law::Log, 0.0f);
        cVoicing = addSwitch ("Voicing early/late", 2, 1);
        jCV1 = addIn ("CV1"); jCV2 = addIn ("CV2");
        jIn1 = addIn ("Audio 1"); jIn2 = addIn ("Audio 2"); jIn3 = addIn ("Audio 3");
        jOut12 = addOut ("12 dB", true); jOut24 = addOut ("24 dB", true);
        leds.assign (1, 0.0f);
    }
    void reset() override { for (auto& y : st) y = 0.0f; fb = 0.0f; warm = 0.0f; cachedC = -1.0f; }

    // Loop gain at which the chosen feedback topology self-oscillates:
    // H(jw) = (1-c) P^5 + c P^4, P = 1/(1 + j w/wc). Find w where arg H = -pi,
    // then k_osc = 1/|H|. Bisection, cached per c.
    static float analyticKosc (float c)
    {
        auto H = [c] (double wn) { // normalised w/wc
            std::complex<double> P = 1.0 / std::complex<double> (1.0, wn);
            std::complex<double> P4 = P * P * P * P;
            return (1.0 - (double) c) * P4 * P + (double) c * P4;
        };
        double lo = 0.1, hi = 50.0;
        for (int i = 0; i < 80; ++i)
        {
            double mid = 0.5 * (lo + hi);
            double ph = std::arg (H (mid));           // decreasing from 0 toward -pi (wraps past -pi)
            // phase crosses -pi when arg wraps to positive values
            if (ph < 0.0 && ph > -3.14159265358979) lo = mid; else hi = mid;
        }
        double mag = std::abs (H (0.5 * (lo + hi)));
        return (float) (1.0 / std::max (1e-6, mag));
    }
    // Small-signal threshold of the *discrete* model (one-pole stages with a one-sub-sample
    // feedback delay). Returns the loop gain at which |H_d| = 1 at the -pi phase crossing.
    static float discreteKosc (float c, float g)
    {
        auto H = [c, g] (double w) { // w = radians per sub-sample
            std::complex<double> z = std::exp (std::complex<double> (0.0, w));
            std::complex<double> S = (double) g / (1.0 - (1.0 - (double) g) / z);
            std::complex<double> S4 = S * S * S * S;
            return ((1.0 - (double) c) * S4 * S + (double) c * S4) / z;
        };
        double lo = 1e-4, hi = 3.0;
        for (int i = 0; i < 80; ++i)
        {
            double mid = 0.5 * (lo + hi);
            double ph = std::arg (H (mid));
            if (ph < 0.0 && ph > -3.14159265358979) lo = mid; else hi = mid;
        }
        return (float) (1.0 / std::max (1e-6, std::abs (H (0.5 * (lo + hi)))));
    }
    float selfOscillationK() const { return kOsc; }
    float currentK() const { return lastK; }

    void tick() override
    {
        const Tunables& t = *T;
        const bool late = sw (cVoicing) == 1;
        const float c = late ? t.vcf3_late_fb_comp : t.vcf3_early_fb_comp;
        if (std::fabs (c - cachedC) > 1e-6f)
        {
            cachedC = c;
            kAnalog = analyticKosc (c);
            // discretisation error grows ~linearly with fc*dt: fit a slope from two reference cutoffs
            const float dtl0 = dt * 0.5f;
            const float g1 = 1.0f - std::exp (-kTwoPi * 1000.0f * dtl0), g2 = 1.0f - std::exp (-kTwoPi * 6000.0f * dtl0);
            const float k1 = discreteKosc (c, g1), k2 = discreteKosc (c, g2);
            kSlope = ((k2 / kAnalog - 1.0f) - (k1 / kAnalog - 1.0f)) / ((6000.0f - 1000.0f) * dtl0);
            kOffset = (k1 / kAnalog - 1.0f) - kSlope * 1000.0f * dtl0;
        }
        const float drive = late ? t.vcf3_late_drive : t.vcf3_early_drive;

        float oct = pot (cFreq) * t.vcf3_pot_octaves + in (jCV1) + (pot (cCV2) - 0.5f) * 2.0f * in (jCV2);
        float fc = t.vcf3_pot_min_hz * std::exp2 (clampf (oct, -4.0f, 16.0f));
        fc = std::min (fc, 0.22f * sr);   // at the 4x sub-rate this is 0.11 fs_local
        const float dtl = dt * 0.5f;
        const float g = 1.0f - std::exp (-kTwoPi * fc * dtl);
        kOsc = kAnalog * (1.0f + kOffset + kSlope * fc * dtl) * t.vcf3_k_osc_scale;

        // resonance pot -> loop gain: reaches k_osc at the calibrated pot position
        const float pos = pot (cRes);
        const float po = clampf (t.vcf3_selfosc_pot_pos, 0.05f, 1.0f);
        float k = pos <= po ? kOsc * (pos / po)
                            : kOsc * (1.0f + (t.vcf3_k_max_ratio - 1.0f) * (pos - po) / std::max (1e-3f, 1.0f - po));
        lastK = k;

        // 3-input mixer, log pots -> drive
        const float x = (in (jIn1) * pot (cLvl1) + in (jIn2) * pot (cLvl2) + in (jIn3) * pot (cLvl3) + noiseFloor())
                        * t.vcf3_input_scale * drive * (1.0f + t.vcf3_passband_comp * k);

        const float a = std::max (0.05f, t.vcf3_vt_mv / 26.0f);   // tanh scale in ladder units
        auto th = [a] (float v) { return a * std::tanh (v / a); };

        float acc12 = 0.0f, acc24 = 0.0f;
        for (int s = 0; s < 2; ++s)
        {
            const float fbv = (1.0f - c) * st[4] + c * st[3];
            float u = th (x - k * fbv);
            for (int i = 0; i < 5; ++i)
            {
                const float y = st[i] + g * (u - th (st[i]));
                st[i] = flushDenorm (y);
                u = th (y);
            }
            acc12 += st[1]; acc24 += st[4];
        }
        const float scale = 1.0f / (t.vcf3_input_scale * drive);
        out (jOut12, clip (acc12 * 0.5f * scale * t.vcf3_tap12_gain));
        out (jOut24, clip (acc24 * 0.5f * scale * t.vcf3_tap24_gain));

        warm = std::min (1.0f, warm + dt / std::max (1.0f, t.vintage_warmup_s));
        leds[0] = warm;
    }

private:
    float st[5] = { 0, 0, 0, 0, 0 };
    float fb = 0.0f, warm = 0.0f, cachedC = -1.0f, kOsc = 4.0f, kAnalog = 4.0f, kSlope = 0.0f, kOffset = 0.0f, lastK = 0.0f;
};

} // namespace fenix
