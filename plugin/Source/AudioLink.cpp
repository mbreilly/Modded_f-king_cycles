#include "AudioLink.h"
#include "DeviceLink.h"

//==============================================================================
void StreamBuffer::reset (int numChannels, double devRate, int devBlock)
{
    channels = juce::jlimit (0, maxChannels, numChannels);
    deviceRate = devRate > 0.0 ? devRate : 48000.0;
    deviceBlock = juce::jlimit (16, 4096, devBlock);
    fifo.reset();
    primed = false;
    configure (hostRate, hostMaxBlock);
}

void StreamBuffer::configure (double rate, int maxBlock)
{
    hostRate = rate > 0.0 ? rate : 48000.0;
    hostMaxBlock = juce::jmax (1, maxBlock);

    // One DAW block, plus the larger of half a DAW block and two device buffers for timing jitter.
    const int need = (int) std::ceil (hostMaxBlock * deviceRate / hostRate * 1.01) + 4;
    target.store (juce::jmin (fifo.getTotalSize() / 2, need + juce::jmax (need / 2, 2 * deviceBlock) + 64));
    wideBand = std::abs (deviceRate / hostRate - 1.0) > 0.001; // real rate conversion: windowed sinc
    primed = false;
}

bool StreamBuffer::write (const float* const* data, int numChannels, int numSamples) noexcept
{
    if (channels == 0 || numSamples <= 0) return true;
    if (fifo.getFreeSpace() < numSamples)
    {
        ++overruns;
        return false;
    }

    int s1, n1, s2, n2;
    fifo.prepareToWrite (numSamples, s1, n1, s2, n2);
    for (int c = 0; c < channels; ++c)
    {
        const float* src = c < numChannels ? data[c] : nullptr;
        if (src != nullptr)
        {
            if (n1 > 0) ring.copyFrom (c, s1, src, n1);
            if (n2 > 0) ring.copyFrom (c, s2, src + n1, n2);
        }
        else
        {
            if (n1 > 0) ring.clear (c, s1, n1);
            if (n2 > 0) ring.clear (c, s2, n2);
        }
    }
    fifo.finishedWrite (n1 + n2);
    return true;
}

bool StreamBuffer::read (float* const* dest, int numSamples, double rate) noexcept
{
    if (channels == 0 || numSamples <= 0) return false;
    if (std::abs (rate - hostRate) > 0.5 || numSamples > hostMaxBlock) configure (rate, juce::jmax (numSamples, hostMaxBlock));

    const int tgt = target.load();
    int avail = fifo.getNumReady();
    const double baseRatio = deviceRate / hostRate;

    if (! primed)
    {
        if (avail < tgt) return false;
        fifo.finishedRead (avail - tgt); // start exactly at the target delay
        avail = tgt;
        for (auto& i : lagrange) i.reset();
        for (auto& i : sinc) i.reset();
        avgFill = tgt;
        integral = 0.0;
        primed = true;
    }
    else if (avail > tgt + 4 * deviceBlock + 2 * (int) (hostMaxBlock * baseRatio))
    {
        // The DAW stopped calling us for a while (or the clocks jumped): drop back to the target.
        ++overruns;
        fifo.finishedRead (avail - tgt);
        avail = tgt;
        avgFill = tgt;
    }

    // Clock drift: nudge the ratio to hold the FIFO at its target (PI loop, ~1 s smoothing).
    const double blockSec = numSamples / hostRate;
    avgFill += (avail - avgFill) * juce::jmin (1.0, blockSec / 1.0);
    const double err = (avgFill - tgt) / deviceRate; // seconds of excess
    integral = juce::jlimit (-0.001, 0.001, integral + err * blockSec * 0.02);
    const double correction = juce::jlimit (-0.002, 0.002, err * 0.2 + integral);
    drift.store (correction);

    const double ratio = baseRatio * (1.0 + correction);
    const int need = (int) std::ceil (numSamples * ratio) + 2;
    if (need > scratch.getNumSamples()) return false;
    if (avail < need)
    {
        ++underruns;
        primed = false; // refill to the target before playing again
        return false;
    }

    // Copy what the resampler may use out of the ring, run it, then consume what it used.
    const int take = juce::jmin (avail, need + 8);
    int s1, n1, s2, n2;
    fifo.prepareToRead (take, s1, n1, s2, n2);
    int used = 0;
    for (int c = 0; c < channels; ++c)
    {
        if (n1 > 0) scratch.copyFrom (c, 0, ring, c, s1, n1);
        if (n2 > 0) scratch.copyFrom (c, n1, ring, c, s2, n2);
        used = wideBand ? sinc[c].process (ratio, scratch.getReadPointer (c), dest[c], numSamples)
                        : lagrange[c].process (ratio, scratch.getReadPointer (c), dest[c], numSamples);
    }
    fifo.finishedRead (juce::jmin (used, take));
    return true;
}

//==============================================================================
AudioLink::AudioLink()
{
   #if JUCE_MAC
    type.reset (juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
   #elif JUCE_WINDOWS
    // Shared mode, so the DAW can keep using its own interface (or this one).
    type.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
   #elif JUCE_LINUX
    type.reset (juce::AudioIODeviceType::createAudioIODeviceType_ALSA());
   #endif
}

AudioLink::~AudioLink()
{
    shutdown();
}

void AudioLink::shutdown()
{
    stopTimer();
    close();
}

void AudioLink::setEnabled (bool shouldStream)
{
    if (enabled.exchange (shouldStream) == shouldStream) return;

    if (shouldStream)
    {
        status.store (Status::Searching);
        startTimer (1500); // hot-plug scan, like the MIDI side
        timerCallback();
    }
    else
    {
        stopTimer();
        close();
        status.store (Status::Off);
        if (onFormatChanged) onFormatChanged();
    }
}

juce::String AudioLink::getDeviceName() const
{
    const juce::ScopedLock sl (nameLock);
    return deviceName;
}

juce::String AudioLink::getError() const
{
    const juce::ScopedLock sl (nameLock);
    return error;
}

double AudioLink::getLatencySeconds() const noexcept
{
    if (! streaming.load()) return 0.0;
    const double rate = stream.getDeviceRate();
    return (stream.getDelayDeviceSamples() + inputLatency.load()) / rate
           + 0.001; // USB MIDI to the device
}

void AudioLink::timerCallback()
{
    if (! enabled.load()) return;

    if (device != nullptr)
    {
        // 2CH <-> 6CH on the device: it comes back with another channel count.
        const bool reshaped = juce::jmin (StreamBuffer::maxChannels, device->getInputChannelNames().size())
                              != stream.getNumChannels();
        // No audio for a while: the Cycles pauses its USB audio while it loads a project. Closing and reopening
        // the device then (which sets its rate again) hung the DAW and once crashed the Cycles (m_reilly, 02/11/2026),
        // so a stalled device stays open as long as the system still lists it; it resumes by itself.
        bool gone = false;
        if (! lost.load() && ! reshaped && juce::Time::getMillisecondCounter() - lastCallback.load() > 1500)
        {
            type->scanForDevices();
            gone = ! type->getDeviceNames (true).contains (getDeviceName());
        }
        if (! lost.load() && ! reshaped && ! gone) return;

        close();
        status.store (Status::Searching);
        if (onFormatChanged) onFormatChanged();
    }

    if (type == nullptr)
    {
        status.store (Status::Failed);
        const juce::ScopedLock sl (nameLock);
        error = "No audio driver on this system";
        return;
    }

    type->scanForDevices();
    for (auto& name : type->getDeviceNames (true))
        if (DeviceLink::looksLikeModelCycles (name))
        {
            open (name);
            return;
        }
}

void AudioLink::open (const juce::String& name)
{
    auto fail = [this] (const juce::String& why)
    {
        device.reset();
        status.store (Status::Failed);
        const juce::ScopedLock sl (nameLock);
        error = why;
    };

    device.reset (type->createDevice ({}, name));
    if (device == nullptr) return fail ("Can't open " + name);

    const int numIn = juce::jmin (StreamBuffer::maxChannels, device->getInputChannelNames().size());
    if (numIn <= 0) return fail (name + " has no audio inputs (set USB MODE to Audio + MIDI)");

    juce::BigInteger ins;
    ins.setRange (0, numIn, true);
    const auto rates = device->getAvailableSampleRates();
    const double rate = rates.contains (48000.0) ? 48000.0 : (rates.isEmpty() ? 0.0 : rates[0]);
    const auto err = device->open (ins, {}, rate, device->getDefaultBufferSize());
    if (err.isNotEmpty()) return fail (err);

    {
        const juce::SpinLock::ScopedLockType sl (readLock);
        streaming.store (false);
        stream.reset (device->getActiveInputChannels().countNumberOfSetBits(),
                      device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    }
    inputLatency.store (device->getInputLatencyInSamples());
    lost.store (false);
    lastCallback.store (juce::Time::getMillisecondCounter());
    {
        const juce::ScopedLock sl (nameLock);
        deviceName = name;
        error.clear();
    }

    device->start (this);
    streaming.store (true);
    status.store (Status::Streaming);
    if (onFormatChanged) onFormatChanged();
}

void AudioLink::close()
{
    if (device == nullptr) return;
    device->stop();
    device->close();
    {
        const juce::SpinLock::ScopedLockType sl (readLock);
        streaming.store (false);
    }
    device.reset();
}

int AudioLink::read (float* const* dest, int numSamples, double hostRate) noexcept
{
    const juce::SpinLock::ScopedTryLockType tl (readLock);
    if (! tl.isLocked() || ! streaming.load()) return 0;
    return stream.read (dest, numSamples, hostRate) ? stream.getNumChannels() : 0;
}

void AudioLink::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                  int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    lastCallback.store (juce::Time::getMillisecondCounter());
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], numSamples);
    if (streaming.load())
        stream.write (in, numIn, numSamples);
}
