#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

/**
    The connection to the Model:Cycles, modelled on how Overbridge behaves:
    the plugin finds the device by itself, reconnects when it's replugged,
    checks that an Elektron unit is answering, and shows the link state.

    The Model:Cycles has no Overbridge support, so the transport underneath is
    its class-compliant USB MIDI port (or any MIDI port chosen manually).
*/
class DeviceLink : private juce::Timer,
                   private juce::MidiInputCallback
{
public:
    enum class Mode   { Auto = 0, Manual = 1, Host = 2 };
    enum class Status { Host, Searching, Connected, PortBusy };

    DeviceLink();
    ~DeviceLink() override;

    /** Called on the MIDI thread for every incoming message from the device. */
    std::function<void (const juce::MidiMessage&)> onMessage;
    /** Called on the MIDI thread for every SysEx from the device (without F0 / F7) before anything else looks at it;
        return true if it was taken (the samples' file-transfer answers). */
    std::function<bool (const juce::uint8*, int)> onSysEx;
    /** Called on the message thread when a device connects (or reconnects). */
    std::function<void()> onConnected;

    void setMode (Mode);
    Mode getMode() const noexcept { return mode.load(); }
    void setManualPorts (const juce::String& inputId, const juce::String& outputId);
    juce::String getManualInput() const  { return manualIn; }
    juce::String getManualOutput() const { return manualOut; }

    Status getStatus() const noexcept       { return status.load(); }
    bool hasDirectOutput() const noexcept   { return directOut.load(); }
    bool isIdentityConfirmed() const noexcept { return identityOk.load(); }
    juce::String getDeviceName() const;

    /** Send immediately to the device (any thread). */
    void sendNow (const juce::MidiMessage&);
    /** Send a timestamped block (e.g. MIDI clock), starting delayMs from now. */
    void sendBlock (const juce::MidiBuffer&, double sampleRate, double delayMs);

    /** Close the ports and stop scanning (call before the owner is destroyed). */
    void shutdown() { stopTimer(); close(); }

    /** Look for the device now instead of waiting for the next scan. */
    void rescan() { timerCallback(); }

    static bool looksLikeModelCycles (const juce::String& portName);

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void open (const juce::MidiDeviceInfo* in, const juce::MidiDeviceInfo* out);
    void close();
    void sendIdentityRequest();

    std::atomic<Mode> mode { Mode::Auto };
    std::atomic<Status> status { Status::Searching };
    std::atomic<bool> directOut { false }, identityOk { false };

    // Heartbeat: CoreMIDI keeps a port's identifier when the device reboots (after a firmware flash, for example), so the
    // scan can miss the gap and keep dead handles. While connected, an identity request goes out every few scans; when
    // nothing at all has come back from the device for kSilentReopenMs, the ports are reopened.
    static constexpr double kSilentReopenMs = 14000.0;
    static constexpr int kPingEveryScans = 3;
    std::atomic<double> lastHeardMs { 0.0 };
    int scansSincePing = 0;

    juce::String manualIn, manualOut;
    juce::String openInId, openOutId, deviceName;

    mutable juce::ReadWriteLock lock;
    std::unique_ptr<juce::MidiInput>  input;
    std::unique_ptr<juce::MidiOutput> output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceLink)
};
