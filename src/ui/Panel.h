// Fénix I emulation — panel UI: eleven module strips plus the rear I/O strip,
// colour-coded banana jacks (black = input, green = audio out, blue = CV out),
// heater LEDs, drag-to-patch cables. Layout is generated per module from its
// controls and sockets so the engine remains the single source of truth.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>
#include "../plugin/PluginProcessor.h"

namespace fenixui {

constexpr int STRIP_W = 168;
constexpr int TITLE_H = 22;
constexpr int KNOB_CELL_W = 56, KNOB_CELL_H = 62;
constexpr int JACK_CELL_W = 42, JACK_CELL_H = 44;
constexpr int HEADER_H = 34, REAR_H = 70;

namespace col {
    const juce::Colour bg        (0xff0f1012);
    const juce::Colour face      (0xff1f2023);
    const juce::Colour faceEdge  (0xff3a3c40);
    const juce::Colour ink       (0xffd7d4cc);
    const juce::Colour inkDim    (0xff8e8c86);
    const juce::Colour rail      (0xff2b2622);
    const juce::Colour jackBlack (0xff141416);
    const juce::Colour jackGreen (0xff3ea35a);
    const juce::Colour jackBlue  (0xff3c7bd6);
    const juce::Colour knob      (0xff2c2d31);
    const juce::Colour knobRim   (0xff5a5c62);
    const juce::Colour pointer   (0xfff2efe6);
    const juce::Colour ledRed    (0xffe0392b);
    const juce::Colour ledGreen  (0xff3fd36a);
    const juce::Colour ledAmber  (0xfff0a830);
}

inline juce::Font uiFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", h, bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Colour jackColour (fenix::JackColor c)
{
    switch (c) { case fenix::JackColor::Green: return col::jackGreen; case fenix::JackColor::Blue: return col::jackBlue; case fenix::JackColor::Black: break; }
    return col::jackBlack;
}

// ------------------------------------------------------------- widgets ------
class JackW;
struct CableHost
{
    virtual ~CableHost() = default;
    virtual void jackDragStart (JackW*) = 0;
    virtual void jackDragMove (juce::Point<float> screen) = 0;
    virtual void jackDragEnd (juce::Point<float> screen) = 0;
    virtual void jackRightClick (JackW*) = 0;
};

class JackW : public juce::Component, public juce::SettableTooltipClient
{
public:
    JackW (fenix::Module& m, int jackIdx, CableHost*& h) : mod (m), jack (jackIdx), host (h)
    {
        setRepaintsOnMouseActivity (true);
        auto& j = m.jacks[(size_t) jackIdx];
        setTooltip (juce::String (m.title) + " / " + j.name + (j.type == fenix::JackType::In ? " (input)" : (j.color == fenix::JackColor::Green ? " (audio out)" : " (CV out)")));
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        auto c = r.getCentre(); float R = r.getWidth() * 0.5f;
        auto body = jackColour (mod.jacks[(size_t) jack].color);
        g.setColour (body); g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawEllipse (r, 1.2f);
        g.setColour (juce::Colour (0xff050505)); g.fillEllipse (juce::Rectangle<float> (R * 0.9f, R * 0.9f).withCentre (c));
        g.setColour (juce::Colour (0xff2a2a2e)); g.fillEllipse (juce::Rectangle<float> (R * 0.45f, R * 0.45f).withCentre (c));
        if (isMouseOverOrDragging() || dropTarget) { g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawEllipse (r.expanded (2.0f), 1.8f); }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! host) return; if (e.mods.isPopupMenu()) host->jackRightClick (this); else host->jackDragStart (this); }
    void mouseDrag (const juce::MouseEvent& e) override { if (host && ! e.mods.isPopupMenu()) host->jackDragMove (e.getScreenPosition().toFloat()); }
    void mouseUp   (const juce::MouseEvent& e) override { if (host && ! e.mods.isPopupMenu()) host->jackDragEnd (e.getScreenPosition().toFloat()); }
    fenix::Module& mod; int jack; CableHost*& host; bool dropTarget = false;
};

class KnobW : public juce::Component, public juce::SettableTooltipClient
{
public:
    KnobW (juce::RangedAudioParameter& p, bool isBig, float def) : big (isBig), defaultVal (def), att (p, [this] (float v) { val = v; repaint(); })
    {
        att.sendInitialUpdate();
        setTooltip (p.getName (64));
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        auto c = r.getCentre(); float R = r.getWidth() * 0.5f;
        // skirt scale
        g.setColour (col::inkDim.withAlpha (0.6f));
        for (int i = 0; i <= 10; ++i)
        {
            float a = juce::degreesToRadians (-135.0f + 27.0f * (float) i);
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (R * 0.98f, a), c.getPointOnCircumference (R * 1.08f, a)), 1.0f);
        }
        auto body = juce::Rectangle<float> (R * 1.7f, R * 1.7f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (col::knob.brighter (0.25f), body.getX(), body.getY(), col::knob.darker (0.4f), body.getRight(), body.getBottom(), true));
        g.fillEllipse (body);
        g.setColour (col::knobRim); g.drawEllipse (body, 1.2f);
        float ang = juce::degreesToRadians (-135.0f + 270.0f * val);
        g.setColour (col::pointer);
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (R * 0.25f, ang), c.getPointOnCircumference (R * 0.8f, ang)), juce::jmax (1.8f, R * 0.14f));
    }
    void mouseDown (const juce::MouseEvent&) override { att.beginGesture(); startVal = val; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        float sens = e.mods.isShiftDown() ? 1500.0f : 260.0f;
        att.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, startVal - (float) e.getDistanceFromDragStartY() / sens));
    }
    void mouseUp (const juce::MouseEvent&) override { att.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { att.setValueAsCompleteGesture (defaultVal); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { att.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, val + (float) w.deltaY * 0.06f)); }
    bool big; float defaultVal, val = 0, startVal = 0;
    juce::ParameterAttachment att;
};

class SwitchW : public juce::Component, public juce::SettableTooltipClient
{
public:
    SwitchW (juce::RangedAudioParameter& p, int nPositions, juce::StringArray labs) : n (nPositions), labels (labs),
        att (p, [this] (float v) { idx = (int) std::lround (v); repaint(); })
    {
        att.sendInitialUpdate(); setTooltip (p.getName (64));
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float cellW = r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + (float) i * cellW, r.getY(), cellW, r.getHeight());
            g.setColour (i == idx ? col::jackBlue : col::knob);
            g.fillRoundedRectangle (cell.reduced (1.0f), 3.0f);
            g.setColour (i == idx ? juce::Colours::white : col::inkDim);
            g.setFont (uiFont (9.5f, i == idx));
            g.drawText (labels[i], cell.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        int i = juce::jlimit (0, n - 1, (int) ((float) e.x / ((float) getWidth() / (float) n)));
        att.setValueAsCompleteGesture ((float) i);
    }
    int n, idx = 0; juce::StringArray labels; juce::ParameterAttachment att;
};

class ButtonW : public juce::Component
{
public:
    std::function<void (bool)> onPress;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (down ? col::ledRed.darker (0.4f) : col::ledRed);
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.15f : 0.35f));
        g.fillEllipse (r.reduced (r.getWidth() * 0.25f).translated (-2.0f, -2.0f));
        g.setColour (col::ink.withAlpha (0.6f)); g.drawEllipse (r, 1.0f);
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); if (onPress) onPress (true); }
    void mouseUp   (const juce::MouseEvent&) override { down = false; repaint(); if (onPress) onPress (false); }
    bool down = false;
};

class LedW : public juce::Component
{
public:
    enum Kind { Heater, Activity, Overload };
    LedW (fenix::Module& m, int ledIdx, Kind k) : mod (m), idx (ledIdx), kind (k) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        juce::Colour c;
        if (kind == Heater) c = col::ledRed.interpolatedWith (col::ledGreen, bright);
        else if (kind == Overload) c = col::ledRed.withAlpha (0.25f + 0.75f * bright);
        else c = col::ledAmber.withAlpha (0.2f + 0.8f * bright);
        g.setColour (c.withAlpha (0.35f)); g.fillEllipse (r.expanded (3.0f));
        g.setColour (c); g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.5f)); g.fillEllipse (r.reduced (r.getWidth() * 0.3f).translated (-1.0f, -1.0f));
    }
    void refresh()
    {
        float b = idx < (int) mod.leds.size() ? mod.leds[(size_t) idx] : 0.0f;
        if (std::fabs (b - bright) > 0.02f) { bright = b; repaint(); }
    }
    fenix::Module& mod; int idx; Kind kind; float bright = 0;
};

// ------------------------------------------------------- module face --------
class ModuleFace : public juce::Component
{
public:
    ModuleFace (fenix::Module& m, CableHost*& hostRef, FenixProcessor& p, bool dark = false)
        : mod (m), host (hostRef), proc (p), darkFace (dark)
    {
        build();
    }

    struct Label { juce::String text; juce::Rectangle<int> r; bool dim; int lines = 1; };
    std::vector<Label> labels;
    int separatorY = -1;

    void build()
    {
        const int W = STRIP_W;
        int y = TITLE_H + 4;
        // ---- controls ----
        int nCtl = (int) mod.ctls.size();
        if (nCtl > 0)
        {
            int perRow = 3; int rows = (nCtl + perRow - 1) / perRow;
            for (int c = 0; c < nCtl; ++c)
            {
                auto& ct = mod.ctls[(size_t) c];
                int row = c / perRow, colIdx = c % perRow;
                int inRow = std::min (perRow, nCtl - row * perRow);
                int x0 = (W - inRow * KNOB_CELL_W) / 2 + colIdx * KNOB_CELL_W;
                juce::Rectangle<int> cell (x0, y + row * KNOB_CELL_H, KNOB_CELL_W, KNOB_CELL_H);
                auto& param = *proc.apvts.getParameter (FenixProcessor::paramID (mod, c));
                juce::String name (ct.name);
                if (ct.momentary)
                {
                    auto* b = new ButtonW(); buttons.add (b); addAndMakeVisible (b);
                    b->setBounds (cell.withSizeKeepingCentre (34, 34).translated (0, -8));
                    auto* mp = &mod; auto* pp = &proc;
                    b->onPress = [pp, mp, c] (bool d) { pp->setMomentary (mp, c, d); };
                }
                else if (ct.steps > 0)
                {
                    juce::StringArray labs;
                    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (&param)) labs = ch->choices;
                    auto* s = new SwitchW (param, ct.steps, labs); switches.add (s); addAndMakeVisible (s);
                    s->setBounds (cell.withSizeKeepingCentre (ct.steps > 2 ? KNOB_CELL_W + 20 : 46, 20).translated (0, -8));
                    name = name.upToFirstOccurrenceOf (" ", false, false) == "Octave" ? "OCTAVE" : name;
                }
                else
                {
                    bool big = name.startsWith ("Freq");
                    auto* k = new KnobW (param, big, ct.def); knobs.add (k); addAndMakeVisible (k);
                    int d = big ? 42 : 34;
                    k->setBounds (cell.withSizeKeepingCentre (d, d).translated (0, -8));
                }
                labels.push_back ({ name.toUpperCase(), cell.removeFromBottom (14).expanded (6, 0), false });
            }
            y += rows * KNOB_CELL_H;
        }
        // ---- sockets: inputs then outputs ----
        std::vector<int> ins, outs;
        for (int j = 0; j < (int) mod.jacks.size(); ++j) (mod.jacks[(size_t) j].type == fenix::JackType::In ? ins : outs).push_back (j);
        auto layJacks = [&] (const std::vector<int>& list)
        {
            if (list.empty()) return;
            int longest = 0;
            for (int j : list) longest = std::max (longest, (int) std::strlen (mod.jacks[(size_t) j].name));
            const int perRow = longest > 9 ? 3 : 4;                 // long legends get wider cells
            const int cellW = longest > 9 ? (W - 8) / 3 : JACK_CELL_W;
            const int cellH = longest > 12 ? JACK_CELL_H + 10 : JACK_CELL_H;   // and two-line labels
            int rows = ((int) list.size() + perRow - 1) / perRow;
            for (int i = 0; i < (int) list.size(); ++i)
            {
                int row = i / perRow, colIdx = i % perRow;
                int inRow = std::min (perRow, (int) list.size() - row * perRow);
                int x0 = (W - inRow * cellW) / 2 + colIdx * cellW;
                juce::Rectangle<int> cell (x0, y + row * cellH, cellW, cellH);
                auto* jw = new JackW (mod, list[(size_t) i], host); jacks.add (jw); addAndMakeVisible (jw);
                const bool two = longest > 12;
                jw->setBounds (cell.withSizeKeepingCentre (24, 24).translated (0, two ? -13 : -8));
                juce::String legend = juce::String (mod.jacks[(size_t) list[(size_t) i]].name).toUpperCase();
                if (two && legend.containsChar (' '))   // break before the last word so neighbours never touch
                    legend = legend.upToLastOccurrenceOf (" ", false, false) + "\n" + legend.fromLastOccurrenceOf (" ", false, false);
                labels.push_back ({ legend, cell.removeFromBottom (two ? 24 : 13).expanded (2, 0), false, two ? 2 : 1 });
            }
            y += rows * cellH;
        };
        layJacks (ins);
        if (! ins.empty() && ! outs.empty()) { separatorY = y - 2; y += 3; }
        layJacks (outs);
        // ---- LEDs ----
        if (! mod.leds.empty())
        {
            bool heater = mod.id.rfind ("vco", 0) == 0 || mod.id.rfind ("vcf", 0) == 0;
            auto* l = new LedW (mod, 0, heater ? LedW::Heater : LedW::Activity); leds.add (l); addAndMakeVisible (l);
            l->setBounds (W - 20, 6, 12, 12);
            if (mod.leds.size() > 1) { auto* o = new LedW (mod, 1, LedW::Overload); leds.add (o); addAndMakeVisible (o); o->setBounds (W - 36, 6, 12, 12); }
        }
        setSize (W, y + 6);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (darkFace ? col::bg : col::face); g.fillRoundedRectangle (b, 3.0f);
        g.setColour (col::faceEdge); g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (col::ink); g.setFont (uiFont (12.5f, true));
        g.drawText (mod.title, 8, 4, getWidth() - 40, TITLE_H - 4, juce::Justification::centredLeft);
        g.setColour (col::faceEdge); g.drawLine (6.0f, (float) TITLE_H, (float) getWidth() - 6.0f, (float) TITLE_H, 1.0f);
        if (separatorY > 0) { g.setColour (col::faceEdge.withAlpha (0.7f)); g.drawLine (14.0f, (float) separatorY, (float) getWidth() - 14.0f, (float) separatorY, 1.0f); }
        for (auto& l : labels)
        {
            g.setColour (l.dim ? col::inkDim : col::ink.withAlpha (0.9f));
            g.setFont (uiFont (8.6f));
            g.drawFittedText (l.text, l.r, juce::Justification::centred, l.lines, 0.75f);
        }
    }

    fenix::Module& mod; CableHost*& host; FenixProcessor& proc; bool darkFace;
    juce::OwnedArray<JackW> jacks; juce::OwnedArray<KnobW> knobs; juce::OwnedArray<SwitchW> switches;
    juce::OwnedArray<ButtonW> buttons; juce::OwnedArray<LedW> leds;
};

// ------------------------------------------------------------ panel view ----
class PanelView : public juce::Component, public CableHost, private juce::Timer
{
public:
    explicit PanelView (FenixProcessor& p) : proc (p)
    {
        hostPtr = this;
        auto& s = proc.sys;
        // the eleven strips (left to right)
        const std::vector<std::vector<fenix::Module*>> strips = {
            { s.vco1 }, { s.vco2, s.vco3 }, { s.lfo1, s.lfo2, s.lfo3 }, { s.vcf1, s.vcf2 }, { s.vcf3, s.noise },
            { s.eg1, s.eg3 }, { s.eg2, s.sh, s.comparator }, { s.vca1, s.vca2, s.vca3, s.vca4 },
            { s.mixer1, s.mixer2, s.mixer3 }, { s.cvmix1, s.cvmix2, s.portamento, s.slew },
            { s.rm1, s.rm2, s.esp1, s.esp2, s.stereoOut } };
        const int gap = 6, rail = 14;
        int x = rail, maxBottom = 0;
        for (auto& strip : strips)
        {
            int y = HEADER_H + 10;
            for (auto* m : strip)
            {
                auto* f = new ModuleFace (*m, hostPtr, proc); faces.add (f); addAndMakeVisible (f);
                f->setTopLeftPosition (x, y); y += f->getHeight() + gap;
            }
            maxBottom = std::max (maxBottom, y);
            x += STRIP_W + gap;
        }
        logicalW = x - gap + rail;
        // rear I/O strip
        {
            auto* f = new ModuleFace (*s.rear, hostPtr, proc, true); faces.add (f); addAndMakeVisible (f);
            // re-lay horizontally: all sockets on one row
            f->setSize (logicalW - 2 * rail, REAR_H);
            int n = f->jacks.size(); int cw = (f->getWidth() - 220) / std::max (1, n);
            f->labels.clear();
            for (int i = 0; i < n; ++i)
            {
                auto* j = f->jacks[i];
                juce::Rectangle<int> cell (200 + i * cw, 8, cw, REAR_H - 12);
                j->setBounds (cell.withSizeKeepingCentre (24, 24).translated (0, -8));
                f->labels.push_back ({ juce::String (s.rear->jacks[(size_t) j->jack].name).toUpperCase(), cell.removeFromBottom (14), false });
            }
            f->separatorY = -1;
            f->setTopLeftPosition (rail, maxBottom + 4);
            rearFace = f;
        }
        logicalH = maxBottom + 4 + REAR_H + 10;
        setSize (logicalW, logicalH);

        presets.addItemList (FenixProcessor::factoryPatchNames(), 1);
        presets.setTextWhenNothingSelected ("PATCHES");
        presets.onChange = [this] { int i = presets.getSelectedItemIndex(); if (i >= 0) { proc.loadFactoryPatch (i); presets.setSelectedId (0, juce::dontSendNotification); } };
        addAndMakeVisible (presets); presets.setBounds (logicalW - 560, 5, 300, HEADER_H - 10);
        clearBtn.setButtonText ("CLEAR"); clearBtn.onClick = [this] { proc.clearAllCables(); };
        addAndMakeVisible (clearBtn); clearBtn.setBounds (logicalW - 250, 5, 80, HEADER_H - 10);
        reloadBtn.setButtonText ("RELOAD PARAMS"); reloadBtn.onClick = [this] { status = proc.reloadParams(); repaint(); };
        addAndMakeVisible (reloadBtn); reloadBtn.setBounds (logicalW - 160, 5, 140, HEADER_H - 10);
        status = proc.paramsStatus;

        for (auto* f : faces) for (auto* j : f->jacks) allJacks.push_back (j);
        startTimerHz (30);
        setOpaque (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::ink); g.setFont (uiFont (16.0f, true));
        g.drawText ("SYNTON  FENIX  I", 14, 0, 300, HEADER_H, juce::Justification::centredLeft);
        g.setColour (col::inkDim); g.setFont (uiFont (10.5f));
        g.drawText ("circuit-level emulation  -  black = input, green = audio out, blue = CV out  -  drag between sockets to patch, right-click a socket to unpatch", 190, 0, 1100, HEADER_H, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f));
        g.drawText (status + "    |    DSP " + juce::String ((int) (proc.cpuLoad.load() * 100.0f)) + " %", 14, logicalH - 16, logicalW - 28, 14, juce::Justification::centredLeft);
        // side rails
        g.setColour (col::rail);
        g.fillRect (0, HEADER_H, 10, logicalH - HEADER_H); g.fillRect (logicalW - 10, HEADER_H, 10, logicalH - HEADER_H);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        auto cables = proc.cableSnapshot();
        int idx = 0;
        for (auto& c : cables)
        {
            auto* a = findJack (c.modA, c.jackA); auto* b = findJack (c.modB, c.jackB);
            if (a && b) drawCable (g, centreOf (a), centreOf (b), cableColour (idx));
            ++idx;
        }
        if (dragSrc != nullptr) drawCable (g, centreOf (dragSrc), dragPos, juce::Colours::white.withAlpha (0.7f));
    }

    void jackDragStart (JackW* j) override { dragSrc = j; dragPos = centreOf (j); repaint(); }
    void jackDragMove (juce::Point<float> screen) override
    {
        dragPos = getLocalPoint (nullptr, screen);
        auto* over = jackAt (dragPos);
        for (auto* j : allJacks) j->dropTarget = (j == over && j != dragSrc);
        repaint();
    }
    void jackDragEnd (juce::Point<float> screen) override
    {
        auto* dst = jackAt (getLocalPoint (nullptr, screen));
        if (dragSrc && dst && dst != dragSrc)
            proc.addCable (proc.sys.engine.indexOf (&dragSrc->mod), dragSrc->jack, proc.sys.engine.indexOf (&dst->mod), dst->jack);
        for (auto* j : allJacks) j->dropTarget = false;
        dragSrc = nullptr; repaint();
    }
    void jackRightClick (JackW* j) override
    {
        juce::PopupMenu menu;
        auto cables = proc.cableSnapshot();
        int mIdx = proc.sys.engine.indexOf (&j->mod);
        std::vector<fenix::Cable> mine;
        for (auto& c : cables) if ((c.modA == mIdx && c.jackA == j->jack) || (c.modB == mIdx && c.jackB == j->jack)) mine.push_back (c);
        if (mine.empty()) return;
        int id = 1;
        for (auto& c : mine)
        {
            bool aIsThis = (c.modA == mIdx && c.jackA == j->jack);
            auto& other = aIsThis ? *proc.sys.engine.mods[(size_t) c.modB] : *proc.sys.engine.mods[(size_t) c.modA];
            int oj = aIsThis ? c.jackB : c.jackA;
            menu.addItem (id++, "Remove cable to " + juce::String (other.title) + " / " + other.jacks[(size_t) oj].name);
        }
        menu.addSeparator(); menu.addItem (999, "Remove all cables here");
        menu.showMenuAsync (juce::PopupMenu::Options(), [this, mine, mIdx, j] (int result)
        {
            if (result == 999) proc.removeCablesAt (mIdx, j->jack);
            else if (result > 0 && result <= (int) mine.size()) proc.removeCable (mine[(size_t) result - 1]);
            repaint();
        });
    }

    int logicalW = 1900, logicalH = 1000;

private:
    void timerCallback() override
    {
        for (auto* f : faces) for (auto* l : f->leds) l->refresh();
        int cv = proc.cableVersion();
        if (cv != lastCableVer) { lastCableVer = cv; repaint(); }
        if (++tick % 15 == 0) repaint (0, logicalH - 18, logicalW, 18);
    }
    JackW* findJack (int modIdx, int jackIdx)
    {
        auto* m = proc.sys.engine.mods[(size_t) modIdx].get();
        for (auto* j : allJacks) if (&j->mod == m && j->jack == jackIdx) return j;
        return nullptr;
    }
    juce::Point<float> centreOf (JackW* j) { return getLocalPoint (j, j->getLocalBounds().getCentre().toFloat()); }
    JackW* jackAt (juce::Point<float> p)
    {
        for (auto* j : allJacks) if (getLocalArea (j, j->getLocalBounds().toFloat()).expanded (5.0f).contains (p)) return j;
        return nullptr;
    }
    static juce::Colour cableColour (int i)
    {
        static const juce::Colour cols[] = { juce::Colour (0xffd84a3a), juce::Colour (0xffe8c547), juce::Colour (0xff4f8fe0), juce::Colour (0xff57c46a), juce::Colour (0xffe07bd0), juce::Colour (0xffc9c4b5) };
        return cols[i % 6];
    }
    static void drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c)
    {
        float dist = a.getDistanceFrom (b);
        float sag = juce::jmin (160.0f, 20.0f + dist * 0.18f);
        juce::Point<float> m ((a.x + b.x) * 0.5f, juce::jmax (a.y, b.y) + sag);
        juce::Path p; p.startNewSubPath (a); p.quadraticTo (m, b);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.strokePath (p, juce::PathStrokeType (6.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.9f)); g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.2f)); g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (auto pt : { a, b }) { g.setColour (c.darker (0.5f)); g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (pt)); g.setColour (c); g.fillEllipse (juce::Rectangle<float> (9, 9).withCentre (pt)); }
    }

    FenixProcessor& proc;
    CableHost* hostPtr = nullptr;
    juce::OwnedArray<ModuleFace> faces; ModuleFace* rearFace = nullptr;
    std::vector<JackW*> allJacks;
    juce::ComboBox presets; juce::TextButton clearBtn, reloadBtn; juce::String status;
    JackW* dragSrc = nullptr; juce::Point<float> dragPos;
    int lastCableVer = -1, tick = 0;
};

class FenixEditor : public juce::AudioProcessorEditor
{
public:
    explicit FenixEditor (FenixProcessor& p) : AudioProcessorEditor (p), view (p)
    {
        addAndMakeVisible (view);
        const float aspect = (float) view.logicalW / (float) view.logicalH;
        setResizable (true, true);
        setResizeLimits (600, (int) (600.0f / aspect), 2600, (int) (2600.0f / aspect));
        getConstrainer()->setFixedAspectRatio ((double) aspect);
        setSize ((int) ((float) view.logicalW * 0.7f), (int) ((float) view.logicalH * 0.7f));
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }
    void resized() override
    {
        const float s = juce::jmin ((float) getWidth() / (float) view.logicalW, (float) getHeight() / (float) view.logicalH);
        view.setTransform (juce::AffineTransform::scale (s));
        view.setTopLeftPosition (0, 0);
    }
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
private:
    PanelView view; std::unique_ptr<juce::TooltipWindow> tooltips;
};

} // namespace fenixui

using FenixEditor = fenixui::FenixEditor;
