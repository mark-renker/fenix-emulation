# Factory patches

The reference patches (JonDent's VCO1 → VCF3 stereo-filter test, the SOS-style
bass / ring-mod / FM+sync / S&H / feedback / noise-drum patches, and the FX
audio-in follower) are defined in `src/plugin/PluginProcessor.cpp`
(`FenixProcessor::loadFactoryPatch`) as cable lists plus knob settings, using
the designer's module and socket names. Load them from the **PATCHES** menu.

A user patch (cables + every knob) is saved with the host session; the state
format is the JUCE ValueTree written by `getStateInformation`, with cables stored
by module id and socket name so it survives future layout changes.
