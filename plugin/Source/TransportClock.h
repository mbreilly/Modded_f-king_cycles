#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

/**
    Keeps the Model:Cycles sequencer locked to the DAW, the way Overbridge ties
    the device to the host transport: 24 PPQN MIDI clock placed on the exact
    sample of each tick, Song Position Pointer + Start/Continue when playback
    starts or jumps (loops, locating), and Stop when it stops.
*/
class TransportClock
{
public:
    void reset() noexcept { wasPlaying = false; }

    /** Adds this block's clock/transport messages to `out` at sample offsets. */
    void process (const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                  int numSamples, double sampleRate, juce::MidiBuffer& out)
    {
        const bool playing = pos.hasValue() && pos->getIsPlaying();
        const auto ppqOpt  = pos.hasValue() ? pos->getPpqPosition() : juce::Optional<double>();
        const auto bpmOpt  = pos.hasValue() ? pos->getBpm()         : juce::Optional<double>();

        if (! playing || ! ppqOpt.hasValue() || ! bpmOpt.hasValue() || *bpmOpt <= 0.0)
        {
            if (wasPlaying) out.addEvent (juce::MidiMessage::midiStop(), 0);
            wasPlaying = false;
            return;
        }

        const double ppq = *ppqOpt;
        const double samplesPerBeat = sampleRate * 60.0 / *bpmOpt;
        const double ppqEnd = ppq + numSamples / samplesPerBeat;

        const bool jumped = wasPlaying && std::abs (ppq - expectedPpq) > 1.0 / 48.0;

        if (! wasPlaying || jumped)
        {
            if (jumped) out.addEvent (juce::MidiMessage::midiStop(), 0);

            // SPP counts 16th notes; ticks resume from that 16th.
            const int sixteenths = juce::jmax (0, (int) std::floor (ppq * 4.0 + 1.0e-6));
            out.addEvent (juce::MidiMessage::songPositionPointer (sixteenths), 0);
            out.addEvent (sixteenths == 0 ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue(), 0);
            nextTick = (juce::int64) sixteenths * 6;
        }

        for (;;)
        {
            const double tickPpq = (double) nextTick / 24.0;
            if (tickPpq >= ppqEnd) break;
            const int offset = juce::jlimit (0, numSamples - 1, (int) std::floor ((tickPpq - ppq) * samplesPerBeat));
            out.addEvent (juce::MidiMessage::midiClock(), offset);
            ++nextTick;
        }

        expectedPpq = ppqEnd;
        wasPlaying = true;
    }

private:
    bool wasPlaying = false;
    double expectedPpq = 0.0;
    juce::int64 nextTick = 0;
};
