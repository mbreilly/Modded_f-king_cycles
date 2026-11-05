#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

/**
    The sample window of a SAMPLER / GRANULAR / WAVETABLE track (firmware notes/91 and 93): the track's sample as
    a waveform with what the knobs do drawn over it, as the Cycles' own screen draws it but larger:
      SAMPLER    Start and End bars, the part that doesn't play dimmed, Loop Start in LOOP and PING
      GRANULAR   the position, Spray's spread, one mark per grain (Density), Scan as an arrow
      WAVETABLE  the frame that plays as Warp reads it, the strip of frames with the current one, Env as an arrow
    and under it the samples on the +Drive (sent with the Transfer app): double-click one, or LOAD, to put it on
    the track.
*/
class SampleView : public juce::Component,
                   private juce::Timer,
                   private juce::ListBoxModel
{
public:
    SampleView (CyclesProcessor&, int track);
    ~SampleView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The overlay of a machine, in sample positions, from its knob values (0-127): the firmware's formulas. */
    struct Marks
    {
        int start = 0, end = 0, loop = -1;  // SAMPLER: played part [start, end), loop start (LOOP, PING) or -1
        int mode = 0;                       // SAMPLER: 0 FWD, 1 REV, 2 LOOP, 3 PING
        int pos = 0, spray = 0, grains = 1; // GRANULAR: position, half spread, grain marks; WAVETABLE: pos = frame << 8
        int scan = 0;                       // GRANULAR Scan / WAVETABLE Env (0-127)
        int frames = 1, warp = 256;         // WAVETABLE: frames, read speed (256 = once per period)
    };
    enum class Kind { Sampler, Granular, Wavetable, Other };
    /** The SAMPLER lines that can be dragged, and the knob value (0-127) that puts one at a sample position. */
    enum class Handle { None, Start, End, Loop };
    static int valueFor (Handle, int numSamples, int samplePos, const Marks&);
    static Kind kindOf (int machine);
    static Marks marks (Kind, int numSamples, int color, int shape, int sweep, int contour, int extra3);

private:
    void timerCallback() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    Handle handleAt (float x) const;
    juce::Rectangle<float> waveRect() const { return waveArea.toFloat().reduced (6.0f, 10.0f); }
    juce::RangedAudioParameter* param (const char* key) const;
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void loadSelected();

    int knob (const char* key) const;
    void drawWave (juce::Graphics&, juce::Rectangle<float>, const SampleLibrary::Wave&, Kind, const Marks&);

    CyclesProcessor& proc;
    const int track;
    juce::ListBox list { "samples", this };
    juce::TextButton loadButton { "LOAD" }, refreshButton { "REFRESH" };
    std::vector<SampleLibrary::Sample> rows;
    int seenSerial = -1;
    juce::uint32 seenHash = 0;
    bool askedList = false;
    juce::Rectangle<int> waveArea, infoArea;
    Handle dragging = Handle::None;
    int dragN = 0;                  // the sample's length while dragging
};
