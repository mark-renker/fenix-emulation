// Fénix I emulation — patchable engine core (no JUCE dependency; headless-testable).
//
// Electrical contract (brief §2): everything is a sample-rate voltage. Audio is
// ±2 V nominal (4 Vpp), CVs are −4…+4 V or 0…+8 V, gates are 0 / +8 V, and every
// op-amp output soft-clips at ±13.5 V. There is no CV/audio distinction (§5).
//
// Graph rules (§5): any output may feed any input, including itself. The
// scheduler orders modules topologically; edges that close a cycle get a
// one-sample delay at the (oversampled) engine rate and are listed in
// Engine::delayedEdges() so the harness can report which edge got the delay.
//
// Stacking (§2): several cables into one INPUT sum (fan-in). Two OUTPUTS tied
// together with a cable form a "net" that resolves to the current-limited
// average described in the brief — never a sum.
#pragma once
#include "Tunables.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

namespace fenix {

constexpr float kAudioV   = 2.0f;   // nominal audio amplitude (±)
constexpr float kCVFullV  = 8.0f;   // unipolar CV / EG full scale
constexpr float kCVBiV    = 4.0f;   // bipolar CV full scale (±)
constexpr float kPi       = 3.14159265358979f;
constexpr float kTwoPi    = 6.28318530717959f;

inline float flushDenorm (float v) { return std::fabs (v) < 1.0e-18f ? 0.0f : v; }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

// Op-amp output stage: linear up to (clip - knee), then a smooth knee that
// lands at exactly ±clip. Deterministic and cheap (brief §5 'soft knee <= 0.5 V').
inline float opampClip (float v, float clip, float knee)
{
    const float lin = clip - knee;
    const float a = std::fabs (v);
    if (a <= lin) return v;
    if (knee <= 1e-6f) return v > 0 ? clip : -clip;
    const float x = (a - lin) / knee;              // 0..inf
    const float y = lin + knee * std::tanh (x);     // asymptotes to clip
    return v > 0 ? y : -y;
}

// Pot laws (brief §5). 'B' audio taper: 10 % at mid-rotation; 'C' reverse log is
// the mirror image; 'S' is a smoothstep-like curve around the centre.
enum class Law : uint8_t { Lin, Log, RevLog, S };
inline float potLaw (Law law, float x)
{
    x = clampf (x, 0.0f, 1.0f);
    switch (law)
    {
        case Law::Lin:    return x;
        case Law::Log:    // 10 % at mid: y = (b^x - 1)/(b - 1) with b = 81
        {
            constexpr float b = 81.0f;
            return (std::pow (b, x) - 1.0f) / (b - 1.0f);
        }
        case Law::RevLog: // mirror image of B: 90 % at mid
        {
            constexpr float b = 81.0f;
            return 1.0f - (std::pow (b, 1.0f - x) - 1.0f) / (b - 1.0f);
        }
        case Law::S:      return x * x * (3.0f - 2.0f * x);
    }
    return x;
}

// Exponential time mapping shared by the 2M2-log time pots: min at CCW, max at CW.
inline float expoMap (float x01, float minV, float maxV)
{
    return minV * std::pow (maxV / minV, clampf (x01, 0.0f, 1.0f));
}

// Gate/trigger input with hysteresis (brief §2: high > +5 V, low < +1 V).
struct GateIn
{
    bool high = false;
    // returns true on the rising edge
    bool update (float v, const Tunables& t)
    {
        if (! high && v > t.sys_gate_hi_v) { high = true; return true; }
        if (high && v < t.sys_gate_lo_v) high = false;
        return false;
    }
};

// Windowed-sinc half-band FIR for 2x decimation / interpolation (brief §6).
class HalfBand
{
public:
    void design (int numTaps = 63, float beta = 9.0f)
    {
        taps = numTaps | 1;
        h.assign ((size_t) taps, 0.0f);
        const int M = (taps - 1) / 2;
        auto I0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 50; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-12 * s) break; } return s; };
        const double denom = I0 (beta);
        double sum = 0;
        for (int n = 0; n < taps; ++n)
        {
            const int k = n - M;
            const double sinc = k == 0 ? 0.5 : std::sin (0.5 * (double) kPi * k) / ((double) kPi * k); // cutoff at fs/4
            const double r = (double) k / (double) M;
            const double w = I0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / denom;
            h[(size_t) n] = (float) (sinc * w);
            sum += h[(size_t) n];
        }
        for (auto& c : h) c = (float) (c / sum);
        z.assign ((size_t) taps, 0.0f); pos = 0;
    }
    void reset() { std::fill (z.begin(), z.end(), 0.0f); pos = 0; }
    inline void push (float x) { z[(size_t) pos] = x; if (++pos == taps) pos = 0; }
    inline float out() const
    {
        float acc = 0.0f; int i = pos;
        for (int n = 0; n < taps; ++n) { if (--i < 0) i = taps - 1; acc += h[(size_t) n] * z[(size_t) i]; }
        return acc;
    }
    // decimate: feed two samples, read one
    inline float decimate (float a, float b) { push (a); push (b); return out(); }
    // interpolate: feed one sample, get two (zero-stuff * 2)
    inline void interpolate (float x, float& a, float& b) { push (2.0f * x); a = out(); push (0.0f); b = out(); }
    int taps = 0, pos = 0;
    std::vector<float> h, z;
};

// Half-band decimator that only touches the non-zero taps (centre + odd offsets).
class HalfBandFast
{
public:
    void design (int numTaps = 23, float beta = 8.0f)
    {
        HalfBand full; full.design (numTaps, beta);
        taps = full.taps; M = (taps - 1) / 2;
        centre = full.h[(size_t) M];
        odd.clear();
        for (int k = 1; M + k < taps; k += 2) odd.push_back (full.h[(size_t) (M + k)]);
        z.assign ((size_t) taps, 0.0f); pos = 0;
    }
    void reset() { std::fill (z.begin(), z.end(), 0.0f); pos = 0; }
    inline float decimate (float a, float b)
    {
        z[(size_t) pos] = a; if (++pos == taps) pos = 0;
        z[(size_t) pos] = b; if (++pos == taps) pos = 0;
        // newest sample is at pos-1; tap n reads z[pos-1-n]
        auto at = [&] (int n) { int i = pos - 1 - n; while (i < 0) i += taps; return z[(size_t) i]; };
        float acc = centre * at (M);
        for (size_t k = 0; k < odd.size(); ++k)
        {
            const int o = 2 * (int) k + 1;
            acc += odd[k] * (at (M - o) + at (M + o));
        }
        return acc;
    }
    int taps = 0, M = 0, pos = 0; float centre = 0.5f;
    std::vector<float> odd, z;
};

// ------------------------------------------------------------------ graph ----
enum class JackType  : uint8_t { In, Out };
enum class JackColor : uint8_t { Black, Green, Blue }; // input, audio out, CV out (brief §2)

struct Jack
{
    const char* name = "";
    JackType  type  = JackType::In;
    JackColor color = JackColor::Black;
    float value = 0.0f;   // output value (Out jacks)
};

struct Ctl
{
    const char* name = "";
    float v = 0.0f;       // target (host/UI), 0..1 or step index
    float s = 0.0f;       // smoothed (DSP-side)
    float def = 0.0f;
    int   steps = 0;      // 0 = continuous; >0 = switch with N positions (v = index)
    Law   law = Law::Lin;
    bool  momentary = false;
};

class Engine;

class Module
{
public:
    virtual ~Module() = default;

    std::string id;      // stable id, e.g. "vco1"
    std::string title;   // panel title
    std::vector<Jack> jacks;
    std::vector<Ctl>  ctls;
    std::vector<float> leds; // 0..1, written by tick, read by UI

    const Tunables* T = nullptr;
    float sr = 96000.0f;  // engine (oversampled) rate
    float dt = 1.0f / 96000.0f;

    virtual void prepare (double sampleRate) { sr = (float) sampleRate; dt = 1.0f / sr; }
    virtual void reset() {}
    virtual void tick() = 0;   // one sample: read inputs, write jack values

    // ---- helpers wired by Engine::rebuild() ----
    inline float in (int jackIdx) const            // fan-in sum, rail-clamped
    {
        const auto& f = feeds[(size_t) jackIdx];
        float s = 0.0f;
        for (auto* p : f) s += *p;
        return s > 15.0f ? 15.0f : (s < -15.0f ? -15.0f : s);
    }
    inline bool connected (int jackIdx) const { return ! feeds[(size_t) jackIdx].empty(); }
    inline float pot (int c) const { return potLaw (ctls[(size_t) c].law, ctls[(size_t) c].s); }
    inline int   sw  (int c) const { return (int) std::lround (ctls[(size_t) c].v); }
    inline void  out (int j, float v) { jacks[(size_t) j].value = flushDenorm (v); }
    inline float clip (float v) const { return opampClip (v, T->sys_clip_v, T->sys_clip_knee_v); }

    // cosmetic op-amp noise floor (brief §5), per-module deterministic generator
    inline float noiseFloor()
    {
        if (noiseGain <= 0.0f) return 0.0f;
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        const float u = (float) ((rng >> 11) & 0xFFFFFF) / 8388608.0f - 1.0f; // -1..1
        return u * noiseGain;
    }
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    float noiseGain = 0.0f;   // set by Engine::prepare from sys.noise_floor_dbfs

    int addIn  (const char* n) { jacks.push_back ({ n, JackType::In, JackColor::Black, 0.0f }); return (int) jacks.size() - 1; }
    int addOut (const char* n, bool audio) { jacks.push_back ({ n, JackType::Out, audio ? JackColor::Green : JackColor::Blue, 0.0f }); return (int) jacks.size() - 1; }
    int addPot (const char* n, Law law, float def = 0.5f) { ctls.push_back ({ n, def, def, def, 0, law, false }); return (int) ctls.size() - 1; }
    int addSwitch (const char* n, int steps, int def = 0) { ctls.push_back ({ n, (float) def, (float) def, (float) def, steps, Law::Lin, false }); return (int) ctls.size() - 1; }
    int addButton (const char* n) { ctls.push_back ({ n, 0.0f, 0.0f, 0.0f, 2, Law::Lin, true }); return (int) ctls.size() - 1; }

    std::vector<std::vector<const float*>> feeds; // per input jack: pointers to source values
};

struct Cable
{
    int modA = 0, jackA = 0, modB = 0, jackB = 0;
    bool operator== (const Cable& o) const
    {
        return (modA == o.modA && jackA == o.jackA && modB == o.modB && jackB == o.jackB)
            || (modA == o.modB && jackA == o.jackB && modB == o.modA && jackB == o.jackA);
    }
};

// Two or more outputs tied together (brief §2 'stacked outputs' rule).
struct Net
{
    std::vector<const float*> drivers;
    float value = 0.0f;
    // KCL with current-limited drivers: find v where sum_i clamp((v_i - v)/R, -I_sink, +I_source) = 0.
    // The sum is monotonic in v, so a short bisection is exact. With asymmetric limits the
    // sourcing driver wins a strong fight (v = v_hi - I_sink*R): deterministic and cheap.
    void resolve (const Tunables& t)
    {
        const int n = (int) drivers.size();
        if (n == 0) { value = 0.0f; return; }
        float lo = 1e9f, hi = -1e9f;
        for (auto* d : drivers) { lo = std::min (lo, *d); hi = std::max (hi, *d); }
        if (hi - lo < 1e-6f) { value = lo; return; }
        const float r = std::max (1.0f, t.sys_rout_ohm);
        const float iSrc = t.sys_ilimit_ma * 1e-3f, iSnk = iSrc * clampf (t.sys_isink_ratio, 0.05f, 1.0f);
        auto net = [&] (float v) { float s = 0.0f; for (auto* d : drivers) s += clampf ((*d - v) / r, -iSnk, iSrc); return s; };
        float a = lo, b = hi;
        for (int i = 0; i < 24; ++i)
        {
            const float m = 0.5f * (a + b);
            if (net (m) > 0.0f) a = m; else b = m;
        }
        value = 0.5f * (a + b);
    }
};

struct DelayedEdge { int srcMod, srcJack, dstMod, dstJack; };

class Engine
{
public:
    Tunables tun;
    std::vector<std::unique_ptr<Module>> mods;
    std::vector<Cable> cables;
    std::vector<Net>   nets;
    std::vector<DelayedEdge> delayed;   // edges that received the one-sample delay
    int oversample = 2;
    double hostRate = 48000.0, rate = 96000.0;

    template <typename T, typename... A>
    T* add (const char* id, const char* title, A&&... a)
    {
        auto m = std::make_unique<T> (std::forward<A> (a)...);
        m->id = id; m->title = title; m->T = &tun;
        m->rng = 0x9E3779B97F4A7C15ull ^ (uint64_t) (mods.size() + 1) * 0xD1B54A32D192ED03ull;
        auto* raw = m.get();
        mods.push_back (std::move (m));
        return raw;
    }
    Module* byId (const std::string& id)
    {
        for (auto& m : mods) if (m->id == id) return m.get();
        return nullptr;
    }
    int indexOf (const Module* m) const
    {
        for (size_t i = 0; i < mods.size(); ++i) if (mods[i].get() == m) return (int) i;
        return -1;
    }
    int jackIndex (const Module* m, const std::string& name) const
    {
        for (size_t i = 0; i < m->jacks.size(); ++i) if (name == m->jacks[i].name) return (int) i;
        return -1;
    }
    int ctlIndex (const Module* m, const std::string& name) const
    {
        for (size_t i = 0; i < m->ctls.size(); ++i) if (name == m->ctls[i].name) return (int) i;
        return -1;
    }

    void prepare (double hostSampleRate, int oversampleFactor = 2)
    {
        hostRate = hostSampleRate; oversample = std::max (1, oversampleFactor);
        rate = hostRate * oversample;
        smoothCoef = 1.0f - std::exp (-1.0f / (float) (rate * std::max (0.1f, tun.sys_ctl_smooth_ms) * 1e-3));
        const float ng = tun.sys_noise_floor_dbfs <= -199.0f ? 0.0f : kCVFullV * std::pow (10.0f, tun.sys_noise_floor_dbfs / 20.0f) * 1.7f;
        for (auto& m : mods) { m->noiseGain = ng; m->prepare (rate); m->reset(); for (auto& c : m->ctls) c.s = c.v; }
        rebuild();
    }
    void resetState() { for (auto& m : mods) { m->reset(); for (auto& j : m->jacks) j.value = 0.0f; } }

    // ---- patching ----
    bool connect (int mA, int jA, int mB, int jB)
    {
        if (mA < 0 || mB < 0 || mA >= (int) mods.size() || mB >= (int) mods.size()) return false;
        if (jA < 0 || jB < 0 || jA >= (int) mods[(size_t) mA]->jacks.size() || jB >= (int) mods[(size_t) mB]->jacks.size()) return false;
        if (mA == mB && jA == jB) return false;
        const auto& a = mods[(size_t) mA]->jacks[(size_t) jA];
        const auto& b = mods[(size_t) mB]->jacks[(size_t) jB];
        if (a.type == JackType::In && b.type == JackType::In) return false; // two inputs: no source, no-op
        Cable c { mA, jA, mB, jB };
        for (auto& e : cables) if (e == c) return false;
        cables.push_back (c);
        rebuild();
        return true;
    }
    bool connect (Module* a, const char* ja, Module* b, const char* jb)
    {
        return connect (indexOf (a), jackIndex (a, ja), indexOf (b), jackIndex (b, jb));
    }
    void disconnectAt (int m, int j)
    {
        cables.erase (std::remove_if (cables.begin(), cables.end(),
            [&] (const Cable& c) { return (c.modA == m && c.jackA == j) || (c.modB == m && c.jackB == j); }), cables.end());
        rebuild();
    }
    void removeCable (const Cable& c) { cables.erase (std::remove (cables.begin(), cables.end(), c), cables.end()); rebuild(); }
    void clearCables() { cables.clear(); rebuild(); }

    // Resolve feeds, nets, and the execution order.
    void rebuild()
    {
        const int M = (int) mods.size();
        for (auto& m : mods) m->feeds.assign (m->jacks.size(), {});
        nets.clear(); delayed.clear();

        // --- union-find over OUTPUT jacks tied together by Out–Out cables ---
        struct OJ { int m, j; };
        std::vector<OJ> outs; std::vector<std::vector<int>> outIdx ((size_t) M);
        for (int mi = 0; mi < M; ++mi)
        {
            outIdx[(size_t) mi].assign (mods[(size_t) mi]->jacks.size(), -1);
            for (int ji = 0; ji < (int) mods[(size_t) mi]->jacks.size(); ++ji)
                if (mods[(size_t) mi]->jacks[(size_t) ji].type == JackType::Out)
                { outIdx[(size_t) mi][(size_t) ji] = (int) outs.size(); outs.push_back ({ mi, ji }); }
        }
        std::vector<int> parent (outs.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int) i;
        std::function<int (int)> find = [&] (int x) { while (parent[(size_t) x] != x) { parent[(size_t) x] = parent[(size_t) parent[(size_t) x]]; x = parent[(size_t) x]; } return x; };
        auto isOut = [&] (int m, int j) { return mods[(size_t) m]->jacks[(size_t) j].type == JackType::Out; };
        for (auto& c : cables)
            if (isOut (c.modA, c.jackA) && isOut (c.modB, c.jackB))
                parent[(size_t) find (outIdx[(size_t) c.modA][(size_t) c.jackA])] = find (outIdx[(size_t) c.modB][(size_t) c.jackB]);

        // group → net (only for groups with >1 driver)
        std::vector<int> groupNet (outs.size(), -1);
        std::vector<int> groupSize (outs.size(), 0);
        for (size_t i = 0; i < outs.size(); ++i) ++groupSize[(size_t) find ((int) i)];
        for (size_t i = 0; i < outs.size(); ++i)
        {
            int g = find ((int) i);
            if (groupSize[(size_t) g] > 1)
            {
                if (groupNet[(size_t) g] < 0) { groupNet[(size_t) g] = (int) nets.size(); nets.emplace_back(); }
                nets[(size_t) groupNet[(size_t) g]].drivers.push_back (&mods[(size_t) outs[i].m]->jacks[(size_t) outs[i].j].value);
            }
        }
        auto sourcePtr = [&] (int m, int j) -> const float*
        {
            int g = find (outIdx[(size_t) m][(size_t) j]);
            int n = groupNet[(size_t) g];
            return n >= 0 ? &nets[(size_t) n].value : &mods[(size_t) m]->jacks[(size_t) j].value;
        };
        auto sourceNode = [&] (int m, int j) -> int
        {
            int g = find (outIdx[(size_t) m][(size_t) j]);
            int n = groupNet[(size_t) g];
            return n >= 0 ? M + n : m;
        };

        // --- feeds: each In jack sums the (net-resolved) sources cabled to it ---
        const int NN = M + (int) nets.size();
        std::vector<std::vector<std::pair<int, DelayedEdge>>> adj ((size_t) NN); // node -> (dest node, edge)
        for (auto& c : cables)
        {
            auto wire = [&] (int om, int oj, int im, int ij)
            {
                auto* p = sourcePtr (om, oj);
                auto& f = mods[(size_t) im]->feeds[(size_t) ij];
                if (std::find (f.begin(), f.end(), p) == f.end())   // a net feeds an input once
                {
                    f.push_back (p);
                    adj[(size_t) sourceNode (om, oj)].push_back ({ im, { om, oj, im, ij } });
                }
            };
            bool aOut = isOut (c.modA, c.jackA), bOut = isOut (c.modB, c.jackB);
            if (aOut && ! bOut) wire (c.modA, c.jackA, c.modB, c.jackB);
            else if (bOut && ! aOut) wire (c.modB, c.jackB, c.modA, c.jackA);
        }
        // net nodes depend on their drivers
        for (size_t n = 0; n < nets.size(); ++n)
            for (auto* d : nets[n].drivers)
                for (size_t i = 0; i < outs.size(); ++i)
                    if (d == &mods[(size_t) outs[i].m]->jacks[(size_t) outs[i].j].value)
                        adj[(size_t) outs[i].m].push_back ({ M + (int) n, { outs[i].m, outs[i].j, -1, -1 } });

        // --- DFS topological order with back-edge (cycle) detection ---
        std::vector<int> color ((size_t) NN, 0); // 0 white, 1 grey, 2 black
        std::vector<int> post; post.reserve ((size_t) NN);
        std::function<void (int)> dfs = [&] (int u)
        {
            color[(size_t) u] = 1;
            for (auto& [v, e] : adj[(size_t) u])
            {
                if (color[(size_t) v] == 0) dfs (v);
                else if (color[(size_t) v] == 1) { if (e.dstMod >= 0) delayed.push_back (e); else delayed.push_back (e); }
            }
            color[(size_t) u] = 2;
            post.push_back (u);
        };
        for (int u = 0; u < NN; ++u) if (color[(size_t) u] == 0) dfs (u);
        order.assign (post.rbegin(), post.rend());
    }

    // one engine sample
    inline void tick()
    {
        const int M = (int) mods.size();
        for (auto& m : mods)
            for (auto& c : m->ctls)
                c.s = c.steps > 0 ? c.v : c.s + smoothCoef * (c.v - c.s);
        for (int node : order)
        {
            if (node < M) mods[(size_t) node]->tick();
            else nets[(size_t) (node - M)].resolve (tun);
        }
    }
    const std::vector<int>& executionOrder() const { return order; }

private:
    std::vector<int> order;
    float smoothCoef = 0.001f;
};

} // namespace fenix
