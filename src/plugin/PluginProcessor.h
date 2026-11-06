// Fénix I emulation — JUCE processor: host I/O, 2x oversampling wrap, MIDI-to-CV,
// parameter binding, patch state, factory presets, tunables (JSON) loading.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/System.h"

class FenixProcessor : public juce::AudioProcessor
{
public:
    FenixProcessor();
    ~FenixProcessor() override = default;

    void prepareToPlay (double sr, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- patch (cable) operations, UI thread ----
    bool addCable (int mA, int jA, int mB, int jB);
    void removeCablesAt (int m, int j);
    void removeCable (const fenix::Cable& c);
    void clearAllCables();
    std::vector<fenix::Cable> cableSnapshot();
    int cableVersion() const noexcept { return cableVer.load(); }
    void loadFactoryPatch (int index);
    static juce::StringArray factoryPatchNames();
    void setMomentary (fenix::Module* m, int ctlIdx, bool down);

    // ---- tunables ----
    static juce::File userParamsFile();
    juce::String reloadParams();          // returns a status line for the UI
    juce::String paramsStatus;

    fenix::System sys;
    juce::AudioProcessorValueTreeState apvts;
    juce::SpinLock engineLock;
    std::atomic<float> cpuLoad { 0.0f };

    static juce::String paramID (const fenix::Module& m, int ctlIdx);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void applyParams();
    juce::ValueTree cablesToTree();
    void cablesFromTree (const juce::ValueTree&);

    std::vector<std::atomic<float>*> paramPtrs;
    struct MomentaryFlag { fenix::Module* mod; int ctl; std::atomic<bool> down { false }; };
    std::vector<std::unique_ptr<MomentaryFlag>> momentaries;

    std::atomic<int> cableVer { 0 };
    fenix::HalfBand upL, upR, downL, downR;
    juce::SortedSet<int> notesDown; int heldNote = -1; float heldVel = 0.0f; float bendV = 0.0f, modV = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FenixProcessor)
};
