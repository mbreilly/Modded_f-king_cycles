#include "DeviceLink.h"

DeviceLink::DeviceLink()
{
    startTimer (1500); // hot-plug scan, like Overbridge's device discovery
}

DeviceLink::~DeviceLink()
{
    stopTimer();
    close();
}

bool DeviceLink::looksLikeModelCycles (const juce::String& portName)
{
    const auto n = portName.toLowerCase();
    return n.contains ("cycles") && n.contains ("model");
}

juce::String DeviceLink::getDeviceName() const
{
    const juce::ScopedReadLock sl (lock);
    return deviceName;
}

void DeviceLink::setMode (Mode m)
{
    mode.store (m);
    close();
    status.store (m == Mode::Host ? Status::Host : Status::Searching);
    timerCallback();
}

void DeviceLink::setManualPorts (const juce::String& inId, const juce::String& outId)
{
    manualIn = inId;
    manualOut = outId;
    if (mode.load() == Mode::Manual)
    {
        close();
        timerCallback();
    }
}

void DeviceLink::timerCallback()
{
    const auto m = mode.load();
    if (m == Mode::Host)
    {
        if (input != nullptr || output != nullptr) close();
        status.store (Status::Host);
        return;
    }

    const auto ins  = juce::MidiInput::getAvailableDevices();
    const auto outs = juce::MidiOutput::getAvailableDevices();

    const juce::MidiDeviceInfo* wantIn = nullptr;
    const juce::MidiDeviceInfo* wantOut = nullptr;

    for (auto& d : ins)
        if (m == Mode::Auto ? looksLikeModelCycles (d.name) : d.identifier == manualIn) { wantIn = &d; break; }
    for (auto& d : outs)
        if (m == Mode::Auto ? looksLikeModelCycles (d.name) : d.identifier == manualOut) { wantOut = &d; break; }

    const bool connected = status.load() == Status::Connected;

    // Device unplugged: drop the link and go back to searching.
    if (connected && ((openOutId.isNotEmpty() && wantOut == nullptr) || (openInId.isNotEmpty() && wantIn == nullptr)))
    {
        close();
        status.store (Status::Searching);
        return;
    }

    const bool needOpen = (wantOut != nullptr && wantOut->identifier != openOutId)
                       || (wantIn  != nullptr && wantIn->identifier  != openInId);

    // Connected to both ports but silent for too long: the device most likely rebooted under the same port
    // identifier, so the handles are dead. Reopen (which also asks for its identity again).
    if (connected && ! needOpen && openInId.isNotEmpty() && openOutId.isNotEmpty())
    {
        if (juce::Time::getMillisecondCounterHiRes() - lastHeardMs.load() > kSilentReopenMs)
        {
            open (wantIn, wantOut);
            return;
        }
        if (++scansSincePing >= kPingEveryScans)
        {
            scansSincePing = 0;
            sendIdentityRequest();
        }
        return;
    }

    if (! connected || needOpen)
        if (wantIn != nullptr || wantOut != nullptr)
            open (wantIn, wantOut);
}

void DeviceLink::open (const juce::MidiDeviceInfo* in, const juce::MidiDeviceInfo* out)
{
    close();

    bool ok = true;
    {
        const juce::ScopedWriteLock sl (lock);

        if (out != nullptr)
        {
            output = juce::MidiOutput::openDevice (out->identifier);
            if (output != nullptr) { output->startBackgroundThread(); openOutId = out->identifier; }
            else ok = false;
        }
        if (in != nullptr)
        {
            input = juce::MidiInput::openDevice (in->identifier, this);
            if (input != nullptr) { input->start(); openInId = in->identifier; }
            else ok = false;
        }
        deviceName = out != nullptr ? out->name : (in != nullptr ? in->name : juce::String());
    }

    directOut.store (output != nullptr);

    if (! ok && input == nullptr && output == nullptr)
    {
        // Typically another app (or the DAW itself, on Windows) holds the port.
        status.store (Status::PortBusy);
        return;
    }

    lastHeardMs.store (juce::Time::getMillisecondCounterHiRes());
    scansSincePing = 0;
    status.store (Status::Connected);
    sendIdentityRequest();
    if (onConnected) onConnected();
}

void DeviceLink::close()
{
    const juce::ScopedWriteLock sl (lock);
    if (input != nullptr) input->stop();
    input.reset();
    if (output != nullptr) output->stopBackgroundThread();
    output.reset();
    openInId.clear();
    openOutId.clear();
    directOut.store (false);
    identityOk.store (false);
}

void DeviceLink::sendIdentityRequest()
{
    // Universal Device Inquiry: F0 7E 7F 06 01 F7
    const juce::uint8 req[] = { 0x7e, 0x7f, 0x06, 0x01 };
    sendNow (juce::MidiMessage::createSysExMessage (req, (int) sizeof (req)));
}

void DeviceLink::sendNow (const juce::MidiMessage& m)
{
    // Readers never block each other; only open/close take the write lock.
    const juce::ScopedReadLock sl (lock);
    if (output != nullptr)
    {
        juce::MidiBuffer b;
        b.addEvent (m, 0);
        output->sendBlockOfMessages (b, juce::Time::getMillisecondCounterHiRes(), 1000.0);
    }
}

void DeviceLink::sendBlock (const juce::MidiBuffer& buffer, double sampleRate, double delayMs)
{
    if (buffer.isEmpty()) return;
    const juce::ScopedReadLock sl (lock);
    if (output != nullptr)
        output->sendBlockOfMessages (buffer, juce::Time::getMillisecondCounterHiRes() + delayMs, sampleRate);
}

void DeviceLink::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    lastHeardMs.store (juce::Time::getMillisecondCounterHiRes());

    // Identity Reply: F0 7E <id> 06 02 <manufacturer...>. Elektron's manufacturer ID is 00 20 3C.
    if (m.isSysEx())
    {
        const auto* d = m.getSysExData();
        const int n = m.getSysExDataSize();
        if (onSysEx && onSysEx (d, n)) return;
        if (n >= 7 && d[0] == 0x7e && d[2] == 0x06 && d[3] == 0x02
            && d[4] == 0x00 && d[5] == 0x20 && d[6] == 0x3c)
            identityOk.store (true);
        return;
    }

    if (onMessage) onMessage (m);
}
