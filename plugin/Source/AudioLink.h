#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

/**
    Device audio → DAW audio, the part of Overbridge that streams the device's
    sound into the plugin's outputs.

    The device runs on its own 48 kHz clock and the DAW on another, so the
    samples go through a FIFO and are resampled to the DAW's rate with a ratio
    that is nudged to keep the FIFO at a fixed fill: the delay stays constant
    and can be reported to the DAW for delay compensation.

    Single producer (device callback) / single consumer (processBlock).
*/
class StreamBuffer
{
public:
    static constexpr int maxChannels = 6;

    /** Not while write() or read() can run. */
    void reset (int numChannels, double deviceRate, int deviceBlock);
    /** The DAW's rate and largest block; sets the FIFO target (the delay). Not while read() can run. */
    void configure (double hostRate, int hostMaxBlock);

    /** Device side: append numSamples frames. Returns false (and drops them) if the FIFO is full. */
    bool write (const float* const* data, int numChannels, int numSamples) noexcept;

    /** DAW side: produce numSamples at hostRate into dest[0..numChannels). Returns false (silence) while
        the FIFO fills up after a start or an underrun. */
    bool read (float* const* dest, int numSamples, double hostRate) noexcept;

    int getNumChannels() const noexcept { return channels; }
    double getDeviceRate() const noexcept { return deviceRate; }

    /** FIFO fill the reader holds, in device samples. */
    int getTargetFill() const noexcept { return target.load(); }
    /** Delay from capture to output, in device samples (FIFO target + resampler). */
    double getDelayDeviceSamples() const noexcept { return target.load() + (wideBand ? 100.0 : 2.0); }
    /** Current ratio correction (drift between the two clocks), e.g. 1.0e-4 = 100 ppm. */
    double getDrift() const noexcept { return drift.load(); }

    std::atomic<int> underruns { 0 }, overruns { 0 };

private:
    juce::AbstractFifo fifo { 1 << 15 };
    juce::AudioBuffer<float> ring { maxChannels, 1 << 15 }, scratch { maxChannels, 1 << 14 };
    juce::Interpolators::Lagrange lagrange[maxChannels];
    juce::Interpolators::WindowedSinc sinc[maxChannels];

    int channels = 0, deviceBlock = 256, hostMaxBlock = 512;
    double deviceRate = 48000.0, hostRate = 48000.0, avgFill = 0.0, integral = 0.0;
    bool primed = false, wideBand = false;
    std::atomic<int> target { 512 };
    std::atomic<double> drift { 0.0 };
};

/**
    Finds the Model:Cycles' USB audio interface and streams it: 2 channels (the
    stereo mix, stock or 2CH mode) or 6 channels (one per track, the 6CH mode of
    the Modded Cycles firmware). Reconnects when the device is replugged or
    switches between 2CH and 6CH.
*/
class AudioLink : private juce::Timer,
                  private juce::AudioIODeviceCallback
{
public:
    enum class Status { Off, Searching, Streaming, Failed };

    AudioLink();
    ~AudioLink() override;

    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled.load(); }
    void shutdown();

    Status getStatus() const noexcept { return status.load(); }
    int getNumChannels() const noexcept { return streaming.load() ? stream.getNumChannels() : 0; }
    juce::String getDeviceName() const;
    /** Open, but no audio from the device for a moment (the Cycles pauses its USB audio while it loads a project). */
    bool isStalled() const noexcept
    {
        return device != nullptr && juce::Time::getMillisecondCounter() - lastCallback.load() > 150;
    }
    juce::String getError() const;

    /** Delay from the device's audio to the plugin's output, in seconds (USB input + FIFO + resampler). */
    double getLatencySeconds() const noexcept;

    /** The DAW's rate and largest block (prepareToPlay). */
    void prepare (double hostRate, int maxBlock)
    {
        const juce::SpinLock::ScopedLockType sl (readLock);
        stream.configure (hostRate, maxBlock);
    }

    /** Audio thread: fills dest[0..getNumChannels()) and returns the channel count, or 0 (nothing to play). */
    int read (float* const* dest, int numSamples, double hostRate) noexcept;

    StreamBuffer& getStream() noexcept { return stream; }

    /** Tests: stream from getStream().write() as if a device with this many channels were open. */
    void startTestStream (int numChannels, double rate, int block)
    {
        enabled.store (true);
        {
            const juce::SpinLock::ScopedLockType sl (readLock);
            stream.reset (numChannels, rate, block);
        }
        streaming.store (true);
        status.store (Status::Streaming);
        if (onFormatChanged) onFormatChanged();
    }

    /** Message thread: the stream started, stopped or changed format (channel count, latency). */
    std::function<void()> onFormatChanged;

private:
    void timerCallback() override;
    void open (const juce::String& name);
    void close();

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override { lost.store (true); }
    void audioDeviceError (const juce::String&) override { lost.store (true); }

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    StreamBuffer stream;
    juce::SpinLock readLock; // the reader holds it; open/close take it to reset the stream

    std::atomic<bool> enabled { false }, streaming { false }, lost { false };
    std::atomic<Status> status { Status::Off };
    std::atomic<juce::uint32> lastCallback { 0 };
    std::atomic<int> inputLatency { 0 };
    mutable juce::CriticalSection nameLock;
    juce::String deviceName, error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLink)
};
