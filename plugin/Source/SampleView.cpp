#include "SampleView.h"
#include "PluginEditor.h"

using LF = CyclesLookAndFeel;

namespace
{
    constexpr int frameLen = 2048; // WAVETABLE frames (firmware wave.c)

    juce::String lengthText (int n)
    {
        return juce::String ((double) n / 48000.0, 2) + " s";
    }
}

SampleView::SampleView (CyclesProcessor& p, int t) : proc (p), track (t)
{
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, LF::knobBody);
    list.setColour (juce::ListBox::outlineColourId, LF::outline);
    addAndMakeVisible (list);

    loadButton.setTooltip ("Put the selected sample on this track (double-click a sample does the same)");
    loadButton.onClick = [this] { loadSelected(); };
    addAndMakeVisible (loadButton);

    refreshButton.setTooltip ("Read the list of samples on the Model:Cycles' +Drive again (after sending new ones with Transfer)");
    refreshButton.onClick = [this] { proc.samples.refresh(); };
    addAndMakeVisible (refreshButton);

    startTimerHz (15);
}

SampleView::~SampleView() { stopTimer(); }

SampleView::Kind SampleView::kindOf (int machine)
{
    if (machine < 0 || machine >= modded::numMachines) return Kind::Other;
    const juce::String n (modded::machines[machine].name);
    return n == "SAMPLER" ? Kind::Sampler : n == "GRANULAR" ? Kind::Granular : n == "WAVETABLE" ? Kind::Wavetable : Kind::Other;
}

// The firmware's formulas (smp_view.c, smp.c note_on, gran.c, wave.c), so the marks sit where the Cycles plays.
SampleView::Marks SampleView::marks (Kind k, int n, int color, int shape, int sweep, int contour, int extra3)
{
    Marks m;
    auto K = [] (int v) { return juce::jlimit (0, 127, v); };
    color = K (color); shape = K (shape); sweep = K (sweep); contour = K (contour); extra3 = K (extra3);
    if (n <= 0) return m;
    if (k == Kind::Sampler)
    {
        m.mode = juce::jlimit (0, 3, sweep);
        m.start = (int) (((juce::int64) n * color) >> 7);
        int hi = (int) (((juce::int64) n * (shape + 1)) >> 7);
        hi = hi > n ? n : (hi < m.start + 2 ? juce::jmin (n, m.start + 2) : hi);
        m.end = hi;
        int lp = m.start + (int) (((juce::int64) (hi - m.start) * extra3) >> 7);
        if (lp > hi - 2) lp = hi - 2 < m.start ? m.start : hi - 2;
        m.loop = m.mode >= 2 && lp > m.start ? lp : -1;
    }
    else if (k == Kind::Granular)
    {
        m.pos = (int) (((juce::int64) n * color) >> 7);
        m.spray = (int) (((juce::int64) n * contour) >> 9);
        m.grains = 1 + sweep * 7 / 127;
        m.scan = extra3;
    }
    else if (k == Kind::Wavetable)
    {
        const int f = juce::jmin (n, frameLen);
        m.frames = juce::jmax (1, n / f);
        m.pos = juce::jlimit (0, (m.frames - 1) * 256, color * (m.frames - 1) * 256 / 127);
        m.warp = 256 + shape * 768 / 127;
        m.scan = sweep; // Env
    }
    return m;
}

int SampleView::valueFor (Handle h, int n, int pos, const Marks& m)
{
    if (n <= 0) return 0;
    pos = juce::jlimit (0, n, pos);
    const auto v = [] (double x) { return juce::jlimit (0, 127, juce::roundToInt (x)); };
    switch (h)
    {
        case Handle::Start: return v ((double) pos * 128.0 / n);               // start = n * color >> 7
        case Handle::End:   return v ((double) pos * 128.0 / n - 1.0);         // end = n * (shape + 1) >> 7
        case Handle::Loop:  return m.end - m.start > 0 ? v ((double) (pos - m.start) * 128.0 / (m.end - m.start)) : 0;
        case Handle::None:  break;
    }
    return 0;
}

juce::RangedAudioParameter* SampleView::param (const char* key) const
{
    return proc.apvts.getParameter ("t" + juce::String (track + 1) + "_" + key);
}

// The Start and End bars (and Loop Start in LOOP / PING) of a SAMPLER track follow the mouse: the knob moves, the
// Cycles follows by its CC, and the DAW records it as automation.
SampleView::Handle SampleView::handleAt (float x) const
{
    if (kindOf (knob ("machine")) != Kind::Sampler) return Handle::None;
    auto wave = proc.samples.getWave (proc.getTrackSample (track));
    if (wave == nullptr || wave->numSamples <= 0) return Handle::None;
    const int n = wave->numSamples;
    const auto m = marks (Kind::Sampler, n, knob ("color"), knob ("shape"), knob ("sweep"), knob ("contour"), knob ("x3"));
    const auto r = waveRect();
    auto xOf = [&] (int s) { return r.getX() + r.getWidth() * (float) s / (float) n; };
    Handle best = Handle::None;
    float dist = 8.0f;
    for (auto [h, s] : { std::pair { Handle::Start, m.start }, std::pair { Handle::End, m.end }, std::pair { Handle::Loop, m.loop } })
        if (s >= 0 && std::abs (xOf (s) - x) < dist) { dist = std::abs (xOf (s) - x); best = h; }
    return best;
}

void SampleView::mouseMove (const juce::MouseEvent& e)
{
    const bool on = waveArea.contains (e.getPosition()) && handleAt ((float) e.x) != Handle::None;
    setMouseCursor (on ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void SampleView::mouseDown (const juce::MouseEvent& e)
{
    dragging = waveArea.contains (e.getPosition()) ? handleAt ((float) e.x) : Handle::None;
    if (dragging == Handle::None) return;
    dragN = proc.samples.getWave (proc.getTrackSample (track))->numSamples;
    if (auto* p = param (dragging == Handle::Start ? "color" : dragging == Handle::End ? "shape" : "x3"))
        p->beginChangeGesture();
    mouseDrag (e);
}

void SampleView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::None || dragN <= 0) return;
    const auto r = waveRect();
    const int pos = juce::roundToInt ((e.position.x - r.getX()) / r.getWidth() * (float) dragN);
    const auto m = marks (Kind::Sampler, dragN, knob ("color"), knob ("shape"), knob ("sweep"), knob ("contour"), knob ("x3"));
    int v = valueFor (dragging, dragN, pos, m);
    // Start stays before End, as on the device
    if (dragging == Handle::Start) v = juce::jmin (v, knob ("shape"));
    if (dragging == Handle::End) v = juce::jmax (v, knob ("color"));
    if (auto* p = param (dragging == Handle::Start ? "color" : dragging == Handle::End ? "shape" : "x3"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    repaint();
}

void SampleView::mouseUp (const juce::MouseEvent&)
{
    if (dragging != Handle::None)
        if (auto* p = param (dragging == Handle::Start ? "color" : dragging == Handle::End ? "shape" : "x3"))
            p->endChangeGesture();
    dragging = Handle::None;
}

int SampleView::knob (const char* key) const
{
    if (auto* v = proc.apvts.getRawParameterValue ("t" + juce::String (track + 1) + "_" + key))
        return juce::roundToInt (v->load());
    return 0;
}

void SampleView::timerCallback()
{
    auto& lib = proc.samples;
    if (! askedList && ! lib.isBusy() && lib.getSamples().empty())
    {
        askedList = true;
        lib.refresh();
    }
    if (lib.getSerial() != seenSerial)
    {
        seenSerial = lib.getSerial();
        rows = lib.getSamples();
        list.updateContent();
    }
    const auto h = proc.getTrackSample (track);
    if (h != seenHash)
    {
        seenHash = h;
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].hash == h) { list.selectRow (i); break; }
    }
    loadButton.setEnabled (list.getSelectedRow() >= 0);
    refreshButton.setEnabled (! lib.isBusy());
    repaint (waveArea.getUnion (infoArea));
}

int SampleView::getNumRows() { return (int) rows.size(); }

void SampleView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& s = rows[(size_t) row];
    const bool onTrack = s.hash == proc.getTrackSample (track);
    if (selected) g.fillAll (LF::raised);
    g.setColour (onTrack ? LF::accent : LF::text);
    g.setFont (juce::FontOptions (14.0f, onTrack ? juce::Font::bold : juce::Font::plain));
    g.drawText (s.path.trimCharactersAtStart ("/"), 8, 0, w - 90, h, juce::Justification::centredLeft, true);
    g.setColour (LF::dim);
    g.drawText (lengthText (s.numSamples), w - 80, 0, 72, h, juce::Justification::centredRight);
}

void SampleView::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    loadSelected();
}

void SampleView::loadSelected()
{
    const int row = list.getSelectedRow();
    if (row >= 0 && row < (int) rows.size())
        proc.pickSample (track, rows[(size_t) row].hash);
}

void SampleView::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (24);                       // title
    waveArea = r.removeFromTop (juce::jmin (230, r.getHeight() / 2));
    infoArea = r.removeFromTop (24);
    r.removeFromTop (6);
    auto buttons = r.removeFromBottom (30);
    refreshButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    loadButton.setBounds (buttons.removeFromRight (90));
    r.removeFromBottom (6);
    list.setBounds (r);
}

void SampleView::paint (juce::Graphics& g)
{
    g.setColour (LF::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    const int machine = knob ("machine");
    const auto kind = kindOf (machine);
    const auto h = proc.getTrackSample (track);
    SampleLibrary::Sample s;
    const bool listed = proc.samples.findByHash (h, s) != nullptr;
    auto wave = proc.samples.getWave (h);

    auto title = getLocalBounds().reduced (8).removeFromTop (22);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setColour (LF::text);
    g.drawText ("SAMPLE", title.removeFromLeft (80), juce::Justification::centredLeft);
    g.setColour (LF::accent);
    g.drawText (h == 0 ? juce::String ("none on this track") : listed ? s.path.trimCharactersAtStart ("/") : "SMP" + juce::String::toHexString ((juce::int64) h).toUpperCase(),
                title, juce::Justification::centredLeft, true);

    g.setColour (LF::knobBody);
    g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);
    const int n = wave != nullptr ? wave->numSamples : 0;
    const auto m = marks (kind, n, knob ("color"), knob ("shape"), knob ("sweep"), knob ("contour"), knob ("x3"));

    g.setFont (juce::FontOptions (14.0f));
    if (wave != nullptr && kind != Kind::Other)
        drawWave (g, waveRect(), *wave, kind, m);
    else
    {
        g.setColour (LF::dim);
        juce::String msg = kind == Kind::Other ? "Pick SAMPLER, GRANULAR or WAVETABLE on this track"
                         : h == 0 ? "NO SAMPLE: pick one below (or with FUNC + PRESET on the Cycles)"
                         : proc.samples.isBusy() ? "READING " + juce::String (juce::roundToInt (proc.samples.getProgress() * 100)) + " %"
                         : ! listed ? "Not in the +Drive list: REFRESH" : "Could not read the sample";
        g.drawText (msg, waveArea, juce::Justification::centred);
    }

    // values under the waveform, as the device's bottom line
    juce::String info;
    if (kind == Kind::Sampler)
        info = "Start " + juce::String (knob ("color")) + "   End " + juce::String (knob ("shape")) + "   "
             + juce::StringArray { "FWD", "REV", "LOOP", "PING" }[m.mode]
             + (m.loop >= 0 ? "   Loop Start " + juce::String (knob ("x3")) : juce::String());
    else if (kind == Kind::Granular)
        info = "Position " + juce::String (knob ("color")) + "   Size " + juce::String (knob ("shape")) + "   Density "
             + juce::String (knob ("sweep")) + "   Spray " + juce::String (knob ("contour")) + "   Scan " + juce::String (knob ("x3"));
    else if (kind == Kind::Wavetable)
        info = "Frame " + juce::String ((m.pos >> 8) + 1) + " / " + juce::String (m.frames) + "   Warp " + juce::String (knob ("shape"))
             + "   Env " + juce::String (knob ("sweep")) + "   Env Speed " + juce::String (knob ("contour"));
    g.setColour (LF::text);
    g.drawText (info, infoArea, juce::Justification::centredLeft, true);
    g.setColour (LF::dim);
    g.drawText ((n > 0 ? lengthText (n) + "   " : juce::String()) + proc.samples.getStatus(), infoArea,
                juce::Justification::centredRight, true);
}

void SampleView::drawWave (juce::Graphics& g, juce::Rectangle<float> r, const SampleLibrary::Wave& w, Kind kind, const Marks& m)
{
    const float cy = r.getCentreY(), half = r.getHeight() * 0.5f;
    const int n = juce::jmax (1, w.numSamples);
    auto xOf = [&] (int s) { return r.getX() + r.getWidth() * (float) juce::jlimit (0, n, s) / (float) n; };

    g.setColour (LF::dim.withAlpha (0.4f));
    g.drawHorizontalLine ((int) cy, r.getX(), r.getRight());

    if (kind == Kind::Wavetable && ! w.pcm.empty())
    {
        // the frame that plays, read `warp` / 256 times per period as wave.c does
        const int f = juce::jmin (n, frameLen);
        const auto* fr = w.pcm.data() + (size_t) (m.pos >> 8) * (size_t) f;
        const int cols = juce::jmax (1, (int) r.getWidth());
        juce::Path p;
        int pk = 1;
        for (int i = 0; i < f; ++i) pk = juce::jmax (pk, std::abs ((int) fr[i]));
        for (int c = 0; c < cols; ++c)
        {
            const int at = (int) (((juce::int64) f * m.warp / 256 * c / cols) % f);
            const float y = cy - half * 0.9f * (float) fr[at] / (float) pk;
            if (c == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + (float) c, y);
        }
        g.setColour (LF::accent);
        g.strokePath (p, juce::PathStrokeType (1.6f));
        // the frames as a strip at the bottom, the current one lit, Env as an arrow
        auto strip = r.removeFromBottom (10.0f).translated (0, 8.0f);
        const float fw = strip.getWidth() / (float) m.frames;
        for (int i = 0; i < m.frames; ++i)
        {
            g.setColour (i == (m.pos >> 8) ? LF::accent : LF::raised);
            g.fillRect (strip.getX() + fw * (float) i + 0.5f, strip.getY(), juce::jmax (1.0f, fw - 1.0f), strip.getHeight());
        }
        if (m.scan > 0)
        {
            const float x0 = strip.getX() + fw * ((float) m.pos / 256.0f + 0.5f), x1 = x0 + strip.getWidth() * (float) m.scan / 127.0f;
            g.setColour (LF::text);
            g.drawArrow ({ x0, strip.getY() - 6.0f, juce::jmin (x1, strip.getRight()), strip.getY() - 6.0f }, 1.5f, 6.0f, 6.0f);
        }
        return;
    }

    // min / max columns; SAMPLER dims what doesn't play
    const int cols = (int) w.lo.size();
    int pk = 1;
    for (int c = 0; c < cols; ++c) pk = juce::jmax (pk, juce::jmax (std::abs ((int) w.lo[(size_t) c]), std::abs ((int) w.hi[(size_t) c])));
    const float colW = r.getWidth() / (float) juce::jmax (1, cols);
    for (int c = 0; c < cols; ++c)
    {
        const int s0 = (int) ((juce::int64) n * c / juce::jmax (1, cols));
        const bool plays = kind != Kind::Sampler || (s0 >= m.start && s0 < m.end);
        g.setColour (plays ? LF::accent : LF::dim.withAlpha (0.45f));
        const float y0 = cy - half * (float) w.hi[(size_t) c] / (float) pk, y1 = cy - half * (float) w.lo[(size_t) c] / (float) pk;
        g.fillRect (r.getX() + colW * (float) c, y0, juce::jmax (1.0f, colW), juce::jmax (1.0f, y1 - y0));
    }

    g.setColour (LF::text);
    if (kind == Kind::Sampler)
    {
        for (auto [s, dir] : { std::pair { m.start, 1.0f }, std::pair { m.end, -1.0f } })
        {
            const float x = xOf (s);
            g.drawLine (x, r.getY(), x, r.getBottom(), 2.0f);
            juce::Path flag;
            flag.addTriangle (x, r.getY(), x + 9.0f * dir, r.getY() + 5.0f, x, r.getY() + 10.0f);
            g.fillPath (flag);
        }
        if (m.loop >= 0)
        {
            const float x = xOf (m.loop);
            const float dash[] = { 3.0f, 3.0f };
            g.drawDashedLine ({ x, r.getY(), x, r.getBottom() }, dash, 2, 1.5f);
        }
    }
    else if (kind == Kind::Granular)
    {
        const float x = xOf (m.pos), a = xOf (m.pos - m.spray), b = xOf (m.pos + m.spray);
        g.drawLine (x, r.getY(), x, r.getBottom(), 2.0f);
        if (b > a + 1.0f)
        {
            g.drawLine (a, r.getY() + 2.0f, b, r.getY() + 2.0f, 1.5f);
            g.drawLine (a, r.getY() + 2.0f, a, r.getY() + 9.0f, 1.5f);
            g.drawLine (b, r.getY() + 2.0f, b, r.getY() + 9.0f, 1.5f);
        }
        if (m.scan > 0)
            g.drawArrow ({ x, r.getY() + 16.0f, x + 6.0f + r.getWidth() * 0.3f * (float) m.scan / 127.0f, r.getY() + 16.0f }, 1.5f, 7.0f, 7.0f);
        const float span = juce::jmax (b - a, 12.0f * (float) m.grains);
        for (int k = 0; k < m.grains; ++k)
        {
            const float gx = x - span / 2 + (float) (2 * k + 1) * span / (float) (2 * m.grains);
            juce::Path tri;
            tri.addTriangle (gx - 4.0f, r.getBottom() + 8.0f, gx + 4.0f, r.getBottom() + 8.0f, gx, r.getBottom() + 2.0f);
            g.fillPath (tri);
        }
    }
}
