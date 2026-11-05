#include "ElektronFs.h"

namespace
{
    const juce::uint8 header[] = { 0x00, 0x20, 0x3c, 0x10, 0x00 }; // after F0

    juce::uint32 be32 (const juce::uint8* p) { return (juce::uint32) p[0] << 24 | (juce::uint32) p[1] << 16 | (juce::uint32) p[2] << 8 | p[3]; }
    void put32 (juce::MemoryOutputStream& o, juce::uint32 v)
    {
        o.writeByte ((char) (v >> 24)); o.writeByte ((char) (v >> 16)); o.writeByte ((char) (v >> 8)); o.writeByte ((char) v);
    }
    void putPath (juce::MemoryOutputStream& o, const juce::String& path)
    {
        o.write (path.toRawUTF8(), path.getNumBytesAsUTF8());
        o.writeByte (0);
    }
}

// 8 bytes on the wire for every 7: first a byte with their top bits (the first byte's in bit 6), then the 7 bytes
// without them.
juce::MemoryBlock ElektronFs::encode7 (const juce::uint8* src, size_t n)
{
    juce::MemoryBlock out (n + (n + 6) / 7, true);
    auto* o = static_cast<juce::uint8*> (out.getData());
    for (size_t i = 0, j = 0; j < n; i += 8, j += 7)
    {
        juce::uint8 hi = 0;
        for (size_t k = 0; k < 7; ++k)
        {
            hi = (juce::uint8) (hi << 1);
            if (j + k < n)
            {
                if (src[j + k] & 0x80) hi |= 1;
                o[i + k + 1] = src[j + k] & 0x7f;
            }
        }
        o[i] = hi;
    }
    return out; // the last group is only as long as its bytes: n + ceil (n / 7) in all
}

juce::MemoryBlock ElektronFs::decode7 (const juce::uint8* src, size_t n)
{
    juce::MemoryBlock out;
    juce::MemoryOutputStream o (out, false);
    for (size_t i = 0; i < n; i += 8)
    {
        juce::uint8 bit = 0x40;
        for (size_t k = 0; k < 7 && i + k + 1 < n; ++k, bit >>= 1)
            o.writeByte ((char) (src[i + k + 1] | ((src[i] & bit) ? 0x80 : 0)));
    }
    o.flush();
    return out;
}

juce::MidiMessage ElektronFs::frame (const juce::MemoryBlock& payload)
{
    const auto enc = encode7 (static_cast<const juce::uint8*> (payload.getData()), payload.getSize());
    juce::MemoryBlock body;
    body.append (header, sizeof (header));
    body.append (enc.getData(), enc.getSize());
    return juce::MidiMessage::createSysExMessage (body.getData(), (int) body.getSize());
}

bool ElektronFs::handleSysEx (const juce::uint8* d, int n)
{
    // d excludes F0 / F7
    if (n < (int) sizeof (header) + 6 || std::memcmp (d, header, sizeof (header)) != 0) return false;
    auto msg = decode7 (d + sizeof (header), (size_t) n - sizeof (header));
    if (msg.getSize() < 5) return true;
    const auto* p = static_cast<const juce::uint8*> (msg.getData());
    const juce::ScopedLock sl (lock);
    if (waiting && (juce::uint16) (p[2] << 8 | p[3]) == waitSeq && p[4] == waitType)
    {
        reply = std::move (msg);
        waiting = false;
        answered.signal();
    }
    return true;
}

void ElektronFs::cancel()
{
    cancelled.store (true);
    answered.signal();
}

juce::MemoryBlock ElektronFs::request (const juce::MemoryBlock& body, int timeoutMs)
{
    if (body.isEmpty() || send == nullptr) return {};
    // wait while the device is busy (holdOff), at most 30 s
    for (int i = 0; i < 600 && (juce::int32) (quietUntil.load() - juce::Time::getMillisecondCounter()) > 0; ++i)
    {
        if (cancelled.load()) return {};
        juce::Thread::sleep (50);
    }
    juce::MemoryBlock payload;
    {
        const juce::ScopedLock sl (lock);
        waitSeq = seq++;
        waitType = (juce::uint8) (static_cast<const juce::uint8*> (body.getData())[0] | 0x80);
        waiting = true;
        reply.reset();
        answered.reset();
        const juce::uint8 h[] = { (juce::uint8) (waitSeq >> 8), (juce::uint8) waitSeq, 0, 0 };
        payload.append (h, 4);
        payload.append (body.getData(), body.getSize());
    }
    cancelled.store (false);
    send (frame (payload));
    answered.wait (timeoutMs);
    juce::MemoryBlock r;
    {
        const juce::ScopedLock sl (lock);
        waiting = false;
        if (! cancelled.load()) r = std::move (reply);
    }
    if (logFile != juce::File())
        log (juce::String::toHexString (body.getData(), (int) juce::jmin ((size_t) 24, body.getSize())) + "  ->  "
             + (r.isEmpty() ? juce::String ("no answer")
                            : juce::String ((int) r.getSize()) + " bytes: "
                                  + juce::String::toHexString (r.getData(), (int) juce::jmin ((size_t) 32, r.getSize()))));
    return r;
}

void ElektronFs::setLogFile (const juce::File& f)
{
    logFile = f;
    logLines = 0;
    f.getParentDirectory().createDirectory();
    f.replaceWithText ("Modded_f-king_cycles: file-transfer requests and answers (hex)\n");
}

void ElektronFs::log (const juce::String& line)
{
    if (++logLines > 400) return;
    logFile.appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + line + "\n");
}

// One entry: hash (4), size (4), write-protected (1), type ('D' folder, 'F' file), name, NUL (the OS's readdir
// answer, 0x40088af4..0x40088cc8). The list starts at [5] or [6] (a status byte): take the one that parses whole.
bool ElektronFs::parseEntries (const juce::uint8* p, size_t n, std::vector<Entry>& out)
{
    for (size_t start : { (size_t) 5, (size_t) 6 })
    {
        std::vector<Entry> list;
        size_t i = start;
        bool ok = start <= n;
        while (ok && i < n)
        {
            if (i + 11 > n || (p[i + 9] != 'D' && p[i + 9] != 'F')) { ok = false; break; }
            Entry e;
            e.hash = be32 (p + i);
            e.size = be32 (p + i + 4);
            e.folder = p[i + 9] == 'D';
            size_t j = i + 10;
            while (j < n && p[j] != 0) ++j;
            if (j >= n || j == i + 10) { ok = false; break; }
            e.name = juce::String::fromUTF8 (reinterpret_cast<const char*> (p + i + 10), (int) (j - i - 10));
            list.push_back (e);
            i = j + 1;
        }
        if (ok)
        {
            out = std::move (list);
            return true;
        }
    }
    return false;
}

bool ElektronFs::listFolder (const juce::String& path, std::vector<Entry>& out)
{
    juce::MemoryBlock body;
    {
        juce::MemoryOutputStream o (body, false);
        o.writeByte (0x14);
        putPath (o, path);
    }
    const auto r = request (body);
    return r.getSize() >= 5 && parseEntries (static_cast<const juce::uint8*> (r.getData()), r.getSize(), out);
}

bool ElektronFs::openRead (const juce::String& path, juce::uint32& id, juce::uint32& fileSize)
{
    juce::MemoryBlock body;
    {
        juce::MemoryOutputStream o (body, false);
        o.writeByte (0x33);
        putPath (o, path);
    }
    // [5] status (non-zero = ok), [6..9] the reader's id, [10..13] the file's size (OS 1.13: 0x400ecd00)
    const auto r = request (body);
    const auto* p = static_cast<const juce::uint8*> (r.getData());
    if (r.getSize() < 14 || p[5] == 0) return false;
    id = be32 (p + 6);
    fileSize = be32 (p + 10);
    return true;
}

bool ElektronFs::readChunk (juce::uint32 id, juce::uint32 start, juce::uint32 size, juce::MemoryBlock& append)
{
    for (juce::uint32 at = start; at < start + size; )
    {
        const juce::uint32 want = juce::jmin (chunk.load(), start + size - at);
        juce::MemoryBlock rb;
        {
            juce::MemoryOutputStream o (rb, false);
            o.writeByte (0x35);
            put32 (o, id);
            put32 (o, want);   // size before start (seen on the Model:Cycles, 01/11/2026)
            put32 (o, at);
        }
        const auto d = request (rb);
        // the answer: status, id, size, start, size read (22 bytes), then the data
        const int head = readHeader.load();
        juce::uint32 got = 0;
        if (d.getSize() >= 5 + (size_t) want && (head < 0 || d.getSize() == (size_t) head + want))
        {
            got = want;
            if (head < 0) readHeader.store ((int) (d.getSize() - want));
        }
        else if (head >= 5 && d.getSize() > (size_t) head)
            got = (juce::uint32) (d.getSize() - (size_t) head); // less than asked: ask less from now on
        if (got == 0 || got > want)
        {
            // no answer, or one we can't read: try again asking for less, down to the size a header read uses
            if (want <= minChunk || cancelled.load()) return false;
            chunk.store (juce::jmax (minChunk, want / 2));
            continue;
        }
        if (got < want) chunk.store (juce::jmax (minChunk, got));
        append.append (static_cast<const juce::uint8*> (d.getData()) + d.getSize() - got, got);
        at += got;
    }
    return true;
}

void ElektronFs::closeRead (juce::uint32 id)
{
    juce::MemoryBlock cb;
    {
        juce::MemoryOutputStream o (cb, false);
        o.writeByte (0x34);
        put32 (o, id);
    }
    request (cb, 1000);
}

bool ElektronFs::readFile (const juce::String& path, juce::uint32 start, juce::uint32 size, juce::MemoryBlock& out,
                           const std::function<bool (float)>& progress)
{
    out.reset();
    juce::uint32 id = 0, fileSize = 0;
    if (! openRead (path, id, fileSize)) return false;
    const juce::uint32 end = juce::jmin (start + size, fileSize);
    bool ok = true;
    for (juce::uint32 at = start; at < end && ok; )
    {
        const juce::uint32 want = juce::jmin (maxChunk, end - at);
        ok = readChunk (id, at, want, out);
        at += want;
        if (ok && progress && ! progress ((float) (at - start) / (float) juce::jmax (1u, end - start))) ok = false;
    }
    closeRead (id);
    return ok;
}
