#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include "ElektronFs.h"

/**
    The samples on the Model:Cycles' +Drive, for the SAMPLER / GRANULAR / WAVETABLE window (firmware notes/93):
    the list of sample files (read from the device, each one's header checked), and each sample's waveform, read
    back once and kept in a cache folder by its hash. All device work runs on this object's own thread.

    A sample file: a 64-byte header (+4 the PCM's length in bytes, +8 the rate, 48 000) then 16-bit big-endian mono
    PCM, as the firmware's loader reads it (smp.c, smp_load).
*/
class SampleLibrary : private juce::Thread
{
public:
    struct Sample
    {
        juce::String path, name;
        juce::uint32 hash = 0, size = 0;
        int numSamples = 0;
    };

    /** A waveform: columns (min, max) over the whole sample, and the PCM itself when it is short enough. */
    struct Wave
    {
        int numSamples = 0;
        std::vector<juce::int16> lo, hi;
        std::vector<juce::int16> pcm; // whole sample (<= wholeLimit bytes), else empty
    };

    static constexpr int columns = 1024;
    static constexpr juce::uint32 wholeLimit = 2u << 20; // 2 MB = 21.8 s: read whole, else sampled windows

    explicit SampleLibrary (juce::File cacheDir = defaultCacheDir());
    ~SampleLibrary() override;

    ElektronFs fs;

    /** Read the +Drive's list again (message thread). */
    void refresh();
    std::vector<Sample> getSamples() const;
    const Sample* findByHash (juce::uint32 hash, Sample& out) const;

    /** The waveform of a sample, or null while it is being read (asks for it). */
    std::shared_ptr<const Wave> getWave (juce::uint32 hash);

    juce::String getStatus() const;
    float getProgress() const noexcept { return progress.load(); }
    bool isBusy() const noexcept { return busy.load(); }
    /** Bumped whenever the list or a waveform changes, for the UI. */
    int getSerial() const noexcept { return serial.load(); }

    static juce::File defaultCacheDir();

    // Exposed for the tests
    void addForTests (const Sample& s, std::shared_ptr<const Wave> w)
    {
        const juce::ScopedLock sl (lock);
        samples.push_back (s);
        if (w != nullptr) waves[s.hash] = std::move (w);
        ++serial;
    }
    static std::shared_ptr<Wave> waveFromFile (const juce::uint8* file, size_t size);
    static bool headerOk (const juce::uint8* p, size_t n, int& numSamples);

private:
    void run() override;
    void doRefresh();
    void doWave (juce::uint32 hash);
    void setStatus (const juce::String&);
    bool saveCache (juce::uint32 hash, const Wave&) const;
    std::shared_ptr<Wave> loadCache (juce::uint32 hash) const;

    juce::File cacheDir;
    mutable juce::CriticalSection lock;
    std::vector<Sample> samples;
    std::map<juce::uint32, std::shared_ptr<const Wave>> waves;
    std::vector<juce::uint32> wanted;      // waveforms asked for, not read yet
    std::map<juce::uint32, int> failed;    // hash -> tries
    bool refreshWanted = false;
    juce::uint32 lastAutoRefresh = 0;      // ms counter of the last list read asked by a missing waveform
    juce::String status { "Not read yet" };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> busy { false };
    std::atomic<int> serial { 0 };
    juce::WaitableEvent work;
};
