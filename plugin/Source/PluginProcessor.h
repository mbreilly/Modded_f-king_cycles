#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <map>
#include <unordered_map>
#include "CyclesParams.h"
#include "DeviceLink.h"
#include "AudioLink.h"
#include "TransportClock.h"
#include "SampleLibrary.h"

// Small lock-protected ring buffer of packed 3-byte CC messages.
class MidiQueue
{
public:
    static constexpr int capacity = 4096;

    bool push (juce::uint32 m) noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (count == capacity) return false;
        data[(size_t) ((head + count) % capacity)] = m;
        ++count;
        return true;
    }

    /** Pushes all or nothing, in order, so a NRPN's four CCs are never split by another message. */
    bool pushAll (std::initializer_list<juce::uint32> ms) noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (count + (int) ms.size() > capacity) return false;
        for (auto m : ms)
            data[(size_t) ((head + count++) % capacity)] = m;
        return true;
    }

    bool pop (juce::uint32& m) noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (count == 0) return false;
        m = data[(size_t) head];
        head = (head + 1) % capacity;
        --count;
        return true;
    }

    void clear() noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        head = count = 0;
    }

    static juce::uint32 pack (int channel0, int cc, int value) noexcept
    {
        return (juce::uint32) ((channel0 & 0x0f) << 16 | (cc & 0x7f) << 8 | (value & 0x7f));
    }
    /** Marks a CC that came from the DAW's MIDI track and was already forwarded to the device. */
    static constexpr juce::uint32 fromHost = 1u << 24;

    static int channelOf (juce::uint32 m) noexcept { return (int) ((m >> 16) & 0x0f) + 1; } // 1-16
    static int ccOf      (juce::uint32 m) noexcept { return (int) ((m >> 8) & 0x7f); }
    static int valueOf   (juce::uint32 m) noexcept { return (int) (m & 0x7f); }
    static juce::MidiMessage toMessage (juce::uint32 m) { return juce::MidiMessage::controllerEvent (channelOf (m), ccOf (m), valueOf (m)); }

private:
    juce::SpinLock lock;
    std::array<juce::uint32, capacity> data {};
    int head = 0, count = 0;
};

class CyclesProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer,
                        private juce::HighResolutionTimer,
                        private juce::AsyncUpdater
{
public:
    CyclesProcessor();
    ~CyclesProcessor() override;

    //== AudioProcessor ======================================================
    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Model:Cycles link ===================================================
    juce::AudioProcessorValueTreeState apvts;
    DeviceLink link;
    AudioLink audio;

    /** Push every parameter value to the Model:Cycles. */
    void sendAll();

    /** GET (Modded Cycles firmware, notes/94): ask the device for the project it has loaded. It answers with every
        knob's CC and the sidechain/polarity NRPNs, then NRPN 1:58; the plugin takes them like knob moves. */
    void requestGet();
    enum GetState { getIdle, getWaiting, getDone, getNoAnswer };
    std::atomic<int> getState { getIdle }, getCount { 0 }, getSerial { 0 }; // getSerial: +1 each time a GET ends

    // Options (all saved with the project)
    enum Option { optReceive, optRecall, optClock, optFollow, optAudio, numOptions };
    bool getOption (Option o) const noexcept { return options[(size_t) o].load(); }
    void setOption (Option, bool);

    int  getChannel (int slot) const noexcept { return channels[(size_t) slot].load(); } // 1-16
    void setChannel (int slot, int channel1to16);

    int  getClockOffsetMs() const noexcept { return clockOffsetMs.load(); }
    void setClockOffsetMs (int ms);

    /** Ask the device to switch its USB audio to 2CH or 6CH (Modded Cycles firmware with the USB-mode NRPN): NRPN 1:47
        on the FX channel, 0 = 2CH, 1 = 6CH. The device reconnects and AudioLink follows the new channel count. */
    void requestUsbAudioMode (bool sixChannels);
    std::atomic<juce::uint32> usbModeRequestTime { 0 }; // ms counter of the last request, for the UI

    //== Samples (SAMPLER / GRANULAR / WAVETABLE, firmware notes/93) =========
    /** The +Drive's samples and their waveforms, read from the device. */
    SampleLibrary samples;
    /** The sample each track names on the device ("SMP" + hash), as the firmware reports it; 0 = none. */
    juce::uint32 getTrackSample (int track) const noexcept { return trackSample[(size_t) track].load(); }
    /** Put a +Drive sample on a track (NRPN 1:59, 1:60, 1:61 on the FX channel); the track's machine must be one
        of the three sample machines on the device. */
    void pickSample (int track, juce::uint32 hash);
    /** Ask the firmware for every track's sample (NRPN 1:62); it answers with 1:59..1:61 per track. */
    void requestSampleReport();

    // Projects (Modded Cycles firmware, notes/109), NRPN on the FX channel, value on CC 6 only: 1:66 asks for the
    // list; the device answers 1:67 = slot (0-95), then 1:68 = one character of its name, for each used slot, then
    // 1:69 = current slot + 1 (0 = not saved). 1:69 also comes on its own when the current slot changes or a load
    // ends. 1:71 = the current project has unsaved changes (0/1), sent just before each 1:69 and when it changes.
    // 1:70 = load that slot (the PROJECT menu's LOAD, with its prompts on the device); 1:72 = load it without saving;
    // 1:73 = save the current project in its slot, then load it.
    static constexpr int projListNrpn = 66, projSlotNrpn = 67, projCharNrpn = 68, projCurrentNrpn = 69,
                         projLoadNrpn = 70, projDirtyNrpn = 71, projDiscardNrpn = 72, projSaveNrpn = 73,
                         numProjectSlots = 96;
    enum class LoadHow { askOnDevice, dontSave, saveFirst };
    void requestProjectList();
    void loadProject (int slot, LoadHow how = LoadHow::askOnDevice);
    std::vector<std::pair<int, juce::String>> getProjects() const; // slot, name; sorted by slot
    std::atomic<int> currentProject { -2 };  // -2 = unknown (firmware without it), -1 = not saved in a slot
    std::atomic<int> projectDirty { -1 };    // unsaved changes: -1 unknown (older firmware), 0 no, 1 yes
    std::atomic<int> projectSerial { 0 };    // bumped when the list or the current project changes
    std::atomic<bool> projectLoading { false }; // a load asked by the plugin, until the device reports its slot
    static bool isSampleMachine (int machine);
    void handleDeviceMessageForTests (const juce::MidiMessage& m) { onDeviceMessage (m); }

    /** Added to the audio delay reported to the DAW, to line the device's audio up with the grid. */
    int  getAudioTrimMs() const noexcept { return audioTrimMs.load(); }
    void setAudioTrimMs (int ms);

    void setLinkMode (DeviceLink::Mode);
    void setManualPorts (const juce::String& in, const juce::String& out);

    // Activity readout for the UI
    std::atomic<juce::uint32> lastReceived { 0xffffffff }, lastSent { 0xffffffff };
    std::array<std::atomic<juce::uint32>, cycles::numTracks + 1> rxTime {}, txTime {}; // ms counter per slot
    std::atomic<int> lastIncomingSlot { -1 }, incomingSerial { 0 };
    static constexpr int trackSelectCc = modded::kTrackSelectCC; // on the FX channel, from a modded firmware: the track selected on the device
    std::atomic<bool> clockRunning { false };
    // Output peaks (main L, main R, tracks 1-6), decaying, for the meters
    std::array<std::atomic<float>, 2 + cycles::numTracks> meters {};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    void parameterChanged (const juce::String& id, float newValue) override;
    void onDeviceMessage (const juce::MidiMessage&);
    void timerCallback() override;        // message thread: received CCs, automation gestures
    void hiResTimerCallback() override;   // paces outgoing CCs to the direct port
    void handleAsyncUpdate() override;    // applies loaded settings
    void updateLatency();                 // message thread: reports the audio delay to the DAW
    void renderAudio (juce::AudioBuffer<float>&);

    void queueParam (int index, int ccValue);
    int  currentCcValue (int index) const;
    void rebuildLookup();
    void syncLastValues();
    bool isCyclesChannel (int channel) const noexcept;

    std::vector<juce::RangedAudioParameter*> params;
    std::unordered_map<juce::String, int> idToIndex;
    std::vector<std::atomic<int>> lastValue;   // last CC value exchanged with the device, per parameter
    std::vector<int> partner;                  // FX On <-> FX Type of the same track (they share CC 27), else -1
    int insertCc (int index) const;
    int nrpnValue (int index) const;           // value of a NRPN parameter (the duck mask for the six Duck toggles)
    void queueNrpn (int lsb, int value);       // NRPN 1:lsb on the FX channel
    void queueNrpn14 (int lsb, int value);     // the same with a 14-bit value (CC 6 = top 7 bits, CC 38 = low 7)
    void queueNrpnMsb (int lsb, int value);    // the same, CC 6 only (the firmware acts on it at once)
    void onProjectNrpn (int number, int value); // MIDI thread: the firmware's project list and current slot
    mutable juce::SpinLock projectLock;
    std::map<int, juce::String> projects, projectsRx; // shown / being received (MIDI thread)
    int rxProjectSlot = -1;
    bool projectListing = false;                       // a list is on its way (under projectLock)
    std::atomic<bool> projectListPending { false };    // ask for the list from the message thread
    juce::uint32 projectLoadTime = 0;
    void onSampleNrpn (int number, int value, bool lsbPart); // MIDI thread: the firmware's sample report
    std::array<std::atomic<juce::uint32>, cycles::numTracks> trackSample {};
    std::atomic<bool> trackSampleDirty { false };
    int rxNrpnMsb = -1, rxNrpnLsb = -1, rxValue = 0;     // MIDI thread: NRPN in progress on the FX channel
    std::array<std::atomic<int>, 3> rxEq { -1, -1, -1 }; // MIDI thread -> message thread: master EQ from the device
    juce::uint32 rxHi = 0, rxMid = 0;            // CC 27 value of that pair: 0 while off, else type + 1
    void receiveNrpn (int lsb, int value);     // a NRPN 1:lsb from the device on the FX channel (message thread)
    std::map<int, std::vector<int>> nrpnParams; // NRPN lsb -> the parameters it carries (a mask: one per track)
    int getNrpnMsb = -1, getNrpnLsb = -1, getDataMsb = 0; // NRPN being received on the FX channel (message thread)
    std::atomic<bool> applyingNrpn { false };  // setting parameters from a received NRPN: don't send them back
    juce::uint32 getStartTime = 0;
    std::atomic<bool> getPending { false };    // GET as soon as the device is connected (a project without a sound)
    std::atomic<bool> applyingInsert { false }; // setting a received CC 27 on the pair: don't send halfway states

    // Hardware-knob automation: one open gesture per parameter while it's moving (message thread)
    std::vector<double> gestureLastTouch;
    std::vector<bool> gestureOpen;

    std::array<std::atomic<int>, cycles::numTracks + 1> channels;
    std::array<std::array<int, 128>, 16> lookup {}; // [channel-1][cc] -> param index or -1 (message thread)

    std::array<std::atomic<bool>, numOptions> options;
    std::atomic<int> clockOffsetMs { 0 }, audioTrimMs { 0 };
    std::atomic<bool> audioAligned { false }; // the DAW compensates the audio delay: send clock without the extra block
    std::atomic<bool> loadingState { false }, recallPending { false };
    std::atomic<bool> stateKnown { false }; // the plugin holds a real sound (edited, received or loaded)

    MidiQueue outgoing, incoming;
    TransportClock clock;

    juce::AudioBuffer<float> streamBuffer { StreamBuffer::maxChannels, 8192 };
    std::array<std::atomic<float>*, cycles::numTracks> panParams {};
    std::array<float, cycles::numTracks> panL {}, panR {};

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CyclesProcessor)
};
