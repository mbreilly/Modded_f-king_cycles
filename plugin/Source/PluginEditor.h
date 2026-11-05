#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SampleView.h"
#include <array>
#include <map>

//==============================================================================
class CyclesLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CyclesLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    /** A level meter (0..1 linear peak), green to amber to red. */
    static void drawMeter (juce::Graphics&, juce::Rectangle<float>, float peak, bool vertical);

    // Colours: the "Steel blue" look picked in ui-designer/modelcycles-link-colors.html
    static const juce::Colour bg, panel, raised, accent, text, dim, ok, warn, bad, knobTrack, knobBody, outline, comboEdge;
};

//==============================================================================
/** One Model:Cycles parameter: a dial (0-127) or a dropdown, with its name and CC number. */
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl (juce::AudioProcessorValueTreeState&, const cycles::ParamInfo&, bool compact = false);
    void paint (juce::Graphics&) override;
    void resized() override;

    /** The track's machine changed: relabel (machine knobs and extras take their names from the machine). */
    void machineChanged();

private:
    int machine() const;
    juce::AudioProcessorValueTreeState& apvts;
    const cycles::ParamInfo& info;
    bool compact;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
};

/** A titled grid of ParamControls (one device page). */
class Section : public juce::Component
{
public:
    Section (juce::String title, int columns);
    void add (std::unique_ptr<ParamControl>);
    void machineChanged() { for (auto& c : controls) c->machineChanged(); }
    int  heightFor() const;
    int  widthFor() const { return columns * cellW + 8; }
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int cellW = 94, cellH = 98, titleH = 22;

private:
    juce::String title;
    int columns;
    std::vector<std::unique_ptr<ParamControl>> controls;
};

//==============================================================================
/** Everything for one track (or the FX block), laid out as its pages. */
class TrackView : public juce::Component, private juce::Timer
{
public:
    TrackView (CyclesProcessor&, int slot);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static constexpr int headerH = 34;
    CyclesProcessor& proc;
    int slot;
    std::map<juce::String, std::unique_ptr<Section>> pages;
    int shownMachine = -1;
    // SAMPLER / GRANULAR / WAVETABLE: the sample window in place of the right column (SAMPLE toggles it)
    std::unique_ptr<SampleView> sampleView;
    juce::TextButton sampleButton { "SAMPLE" };
    bool sampleShown = true;
    void updateSampleView();
    juce::TextButton muteButton { "MUTE" };   // the track's Mute, in the header like Overbridge's track strip
    // FX: a KILL under each master EQ band, like a DJ mixer: the band to 0 (kill), again: back where it was
    std::array<juce::TextButton, 3> killButtons;
    std::array<int, 3> killRestore { 64, 64, 64 };
    void toggleKill (int band);
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment;
};

/** Overbridge-style mixer: one strip per track plus the FX return. */
class MixerView : public juce::Component, private juce::Timer
{
public:
    explicit MixerView (CyclesProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    CyclesProcessor& proc;
    struct Strip { juce::String title; std::vector<std::unique_ptr<ParamControl>> controls; };
    std::vector<Strip> strips;
};

/** Connection, channels and timing. */
class SetupView : public juce::Component, private juce::Timer
{
public:
    explicit SetupView (CyclesProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPorts();
    void updateEnablement();

    CyclesProcessor& proc;
    juce::Label modeLabel { {}, "Connection" }, inLabel { {}, "MIDI In" }, outLabel { {}, "MIDI Out" },
                offsetLabel { {}, "Clock offset (ms)" }, trimLabel { {}, "Audio trim (ms)" };
    juce::ComboBox modeBox, inBox, outBox;
    juce::TextButton rescan { "Rescan" }, usb2 { "2CH" }, usb6 { "6CH" };
    juce::Label usbLabel { {}, "Device USB audio" };
    juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 trim { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::OwnedArray<juce::Label> chanLabels;
    juce::OwnedArray<juce::ComboBox> chanBoxes;
    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
};

//==============================================================================
/** Left-hand rail: track selector with machine name, mute state and MIDI activity. */
class TrackRail : public juce::Component
{
public:
    explicit TrackRail (CyclesProcessor&);
    std::function<void (int)> onSelect; // 0-5 tracks, 6 FX, 7 mixer, 8 setup
    void setSelected (int);
    int  getSelected() const { return selected; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int numItems = 9;

private:
    juce::Rectangle<int> itemBounds (int i) const;
    CyclesProcessor& proc;
    int selected = 0;
};

//==============================================================================
/** The project picker under the rail: the device's projects (Modded Cycles firmware with the project list). */
class ProjectBox : public juce::ComboBox
{
public:
    explicit ProjectBox (CyclesProcessor&);
    void refresh();          // items and selection from the processor (message thread)
    void showPopup() override;

private:
    CyclesProcessor& proc;
    int shownSerial = -1;
};

//==============================================================================
class CyclesEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CyclesEditor (CyclesProcessor&);
    ~CyclesEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showView (int index); // see TrackRail::onSelect

private:
    void timerCallback() override;
    static juce::String describe (juce::uint32 msg);

    CyclesProcessor& proc;
    CyclesLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::TextButton receiveButton { "RECEIVE" }, recallButton { "RECALL" }, clockButton { "CLOCK SYNC" },
                     followButton { "FOLLOW" }, audioButton { "AUDIO" }, sendAllButton { "SEND ALL" },
                     getButton { "GET" };
    TrackRail rail;
    ProjectBox projectBox { proc };
    juce::Rectangle<int> projectArea;
    std::unique_ptr<juce::Component> view;
    int viewIndex = -1, seenSerial = 0, seenGetSerial = 0;
    juce::String flash;
    int flashTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CyclesEditor)
};
