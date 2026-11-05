#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <vector>

/**
    The Model:Cycles' file system over USB, through Elektron's file-transfer messages (the ones Transfer uses):
    list a folder of the +Drive and read a file back. Read-only on purpose: samples get onto the device with the
    Transfer app (m_reilly, 29/10/2026).

    Every message is F0 00 20 3C 10 00 <payload packed 7 bits in 8> F7. Decoded payload: [0..1] the request's
    sequence number, [2..3] 0 in a request (the request's sequence in the response), [4] the command (response:
    command | 0x80), then its body. The stock Cycles OS answers the raw file-system family (0x14 list, 0x33 open,
    0x35 read, 0x34 close) in its own Transfer mode, so nothing has to be switched on the device (firmware notes/93).

    Written from the protocol facts (firmware repo: docs/flasher/flasher.js for the framing, the OS 1.13 code for the
    commands); no code from Elektroid (GPL).

    request() blocks: call it from a background thread, never from the message or audio thread.
*/
class ElektronFs
{
public:
    struct Entry
    {
        juce::String name;      // file or folder name, no path
        juce::uint32 hash = 0;  // the +Drive's file hash (what the firmware's "SMP" sound names carry)
        juce::uint32 size = 0;  // bytes
        bool folder = false;
    };

    /** Sends a whole SysEx message (F0 ... F7) to the device. */
    std::function<void (const juce::MidiMessage&)> send;

    /** Feed every SysEx from the device here (any thread). Returns true if it was a file-transfer message. */
    bool handleSysEx (const juce::uint8* data, int size);

    /** One request: body = command byte + fields. Returns the decoded response payload, or empty on timeout. */
    juce::MemoryBlock request (const juce::MemoryBlock& body, int timeoutMs = 3000);

    bool listFolder (const juce::String& path, std::vector<Entry>& out);
    /** Reads [start, start + size) of a file, in chunks; progress (0..1) may be null. */
    bool readFile (const juce::String& path, juce::uint32 start, juce::uint32 size, juce::MemoryBlock& out,
                   const std::function<bool (float)>& progress = {});

    /** The same in steps: open a file (its reader id and size), read ranges of it, close it. */
    bool openRead (const juce::String& path, juce::uint32& id, juce::uint32& fileSize);
    bool readChunk (juce::uint32 id, juce::uint32 start, juce::uint32 size, juce::MemoryBlock& append);
    void closeRead (juce::uint32 id);

    /** No request goes out for the next ms milliseconds (requests wait): the Cycles is busy, e.g. loading a project,
        and reading its +Drive at the same time is not safe. */
    void holdOff (int ms) noexcept
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        if ((juce::int32) (until - quietUntil.load()) > 0) quietUntil.store (until);
    }

    /** Cancels a request in flight (it returns empty). */
    void cancel();

    static constexpr juce::uint32 maxChunk = 0x2000, minChunk = 64;

    /** Optional: every request and the start of its answer go to this file (the first 400), to see what the device
        really answers. */
    void setLogFile (const juce::File& f);

    // Exposed for the tests
    static juce::MemoryBlock encode7 (const juce::uint8*, size_t);
    static juce::MemoryBlock decode7 (const juce::uint8*, size_t);
    static juce::MidiMessage frame (const juce::MemoryBlock& payload);
    static bool parseEntries (const juce::uint8* p, size_t n, std::vector<Entry>& out);

private:
    juce::CriticalSection lock;
    juce::WaitableEvent answered;
    juce::uint16 seq = 0, waitSeq = 0;
    juce::uint8 waitType = 0;
    bool waiting = false;
    juce::MemoryBlock reply;
    std::atomic<bool> cancelled { false };
    std::atomic<juce::uint32> quietUntil { 0 };

    // A read's answer is a header then the data. The header's length is learned from the first full answer; a
    // device that sends less than asked (a smaller buffer) then still gives its data, and the next reads ask less.
    std::atomic<int> readHeader { -1 };
    std::atomic<juce::uint32> chunk { maxChunk };

    juce::File logFile;
    int logLines = 0;
    void log (const juce::String& line);
};
