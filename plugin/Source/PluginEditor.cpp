#include "PluginEditor.h"

using namespace cycles;

const juce::Colour CyclesLookAndFeel::bg        { 0xff2e363f };
const juce::Colour CyclesLookAndFeel::panel     { 0xff38424c };
const juce::Colour CyclesLookAndFeel::raised    { 0xff434f5b };
const juce::Colour CyclesLookAndFeel::accent    { 0xfff07b2c };
const juce::Colour CyclesLookAndFeel::text      { 0xffe6ecf1 };
const juce::Colour CyclesLookAndFeel::dim       { 0xff8c9aa6 };
const juce::Colour CyclesLookAndFeel::ok        { 0xff4cd07d };
const juce::Colour CyclesLookAndFeel::warn      { 0xffe8b33c };
const juce::Colour CyclesLookAndFeel::bad       { 0xffe5534b };
const juce::Colour CyclesLookAndFeel::knobTrack { 0xff161b21 };
const juce::Colour CyclesLookAndFeel::knobBody  { 0xff12161a };
const juce::Colour CyclesLookAndFeel::outline   { 0xff161b21 };
const juce::Colour CyclesLookAndFeel::comboEdge { 0xff161b21 };

using LF = CyclesLookAndFeel;

static float paramValue (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    if (auto* v = s.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

static juce::String machineOf (juce::AudioProcessorValueTreeState& s, int track)
{
    const int m = (int) paramValue (s, "t" + juce::String (track + 1) + "_machine");
    return machineNames()[juce::jlimit (0, machineNames().size() - 1, m)];
}

static juce::String audioStatusText (CyclesProcessor& p, bool full)
{
    auto& a = p.audio;
    switch (a.getStatus())
    {
        case AudioLink::Status::Off:       return full ? "Off. Turn on AUDIO to stream the device into the plugin." : "AUDIO OFF";
        case AudioLink::Status::Searching: return full ? "Looking for the Model:Cycles' USB audio (USB MODE: Audio + MIDI)."
                                                       : "AUDIO: SEARCHING";
        case AudioLink::Status::Failed:    return full ? "Can't stream: " + a.getError() : "AUDIO: ERROR";
        case AudioLink::Status::Streaming: break;
    }
    const int ch = a.getNumChannels();
    const auto rate = juce::String (a.getStream().getDeviceRate() / 1000.0, 1) + " kHz";
    const auto delay = juce::String (p.getLatencySamples() * 1000.0 / juce::jmax (1.0, p.getSampleRate()), 1) + " ms";
    if (! full)
        return "AUDIO " + juce::String (ch) + "CH  " + rate + "  " + delay;
    return juce::String (ch) + " channels from " + a.getDeviceName() + ", " + rate + ".\n"
         + (ch >= numTracks ? "Track 1-6 outputs + Main (no delay/reverb). " : "Stereo mix on Main. ")
         + "Delay " + delay + ", compensated by the DAW.";
}

static bool isMuted (juce::AudioProcessorValueTreeState& s, int track)
{
    return paramValue (s, "t" + juce::String (track + 1) + "_mute") > 0.5f;
}

//==============================================================================
CyclesLookAndFeel::CyclesLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, knobTrack);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::outlineColourId, comboEdge);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
}

// Bakelite knob with a printed scale
void CyclesLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    g.setColour (dim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        g.drawLine ({ c.getPointOnCircumference (radius * 0.92f, a),
                      c.getPointOnCircumference (radius * (i % 5 == 0 ? 1.0f : 0.97f), a) }, i % 5 == 0 ? 1.6f : 1.0f);
    }

    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius * 0.84f, radius * 0.84f, 0.0f, startAngle, angle, true);
    g.setColour (accent);
    g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float kr = radius * 0.70f, cap = kr * 0.78f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - kr + 1.5f, c.y - kr + 3.0f, kr * 2.0f, kr * 2.0f);
    g.setGradientFill (juce::ColourGradient (knobBody.interpolatedWith (juce::Colours::white, 0.16f), c.x, c.y - kr,
                                             knobBody, c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    g.setGradientFill (juce::ColourGradient (knobBody.interpolatedWith (juce::Colours::white, 0.10f), c.x, c.y - cap,
                                             knobBody.interpolatedWith (juce::Colours::black, 0.6f), c.x, c.y + cap, false));
    g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);

    g.setColour (text);
    g.drawLine ({ c.getPointOnCircumference (kr * 0.25f, angle), c.getPointOnCircumference (kr * 0.92f, angle) }, 2.2f);
}

juce::PopupMenu::Options CyclesLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    // One scrolling column, like a hardware menu, instead of a grid of the 30 machines
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withMaximumNumColumns (1);
}

void CyclesLookAndFeel::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float peak, bool vertical)
{
    g.setColour (knobTrack);
    g.fillRoundedRectangle (r, 1.5f);
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float level = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (level <= 0.0f) return;
    g.setColour (db > -3.0f ? bad : db > -12.0f ? warn : ok);
    g.fillRoundedRectangle (vertical ? r.removeFromBottom (r.getHeight() * level) : r.removeFromLeft (r.getWidth() * level), 1.5f);
}

void CyclesLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                                              bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = colour;
    if (highlighted) c = c.brighter (0.1f);
    if (down)        c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.getToggleState() ? accent : outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

// "CC 74", or "NRPN 1:50" for the sidechain
static juce::String midiLabel (const Def& d)
{
    return d.nrpn >= 0 ? "NRPN 1:" + juce::String (d.nrpn) : "CC " + juce::String (d.cc);
}

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& state, const ParamInfo& p, bool isCompact)
    : apvts (state), info (p), compact (isCompact)
{
    setName (info.def->key);
    if (info.def->kind == Kind::Dial)
    {
        slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
        addAndMakeVisible (*slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, info.id, *slider);
        slider->setDoubleClickReturnValue (true, info.defaultValue);
        slider->textFromValueFunction = [this] (double v) { return dialText (*info.def, juce::roundToInt (v), machine()); };
        slider->updateText();
    }
    else
    {
        combo = std::make_unique<juce::ComboBox>();
        combo->addItemList (info.def->choices, 1);
        addAndMakeVisible (*combo);
        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, info.id, *combo);
    }
    setTooltip (info.name + "  (" + midiLabel (*info.def) + ", ch "
                + (info.slot == fxSlot ? juce::String ("FX") : "T" + juce::String (info.slot + 1)) + ")");
}

int ParamControl::machine() const
{
    return info.slot == fxSlot ? 0 : (int) paramValue (apvts, "t" + juce::String (info.slot + 1) + "_machine");
}

void ParamControl::machineChanged()
{
    if (slider != nullptr) slider->updateText();
    const bool used = nameFor (*info.def, machine()).isNotEmpty();
    setAlpha (used ? 1.0f : 0.35f);
    repaint();
}

void ParamControl::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (2);
    const auto name = nameFor (*info.def, machine());
    g.setColour (LF::text);
    g.setFont (juce::FontOptions (compact ? 11.5f : 12.5f, juce::Font::bold));
    g.drawFittedText (name.isEmpty() ? juce::String ("-") : name, r.removeFromTop (compact ? 13 : 15),
                      juce::Justification::centred, 1);
    if (! compact)
    {
        g.setColour (LF::dim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText (midiLabel (*info.def), r.removeFromTop (12), juce::Justification::centred, 1);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds().reduced (4, 2).withTrimmedTop (compact ? 14 : 27);
    if (slider != nullptr) slider->setBounds (r);
    if (combo != nullptr)
        combo->setBounds (compact ? r.withHeight (24) : r.withSizeKeepingCentre (r.getWidth(), 24).translated (0, -8));
}

//==============================================================================
Section::Section (juce::String t, int cols) : title (std::move (t)), columns (cols) {}

void Section::add (std::unique_ptr<ParamControl> c)
{
    addAndMakeVisible (*c);
    controls.push_back (std::move (c));
}

int Section::heightFor() const
{
    const int rows = ((int) controls.size() + columns - 1) / columns;
    return titleH + rows * cellH + 4;
}

void Section::paint (juce::Graphics& g)
{
    g.setColour (LF::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (LF::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title, 10, 4, getWidth() - 20, titleH - 6, juce::Justification::centredLeft);
}

void Section::resized()
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const int col = (int) i % columns, row = (int) i / columns;
        controls[i]->setBounds (4 + col * cellW, titleH + row * cellH, cellW, cellH);
    }
}

//==============================================================================
TrackView::TrackView (CyclesProcessor& p, int s) : proc (p), slot (s)
{
    for (auto& info : allParams())
    {
        if (info.slot != slot) continue;
        if (info.def->key == "mute")
        {
            muteButton.setClickingTogglesState (true);
            muteButton.setColour (juce::TextButton::buttonOnColourId, LF::bad);
            muteButton.setTooltip ("Mute the track (CC 94)");
            muteAttachment = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter (info.id), muteButton);
            addAndMakeVisible (muteButton);
            continue;
        }
        auto& page = pages[info.def->section];
        if (page == nullptr)
        {
            // MIX, FILTER, TRACK FX and LFO share the right column at the same width
            const bool wide = info.def->section == "FILTER" || info.def->section == "MIX" || info.def->section == "LFO"
                           || info.def->section == "TRACK FX" || info.def->section == "SIDECHAIN";
            page = std::make_unique<Section> (info.def->section, info.def->section == "DUCKED TRACKS" ? 6 : wide ? 5 : 4);
            addAndMakeVisible (*page);
        }
        page->add (std::make_unique<ParamControl> (proc.apvts, info));
    }
    if (slot == fxSlot)
    {
        for (int b = 0; b < 3; ++b)
        {
            auto& k = killButtons[(size_t) b];
            k.setButtonText ("KILL");
            k.setColour (juce::TextButton::buttonOnColourId, LF::bad);
            k.setTooltip ("Kill the band (value 0); click again to bring it back");
            k.onClick = [this, b] { toggleKill (b); };
            addAndMakeVisible (k);
        }
        timerCallback();
    }
    if (slot != fxSlot)
    {
        sampleView = std::make_unique<SampleView> (proc, slot);
        addChildComponent (*sampleView);
        sampleButton.setClickingTogglesState (true);
        sampleButton.setToggleState (true, juce::dontSendNotification);
        sampleButton.setTooltip ("Show the track's sample (waveform and the +Drive's samples) in place of MIX, FILTER, TRACK FX and LFO");
        sampleButton.onClick = [this] { sampleShown = sampleButton.getToggleState(); updateSampleView(); };
        addChildComponent (sampleButton);
        timerCallback(); // the machine shown now, and the sample window if it is a sample machine
    }
    startTimerHz (8);
}

void TrackView::updateSampleView()
{
    if (sampleView == nullptr) return;
    const bool isSample = CyclesProcessor::isSampleMachine (shownMachine);
    sampleButton.setVisible (isSample);
    const bool show = isSample && sampleShown;
    sampleView->setVisible (show);
    for (auto* name : { "MIX", "FILTER", "TRACK FX", "LFO" })
        if (auto it = pages.find (name); it != pages.end()) it->second->setVisible (! show);
}

static const char* const eqKeys[3] = { "fx_eqlow", "fx_eqmid", "fx_eqhigh" };

void TrackView::toggleKill (int band)
{
    auto* p = proc.apvts.getParameter (eqKeys[band]);
    const int v = (int) paramValue (proc.apvts, eqKeys[band]);
    int to = 0;
    if (v > 0)
        killRestore[(size_t) band] = v;
    else
        to = killRestore[(size_t) band] > 0 ? killRestore[(size_t) band] : 64;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) to));
    p->endChangeGesture();
    timerCallback();
}

void TrackView::timerCallback()
{
    repaint (0, 0, getWidth(), headerH);
    if (slot == fxSlot)
    {
        for (int b = 0; b < 3; ++b)   // lit while the band is killed (here, on the device or by automation)
            killButtons[(size_t) b].setToggleState (paramValue (proc.apvts, eqKeys[b]) < 0.5f, juce::dontSendNotification);
        return;
    }
    const int m = (int) paramValue (proc.apvts, "t" + juce::String (slot + 1) + "_machine");
    if (m != shownMachine)
    {
        shownMachine = m;
        for (auto& [name, page] : pages) page->machineChanged();
        updateSampleView();
    }
}

void TrackView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromTop (headerH).withTrimmedLeft (4);
    g.setColour (LF::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    const auto title = slot == fxSlot ? juce::String ("FX") : "TRACK " + juce::String (slot + 1);
    g.drawText (title, r.removeFromLeft (slot == fxSlot ? 50 : 110), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    if (slot == fxSlot)
    {
        g.setColour (LF::dim);
        g.drawText ("Delay, reverb, master EQ and sidechain, on the FX channel " + juce::String (proc.getChannel (fxSlot)),
                    r, juce::Justification::centredLeft);
        return;
    }
    g.setColour (LF::accent);
    g.drawText (machineOf (proc.apvts, slot).toUpperCase(), r.removeFromLeft (90), juce::Justification::centredLeft);
    if (isMuted (proc.apvts, slot))
    {
        g.setColour (LF::bad);
        g.drawText ("MUTED", r.removeFromLeft (70), juce::Justification::centredLeft);
    }
    g.setColour (LF::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("MIDI channel " + juce::String (proc.getChannel (slot)), r, juce::Justification::centredRight);
}

void TrackView::resized()
{
    auto r = getLocalBounds().withTrimmedTop (headerH);
    muteButton.setBounds (getWidth() - 190, 5, 70, headerH - 10);
    sampleButton.setBounds (getWidth() - 270, 5, 74, headerH - 10);

    if (slot == fxSlot)
    {
        // FX and the master EQ (with a KILL under each band), then the modded firmware's sidechain (source,
        // envelope, and which tracks it ducks)
        auto& fx = *pages["FX"];
        auto& eq = *pages["MASTER EQ"];
        fx.setBounds (r.getX(), r.getY(), fx.widthFor(), fx.heightFor());
        eq.setBounds (fx.getRight() + 10, r.getY(), eq.widthFor(), eq.heightFor());
        for (int b = 0; b < 3; ++b)
            killButtons[(size_t) b].setBounds (eq.getX() + 4 + b * Section::cellW + 12, eq.getBottom() + 4, Section::cellW - 24, 24);
        int y = juce::jmax (fx.getBottom(), eq.getBottom() + 28) + 8;
        for (auto* name : { "SIDECHAIN", "DUCKED TRACKS" })
        {
            auto& page = pages[name];
            page->setBounds (r.getX(), y, page->widthFor(), page->heightFor());
            y += page->heightFor() + 8;
        }
        return;
    }

    // Two columns of device pages: MACHINE + EXTRAS + TRIG | MIX + FILTER + TRACK FX + LFO
    auto left = r.removeFromLeft (pages["MACHINE"]->widthFor());
    r.removeFromLeft (10);
    auto right = r.withWidth (pages["FILTER"]->widthFor());

    auto place = [] (juce::Rectangle<int>& col, Section& s)
    {
        s.setBounds (col.removeFromTop (s.heightFor()).withWidth (s.widthFor()));
        col.removeFromTop (8);
    };
    place (left,  *pages["MACHINE"]);
    place (left,  *pages["EXTRAS"]);
    place (left,  *pages["TRIG"]);
    place (right, *pages["MIX"]);
    place (right, *pages["FILTER"]);
    place (right, *pages["TRACK FX"]);
    place (right, *pages["LFO"]);
    if (sampleView != nullptr)
        sampleView->setBounds (right.getX(), r.getY(), right.getWidth(), getHeight() - r.getY() - 4);
}

//==============================================================================
MixerView::MixerView (CyclesProcessor& p) : proc (p)
{
    static const juce::StringArray stripKeys { "machine", "mute", "level", "voldist", "pan", "delay", "reverb", "fxon", "fxtype" };

    for (int slot = 0; slot <= numTracks; ++slot)
    {
        Strip strip;
        strip.title = slot == fxSlot ? juce::String ("FX") : "T" + juce::String (slot + 1);
        for (auto& info : allParams())
        {
            if (info.slot != slot) continue;
            // Track strips show machine, mute and the mix knobs, in that order
            if (slot != fxSlot && ! stripKeys.contains (info.def->key)) continue;
            if (slot == fxSlot && info.def->section != "FX") continue; // the FX strip: delay and reverb only
            strip.controls.push_back (std::make_unique<ParamControl> (proc.apvts, info, true));
        }
        if (slot != fxSlot)
            std::stable_sort (strip.controls.begin(), strip.controls.end(), [] (const auto& a, const auto& b)
                              { return stripKeys.indexOf (a->getName()) < stripKeys.indexOf (b->getName()); });
        for (auto& c : strip.controls) addAndMakeVisible (*c);
        strips.push_back (std::move (strip));
    }
    startTimerHz (20); // meters
}

void MixerView::paint (juce::Graphics& g)
{
    const int w = getWidth() / (int) strips.size();
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        auto r = juce::Rectangle<int> (i * w, 0, w, getHeight()).reduced (3, 0);
        g.setColour (i == fxSlot ? LF::raised : LF::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (i == fxSlot ? LF::accent : LF::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (strips[(size_t) i].title, r.removeFromTop (26), juce::Justification::centred);

        // Audio meters: each track's stem, and the main output on the FX strip
        auto m = r.removeFromRight (10).withTrimmedBottom (6).toFloat();
        if (i < numTracks)
            LF::drawMeter (g, m.withWidth (4.0f), proc.meters[(size_t) (2 + i)].load(), true);
        else
        {
            LF::drawMeter (g, m.withWidth (3.0f).translated (-2.0f, 0.0f), proc.meters[0].load(), true);
            LF::drawMeter (g, m.withWidth (3.0f).translated (2.0f, 0.0f), proc.meters[1].load(), true);
        }
    }
}

void MixerView::resized()
{
    const int w = getWidth() / (int) strips.size();
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        auto r = juce::Rectangle<int> (i * w, 26, w, getHeight() - 26).reduced (5, 2).withTrimmedRight (8);
        for (auto& c : strips[(size_t) i].controls)
        {
            const bool isCombo = c->getNumChildComponents() > 0 && dynamic_cast<juce::ComboBox*> (c->getChildComponent (0)) != nullptr;
            c->setBounds (r.removeFromTop (isCombo ? 46 : 82));
        }
    }
}

//==============================================================================
SetupView::SetupView (CyclesProcessor& p) : proc (p)
{
    modeBox.addItem ("Auto-detect Model:Cycles (USB)", 1);
    modeBox.addItem ("Choose MIDI ports", 2);
    modeBox.addItem ("Host MIDI routing", 3);
    modeBox.setSelectedId ((int) proc.link.getMode() + 1, juce::dontSendNotification);
    modeBox.onChange = [this]
    {
        proc.setLinkMode ((DeviceLink::Mode) (modeBox.getSelectedId() - 1));
        updateEnablement();
    };

    auto portChanged = [this]
    {
        const int i = inBox.getSelectedItemIndex(), o = outBox.getSelectedItemIndex();
        proc.setManualPorts (i > 0 ? inputs[i - 1].identifier : juce::String(),
                             o > 0 ? outputs[o - 1].identifier : juce::String());
    };
    inBox.onChange = portChanged;
    outBox.onChange = portChanged;
    rescan.onClick = [this] { refreshPorts(); proc.link.rescan(); };

    offset.setRange (0, 250, 1);
    offset.setValue (proc.getClockOffsetMs(), juce::dontSendNotification);
    offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    offset.onValueChange = [this] { proc.setClockOffsetMs ((int) offset.getValue()); };
    offset.setTooltip ("Delays clock to the device to line it up with the DAW's audio. Raise it if the Model:Cycles is early.");

    trim.setRange (-20, 50, 1);
    trim.setValue (proc.getAudioTrimMs(), juce::dontSendNotification);
    trim.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    trim.onValueChange = [this] { proc.setAudioTrimMs ((int) trim.getValue()); };
    trim.setTooltip ("Added to the audio delay the DAW compensates. Raise it if the streamed audio is late on the grid, lower it if it's early.");

    // The device's USB audio mode, switched from here (Modded Cycles Techno Edition firmware); lit from what the stream reports
    for (auto* b : { &usb2, &usb6 })
    {
        b->setClickingTogglesState (false);
        b->setTooltip ("Switch the Model:Cycles' USB audio (SETTINGS > Mods > USB Audio): 2CH = stereo mix, "
                       "6CH = one channel per track. The device reconnects about a second later.");
        b->onClick = [this, b] { proc.requestUsbAudioMode (b == &usb6); };
    }

    for (auto* c : std::initializer_list<juce::Component*> { &modeLabel, &inLabel, &outLabel, &offsetLabel, &trimLabel, &usbLabel,
                                                             &modeBox, &inBox, &outBox, &rescan, &offset, &trim, &usb2, &usb6 })
        addAndMakeVisible (c);

    for (int slot = 0; slot <= numTracks; ++slot)
    {
        auto* l = chanLabels.add (new juce::Label ({}, slot == fxSlot ? juce::String ("FX channel")
                                                                      : "Track " + juce::String (slot + 1) + " channel"));
        addAndMakeVisible (l);
        auto* b = chanBoxes.add (new juce::ComboBox());
        for (int ch = 1; ch <= 16; ++ch) b->addItem (juce::String (ch), ch);
        b->setSelectedId (proc.getChannel (slot), juce::dontSendNotification);
        b->onChange = [this, slot, b] { proc.setChannel (slot, b->getSelectedId()); };
        addAndMakeVisible (b);
    }

    refreshPorts();
    updateEnablement();
    startTimer (250);
}

void SetupView::timerCallback()
{
    repaint (0, 470, 410, 160); // audio status
    const int ch = proc.audio.getNumChannels();
    usb2.setToggleState (ch == 2, juce::dontSendNotification);
    usb6.setToggleState (ch >= numTracks, juce::dontSendNotification);
    // Keep the port lists current while the page is open (hot-plug)
    static int ticks = 0;
    if (++ticks % 8 == 0 && ! inBox.isPopupActive() && ! outBox.isPopupActive()) refreshPorts();
}

void SetupView::refreshPorts()
{
    inputs  = juce::MidiInput::getAvailableDevices();
    outputs = juce::MidiOutput::getAvailableDevices();

    auto fill = [] (juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& devs, const juce::String& current)
    {
        box.clear (juce::dontSendNotification);
        box.addItem ("None", 1);
        int selected = 1;
        for (int i = 0; i < devs.size(); ++i)
        {
            box.addItem (devs[i].name, i + 2);
            if (devs[i].identifier == current) selected = i + 2;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    };
    fill (inBox, inputs, proc.link.getManualInput());
    fill (outBox, outputs, proc.link.getManualOutput());
}

void SetupView::updateEnablement()
{
    const bool manual = proc.link.getMode() == DeviceLink::Mode::Manual;
    inBox.setEnabled (manual);
    outBox.setEnabled (manual);
}

void SetupView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (LF::panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);

    g.setColour (LF::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("CONNECTION", 16, 10, 200, 16, juce::Justification::centredLeft);
    g.drawText ("MIDI CHANNELS", 16, 214, 200, 16, juce::Justification::centredLeft);
    g.drawText ("AUDIO STREAMING (USB)", 16, 474, 300, 16, juce::Justification::centredLeft);

    g.setColour (proc.audio.getStatus() == AudioLink::Status::Streaming ? LF::text : LF::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (audioStatusText (proc, true), juce::Rectangle<int> (16, 494, 390, 40), juce::Justification::topLeft, 2);
    if (juce::Time::getMillisecondCounter() - proc.usbModeRequestTime.load() < 5000 && proc.usbModeRequestTime.load() != 0)
    {
        g.setColour (LF::warn);
        g.drawText ("Switching... the device reconnects", 16, 604, 390, 18, juce::Justification::centredLeft);
    }
    g.drawText ("ON THE MODEL:CYCLES", 420, 10, 300, 16, juce::Justification::centredLeft);

    g.setColour (LF::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("SETTINGS > MIDI CONFIG\n"
                      "  SYNC: turn on Receive CC/NRPN, Send CC/NRPN,\n"
                      "  Clock Receive and Transport Receive.\n"
                      "  PORT CONFIG: Input/Output from USB (or MIDI).\n"
                      "  CHANNELS: match the channels on the left.\n"
                      "  ENCODER DEST: I+E, and MIDI OUT on for each track,\n"
                      "  or turning a knob on the device sends nothing.\n\n"
                      "How the link works\n"
                      "  Auto-detect finds the Model:Cycles USB port, reconnects\n"
                      "  when you replug it, and checks it answers as an Elektron\n"
                      "  device. It works in any DAW, including Ableton Live.\n"
                      "  On Windows a port can only be open in one program:\n"
                      "  if it shows PORT BUSY, untick the Model:Cycles in your\n"
                      "  DAW's MIDI preferences.\n\n"
                      "  RECALL pushes the sound saved in your project to the\n"
                      "  device when the project opens or the device reconnects.\n"
                      "  GET reads the sound loaded on the device into the plugin\n"
                      "  (Modded Cycles Techno Edition firmware; the FX channel must be on). With\n"
                      "  no saved sound, the plugin does a GET when it connects.\n"
                      "  Moving knobs on the device while your DAW is recording\n"
                      "  automation writes them into the automation lanes.\n\n"
                      "Audio (AUDIO button)\n"
                      "  The plugin plays the Model:Cycles' USB audio on its\n"
                      "  outputs, delay-compensated. In 6CH (SETTINGS > Mods >\n"
                      "  USB Audio) each track has its own output, panned, and\n"
                      "  Main is their sum, without the delay and reverb (the\n"
                      "  stems are taken before them). In 2CH Main is the full\n"
                      "  mix. Route Track 1-6 in your DAW's multi-out routing.\n"
                      "  Don't also monitor the Model:Cycles' audio input.",
                      juce::Rectangle<int> (420, 32, getWidth() - 436, getHeight() - 40),
                      juce::Justification::topLeft, 40);
}

void SetupView::resized()
{
    auto row = [] (int y) { return juce::Rectangle<int> (16, y, 380, 26); };

    auto r = row (34);
    modeLabel.setBounds (r.removeFromLeft (110)); modeBox.setBounds (r);
    r = row (66);
    inLabel.setBounds (r.removeFromLeft (110)); inBox.setBounds (r.withTrimmedRight (72)); rescan.setBounds (r.removeFromRight (64));
    r = row (98);
    outLabel.setBounds (r.removeFromLeft (110)); outBox.setBounds (r.withTrimmedRight (72));
    r = row (148);
    offsetLabel.setBounds (r.removeFromLeft (130)); offset.setBounds (r);
    r = row (540);
    trimLabel.setBounds (r.removeFromLeft (130)); trim.setBounds (r);
    r = row (574);
    usbLabel.setBounds (r.removeFromLeft (130)); usb2.setBounds (r.removeFromLeft (64)); r.removeFromLeft (6); usb6.setBounds (r.removeFromLeft (64));

    for (int i = 0; i < chanLabels.size(); ++i)
    {
        r = row (236 + i * 32);
        chanLabels[i]->setBounds (r.removeFromLeft (130));
        chanBoxes[i]->setBounds (r.removeFromLeft (80));
    }
}

//==============================================================================
TrackRail::TrackRail (CyclesProcessor& p) : proc (p) {}

void TrackRail::setSelected (int i)
{
    selected = i;
    repaint();
}

juce::Rectangle<int> TrackRail::itemBounds (int i) const
{
    if (i < numTracks) return { 0, i * 56, getWidth(), 50 };
    const int top = numTracks * 56 + 14;
    return { 0, top + (i - numTracks) * 44, getWidth(), 38 };
}

void TrackRail::paint (juce::Graphics& g)
{
    const auto now = juce::Time::getMillisecondCounter();
    static const char* extras[] = { "FX", "MIXER", "SETUP" };

    for (int i = 0; i < numItems; ++i)
    {
        auto r = itemBounds (i);
        const bool sel = i == selected;
        g.setColour (sel ? LF::raised : LF::panel);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        if (sel)
        {
            g.setColour (LF::accent);
            g.fillRoundedRectangle (r.removeFromLeft (4).toFloat(), 2.0f);
        }
        else r.removeFromLeft (4);

        r.reduce (8, 0);

        // Activity LEDs (in from device / out to device) for tracks and FX
        if (i <= fxSlot)
        {
            auto leds = r.removeFromRight (12);
            const bool rx = now - proc.rxTime[(size_t) i].load() < 150;
            const bool tx = now - proc.txTime[(size_t) i].load() < 150;
            g.setColour (rx ? LF::ok : LF::knobTrack);
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ (float) leds.getCentreX(), (float) r.getCentreY() - 6 }));
            g.setColour (tx ? LF::accent : LF::knobTrack);
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ (float) leds.getCentreX(), (float) r.getCentreY() + 6 }));
        }

        if (i < numTracks)
        {
            const bool muted = isMuted (proc.apvts, i);
            g.setColour (sel ? LF::text : LF::text.withAlpha (0.85f));
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText ("T" + juce::String (i + 1), r.removeFromTop (28).withTrimmedTop (6), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.setColour (muted ? LF::bad : (sel ? LF::accent : LF::dim));
            g.drawText (machineOf (proc.apvts, i).toUpperCase() + (muted ? "  MUTED" : ""), r.withTrimmedBottom (6),
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (sel ? LF::accent : LF::text.withAlpha (0.85f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (extras[i - numTracks], r, juce::Justification::centredLeft);
        }

        if (i < numTracks && proc.audio.getNumChannels() >= numTracks)
        {
            const auto b = itemBounds (i);
            LF::drawMeter (g, juce::Rectangle<float> (12.0f, (float) b.getBottom() - 6.0f, (float) b.getWidth() - 34.0f, 3.0f),
                           proc.meters[(size_t) (2 + i)].load(), false);
        }
    }
}

void TrackRail::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < numItems; ++i)
        if (itemBounds (i).contains (e.getPosition()))
        {
            if (onSelect) onSelect (i);
            return;
        }
}

//==============================================================================
ProjectBox::ProjectBox (CyclesProcessor& p) : proc (p)
{
    setTooltip ("The Model:Cycles' projects. Pick one to load it on the device; if the current project has unsaved "
                "changes, the plugin asks whether to save it first. Needs the Modded Cycles Techno Edition firmware with the project list.");
    setTextWhenNothingSelected ("-");
    setTextWhenNoChoicesAvailable ("No project list");
    onChange = [this]
    {
        const int id = getSelectedId();
        const int cur = proc.currentProject.load();
        if (id <= 0 || id - 1 == cur)
            return;
        if (proc.projectDirty.load() != 1)
        {
            proc.loadProject (id - 1, proc.projectDirty.load() == 0 ? CyclesProcessor::LoadHow::dontSave
                                                                     : CyclesProcessor::LoadHow::askOnDevice);
            return;
        }
        // Unsaved changes: asked here rather than on the device. A project in no slot can only be saved on the
        // device (it asks for a slot), so Save there loads as the device's LOAD does.
        const juce::String name = getItemText (indexOfItemId (id));
        auto opts = juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::QuestionIcon)
                        .withTitle ("Load " + name.fromFirstOccurrenceOf ("  ", false, false))
                        .withMessage (cur >= 0 ? "Save the current project before loading?"
                                               : "The current project is not saved. Save it on the Model:Cycles first?")
                        .withButton ("Save")
                        .withButton ("Don't save")
                        .withButton ("Cancel")
                        .withAssociatedComponent (this);
        juce::Component::SafePointer<ProjectBox> safe (this);
        const int slot = id - 1;
        juce::AlertWindow::showAsync (opts, [safe, slot, cur] (int r)
        {
            if (safe == nullptr) return;
            auto& p = safe->proc;
            if (r == 1)      p.loadProject (slot, cur >= 0 ? CyclesProcessor::LoadHow::saveFirst
                                                           : CyclesProcessor::LoadHow::askOnDevice);
            else if (r == 2) p.loadProject (slot, CyclesProcessor::LoadHow::dontSave);
            safe->shownSerial = -1; // Cancel (0): back to the current project
            safe->refresh();
        });
    };
    refresh();
}

void ProjectBox::refresh()
{
    const int serial = proc.projectSerial.load();
    if (serial == shownSerial) return;
    shownSerial = serial;

    clear (juce::dontSendNotification);
    const int cur = proc.currentProject.load();
    for (const auto& [slot, name] : proc.getProjects())
        addItem (juce::String (slot + 1).paddedLeft ('0', 2) + "  " + (name.isEmpty() ? juce::String ("(no name)") : name), slot + 1);

    if (proc.projectLoading.load())
        setText ("LOADING...", juce::dontSendNotification);
    else if (cur >= 0 && indexOfItemId (cur + 1) >= 0)
        setSelectedId (cur + 1, juce::dontSendNotification);
    else
        setText (cur == -1 ? "(not saved)" : (getNumItems() > 0 ? "-" : ""), juce::dontSendNotification);
}

void ProjectBox::showPopup()
{
    proc.requestProjectList(); // names may have changed on the device: the next opening shows them
    juce::ComboBox::showPopup();
}

//==============================================================================
CyclesEditor::CyclesEditor (CyclesProcessor& p) : AudioProcessorEditor (p), proc (p), rail (p)
{
    setLookAndFeel (&lnf);

    struct Opt { juce::TextButton* b; CyclesProcessor::Option o; const char* tip; };
    for (auto& o : { Opt { &receiveButton, CyclesProcessor::optReceive, "Knob moves on the Model:Cycles update the plugin (and record as automation)." },
                     Opt { &recallButton,  CyclesProcessor::optRecall,  "Total recall: send the project's sound to the device when the project opens or the device reconnects." },
                     Opt { &clockButton,   CyclesProcessor::optClock,   "Send MIDI clock, start/stop and song position from the DAW so the device follows its transport." },
                     Opt { &followButton,  CyclesProcessor::optFollow,  "Jump to the track you're tweaking on the device." },
                     Opt { &audioButton,   CyclesProcessor::optAudio,   "Stream the Model:Cycles' USB audio into the plugin's outputs (Main + one per track in 6CH), delay-compensated." } })
    {
        o.b->setClickingTogglesState (true);
        o.b->setToggleState (proc.getOption (o.o), juce::dontSendNotification);
        o.b->setTooltip (o.tip);
        o.b->onClick = [this, b = o.b, opt = o.o] { proc.setOption (opt, b->getToggleState()); };
        addAndMakeVisible (*o.b);
    }

    sendAllButton.setTooltip ("Push every value in the plugin to the Model:Cycles now.");
    sendAllButton.onClick = [this]
    {
        proc.sendAll();
        flash = "Sent all " + juce::String ((int) allParams().size()) + " values";
        flashTicks = 45;
    };
    addAndMakeVisible (sendAllButton);

    getButton.setTooltip ("Read the sound loaded on the Model:Cycles into the plugin (Modded Cycles Techno Edition firmware with GET).");
    getButton.onClick = [this]
    {
        proc.requestGet();
        flash = "Asking the Cycles for its sound...";
        flashTicks = 60;
    };
    addAndMakeVisible (getButton);
    seenGetSerial = proc.getSerial.load();

    rail.onSelect = [this] (int i) { showView (i); };
    addAndMakeVisible (rail);
    addAndMakeVisible (projectBox);

    seenSerial = proc.incomingSerial.load();
    setSize (1060, 740);
    showView (0);
    startTimerHz (20);
}

CyclesEditor::~CyclesEditor() { setLookAndFeel (nullptr); }

void CyclesEditor::showView (int index)
{
    if (index == viewIndex) return;
    viewIndex = index;
    rail.setSelected (index);

    if (index <= fxSlot)        view = std::make_unique<TrackView> (proc, index);
    else if (index == fxSlot + 1) view = std::make_unique<MixerView> (proc);
    else                        view = std::make_unique<SetupView> (proc);

    addAndMakeVisible (*view);
    resized();
}

juce::String CyclesEditor::describe (juce::uint32 msg)
{
    if (msg == 0xffffffff) return "-";
    return "ch " + juce::String (MidiQueue::channelOf (msg)) + "  CC " + juce::String (MidiQueue::ccOf (msg))
           + " = " + juce::String (MidiQueue::valueOf (msg));
}

void CyclesEditor::timerCallback()
{
    for (auto& [b, o] : { std::pair { &receiveButton, CyclesProcessor::optReceive }, std::pair { &recallButton, CyclesProcessor::optRecall },
                          std::pair { &clockButton, CyclesProcessor::optClock },     std::pair { &followButton, CyclesProcessor::optFollow },
                          std::pair { &audioButton, CyclesProcessor::optAudio } })
        if (b->getToggleState() != proc.getOption (o))
            b->setToggleState (proc.getOption (o), juce::dontSendNotification);

    // Follow: show the track whose knob was just turned on the device (only from track/FX pages)
    const int serial = proc.incomingSerial.load();
    if (serial != seenSerial)
    {
        seenSerial = serial;
        const int slot = proc.lastIncomingSlot.load();
        if (proc.getOption (CyclesProcessor::optFollow) && viewIndex <= fxSlot && slot >= 0 && slot != viewIndex)
            showView (slot);
    }

    if (const int g = proc.getSerial.load(); g != seenGetSerial)
    {
        seenGetSerial = g;
        flash = proc.getState.load() == CyclesProcessor::getDone
                    ? "Got " + juce::String (proc.getCount.load()) + " values from the Cycles"
                    : juce::String ("No answer from the Cycles: firmware without GET, or FX channel off");
        flashTicks = 80;
    }

    if (flashTicks > 0 && --flashTicks == 0) flash.clear();

    if (! projectBox.isPopupActive()) projectBox.refresh();

    rail.repaint();
    repaint (0, 0, getWidth(), 56);                 // connection pill
    repaint (0, getHeight() - 26, getWidth(), 26);  // status bar
}

void CyclesEditor::paint (juce::Graphics& g)
{
    g.fillAll (LF::bg);

    // Logo
    g.setColour (LF::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MODEL:CYCLES", 16, 10, 170, 24, juce::Justification::centredLeft);
    g.setColour (LF::accent);
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    g.drawText ("MODDED CYCLES TECHNO EDITION", 16, 33, 170, 12, juce::Justification::centredLeft);

    // Connection pill
    juce::Colour led;
    juce::String state;
    const auto now = juce::Time::getMillisecondCounter();
    switch (proc.link.getStatus())
    {
        case DeviceLink::Status::Connected:
            led = LF::ok;
            state = "CONNECTED  " + proc.link.getDeviceName();
            if (proc.link.isIdentityConfirmed()) state += "  (ID OK)";
            break;
        case DeviceLink::Status::Searching:
            led = (now / 500) % 2 ? LF::warn : LF::warn.withAlpha (0.25f);
            state = "SEARCHING FOR MODEL:CYCLES...";
            break;
        case DeviceLink::Status::PortBusy:
            led = LF::bad;
            state = "PORT BUSY - free it in your DAW";
            break;
        case DeviceLink::Status::Host:
            led = LF::dim;
            state = "HOST MIDI ROUTING";
            break;
    }
    auto pill = juce::Rectangle<int> (176, 12, 236, 32);
    g.setColour (LF::panel);
    g.fillRoundedRectangle (pill.toFloat(), 16.0f);
    g.setColour (led);
    g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre ({ pill.getX() + 18.0f, (float) pill.getCentreY() }));
    g.setColour (LF::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (state, pill.withTrimmedLeft (32).withTrimmedRight (12), juce::Justification::centredLeft, 1);

    // Project picker label
    g.setColour (LF::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("PROJECT", projectArea.withHeight (18), juce::Justification::bottomLeft);

    // Status bar
    auto status = getLocalBounds().removeFromBottom (24).reduced (16, 0);
    g.setFont (juce::FontOptions (12.0f));
    if (proc.clockRunning.load())
    {
        g.setColour (LF::accent);
        g.drawText ("CLOCK RUNNING", status.removeFromRight (110), juce::Justification::centredRight);
    }
    g.setColour (proc.audio.getStatus() == AudioLink::Status::Streaming ? LF::ok : LF::dim);
    g.drawText (audioStatusText (proc, false), status.removeFromRight (300), juce::Justification::centredRight);
    g.setColour (LF::dim);
    juce::String s = "Last from device: " + describe (proc.lastReceived.load())
                   + "      Last to device: " + describe (proc.lastSent.load());
    if (flash.isNotEmpty()) s = flash + "      " + s;
    g.drawText (s, status, juce::Justification::centredLeft);
}

void CyclesEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56).reduced (12, 12);
    sendAllButton.setBounds (top.removeFromRight (96));
    top.removeFromRight (6);
    getButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (14);
    for (auto* b : { &followButton, &clockButton, &recallButton, &receiveButton, &audioButton })
    {
        b->setBounds (top.removeFromRight (b == &clockButton ? 100 : 80));
        top.removeFromRight (6);
    }

    r.removeFromBottom (26);
    r.reduce (8, 0);
    auto railArea = r.removeFromLeft (140);
    projectArea = railArea.removeFromBottom (54);
    projectBox.setBounds (projectArea.withTrimmedTop (22));
    rail.setBounds (railArea);
    r.removeFromLeft (12);
    if (view != nullptr) view->setBounds (r);
}
