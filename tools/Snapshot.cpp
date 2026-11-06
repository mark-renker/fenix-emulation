// Renders the Fénix I editor to a PNG without a host (for docs and UI checks).
#include <juce_gui_extra/juce_gui_extra.h>
#include "../src/plugin/PluginProcessor.h"
#include "../src/ui/Panel.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("fenix_ui.png"));
    int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 1;
    FenixProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.loadFactoryPatch (preset);
    fenixui::PanelView view (proc);
    juce::Image img (juce::Image::ARGB, view.logicalW, view.logicalH, true);
    { juce::Graphics g (img); view.paintEntireComponent (g, true); }
    juce::PNGImageFormat png; juce::FileOutputStream fo (out);
    if (fo.openedOk()) png.writeImageToStream (img, fo);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
