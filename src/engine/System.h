// Fénix I emulation — the full instrument: 31 modules (designer's count) plus
// the software rear-I/O node, with the designer's module and port names.
#pragma once
#include "ModulesOsc.h"
#include "ModulesVCF.h"
#include "ModulesEG.h"
#include "ModulesUtil.h"

namespace fenix {

struct System
{
    Engine engine;

    VCO *vco1 = nullptr, *vco2 = nullptr, *vco3 = nullptr;
    LFO *lfo1 = nullptr, *lfo2 = nullptr, *lfo3 = nullptr;
    VCF12 *vcf1 = nullptr, *vcf2 = nullptr;
    VCF3 *vcf3 = nullptr;
    EG1 *eg1 = nullptr; EG2 *eg2 = nullptr; EG3 *eg3 = nullptr;
    VCA *vca1 = nullptr, *vca2 = nullptr, *vca3 = nullptr, *vca4 = nullptr;
    Mixer1 *mixer1 = nullptr; Mixer23 *mixer2 = nullptr, *mixer3 = nullptr;
    CVMix1 *cvmix1 = nullptr; CVMix2 *cvmix2 = nullptr;
    Portamento *portamento = nullptr; Slew *slew = nullptr; SampleHold *sh = nullptr; Comparator *comparator = nullptr;
    RingMod *rm1 = nullptr, *rm2 = nullptr;
    Noise *noise = nullptr;
    ESP *esp1 = nullptr, *esp2 = nullptr;
    StereoOut *stereoOut = nullptr;
    RearIO *rear = nullptr;

    System()
    {
        auto& e = engine;
        vco1 = e.add<VCO> ("vco1", "VCO 1", true, 1);
        vco2 = e.add<VCO> ("vco2", "VCO 2", false, 2);
        vco3 = e.add<VCO> ("vco3", "VCO 3", false, 3);
        lfo1 = e.add<LFO> ("lfo1", "LFO 1", 1);
        lfo2 = e.add<LFO> ("lfo2", "LFO 2", 2);
        lfo3 = e.add<LFO> ("lfo3", "LFO 3", 3);
        vcf1 = e.add<VCF12> ("vcf1", "VCF 1");
        vcf2 = e.add<VCF12> ("vcf2", "VCF 2");
        vcf3 = e.add<VCF3> ("vcf3", "VCF 3");
        eg1 = e.add<EG1> ("eg1", "EG 1");
        eg2 = e.add<EG2> ("eg2", "EG 2");
        eg3 = e.add<EG3> ("eg3", "EG 3");
        vca1 = e.add<VCA> ("vca1", "VCA 1"); vca2 = e.add<VCA> ("vca2", "VCA 2");
        vca3 = e.add<VCA> ("vca3", "VCA 3"); vca4 = e.add<VCA> ("vca4", "VCA 4");
        mixer1 = e.add<Mixer1> ("mixer1", "MIXER 1");
        mixer2 = e.add<Mixer23> ("mixer2", "MIXER 2");
        mixer3 = e.add<Mixer23> ("mixer3", "MIXER 3");
        cvmix1 = e.add<CVMix1> ("cvmix1", "CV MIX 1");
        cvmix2 = e.add<CVMix2> ("cvmix2", "CV MIX 2");
        portamento = e.add<Portamento> ("portamento", "PORTAMENTO");
        slew = e.add<Slew> ("slew", "SLEW LIMITER");
        sh = e.add<SampleHold> ("sh", "S&H");
        comparator = e.add<Comparator> ("comparator", "COMPARATOR");
        rm1 = e.add<RingMod> ("rm1", "RING MOD 1", true);
        rm2 = e.add<RingMod> ("rm2", "RING MOD 2", false);
        noise = e.add<Noise> ("noise", "NOISE");
        esp1 = e.add<ESP> ("esp1", "AUDIO IN 1", true);
        esp2 = e.add<ESP> ("esp2", "AUDIO IN 2", false);
        stereoOut = e.add<StereoOut> ("out", "STEREO OUT");
        rear = e.add<RearIO> ("rear", "REAR I/O + MIDI");
    }

    void prepare (double hostSr, int oversample = 2) { engine.prepare (hostSr, oversample); }

    bool patch (Module* a, const char* ja, Module* b, const char* jb) { return engine.connect (a, ja, b, jb); }
    bool patch (Module* a, int ja, Module* b, int jb) { return engine.connect (engine.indexOf (a), ja, engine.indexOf (b), jb); }

    static int moduleCountOnPanel() { return 31; }
};

} // namespace fenix
