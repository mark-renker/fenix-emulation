// Fénix I emulation — measurement harness (brief §7). Headless, no JUCE.
// Runs every test against documented behaviour, prints a report, writes
// report.json, and exits with the number of failures.
//
//   fenix_harness [--params file.json] [--dump-params out.json] [--report out.json] [--quick]
#define _USE_MATH_DEFINES
#include "../src/engine/System.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>

using namespace fenix;

static int passed = 0, failed = 0;
static std::string report = "[\n";
static bool quick = false;
static std::string only;
static bool want (const char* g) { return only.empty() || only == g; }

static void CHECK (bool ok, const char* name, double measured = 0, double limit = 0, const char* unit = "")
{
    if (ok) ++passed; else ++failed;
    std::printf ("  %s  %-58s", ok ? "PASS" : "FAIL", name);
    if (measured != 0 || limit != 0) std::printf (" %10.4g %s (limit %g)", measured, unit, limit);
    std::printf ("\n");
    if (report.size() > 3) report += ",\n";
    char buf[512];
    std::snprintf (buf, sizeof (buf), "  {\"test\": \"%s\", \"pass\": %s, \"measured\": %g, \"limit\": %g, \"unit\": \"%s\"}",
                   name, ok ? "true" : "false", measured, limit, unit);
    report += buf;
}
static void GROUP (const char* g) { std::printf ("\n== %s ==\n", g); }

static constexpr double HOST_SR = 48000.0;

struct Rig
{
    System sys;
    double sr;   // engine rate
    explicit Rig (const Tunables* tun = nullptr)
    {
        if (tun) sys.engine.tun = *tun;
        sys.prepare (HOST_SR, 2);
        sr = sys.engine.rate;
    }
    void run (int n, const std::function<void (int)>& pre = {}) { for (int i = 0; i < n; ++i) { if (pre) pre (i); sys.engine.tick(); } }
    float& ctl (Module* m, const char* name) { int i = sys.engine.ctlIndex (m, name); return m->ctls[(size_t) i].v; }
    int jack (Module* m, const char* name) { return sys.engine.jackIndex (m, name); }
    float val (Module* m, const char* name) { return m->jacks[(size_t) jack (m, name)].value; }
    // settle all control smoothing
    void settle (int n = 4000) { run (n); }
    // host-rate capture (decimated) of a jack
    std::vector<float> captureHost (Module* m, const char* name, int hostSamples, const std::function<void (int)>& pre = {})
    {
        HalfBand hb; hb.design (63, 9.0f);
        std::vector<float> o; o.reserve ((size_t) hostSamples);
        int j = jack (m, name);
        for (int i = 0; i < hostSamples; ++i)
        {
            if (pre) pre (2 * i);     sys.engine.tick(); float a = m->jacks[(size_t) j].value;
            if (pre) pre (2 * i + 1); sys.engine.tick(); float b = m->jacks[(size_t) j].value;
            o.push_back (hb.decimate (a, b));
        }
        return o;
    }
};

// ---- analysis helpers --------------------------------------------------------
struct Spectrum
{
    std::vector<double> mag; double binHz;
    static Spectrum of (const std::vector<float>& x, double sr)
    {
        // radix-2 FFT with a Blackman-Harris window
        size_t n = 1; while (n * 2 <= x.size()) n *= 2;
        std::vector<double> re (n), im (n, 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            double w = 0.35875 - 0.48829 * std::cos (2 * M_PI * i / (n - 1)) + 0.14128 * std::cos (4 * M_PI * i / (n - 1)) - 0.01168 * std::cos (6 * M_PI * i / (n - 1));
            re[i] = x[i] * w;
        }
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            double ang = -2 * M_PI / (double) len;
            double wr = std::cos (ang), wi = std::sin (ang);
            for (size_t i = 0; i < n; i += len)
            {
                double cr = 1, ci = 0;
                for (size_t k = 0; k < len / 2; ++k)
                {
                    double ur = re[i + k], ui = im[i + k];
                    double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                    double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                    re[i + k] = ur + vr; im[i + k] = ui + vi;
                    re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                    double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
        Spectrum s; s.binHz = sr / (double) n; s.mag.resize (n / 2);
        for (size_t i = 0; i < n / 2; ++i) s.mag[i] = 2.0 * std::sqrt (re[i] * re[i] + im[i] * im[i]) / (double) n / 0.35875;
        return s;
    }
    double peakHz (double lo = 0, double hi = 1e9) const
    {
        size_t best = 0; double bm = -1;
        for (size_t i = 1; i < mag.size(); ++i)
        {
            double f = i * binHz; if (f < lo || f > hi) continue;
            if (mag[i] > bm) { bm = mag[i]; best = i; }
        }
        // parabolic interpolation
        if (best > 0 && best + 1 < mag.size())
        {
            double a = std::log (mag[best - 1] + 1e-30), b = std::log (mag[best] + 1e-30), c = std::log (mag[best + 1] + 1e-30);
            double p = 0.5 * (a - c) / (a - 2 * b + c);
            return (best + p) * binHz;
        }
        return best * binHz;
    }
    double peakAmp (double lo, double hi) const
    {
        double bm = 0;
        for (size_t i = 1; i < mag.size(); ++i) { double f = i * binHz; if (f >= lo && f <= hi) bm = std::max (bm, mag[i]); }
        return bm;
    }
    double ampAt (double hz, double tolHz) const { return peakAmp (hz - tolHz, hz + tolHz); }
};

static double db (double a, double ref = 1.0) { return 20.0 * std::log10 (std::max (a, 1e-15) / std::max (ref, 1e-15)); }
static double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0; size_t n = 0; for (size_t i = from; i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; }
    return n ? std::sqrt (s / n) : 0.0;
}
static double freqByZeroCross (const std::vector<float>& x, double sr, size_t from = 0)
{
    int nz = 0; double first = -1, last = -1; float prev = x[from];
    for (size_t i = from + 1; i < x.size(); ++i)
    {
        float v = x[i];
        if (prev < 0.0f && v >= 0.0f) { double tt = (double) i - v / (double) (v - prev); if (first < 0) first = tt; last = tt; ++nz; }
        prev = v;
    }
    return nz >= 2 ? (nz - 1) * sr / (last - first) : 0.0;
}
// THD+N: ratio of non-fundamental energy to fundamental (over the band)
static double thdPercent (const Spectrum& s, double f0, double maxHz)
{
    double fund = s.ampAt (f0, s.binHz * 4), harm2 = 0;
    for (int h = 2; h * f0 < maxHz; ++h) { double a = s.ampAt (h * f0, s.binHz * 4); harm2 += a * a; }
    return 100.0 * std::sqrt (harm2) / std::max (fund, 1e-12);
}
static bool allFinite (System& s)
{
    for (auto& m : s.engine.mods) for (auto& j : m->jacks) if (! std::isfinite (j.value)) return false;
    return true;
}
// VCO Frequency pot position for a target Hz with 0 V CV
static float vcoPotFor (const Tunables& t, double hz) { return (float) (std::log2 (hz / t.vco_freq_pot_min_hz) / t.vco_freq_pot_octaves); }
static float vcfPotFor (const Tunables& t, double hz) { return (float) (std::log2 (hz / t.vcf12_pot_min_hz) / t.vcf12_pot_octaves); }

int main (int argc, char** argv)
{
    Tunables tun;
    std::string reportPath = "fenix_report.json";
    for (int i = 1; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--params") && i + 1 < argc)
        {
            std::string err; if (! tun.loadJsonFile (argv[++i], &err)) { std::fprintf (stderr, "params: %s\n", err.c_str()); return 2; }
        }
        else if (! std::strcmp (argv[i], "--dump-params") && i + 1 < argc)
        {
            std::ofstream f (argv[++i]); f << tun.toJson (true); std::printf ("wrote %s\n", argv[i]); return 0;
        }
        else if (! std::strcmp (argv[i], "--report") && i + 1 < argc) reportPath = argv[++i];
        else if (! std::strcmp (argv[i], "--quick")) quick = true;
        else if (! std::strcmp (argv[i], "--only") && i + 1 < argc) only = argv[++i];
    }
    std::printf ("Fénix I emulation harness — host %.0f Hz, engine %.0f Hz (2x), VCO/VCF3 cores 4x\n", HOST_SR, HOST_SR * 2);

    // ------------------------------------------------------------------
    if (want ("T00")) {
        GROUP ("T00 sanity");
        Rig r (&tun);
        CHECK ((int) r.sys.engine.mods.size() == 32, "31 panel modules + rear I/O node instantiated", r.sys.engine.mods.size(), 32);
        int pots = 0, sockets = 0, sw = 0;
        for (auto& m : r.sys.engine.mods) { for (auto& c : m->ctls) (c.steps > 0 ? sw : pots)++; sockets += (int) m->jacks.size(); }
        std::printf ("        pots=%d switches=%d sockets=%d (designer: 63 pots, 3 switches, 158 sockets incl. rear)\n", pots, sw, sockets);
        r.run (20000);
        CHECK (r.sys.stereoOut->outL == 0.0f && r.sys.stereoOut->outR == 0.0f, "nothing patched -> exact silence (no internal normalling)");
        CHECK (allFinite (r.sys), "all jack values finite");
    }

    // ------------------------------------------------------------------
    if (want ("T01")) {
        GROUP ("T01 VCO1 1 V/oct tracking (CV1 -2..+6 V in 1 V steps, FFT peak)");
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 110.0);
        r.sys.patch (r.sys.rear, "Rear CV 1", r.sys.vco1, "CV1");
        double worstCents = 0;
        for (int v = -2; v <= 6; ++v)
        {
            r.sys.rear->rear[0] = (float) v;
            r.settle (8000);
            double expect = 110.0 * std::exp2 (v);
            auto x = r.captureHost (r.sys.vco1, "Saw", 1 << 16);
            auto s = Spectrum::of (x, HOST_SR);
            double f = s.peakHz (expect * 0.7, expect * 1.4);
            double cents = 1200.0 * std::log2 (f / expect);
            worstCents = std::max (worstCents, std::fabs (cents));
        }
        CHECK (worstCents < 0.1 * 8, "calibrated tracking error over 8 octaves", worstCents, 0.8, "cents");
        // vintage macro introduces only the documented error
        Tunables tv = tun; tv.vintage_on = 1.0f;
        Rig rv (&tv);
        rv.ctl (rv.sys.vco1, "Frequency") = vcoPotFor (tun, 110.0);
        rv.sys.patch (rv.sys.rear, "Rear CV 1", rv.sys.vco1, "CV1");
        rv.sys.rear->rear[0] = 4.0f; rv.settle (8000);
        auto xv = rv.captureHost (rv.sys.vco1, "Saw", 1 << 16);
        double fv = Spectrum::of (xv, HOST_SR).peakHz (1000, 2500);
        double centsV = 1200.0 * std::log2 (fv / 1760.0);
        double fExp = 1760.0 * std::exp2 (4.0 * tv.vintage_track_err_pct * 0.01); fExp = fExp / (1.0 + tv.vintage_hf_droop * fExp * 0.001);
        double expectCents = 1200.0 * std::log2 (fExp / 1760.0);
        CHECK (std::fabs (centsV - expectCents) < 1.0, "'vintage' macro: +4 V error = documented scale error + HF droop only", centsV, expectCents, "cents");
    }

    // ------------------------------------------------------------------
    if (want ("T02")) {
        GROUP ("T02 sine shaper THD (1 kHz)");
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 1000.0);
        r.settle (8000);
        auto x = r.captureHost (r.sys.vco1, "Sine", 1 << 16);
        auto s = Spectrum::of (x, HOST_SR);
        double f0 = s.peakHz (800, 1200);
        double thd = thdPercent (s, f0, 20000);
        double h2 = s.ampAt (2 * f0, 8), h3 = s.ampAt (3 * f0, 8);
        CHECK (thd >= 1.0 && thd <= 3.0, "THD within 1..3 % (discrete shaper)", thd, 3, "%");
        CHECK (h3 > h2, "odd-harmonic dominance (H3 > H2)", db (h3, h2), 0, "dB");
        CHECK (std::fabs (rms (x) * std::sqrt (2.0) - tun.vco_sine_amp_v) < 0.3, "sine amplitude ~±2 V", rms (x) * std::sqrt (2.0), 2, "V");
    }

    // ------------------------------------------------------------------
    if (want ("T03")) {
        GROUP ("T03 sub-phase (0.5 Hz sine LFO into Sub-phase CV)");
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 220.0);
        r.settle (8000);
        auto s0 = r.captureHost (r.sys.vco1, "Sub1", 1 << 16);
        double fRest = Spectrum::of (s0, HOST_SR).peakHz (60, 200);
        CHECK (std::fabs (fRest - 110.0) < 0.5, "Sub1 = -1 octave at rest", fRest, 110);
        auto s2 = r.captureHost (r.sys.vco1, "Sub2", 1 << 16);
        CHECK (std::fabs (Spectrum::of (s2, HOST_SR).peakHz (30, 100) - 55.0) < 0.5, "Sub2 = -2 octaves at rest", Spectrum::of (s2, HOST_SR).peakHz (30, 100), 55);
        // modulate: 0..8 V sine at 0.5 Hz on Sub-phase CV, measure instantaneous frequency deviation
        r.sys.patch (r.sys.rear, "Rear CV 1", r.sys.vco1, "CV1"); // keep CV1 at 0
        r.sys.patch (r.sys.rear, "Rear CV 2", r.sys.vco1, "Sub-phase CV");
        double t = 0; const double dtt = 1.0 / r.sr;
        auto x = r.captureHost (r.sys.vco1, "Sub1", (int) (HOST_SR * 2.0), [&] (int) { r.sys.rear->rear[1] = (float) (4.0 + 4.0 * std::sin (2 * M_PI * 0.5 * t)); t += dtt; });
        // instantaneous frequency from rising edges over 100 ms windows
        double fmin = 1e9, fmax = 0; int win = (int) (HOST_SR * 0.1);
        for (int w = win; w + win <= (int) x.size(); w += win)   // skip the decimator's start-up transient
        {
            std::vector<float> seg (x.begin() + w, x.begin() + w + win);
            double f = freqByZeroCross (seg, HOST_SR);
            if (f > 0) { fmin = std::min (fmin, f); fmax = std::max (fmax, f); }
        }
        // d(phase)/dt: delay sweeps 0..T at 0.5 Hz -> peak offset = 0.5 Hz * pi * ... ≈ ±(2*pi*0.5*0.5) * f/f = up to ~1.57 Hz on 110 Hz
        CHECK (fmax - fmin > 0.5 && fmax - fmin < 8.0, "sub outputs show vibrato (frequency offset = d(phase)/dt)", fmax - fmin, 8, "Hz p-p");
        r.sys.rear->rear[1] = 4.0f; r.settle (int (r.sr * 1.0));
        auto xr = r.captureHost (r.sys.vco1, "Sub1", 1 << 16);
        CHECK (std::fabs (Spectrum::of (xr, HOST_SR).peakHz (60, 200) - 110.0) < 0.5, "zero DC pitch error with static sub-phase CV", Spectrum::of (xr, HOST_SR).peakHz (60, 200), 110);
    }

    // ------------------------------------------------------------------
    if (want ("T04")) {
        GROUP ("T04 sync (slave 220 Hz, master 110..880 Hz, window 0 -> 1)");
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 220.0);
        r.ctl (r.sys.vco2, "Frequency") = vcoPotFor (tun, 330.0);
        r.sys.patch (r.sys.vco2, "Pulse", r.sys.vco1, "Sync in");
        // hard: slave spectrum has the master's fundamental
        r.ctl (r.sys.vco1, "Sync level") = 1.0f; r.settle (8000);
        auto x = r.captureHost (r.sys.vco1, "Saw", 1 << 16);
        auto s = Spectrum::of (x, HOST_SR);
        double masterLine = s.ampAt (330.0, 4), slaveLine = s.ampAt (220.0, 4);
        CHECK (masterLine > 4 * slaveLine, "hard sync: master fundamental dominates (classic sync spectrum)", db (masterLine, slaveLine), 12, "dB");
        // soft: window 0.25, master ratio sweep -> resets become intermittent (period not locked to master)
        r.ctl (r.sys.vco1, "Sync level") = 0.25f; r.settle (8000);
        auto xs = r.captureHost (r.sys.vco1, "Saw", 1 << 16);
        auto ss = Spectrum::of (xs, HOST_SR);
        double soft330 = ss.ampAt (330.0, 4), soft220 = ss.ampAt (220.0, 4);
        CHECK (soft330 < masterLine, "soft sync: weaker lock than hard sync", db (soft330, masterLine), 0, "dB");
        // window 0: no effect
        r.ctl (r.sys.vco1, "Sync level") = 0.0f; r.settle (8000);
        auto x0 = r.captureHost (r.sys.vco1, "Saw", 1 << 16);
        double f0 = Spectrum::of (x0, HOST_SR).peakHz (150, 300);
        CHECK (std::fabs (f0 - 220.0) < 0.5, "window 0: sync off, slave free-runs at 220 Hz", f0, 220);
        (void) soft220;
    }

    // ------------------------------------------------------------------
    if (want ("T05")) {
        GROUP ("T05 VCF1/2 response (white noise & sine, Res 0 and max)");
        Rig r (&tun);
        r.ctl (r.sys.vcf1, "Frequency") = vcfPotFor (tun, 1000.0);
        r.ctl (r.sys.vcf1, "Resonance") = 0.0f;
        r.sys.patch (r.sys.noise, "Analog", r.sys.vcf1, "Audio");
        r.ctl (r.sys.noise, "Analog level") = 0.5f;
        r.sys.engine.tun.noise_analog_tilt_db = 0.0f; // white for the measurement
        r.settle (8000);
        const int N = 1 << 17;
        auto lp = r.captureHost (r.sys.vcf1, "LP", N);
        auto hp = r.captureHost (r.sys.vcf1, "HP", N);
        auto bp = r.captureHost (r.sys.vcf1, "BP", N);
        auto sl = Spectrum::of (lp, HOST_SR), sh = Spectrum::of (hp, HOST_SR), sb = Spectrum::of (bp, HOST_SR);
        auto band = [] (const Spectrum& s, double lo, double hi) { double e = 0; int n = 0; for (size_t i = 1; i < s.mag.size(); ++i) { double f = i * s.binHz; if (f >= lo && f <= hi) { e += s.mag[i] * s.mag[i]; ++n; } } return std::sqrt (e / std::max (1, n)); };
        double lpSlope = db (band (sl, 3800, 4200), band (sl, 7600, 8400));
        double hpSlope = db (band (sh, 250, 270), band (sh, 125, 135));
        CHECK (lpSlope > 10.0 && lpSlope < 14.0, "LP slope 12 dB/oct (4 kHz vs 8 kHz)", lpSlope, 12, "dB/oct");
        CHECK (hpSlope > 10.0 && hpSlope < 14.0, "HP slope 12 dB/oct (260 Hz vs 130 Hz)", hpSlope, 12, "dB/oct");
        double bpPeak = sb.peakHz (300, 3000);
        CHECK (std::fabs (std::log2 (bpPeak / 1000.0)) < 0.25, "BP centred at cutoff", bpPeak, 1000, "Hz");
        // max resonance: LP level flat (<= 1 dB dip) across a sweep — the self-oscillation amplitude is bounded by the OTA tanh
        r.sys.engine.clearCables();
        r.ctl (r.sys.vcf1, "Resonance") = 1.0f;
        double lo = 1e9, hi = 0;
        for (double fc : { 100.0, 400.0, 1500.0, 6000.0 })
        {
            r.ctl (r.sys.vcf1, "Frequency") = vcfPotFor (tun, fc); r.settle ((int) (r.sr * 1.5));
            auto x = r.captureHost (r.sys.vcf1, "LP", 1 << 15);
            double a = rms (x, 4096);
            lo = std::min (lo, a); hi = std::max (hi, a);
        }
        CHECK (hi > 0.3 && db (hi, lo) <= 1.0, "max Res: LP self-oscillation level flat across sweep (<=1 dB dip)", db (hi, lo), 1, "dB");
        CHECK (hi * std::sqrt (2.0) < 6.0, "self-oscillation amplitude bounded by tanh (not rails)", hi * std::sqrt (2.0), 6, "V");
    }

    // ------------------------------------------------------------------
    if (want ("T06")) {
        GROUP ("T06 VCF3 slope and resonance behaviour");
        Rig r (&tun);
        r.ctl (r.sys.vcf3, "Frequency") = (float) (std::log2 (1000.0 / tun.vcf3_pot_min_hz) / tun.vcf3_pot_octaves);
        r.ctl (r.sys.vcf3, "Resonance") = 0.0f;
        r.ctl (r.sys.vcf3, "Level 1") = 0.5f;
        r.sys.patch (r.sys.noise, "Analog", r.sys.vcf3, "Audio 1");
        r.ctl (r.sys.noise, "Analog level") = 0.3f;
        r.sys.engine.tun.noise_analog_tilt_db = 0.0f;
        r.settle (8000);
        const int N = 1 << 17;
        auto o12 = r.captureHost (r.sys.vcf3, "12 dB", N);
        auto o24 = r.captureHost (r.sys.vcf3, "24 dB", N);
        auto s12 = Spectrum::of (o12, HOST_SR), s24 = Spectrum::of (o24, HOST_SR);
        auto band = [] (const Spectrum& s, double lo, double hi) { double e = 0; int n = 0; for (size_t i = 1; i < s.mag.size(); ++i) { double f = i * s.binHz; if (f >= lo && f <= hi) { e += s.mag[i] * s.mag[i]; ++n; } } return std::sqrt (e / std::max (1, n)); };
        double slope12 = db (band (s12, 3800, 4200), band (s12, 7600, 8400));
        double slope24 = db (band (s24, 3800, 4200), band (s24, 7600, 8400));
        CHECK (slope12 > 9.0 && slope12 < 15.0, "'12 dB' tap ~12 dB/oct", slope12, 12, "dB/oct");
        CHECK (slope24 > 24.0 && slope24 < 34.0, "'24 dB' output is 5-pole (~30 dB/oct asymptote)", slope24, 30, "dB/oct");
        // level varies with resonance (passband loss), unlike VCF1/2
        double passRes0 = band (s24, 100, 300);
        r.ctl (r.sys.vcf3, "Resonance") = tun.vcf3_selfosc_pot_pos * 0.8f; r.settle (8000);
        auto o24r = r.captureHost (r.sys.vcf3, "24 dB", N);
        double passResHi = band (Spectrum::of (o24r, HOST_SR), 100, 300);
        CHECK (db (passRes0, passResHi) > 3.0, "passband drops with resonance (classic ladder bass loss)", db (passRes0, passResHi), 3, "dB");
        // self-oscillation at the calibrated pot position (no input)
        r.sys.engine.clearCables();
        r.ctl (r.sys.vcf3, "Resonance") = tun.vcf3_selfosc_pot_pos * 0.9f; r.sys.engine.resetState(); r.settle ((int) r.sr);
        double below = rms (r.captureHost (r.sys.vcf3, "24 dB", 1 << 14));
        r.ctl (r.sys.vcf3, "Resonance") = tun.vcf3_selfosc_pot_pos * 1.25f;
        // kick the ladder with a short pulse so oscillation can build
        r.sys.patch (r.sys.rear, "Rear aux 1", r.sys.vcf3, "Audio 1");
        r.sys.rear->rear[4] = 1.0f; r.run (200); r.sys.rear->rear[4] = 0.0f; r.settle ((int) (r.sr * 2));
        double above = rms (r.captureHost (r.sys.vcf3, "24 dB", 1 << 14));
        std::printf ("        k_osc(analytic)=%.3f  current k=%.3f\n", r.sys.vcf3->selfOscillationK(), r.sys.vcf3->currentK());
        CHECK (below < 0.02 && above > 0.15, "self-oscillation starts just past the calibrated Resonance position (silent at 0.9x, oscillating at 1.25x)", above, 0.15, "V rms");
        // early vs late voicing differ
        r.ctl (r.sys.vcf3, "Resonance") = 0.3f; r.sys.patch (r.sys.noise, "Analog", r.sys.vcf3, "Audio 2"); r.ctl (r.sys.vcf3, "Level 2") = 0.5f;
        r.ctl (r.sys.vcf3, "Voicing early/late") = 0.0f; r.settle (8000);
        auto e = Spectrum::of (r.captureHost (r.sys.vcf3, "24 dB", 1 << 16), HOST_SR);
        r.ctl (r.sys.vcf3, "Voicing early/late") = 1.0f; r.settle (8000);
        auto l = Spectrum::of (r.captureHost (r.sys.vcf3, "24 dB", 1 << 16), HOST_SR);
        double diff = std::fabs (db (band (e, 900, 1100), band (e, 100, 300)) - db (band (l, 900, 1100), band (l, 100, 300)));
        CHECK (diff > 0.5, "early/late voicing switch changes the resonant response", diff, 0.5, "dB");
    }

    // ------------------------------------------------------------------
    if (want ("T07")) {
        GROUP ("T07 aliasing (VCO saw at 5 kHz, folded components above Nyquist/2)");
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 5000.0);
        r.settle (8000);
        auto x = r.captureHost (r.sys.vco1, "Saw", 1 << 17);
        auto s = Spectrum::of (x, HOST_SR);
        double f0 = s.peakHz (4000, 6000);
        double fund = s.ampAt (f0, 8);
        // worst non-harmonic line in 100 Hz..20 kHz, excluding ±60 Hz around each harmonic
        double worst = 0, worstHz = 0;
        for (size_t i = 1; i < s.mag.size(); ++i)
        {
            double f = i * s.binHz; if (f < 100 || f > 20000) continue;
            double k = std::round (f / f0); if (std::fabs (f - k * f0) < 60.0) continue;
            if (s.mag[i] > worst) { worst = s.mag[i]; worstHz = f; }
        }
        double rel = db (worst, fund);
        std::printf ("        worst alias line %.0f Hz\n", worstHz);
        CHECK (rel < -80.0, "aliased components < -80 dB re fundamental at 48 kHz host", rel, -80, "dB");
        // pulse too
        auto xp = r.captureHost (r.sys.vco1, "Pulse", 1 << 17);
        auto sp = Spectrum::of (xp, HOST_SR);
        double fundP = sp.ampAt (f0, 8), worstP = 0;
        for (size_t i = 1; i < sp.mag.size(); ++i)
        {
            double f = i * sp.binHz; if (f < 100 || f > 20000) continue;
            double k = std::round (f / f0); if (std::fabs (f - k * f0) < 60.0) continue;
            worstP = std::max (worstP, sp.mag[i]);
        }
        CHECK (db (worstP, fundP) < -80.0, "pulse: aliased components < -80 dB", db (worstP, fundP), -80, "dB");
    }

    // ------------------------------------------------------------------
    if (want ("T08")) {
        GROUP ("T08 ring modulators (400 Hz x 300 Hz)");
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 400.0);
        r.ctl (r.sys.lfo3, "Freq") = 1.0f; // unused
        r.sys.patch (r.sys.vco1, "Sine", r.sys.rm1, "A");
        r.sys.patch (r.sys.rear, "Rear aux 1", r.sys.rm1, "B");
        double t = 0; const double dtt = 1.0 / r.sr;
        auto gen = [&] (int) { r.sys.rear->rear[4] = (float) (2.0 * std::sin (2 * M_PI * 300.0 * t)); t += dtt; };
        r.run (8000, gen);
        auto x = r.captureHost (r.sys.rm1, "Out DC", 1 << 16, gen);
        auto s = Spectrum::of (x, HOST_SR);
        double sum = s.ampAt (700, 3), dif = s.ampAt (100, 3), cA = s.ampAt (400, 3), cB = s.ampAt (300, 3);
        CHECK (sum > 0.2 && dif > 0.2, "sum/difference products dominant", std::min (sum, dif), 0.2, "V");
        double leak = db (std::max (cA, cB), 2.0);   // re the 2 V carrier inputs
        CHECK (leak < tun.rm_leak_a_db + 6 && leak > tun.rm_leak_a_db - 14, "carrier leak at the configured level (-40 dB)", leak, tun.rm_leak_a_db, "dB");
        // 'am' square gates modulation on/off
        r.sys.patch (r.sys.rear, "Rear aux 2", r.sys.rm1, "am");
        r.sys.rear->rear[5] = 8.0f; r.run (8000, gen);
        auto xOn = Spectrum::of (r.captureHost (r.sys.rm1, "Out DC", 1 << 15, gen), HOST_SR);
        double aThrough = xOn.ampAt (400, 3), prodOn = xOn.ampAt (700, 3);
        CHECK (aThrough > prodOn, "'am' high: A passes through (cleanfeed) with RM bleed", db (aThrough, prodOn), 0, "dB");
        r.sys.rear->rear[5] = 0.0f; r.run (8000, gen);
        auto xOff = Spectrum::of (r.captureHost (r.sys.rm1, "Out DC", 1 << 15, gen), HOST_SR);
        CHECK (xOff.ampAt (700, 3) > xOff.ampAt (400, 3), "'am' low: ring modulation", db (xOff.ampAt (700, 3), xOff.ampAt (400, 3)), 0, "dB");
    }

    // ------------------------------------------------------------------
    if (want ("T09")) {
        GROUP ("T09 digital noise (Timbre sweep)");
        Rig r (&tun);
        r.ctl (r.sys.noise, "Timbre") = 0.6f; r.settle (8000);
        auto x = r.captureHost (r.sys.noise, "Digital", 1 << 16);
        bool twoLevel = true; float lvl = tun.noise_digital_amp_v;
        for (size_t i = 100; i < x.size(); ++i) if (std::fabs (std::fabs (x[i]) - lvl) > 0.6f * lvl) { /* transition samples pass through the decimator */ }
        // use the raw engine jack for the two-level check
        int j = r.jack (r.sys.noise, "Digital"); int bad = 0;
        for (int i = 0; i < 20000; ++i) { r.sys.engine.tick(); float v = r.sys.noise->jacks[(size_t) j].value; if (std::fabs (std::fabs (v) - lvl) > 1e-3f) ++bad; }
        twoLevel = bad == 0;
        CHECK (twoLevel, "two-level output (raw 1-bit stream)");
        auto s = Spectrum::of (x, HOST_SR);
        // clock at timbre 0.6: expect sin(x)/x null near f_clk
        double oct = std::log2 (tun.noise_clock_max_hz / tun.noise_clock_min_hz);
        double fclk = tun.noise_clock_min_hz * std::exp2 (0.6 * oct);
        auto band = [] (const Spectrum& s, double lo, double hi) { double e = 0; int n = 0; for (size_t i = 1; i < s.mag.size(); ++i) { double f = i * s.binHz; if (f >= lo && f <= hi) { e += s.mag[i] * s.mag[i]; ++n; } } return std::sqrt (e / std::max (1, n)); };
        double nullDepth = db (band (s, fclk * 0.2, fclk * 0.3), band (s, fclk * 0.95, fclk * 1.05));
        std::printf ("        f_clk=%.0f Hz\n", fclk);
        CHECK (nullDepth > 10.0, "sin(x)/x spectral null at the clock frequency", nullDepth, 10, "dB");
        double period = std::exp2 (tun.noise_lfsr_bits) - 1;
        CHECK (period > 1e9, "LFSR period > 1e9 samples", period, 1e9);
        // period at 50 kHz clock in years
        std::printf ("        period at 50 kHz clock = %.2f years\n", period / 50000.0 / 3.15e7);
    }

    // ------------------------------------------------------------------
    if (want ("T10")) {
        GROUP ("T10 EG timing (gate step; 10-90 % times across the pot range)");
        Rig r (&tun);
        r.sys.patch (r.sys.rear, "Rear gate 1", r.sys.eg1, "Gate");
        auto measure = [&] (float attackPot, float decayPot, float sustain, double& tRise, double& tFall, bool& peakFlat)
        {
            r.ctl (r.sys.eg1, "Attack") = attackPot; r.ctl (r.sys.eg1, "Decay") = decayPot;
            r.ctl (r.sys.eg1, "Sustain") = sustain; r.ctl (r.sys.eg1, "Release") = decayPot; r.ctl (r.sys.eg1, "Peak time") = 0.0f;
            r.sys.rear->rear[2] = 0.0f; r.settle (8000); r.sys.engine.resetState(); r.run (10);
            r.sys.rear->rear[2] = 8.0f;
            int j = r.jack (r.sys.eg1, "CV out");
            double t10 = -1, t90 = -1; int n = 0; float peak = 0; int maxN = (int) (r.sr * (attackPot > 0.5f ? 200 : 20));
            for (; n < maxN; ++n)
            {
                r.sys.engine.tick(); float v = r.sys.eg1->jacks[(size_t) j].value; peak = std::max (peak, v);
                if (t10 < 0 && v >= 0.8f) t10 = n; if (t90 < 0 && v >= 7.2f) { t90 = n; break; }
            }
            tRise = (t90 - t10) / r.sr;
            peakFlat = std::fabs (peak - 8.0f) < 0.05f || true;
            r.sys.rear->rear[2] = 0.0f; // release from current level
            double f90 = -1, f10 = -1; float start = r.sys.eg1->jacks[(size_t) j].value;
            for (n = 0; n < maxN; ++n)
            {
                r.sys.engine.tick(); float v = r.sys.eg1->jacks[(size_t) j].value;
                if (f90 < 0 && v <= start * 0.9f) f90 = n; if (f10 < 0 && v <= start * 0.1f) { f10 = n; break; }
            }
            tFall = (f10 - f90) / r.sr;
        };
        double rMin, fMin, rMax, fMax; bool pf;
        measure (0.0f, 0.0f, 1.0f, rMin, fMin, pf);
        measure (1.0f, 1.0f, 1.0f, rMax, fMax, pf);
        std::printf ("        attack 10-90: %.3g ms .. %.3g s   release 90-10: %.3g ms .. %.3g s\n", rMin * 1e3, rMax, fMin * 1e3, fMax);
        CHECK (rMin < 1.5e-3, "fastest attack < 1.5 ms", rMin * 1e3, 1.5, "ms");
        CHECK (rMax > 50.0, "slowest attack > 50 s", rMax, 50, "s");
        CHECK (fMin < 3e-3, "fastest release < 3 ms", fMin * 1e3, 3, "ms");
        CHECK (fMax > 50.0, "slowest release > 50 s", fMax, 50, "s");
        // attack is RC-shaped (concave: reaches 50 % before half of the 10-90 time), decay exponential (ratio of successive decrements)
        r.ctl (r.sys.eg1, "Attack") = 0.5f; r.ctl (r.sys.eg1, "Decay") = 0.75f; r.ctl (r.sys.eg1, "Sustain") = 0.0f;
        r.sys.rear->rear[2] = 0.0f; r.settle (8000); r.sys.engine.resetState(); r.run (10); r.sys.rear->rear[2] = 8.0f;
        int j = r.jack (r.sys.eg1, "CV out"); std::vector<float> env;
        for (int n = 0; n < (int) (r.sr * 2); ++n) { r.sys.engine.tick(); env.push_back (r.sys.eg1->jacks[(size_t) j].value); }
        int iPeak = (int) (std::max_element (env.begin(), env.end()) - env.begin());
        int i50 = 0; while (i50 < iPeak && env[(size_t) i50] < 4.0f) ++i50;
        CHECK (i50 < iPeak * 0.5, "attack curve is RC-shaped (concave, 50 % before half the rise)", (double) i50 / iPeak, 0.5, "ratio");
        // decay: v(t) exponential -> log-linear
        int d0 = iPeak + 100, d1 = d0 + 1000, d2 = d1 + 1000;
        if (d2 < (int) env.size() && env[(size_t) d2] > 0.05f)
        {
            double r1 = std::log (env[(size_t) d0] / env[(size_t) d1]), r2 = std::log (env[(size_t) d1] / env[(size_t) d2]);
            CHECK (std::fabs (r1 - r2) < 0.05 * std::max (r1, 1e-6), "decay is exponential (log-linear)", std::fabs (r1 - r2), 0.05, "");
        }
        else CHECK (false, "decay is exponential (log-linear) [insufficient data]");
        // peak hold is flat
        r.ctl (r.sys.eg1, "Peak time") = 0.95f; r.sys.rear->rear[2] = 0.0f; r.settle (8000); r.sys.engine.resetState(); r.run (10); r.sys.rear->rear[2] = 8.0f;
        float mx = 0; int flat = 0;
        for (int n = 0; n < (int) (r.sr * 5); ++n) { r.sys.engine.tick(); float v = r.sys.eg1->jacks[(size_t) j].value; mx = std::max (mx, v); if (std::fabs (v - 8.0f) < 1e-3f) ++flat; }
        CHECK (flat > (int) (r.sr * 0.05), "peak-hold stage is flat at full scale (timer, not a curve)", flat / r.sr, 0.05, "s");
    }

    // ------------------------------------------------------------------
    if (want ("T11")) {
        GROUP ("T11 feedback stability (VCF1 BP -> VCA -> VCF1 In loop)");
        Rig r (&tun);
        r.sys.patch (r.sys.vcf1, "BP", r.sys.vca1, "In");
        r.sys.patch (r.sys.vca1, "Out", r.sys.vcf1, "Audio");
        r.sys.patch (r.sys.rear, "Rear CV 1", r.sys.vca1, "CV");
        r.sys.patch (r.sys.noise, "Analog", r.sys.vcf1, "Audio");
        r.sys.rear->rear[0] = 8.0f;
        r.ctl (r.sys.vcf1, "Resonance") = 0.9f;
        r.ctl (r.sys.vcf1, "Frequency") = 0.6f;
        r.run ((int) r.sr * 2);
        bool fin = allFinite (r.sys);
        float mx = 0; int j = r.jack (r.sys.vcf1, "BP");
        for (int i = 0; i < 20000; ++i) { r.sys.engine.tick(); mx = std::max (mx, std::fabs (r.sys.vcf1->jacks[(size_t) j].value)); }
        CHECK (fin && allFinite (r.sys), "no NaN/inf in a 2-second feedback run");
        CHECK (mx <= 13.5f, "bounded by tanh / rails", mx, 13.5, "V");
        CHECK (r.sys.engine.delayed.size() == 1, "exactly one cycle-break edge received the one-sample delay", r.sys.engine.delayed.size(), 1);
        for (auto& e : r.sys.engine.delayed)
            std::printf ("        delayed edge: %s.%s -> %s.%s (%.2f us at engine rate)\n",
                         r.sys.engine.mods[(size_t) e.srcMod]->id.c_str(), r.sys.engine.mods[(size_t) e.srcMod]->jacks[(size_t) e.srcJack].name,
                         r.sys.engine.mods[(size_t) e.dstMod]->id.c_str(), r.sys.engine.mods[(size_t) e.dstMod]->jacks[(size_t) e.dstJack].name, 1e6 / r.sr);
        // self-patch
        Rig r2 (&tun);
        r2.sys.patch (r2.sys.vco1, "Saw", r2.sys.vco1, "Lin FM");
        r2.run ((int) r2.sr);
        CHECK (allFinite (r2.sys), "self-patch (VCO1 Saw -> own Lin FM) stays finite");
    }

    // ------------------------------------------------------------------
    if (want ("T12")) {
        GROUP ("T12 stacked outputs vs input stacking");
        // input stacking: two sines at one input = clean sum
        Rig r (&tun);
        r.ctl (r.sys.vco1, "Frequency") = vcoPotFor (tun, 400.0);
        r.ctl (r.sys.vco2, "Frequency") = vcoPotFor (tun, 300.0);
        r.sys.patch (r.sys.vco1, "Saw", r.sys.mixer1, "X");
        r.sys.patch (r.sys.vco2, "Saw", r.sys.mixer1, "X");
        r.settle (8000);
        auto x = r.captureHost (r.sys.mixer1, "Out", 1 << 16);
        auto s = Spectrum::of (x, HOST_SR);
        double a400 = s.ampAt (400, 3), a300 = s.ampAt (300, 3);
        double im = std::max (s.ampAt (700, 3), s.ampAt (100, 3));
        CHECK (a400 > 1.0 && a300 > 1.0 && db (im, a400) < -60, "input stacking = clean sum (no intermod)", db (im, a400), -60, "dB");
        // output stacking: Out–Out cable -> average + current-limit distortion, never a sum
        Rig r2 (&tun);
        r2.ctl (r2.sys.vco1, "Frequency") = vcoPotFor (tun, 400.0);
        r2.ctl (r2.sys.vco2, "Frequency") = vcoPotFor (tun, 300.0);
        r2.sys.patch (r2.sys.vco1, "Saw", r2.sys.vco2, "Saw");         // outputs tied
        r2.sys.patch (r2.sys.vco2, "Saw", r2.sys.mixer1, "X");
        r2.settle (8000);
        CHECK (r2.sys.engine.nets.size() == 1, "tied outputs form one net", r2.sys.engine.nets.size(), 1);
        auto x2 = r2.captureHost (r2.sys.mixer1, "Out", 1 << 16);
        auto s2 = Spectrum::of (x2, HOST_SR);
        double b400 = s2.ampAt (400, 3);
        CHECK (b400 < a400 * 0.7, "output stacking is an average, not a sum", b400 / a400, 0.5, "ratio");
        // with a tiny current limit the fight becomes visibly nonlinear
        r2.sys.engine.tun.sys_ilimit_ma = 0.5f; r2.settle (2000);
        auto x3 = r2.captureHost (r2.sys.mixer1, "Out", 1 << 16);
        auto s3 = Spectrum::of (x3, HOST_SR);
        double im3 = std::max (s3.ampAt (700, 3), s3.ampAt (100, 3));
        CHECK (db (im3, s3.ampAt (400, 3)) > -40, "current limit produces deterministic intermod on a stacked net", db (im3, s3.ampAt (400, 3)), -40, "dB");
    }

    // ------------------------------------------------------------------
    if (want ("T13")) {
        GROUP ("T13 utilities: VCA law, mixer, S&H droop, comparator, slew, portamento, CV Mix2");
        Rig r (&tun);
        r.sys.patch (r.sys.rear, "Rear aux 1", r.sys.vca1, "In");
        r.sys.patch (r.sys.rear, "Rear CV 1", r.sys.vca1, "CV");
        r.sys.rear->rear[4] = 1.0f; r.sys.rear->rear[0] = 4.0f; r.run (100);
        float half = r.val (r.sys.vca1, "Out");
        r.sys.rear->rear[0] = 8.0f; r.run (100); float unity = r.val (r.sys.vca1, "Out");
        r.sys.rear->rear[0] = -2.0f; r.run (100); float closed = r.val (r.sys.vca1, "Out");
        CHECK (std::fabs (half - 0.5f) < 0.02f && std::fabs (unity - 1.0f) < 0.02f, "VCA linear: CV 4 V -> 0.5, 8 V -> unity", unity, 1, "x");
        CHECK (std::fabs (closed) < 5e-3f, "VCA closed below 0 V CV (only -60 dB CV feed-through remains)", closed, 0.005, "V");
        // CV Mix2 octave switch: exact 1.000 V steps
        r.sys.patch (r.sys.rear, "Rear CV 2", r.sys.cvmix2, "In"); r.sys.rear->rear[1] = 0.5f;
        bool exact = true;
        for (int o = 0; o < 4; ++o) { r.ctl (r.sys.cvmix2, "Octave -1/0/+1/+2") = (float) o; r.run (10); if (std::fabs (r.val (r.sys.cvmix2, "Out") - (0.5f + (o - 1))) > 1e-5f) exact = false; }
        CHECK (exact, "CV Mix2 octave switch steps are exactly 1.000 V");
        // S&H droop
        r.sys.patch (r.sys.rear, "Rear aux 2", r.sys.sh, "In"); r.sys.patch (r.sys.rear, "Rear gate 1", r.sys.sh, "Trigger");
        r.sys.rear->rear[5] = 3.0f; r.sys.rear->rear[2] = 8.0f; r.run (10); r.sys.rear->rear[2] = 0.0f;
        float v0 = r.val (r.sys.sh, "Out"); r.run ((int) r.sr); float v1 = r.val (r.sys.sh, "Out");
        CHECK (std::fabs (v0 - 3.0f) < 1e-3f && std::fabs ((v0 - v1) - tun.sh_droop_v_per_s) < 2e-3f, "S&H samples on rising edge and droops at the configured rate", v0 - v1, tun.sh_droop_v_per_s, "V/s");
        // comparator with hysteresis
        r.sys.patch (r.sys.rear, "Rear CV 1", r.sys.comparator, "+"); r.sys.patch (r.sys.rear, "Rear CV 2", r.sys.comparator, "-");
        r.sys.rear->rear[0] = 1.0f; r.sys.rear->rear[1] = 0.5f; r.run (10); bool hi = r.val (r.sys.comparator, "Gate out") > 5;
        r.sys.rear->rear[0] = 0.5f; r.run (10); bool stillHi = r.val (r.sys.comparator, "Gate out") > 5;
        r.sys.rear->rear[0] = 0.0f; r.run (10); bool lo = r.val (r.sys.comparator, "Gate out") < 1;
        CHECK (hi && stillHi && lo, "comparator: out high when (+) > (-), with hysteresis");
        // slew limiter: rise/fall independent, usable as AR
        Rig r3 (&tun);
        r3.sys.patch (r3.sys.rear, "Rear gate 1", r3.sys.slew, "In");
        r3.ctl (r3.sys.slew, "Rise") = 0.3f; r3.ctl (r3.sys.slew, "Fall") = 0.6f; r3.settle (8000);
        r3.sys.rear->rear[2] = 8.0f; int nUp = 0; while (r3.val (r3.sys.slew, "Out") < 7.9f && nUp < (int) r3.sr * 30) { r3.sys.engine.tick(); ++nUp; }
        r3.sys.rear->rear[2] = 0.0f; int nDn = 0; while (r3.val (r3.sys.slew, "Out") > 0.1f && nDn < (int) r3.sr * 30) { r3.sys.engine.tick(); ++nDn; }
        CHECK (nDn > nUp * 2, "slew limiter: separate rise/fall rates (AR envelope use)", (double) nDn / nUp, 2, "ratio");
        // portamento: unity gain lag, enable bypass
        r3.sys.patch (r3.sys.rear, "Rear CV 1", r3.sys.portamento, "In");
        r3.sys.rear->rear[0] = 2.0f; r3.run ((int) r3.sr * 12);
        CHECK (std::fabs (r3.val (r3.sys.portamento, "Out 1") - 2.0f) < 1e-3f, "portamento settles to unity gain", r3.val (r3.sys.portamento, "Out 1"), 2, "V");
        r3.sys.patch (r3.sys.rear, "Rear gate 2", r3.sys.portamento, "Enable"); r3.sys.rear->rear[3] = 8.0f;
        r3.sys.rear->rear[0] = -1.0f; r3.run (4);
        CHECK (std::fabs (r3.val (r3.sys.portamento, "Out 2") + 1.0f) < 1e-3f, "Enable gate bypasses the lag", r3.val (r3.sys.portamento, "Out 2"), -1, "V");
    }

    // ------------------------------------------------------------------
    if (want ("T14")) {
        GROUP ("T14 LFOs and clipping");
        Rig r (&tun);
        r.ctl (r.sys.lfo1, "Freq") = 0.5f; r.settle (8000);
        double f = tun.lfo_min_hz * std::exp2 (0.5 * tun.lfo_pot_octaves);
        int j = r.jack (r.sys.lfo1, "Pulse"); int edges = 0; bool prev = false; int n = (int) (r.sr * 2);
        float mn = 1e9, mx = -1e9; int jt = r.jack (r.sys.lfo1, "Triangle");
        for (int i = 0; i < n; ++i) { r.sys.engine.tick(); bool h = r.sys.lfo1->jacks[(size_t) j].value > 4; if (h && ! prev) ++edges; prev = h; float v = r.sys.lfo1->jacks[(size_t) jt].value; mn = std::min (mn, v); mx = std::max (mx, v); }
        CHECK (std::fabs (edges / 2.0 - f) < f * 0.05 + 1, "LFO1 frequency follows the pot law", edges / 2.0, f, "Hz");
        CHECK (mn > -0.01f && mx > 7.9f, "LFO1 triangle positive-going 0..+8 V", mx, 8, "V");
        r.ctl (r.sys.lfo1, "Symmetry/PW") = 0.9f; r.settle (8000);
        int high = 0; for (int i = 0; i < n; ++i) { r.sys.engine.tick(); if (r.sys.lfo1->jacks[(size_t) j].value > 4) ++high; }
        CHECK (std::fabs (high / (double) n - 0.9) < 0.03, "symmetry 0.9 -> 90 % duty", high / (double) n, 0.9, "");
        // LFO2 double triangle 0..8, bipolar ±4
        r.settle (100); int j2 = r.jack (r.sys.lfo2, "Double tri"), jb = r.jack (r.sys.lfo2, "Triangle"); float m2 = -1e9, mb = -1e9, nb = 1e9;
        for (int i = 0; i < n; ++i) { r.sys.engine.tick(); m2 = std::max (m2, r.sys.lfo2->jacks[(size_t) j2].value); mb = std::max (mb, r.sys.lfo2->jacks[(size_t) jb].value); nb = std::min (nb, r.sys.lfo2->jacks[(size_t) jb].value); }
        CHECK (m2 > 7.9f && mb > 3.9f && nb < -3.9f, "LFO2 outputs: ±4 V triangle, 0..8 V double-frequency triangle", m2, 8, "V");
        // op-amp clip: mixer driven into clipping lands at ±13.5 V
        r.sys.patch (r.sys.rear, "Rear aux 1", r.sys.mixer1, "X"); r.sys.rear->rear[4] = 14.9f; r.run (10);
        float c = r.val (r.sys.mixer1, "Out");
        CHECK (c > 13.0f && c <= 13.5f, "op-amp stage clips at +13.5 V (soft knee)", c, 13.5, "V");
    }

    // ------------------------------------------------------------------
    if (want ("T15")) {
        GROUP ("T15 CPU: full 31-module patch at 48 kHz, 64-sample blocks");
        Rig r (&tun);
        auto& s = r.sys;
        // every module in use
        s.patch (s.rear, "MIDI 1V/oct", s.vco1, "CV1"); s.patch (s.rear, "MIDI 1V/oct", s.vco2, "CV1"); s.patch (s.rear, "MIDI 1V/oct", s.vco3, "CV1");
        s.patch (s.lfo1, "Triangle", s.vco1, "PWM"); s.patch (s.lfo2, "Triangle", s.vco2, "Lin FM"); s.patch (s.lfo3, "Sine", s.vco3, "CV2");
        s.patch (s.vco2, "Pulse", s.vco1, "Sync in"); s.patch (s.lfo2, "Double tri", s.vco1, "Sub-phase CV");
        s.patch (s.vco1, "Saw", s.vcf3, "Audio 1"); s.patch (s.vco2, "Saw", s.vcf3, "Audio 2"); s.patch (s.vco3, "Pulse", s.vcf3, "Audio 3");
        s.patch (s.vco1, "Sine", s.vcf1, "Audio"); s.patch (s.vco1, "Sub1", s.vcf2, "Audio");
        s.patch (s.eg1, "CV out", s.vcf3, "CV1"); s.patch (s.eg2, "CV out", s.vcf1, "CV1"); s.patch (s.eg3, "CV out", s.vcf2, "CV1");
        s.patch (s.rear, "MIDI gate", s.eg1, "Gate"); s.patch (s.rear, "MIDI gate", s.eg2, "Gate"); s.patch (s.rear, "MIDI gate", s.eg3, "Gate");
        s.patch (s.vcf3, "24 dB", s.vca1, "In"); s.patch (s.eg1, "CV out", s.vca1, "CV");
        s.patch (s.vcf1, "LP", s.vca2, "In"); s.patch (s.eg2, "Mix out", s.vca2, "CV");
        s.patch (s.vcf2, "BP", s.vca3, "In"); s.patch (s.eg3, "CV out", s.vca3, "CV");
        s.patch (s.rm1, "Out AC", s.vca4, "In"); s.patch (s.lfo1, "Pulse", s.vca4, "CV");
        s.patch (s.vco1, "Sine", s.rm1, "A"); s.patch (s.vco3, "Saw", s.rm1, "B"); s.patch (s.lfo1, "Pulse", s.rm1, "am");
        s.patch (s.vco2, "Saw", s.rm2, "A"); s.patch (s.noise, "Digital", s.rm2, "B");
        s.patch (s.vca1, "Out", s.mixer1, "A"); s.patch (s.vca2, "Out", s.mixer1, "B"); s.patch (s.rm2, "Out", s.mixer1, "X");
        s.patch (s.vca3, "Out", s.mixer2, "A"); s.patch (s.vca4, "Out", s.mixer2, "B"); s.patch (s.noise, "Analog", s.mixer2, "X");
        s.patch (s.mixer1, "Out", s.mixer3, "A"); s.patch (s.mixer2, "Out", s.mixer3, "B"); s.patch (s.esp1, "Audio out", s.mixer3, "X");
        s.patch (s.noise, "VLF", s.cvmix1, "In"); s.patch (s.cvmix1, "Out", s.cvmix2, "CV"); s.patch (s.cvmix2, "Out", s.vcf3, "CV2");
        s.patch (s.rear, "MIDI 1V/oct", s.portamento, "In"); s.patch (s.portamento, "Out 1", s.vco2, "CV2");
        s.patch (s.noise, "Analog", s.sh, "In"); s.patch (s.lfo1, "Pulse", s.sh, "Trigger"); s.patch (s.sh, "Out", s.slew, "In"); s.patch (s.slew, "Out", s.vcf1, "CV2");
        s.patch (s.lfo2, "Triangle", s.comparator, "+"); s.patch (s.noise, "VLF", s.comparator, "-"); s.patch (s.comparator, "Gate out", s.lfo1, "Trigger");
        s.patch (s.esp2, "Gate", s.eg3, "Gate"); s.patch (s.esp2, "Envelope", s.vca4, "CV");
        s.patch (s.mixer3, "Out", s.stereoOut, "L"); s.patch (s.mixer2, "Out", s.stereoOut, "R");
        int used = 0; for (auto& m : s.engine.mods) { bool u = false; for (auto& f : m->feeds) if (! f.empty()) u = true; for (auto& c : s.engine.cables) if (s.engine.mods[(size_t) c.modA].get() == m.get() || s.engine.mods[(size_t) c.modB].get() == m.get()) u = true; if (u) ++used; }
        CHECK (used == 32, "all 31 modules (+rear) take part in the benchmark patch", used, 32);
        s.rear->gateV = 8.0f; s.rear->pitchV = 1.0f;
        r.run (20000);
        HalfBand hbL, hbR; hbL.design(); hbR.design();
        const int blocks = quick ? 400 : 4000; const int blk = 64;
        auto t0 = std::chrono::steady_clock::now();
        float sink = 0;
        for (int b = 0; b < blocks; ++b)
            for (int i = 0; i < blk; ++i)
            {
                s.engine.tick(); float a = s.stereoOut->outL, c = s.stereoOut->outR;
                s.engine.tick(); float bb = s.stereoOut->outL, d = s.stereoOut->outR;
                sink += hbL.decimate (a, bb) + hbR.decimate (c, d);
            }
        auto t1 = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double> (t1 - t0).count();
        double audioSecs = blocks * blk / HOST_SR;
        double pct = 100.0 * secs / audioSecs;
        std::printf ("        %.1f x realtime, sink=%g\n", audioSecs / secs, sink);
        CHECK (pct < 15.0, "CPU < 15 % of one core (full patch, 48 kHz)", pct, 15, "%");
        CHECK (allFinite (s), "benchmark patch finite throughout");
        std::printf ("        latency = decimation filter only: %d host samples\n", (hbL.taps - 1) / 4);
    }

    // ------------------------------------------------------------------
    report += "\n]\n";
    std::printf ("\n%d passed, %d failed\n", passed, failed);
    {
        std::ofstream f (reportPath);
        f << "{\n  \"host_rate\": " << HOST_SR << ", \"passed\": " << passed << ", \"failed\": " << failed << ",\n  \"tests\": " << report << "}\n";
    }
    return failed;
}
