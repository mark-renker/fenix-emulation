#include "PluginProcessor.h"
#include "../ui/Panel.h"

using namespace fenix;

juce::String FenixProcessor::paramID (const Module& m, int c) { return juce::String (m.id) + "_" + juce::String (c); }

static juce::StringArray choicesFor (const Module& m, const Ctl& c)
{
    juce::String n (c.name);
    if (n.startsWith ("Octave")) return { "-1", "0", "+1", "+2" };
    if (n.startsWith ("Voicing")) return { "Early", "Late" };
    if (n == "D/R") return { "D", "R" };
    if (c.momentary) return { "Off", "On" };
    juce::StringArray sa; for (int i = 0; i < c.steps; ++i) sa.add (juce::String (i));
    juce::ignoreUnused (m);
    return sa;
}

juce::AudioProcessorValueTreeState::ParameterLayout FenixProcessor::makeLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    for (auto& m : sys.engine.mods)
        for (int c = 0; c < (int) m->ctls.size(); ++c)
        {
            auto& ct = m->ctls[(size_t) c];
            auto name = juce::String (m->title) + ": " + ct.name;
            if (ct.steps > 0)
                ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (paramID (*m, c), 1), name,
                                                                            choicesFor (*m, ct), (int) std::lround (ct.def)));
            else
                ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (paramID (*m, c), 1), name,
                                                                           juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), ct.def));
        }
    return { ps.begin(), ps.end() };
}

FenixProcessor::FenixProcessor()
    : AudioProcessor (
#if FENIX_FX
        BusesProperties().withInput ("Audio In", juce::AudioChannelSet::stereo(), true)
                         .withInput ("Rear CV", juce::AudioChannelSet::stereo(), false)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#else
        BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      apvts (*this, nullptr, "FENIXPARAMS", makeLayout())
{
    for (auto& m : sys.engine.mods)
        for (int c = 0; c < (int) m->ctls.size(); ++c)
        {
            paramPtrs.push_back (apvts.getRawParameterValue (paramID (*m, c)));
            if (m->ctls[(size_t) c].momentary)
            {
                auto mf = std::make_unique<MomentaryFlag>(); mf->mod = m.get(); mf->ctl = c;
                momentaries.push_back (std::move (mf));
            }
        }
    paramsStatus = reloadParams();
}

const juce::String FenixProcessor::getName() const
{
#if FENIX_FX
    return "Fenix I FX";
#else
    return "Fenix I";
#endif
}

bool FenixProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
#if FENIX_FX
    if (l.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.inputBuses.size() > 1)
    {
        auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo()) return false;
    }
#endif
    return true;
}

void FenixProcessor::prepareToPlay (double sr, int)
{
    const juce::SpinLock::ScopedLockType sl (engineLock);
    sys.prepare (sr, 2);
    for (auto* hb : { &upL, &upR, &downL, &downR }) hb->design (63, 9.0f);
    setLatencySamples ((downL.taps - 1) / 4 + 1);
}

void FenixProcessor::applyParams()
{
    size_t i = 0;
    for (auto& m : sys.engine.mods)
        for (auto& c : m->ctls) c.v = paramPtrs[i++]->load();
    for (auto& mf : momentaries)
        if (mf->down.load()) mf->mod->ctls[(size_t) mf->ctl].v = 1.0f;
}

void FenixProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    const juce::SpinLock::ScopedTryLockType sl (engineLock);
    if (! sl.isLocked()) { buf.clear(); return; }
    const auto t0 = juce::Time::getHighResolutionTicks();

    applyParams();
    const Tunables& tun = sys.engine.tun;
    auto* rear = sys.rear;

#if FENIX_FX
    auto mainIn = getBusBuffer (buf, true, 0);
    const float* inL = mainIn.getReadPointer (0);
    const float* inR = mainIn.getNumChannels() > 1 ? mainIn.getReadPointer (1) : inL;
    const float* rearIn[2] = { nullptr, nullptr };
    if (getBusCount (true) > 1)
    {
        auto sc = getBusBuffer (buf, true, 1);
        if (sc.getNumChannels() >= 2) { rearIn[0] = sc.getReadPointer (0); rearIn[1] = sc.getReadPointer (1); }
    }
#endif
    auto midiIt = midi.begin();

    for (int s = 0; s < n; ++s)
    {
        while (midiIt != midi.end() && (*midiIt).samplePosition <= s)
        {
            auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())          { notesDown.add (msg.getNoteNumber()); heldNote = msg.getNoteNumber(); heldVel = msg.getFloatVelocity(); }
            else if (msg.isNoteOff())    { notesDown.removeValue (msg.getNoteNumber()); if (notesDown.size() > 0) heldNote = notesDown.getLast(); }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) notesDown.clear();
            else if (msg.isPitchWheel()) bendV = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * (2.0f / 12.0f);
            else if (msg.isController() && msg.getControllerNumber() == 1) modV = (float) msg.getControllerValue() / 127.0f * kCVFullV;
            ++midiIt;
        }
        rear->gateV  = notesDown.size() > 0 ? tun.sys_gate_out_v : 0.0f;
        rear->pitchV = (heldNote >= 0 ? (float) (heldNote - 60) / 12.0f : 0.0f) + bendV;
        rear->velV   = heldVel * kCVFullV;
        rear->modV   = modV;

        float aL = 0, bL = 0, aR = 0, bR = 0;
#if FENIX_FX
        upL.interpolate (inL[s] * tun.in_full_scale_v, aL, bL);
        upR.interpolate (inR[s] * tun.in_full_scale_v, aR, bR);
        rear->rear[0] = rearIn[0] ? rearIn[0][s] * kCVFullV : 0.0f;   // host 1.0 = 8 V
        rear->rear[1] = rearIn[1] ? rearIn[1][s] * kCVFullV : 0.0f;
#endif
        sys.esp1->extIn = aL; sys.esp2->extIn = aR;
        sys.engine.tick();
        const float o1L = sys.stereoOut->outL, o1R = sys.stereoOut->outR;
        sys.esp1->extIn = bL; sys.esp2->extIn = bR;
        sys.engine.tick();
        const float o2L = sys.stereoOut->outL, o2R = sys.stereoOut->outR;
        const float g = 1.0f / tun.out_full_scale_v;
        buf.setSample (0, s, downL.decimate (o1L, o2L) * g);
        if (buf.getNumChannels() > 1) buf.setSample (1, s, downR.decimate (o1R, o2R) * g);
    }
    midi.clear();
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double budget = n / getSampleRate();
    cpuLoad.store (0.9f * cpuLoad.load() + 0.1f * (float) (secs / budget));
}

// ------------------------------------------------------------- cables -------
bool FenixProcessor::addCable (int mA, int jA, int mB, int jB)
{
    const juce::SpinLock::ScopedLockType sl (engineLock);
    bool ok = sys.engine.connect (mA, jA, mB, jB);
    if (ok) ++cableVer;
    return ok;
}
void FenixProcessor::removeCablesAt (int m, int j) { const juce::SpinLock::ScopedLockType sl (engineLock); sys.engine.disconnectAt (m, j); ++cableVer; }
void FenixProcessor::removeCable (const Cable& c)  { const juce::SpinLock::ScopedLockType sl (engineLock); sys.engine.removeCable (c); ++cableVer; }
void FenixProcessor::clearAllCables()              { const juce::SpinLock::ScopedLockType sl (engineLock); sys.engine.clearCables(); ++cableVer; }
std::vector<Cable> FenixProcessor::cableSnapshot() { const juce::SpinLock::ScopedLockType sl (engineLock); return sys.engine.cables; }
void FenixProcessor::setMomentary (Module* m, int ctlIdx, bool down)
{
    for (auto& mf : momentaries) if (mf->mod == m && mf->ctl == ctlIdx) mf->down.store (down);
}

// ------------------------------------------------------------- tunables -----
juce::File FenixProcessor::userParamsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FenixI").getChildFile ("topology_params.json");
}
juce::String FenixProcessor::reloadParams()
{
    auto f = userParamsFile();
    Tunables t;
    juce::String status;
    if (f.existsAsFile())
    {
        std::string err; std::vector<std::string> unknown;
        int n = t.loadJsonText (f.loadFileAsString().toStdString(), &err, &unknown);
        if (n < 0) status = "params: " + juce::String (err);
        else status = "params: " + juce::String (n) + " values from " + f.getFileName() + (unknown.empty() ? "" : " (" + juce::String ((int) unknown.size()) + " unknown keys)");
    }
    else status = "params: built-in defaults (no " + f.getFullPathName() + ")";
    {
        const juce::SpinLock::ScopedLockType sl (engineLock);
        sys.engine.tun = t;
        if (getSampleRate() > 0) sys.prepare (getSampleRate(), 2);
    }
    paramsStatus = status;
    return status;
}

// ------------------------------------------------------------- state --------
juce::ValueTree FenixProcessor::cablesToTree()
{
    juce::ValueTree t ("CABLES");
    for (auto& c : sys.engine.cables)
    {
        juce::ValueTree e ("C");
        auto& a = *sys.engine.mods[(size_t) c.modA]; auto& b = *sys.engine.mods[(size_t) c.modB];
        e.setProperty ("a", juce::String (a.id), nullptr); e.setProperty ("ja", juce::String (a.jacks[(size_t) c.jackA].name), nullptr);
        e.setProperty ("b", juce::String (b.id), nullptr); e.setProperty ("jb", juce::String (b.jacks[(size_t) c.jackB].name), nullptr);
        t.appendChild (e, nullptr);
    }
    return t;
}
void FenixProcessor::cablesFromTree (const juce::ValueTree& t)
{
    sys.engine.cables.clear();
    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        auto e = t.getChild (i);
        auto* a = sys.engine.byId (e.getProperty ("a").toString().toStdString());
        auto* b = sys.engine.byId (e.getProperty ("b").toString().toStdString());
        if (a && b)
        {
            int ja = sys.engine.jackIndex (a, e.getProperty ("ja").toString().toStdString());
            int jb = sys.engine.jackIndex (b, e.getProperty ("jb").toString().toStdString());
            if (ja >= 0 && jb >= 0) sys.engine.cables.push_back ({ sys.engine.indexOf (a), ja, sys.engine.indexOf (b), jb });
        }
    }
    sys.engine.rebuild();
    ++cableVer;
}
void FenixProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("FENIX");
    root.appendChild (apvts.copyState(), nullptr);
    { const juce::SpinLock::ScopedLockType sl (engineLock); root.appendChild (cablesToTree(), nullptr); }
    juce::MemoryOutputStream mo (dest, true);
    root.writeToStream (mo);
}
void FenixProcessor::setStateInformation (const void* data, int size)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) size);
    if (! root.isValid() || ! root.hasType ("FENIX")) return;
    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);
    auto cab = root.getChildWithName ("CABLES");
    if (cab.isValid()) { const juce::SpinLock::ScopedLockType sl (engineLock); cablesFromTree (cab); }
}

// ------------------------------------------------------- factory patches ----
juce::StringArray FenixProcessor::factoryPatchNames()
{
    return { "Init (empty)",
             "JonDent: VCO1 -> VCF3 stereo filter",
             "SOS: classic two-VCO bass",
             "SOS: ring-mod bells",
             "SOS: audio-rate FM + sync lead",
             "Random S&H melody",
             "Filter feedback screamer",
             "Digital noise drums",
             "External: audio-in follower (FX)" };
}

void FenixProcessor::loadFactoryPatch (int index)
{
    auto& s = sys;
    auto set = [&] (Module* m, const char* ctl, float v)
    {
        int c = s.engine.ctlIndex (m, ctl); if (c < 0) return;
        if (auto* p = apvts.getParameter (paramID (*m, c)))
        {
            auto& ct = m->ctls[(size_t) c];
            p->setValueNotifyingHost (ct.steps > 0 ? v / (float) (ct.steps - 1) : v);
        }
    };
    // reset every control to its default first
    for (auto& m : s.engine.mods)
        for (int c = 0; c < (int) m->ctls.size(); ++c)
            if (auto* p = apvts.getParameter (paramID (*m, c))) p->setValueNotifyingHost (p->getDefaultValue());
    { const juce::SpinLock::ScopedLockType sl (engineLock); s.engine.clearCables(); }
    auto C = [&] (Module* a, const char* ja, Module* b, const char* jb)
    { addCable (s.engine.indexOf (a), s.engine.jackIndex (a, ja), s.engine.indexOf (b), s.engine.jackIndex (b, jb)); };

    switch (index)
    {
        default: break;
        case 1: // JonDent 2015: VCO1 sine/square/ramp + subs into VCF3, 12 dB and 24 dB outputs as a stereo pair, LFO sweeps
            C (s.rear, "MIDI 1V/oct", s.vco1, "CV1");
            C (s.vco1, "Saw", s.vcf3, "Audio 1"); C (s.vco1, "Pulse", s.vcf3, "Audio 2"); C (s.vco1, "Sub1", s.vcf3, "Audio 3");
            C (s.lfo1, "Triangle", s.vcf3, "CV2");
            C (s.vcf3, "12 dB", s.stereoOut, "L"); C (s.vcf3, "24 dB", s.stereoOut, "R");
            set (s.vco1, "Frequency", 0.47f); set (s.vcf3, "Frequency", 0.45f); set (s.vcf3, "Resonance", 0.22f);
            set (s.vcf3, "Level 1", 0.7f); set (s.vcf3, "Level 2", 0.4f); set (s.vcf3, "Level 3", 0.5f); set (s.vcf3, "CV2", 0.75f);
            set (s.lfo1, "Freq", 0.3f);
            break;
        case 2: // two detuned saws -> mixer -> VCF3 -> VCA, EG1 on both
            C (s.rear, "MIDI 1V/oct", s.vco1, "CV1"); C (s.rear, "MIDI 1V/oct", s.vco2, "CV1"); C (s.rear, "MIDI gate", s.eg1, "Gate");
            C (s.vco1, "Saw", s.mixer1, "A"); C (s.vco2, "Saw", s.mixer1, "B");
            C (s.mixer1, "Out", s.vcf3, "Audio 1"); C (s.eg1, "CV out", s.vcf3, "CV2");
            C (s.vcf3, "24 dB", s.vca1, "In"); C (s.eg1, "CV out", s.vca1, "CV");
            C (s.vca1, "Out", s.stereoOut, "L"); C (s.vca1, "Out", s.stereoOut, "R");
            set (s.vco1, "Frequency", 0.43f); set (s.vco2, "Frequency", 0.43f); set (s.vco2, "Tune", 0.53f);
            set (s.mixer1, "A", 0.8f); set (s.mixer1, "B -/+", 0.9f);
            set (s.vcf3, "Frequency", 0.35f); set (s.vcf3, "Resonance", 0.15f); set (s.vcf3, "CV2", 0.8f); set (s.vcf3, "Level 1", 0.8f);
            set (s.eg1, "Attack", 0.05f); set (s.eg1, "Decay", 0.6f); set (s.eg1, "Sustain", 0.4f); set (s.eg1, "Release", 0.5f);
            break;
        case 3: // ring-mod bells: VCO1 sine x VCO2 saw (inharmonic ratio) -> VCF1 -> VCA <- EG2 accent mix
            C (s.rear, "MIDI 1V/oct", s.vco1, "CV1"); C (s.rear, "MIDI 1V/oct", s.vco2, "CV1"); C (s.rear, "MIDI gate", s.eg2, "Gate");
            C (s.vco1, "Sine", s.rm2, "A"); C (s.vco2, "Saw", s.rm2, "B");
            C (s.rm2, "Out", s.vcf1, "Audio"); C (s.vcf1, "LP", s.vca1, "In"); C (s.eg2, "Mix out", s.vca1, "CV");
            C (s.vca1, "Out", s.stereoOut, "L"); C (s.vcf1, "BP", s.vca2, "In"); C (s.eg2, "CV out", s.vca2, "CV"); C (s.vca2, "Out", s.stereoOut, "R");
            set (s.vco1, "Frequency", 0.5f); set (s.vco2, "Frequency", 0.62f); set (s.vco2, "Tune", 0.57f);
            set (s.vcf1, "Frequency", 0.7f); set (s.vcf1, "Resonance", 0.3f);
            set (s.eg2, "Attack", 0.0f); set (s.eg2, "Decay/Release", 0.72f); set (s.eg2, "Accent", 0.5f);
            break;
        case 4: // audio-rate FM + hard sync lead
            C (s.rear, "MIDI 1V/oct", s.vco1, "CV1"); C (s.rear, "MIDI 1V/oct", s.vco2, "CV1"); C (s.rear, "MIDI gate", s.eg1, "Gate");
            C (s.vco2, "Saw", s.vco1, "Lin FM"); C (s.vco2, "Pulse", s.vco1, "Sync in"); C (s.eg1, "Accent out", s.vco1, "Sync level CV");
            C (s.vco1, "Saw", s.vcf3, "Audio 1"); C (s.eg1, "CV out", s.vcf3, "CV2"); C (s.lfo2, "Triangle", s.vcf3, "CV1");
            C (s.vcf3, "24 dB", s.vca1, "In"); C (s.eg1, "CV out", s.vca1, "CV"); C (s.vca1, "Out", s.stereoOut, "L"); C (s.vca1, "Out", s.stereoOut, "R");
            set (s.vco1, "Frequency", 0.5f); set (s.vco2, "Frequency", 0.45f); set (s.vco1, "Sync level", 0.6f);
            set (s.vcf3, "Frequency", 0.5f); set (s.vcf3, "Resonance", 0.2f); set (s.vcf3, "CV2", 0.7f); set (s.vcf3, "Level 1", 0.75f);
            set (s.lfo2, "Freq", 0.25f); set (s.eg1, "Attack", 0.02f); set (s.eg1, "Decay", 0.55f); set (s.eg1, "Sustain", 0.5f); set (s.eg1, "Accent", 0.45f);
            break;
        case 5: // random S&H melody: noise -> S&H clocked by LFO1 pulse -> portamento -> VCO1, EG2 pings VCF1
            C (s.noise, "Analog", s.sh, "In"); C (s.lfo1, "Pulse", s.sh, "Trigger"); C (s.sh, "Out", s.cvmix1, "In"); C (s.cvmix1, "Out", s.portamento, "In");
            C (s.portamento, "Out 1", s.vco1, "CV1"); C (s.lfo1, "Pulse", s.eg2, "Gate");
            C (s.vco1, "Pulse", s.vcf1, "Audio"); C (s.eg2, "CV out", s.vcf1, "CV1"); C (s.vcf1, "LP", s.vca1, "In"); C (s.eg2, "CV out", s.vca1, "CV");
            C (s.vca1, "Out", s.stereoOut, "L"); C (s.vca1, "Out", s.stereoOut, "R");
            set (s.lfo1, "Freq", 0.46f); set (s.cvmix1, "Gain", 0.5f); set (s.portamento, "Time 1", 0.35f);
            set (s.vco1, "Frequency", 0.48f); set (s.vco1, "PW", 0.3f); set (s.vcf1, "Frequency", 0.35f); set (s.vcf1, "Resonance", 0.55f);
            set (s.eg2, "Attack", 0.0f); set (s.eg2, "Decay/Release", 0.5f);
            break;
        case 6: // filter feedback screamer: VCF1 BP -> VCA1 -> back into VCF1, LFO3 sweeps, VCF3 tames
            C (s.vco1, "Saw", s.vcf1, "Audio"); C (s.vcf1, "BP", s.vca1, "In"); C (s.vca1, "Out", s.vcf1, "Audio");
            C (s.lfo2, "Triangle", s.vca1, "CV"); C (s.lfo3, "Sine", s.vcf1, "CV2");
            C (s.vcf1, "LP", s.vcf3, "Audio 1"); C (s.vcf3, "24 dB", s.stereoOut, "L"); C (s.vcf1, "HP", s.stereoOut, "R");
            set (s.vco1, "Frequency", 0.4f); set (s.vcf1, "Frequency", 0.5f); set (s.vcf1, "Resonance", 0.85f); set (s.vcf1, "CV2", 0.8f);
            set (s.lfo2, "Freq", 0.2f); set (s.lfo3, "Freq", 0.3f); set (s.vcf3, "Frequency", 0.75f); set (s.vcf3, "Level 1", 0.6f);
            break;
        case 7: // digital noise drums: EG3 delayed gate + EG2 accent through VCF2 self-oscillating
            C (s.lfo1, "Pulse", s.eg2, "Gate"); C (s.lfo1, "Pulse", s.eg3, "Gate");
            C (s.noise, "Digital", s.vcf2, "Audio"); C (s.eg2, "Accent out", s.vcf2, "CV1");
            C (s.vcf2, "BP", s.vca1, "In"); C (s.eg2, "CV out", s.vca1, "CV");
            C (s.noise, "Analog", s.vca2, "In"); C (s.eg3, "CV out", s.vca2, "CV");
            C (s.vca1, "Out", s.mixer2, "A"); C (s.vca2, "Out", s.mixer2, "B"); C (s.mixer2, "Out", s.stereoOut, "L"); C (s.mixer2, "Out", s.stereoOut, "R");
            set (s.lfo1, "Freq", 0.42f); set (s.noise, "Timbre", 0.45f); set (s.vcf2, "Frequency", 0.3f); set (s.vcf2, "Resonance", 0.98f);
            set (s.eg2, "Attack", 0.0f); set (s.eg2, "Decay/Release", 0.45f); set (s.eg2, "Accent", 0.35f);
            set (s.eg3, "Delay", 0.55f); set (s.eg3, "Decay", 0.4f); set (s.mixer2, "A-B balance", 0.4f);
            break;
        case 8: // external: audio-in 1 through VCF3, envelope follower on cutoff, gate from audio-in 2 fires EG1
            C (s.esp1, "Audio out", s.vcf3, "Audio 1"); C (s.esp1, "Level CV", s.vcf3, "CV2"); C (s.esp2, "Gate", s.eg1, "Gate"); C (s.eg1, "CV out", s.vcf3, "CV1");
            C (s.vcf3, "24 dB", s.stereoOut, "L"); C (s.vcf3, "12 dB", s.stereoOut, "R");
            set (s.esp1, "Gain", 0.4f); set (s.esp2, "Gain", 0.4f); set (s.esp2, "Threshold", 0.2f);
            set (s.vcf3, "Frequency", 0.3f); set (s.vcf3, "Resonance", 0.2f); set (s.vcf3, "CV2", 0.9f); set (s.vcf3, "Level 1", 0.8f);
            set (s.eg1, "Attack", 0.0f); set (s.eg1, "Decay", 0.5f); set (s.eg1, "Sustain", 0.2f);
            break;
    }
}

juce::AudioProcessorEditor* FenixProcessor::createEditor() { return new FenixEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FenixProcessor(); }
