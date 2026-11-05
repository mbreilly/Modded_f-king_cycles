#include "SampleLibrary.h"

namespace
{
    juce::uint32 be32 (const juce::uint8* p) { return (juce::uint32) p[0] << 24 | (juce::uint32) p[1] << 16 | (juce::uint32) p[2] << 8 | p[3]; }
    juce::int16 be16 (const juce::uint8* p) { return (juce::int16) (p[0] << 8 | p[1]); }
    juce::String hex (juce::uint32 h) { return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 8).toUpperCase(); }

    constexpr int maxFiles = 512, maxDepth = 3;
    constexpr juce::uint32 headerBytes = 64, windowBytes = 2048;
}

SampleLibrary::SampleLibrary (juce::File dir) : juce::Thread ("Model:Cycles samples"), cacheDir (std::move (dir))
{
    fs.setLogFile (cacheDir.getChildFile ("transfer-log.txt"));
    startThread (juce::Thread::Priority::low);
}

SampleLibrary::~SampleLibrary()
{
    signalThreadShouldExit();
    fs.cancel();
    work.signal();
    stopThread (4000);
}

juce::File SampleLibrary::defaultCacheDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Modded_f-king_cycles").getChildFile ("Samples");
}

void SampleLibrary::refresh()
{
    const juce::ScopedLock sl (lock);
    refreshWanted = true;
    failed.clear();     // asked by the user: try the samples that could not be read again
    work.signal();
}

std::vector<SampleLibrary::Sample> SampleLibrary::getSamples() const
{
    const juce::ScopedLock sl (lock);
    return samples;
}

const SampleLibrary::Sample* SampleLibrary::findByHash (juce::uint32 hash, Sample& out) const
{
    const juce::ScopedLock sl (lock);
    for (auto& s : samples)
        if (s.hash == hash) { out = s; return &out; }
    return nullptr;
}

std::shared_ptr<const SampleLibrary::Wave> SampleLibrary::getWave (juce::uint32 hash)
{
    if (hash == 0) return {};
    const juce::ScopedLock sl (lock);
    if (auto it = waves.find (hash); it != waves.end()) return it->second;
    if (std::find (wanted.begin(), wanted.end(), hash) == wanted.end() && failed[hash] < 2)
    {
        wanted.push_back (hash);
        work.signal();
    }
    return {};
}

juce::String SampleLibrary::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void SampleLibrary::setStatus (const juce::String& s)
{
    {
        const juce::ScopedLock sl (lock);
        status = s;
    }
    ++serial;
}

bool SampleLibrary::headerOk (const juce::uint8* p, size_t n, int& numSamples)
{
    if (n < headerBytes || be32 (p + 8) != 48000 || be32 (p + 4) == 0) return false;
    numSamples = (int) (be32 (p + 4) >> 1);
    return true;
}

std::shared_ptr<SampleLibrary::Wave> SampleLibrary::waveFromFile (const juce::uint8* f, size_t size)
{
    int n = 0;
    if (! headerOk (f, size, n)) return {};
    n = juce::jmin (n, (int) ((size - headerBytes) / 2));
    auto w = std::make_shared<Wave>();
    w->numSamples = n;
    w->pcm.resize ((size_t) n);
    for (int i = 0; i < n; ++i) w->pcm[(size_t) i] = be16 (f + headerBytes + 2 * (size_t) i);
    w->lo.assign (columns, 0);
    w->hi.assign (columns, 0);
    for (int c = 0; c < columns && n > 0; ++c)
    {
        int a = (int) ((juce::int64) n * c / columns), b = (int) ((juce::int64) n * (c + 1) / columns);
        b = juce::jlimit (a + 1, n, b);
        a = juce::jmin (a, n - 1);
        juce::int16 lo = 32767, hi = -32768;
        for (int i = a; i < b; ++i) { lo = juce::jmin (lo, w->pcm[(size_t) i]); hi = juce::jmax (hi, w->pcm[(size_t) i]); }
        w->lo[(size_t) c] = lo;
        w->hi[(size_t) c] = hi;
    }
    return w;
}

void SampleLibrary::run()
{
    while (! threadShouldExit())
    {
        work.wait (1000);
        if (threadShouldExit()) break;
        bool doList = false;
        juce::uint32 hash = 0;
        {
            const juce::ScopedLock sl (lock);
            doList = refreshWanted;
            refreshWanted = false;
            if (! doList && ! wanted.empty())
            {
                hash = wanted.front();
                wanted.erase (wanted.begin());
            }
        }
        busy.store (doList || hash != 0);
        if (doList) doRefresh();
        else if (hash != 0) doWave (hash);
        busy.store (false);
        progress.store (0.0f);
        const juce::ScopedLock sl (lock);
        if (refreshWanted || ! wanted.empty()) work.signal();
    }
}

void SampleLibrary::doRefresh()
{
    setStatus ("Reading the +Drive...");
    std::vector<Sample> found;
    std::vector<std::pair<juce::String, int>> folders { { "/", 0 } };
    std::vector<std::pair<juce::String, ElektronFs::Entry>> files;
    bool anyAnswer = false;
    while (! folders.empty() && (int) files.size() < maxFiles && ! threadShouldExit())
    {
        auto [path, depth] = folders.back();
        folders.pop_back();
        std::vector<ElektronFs::Entry> entries;
        if (! fs.listFolder (path, entries)) continue;
        anyAnswer = true;
        for (auto& e : entries)
        {
            const auto full = (path.endsWith ("/") ? path : path + "/") + e.name;
            if (e.folder) { if (depth + 1 < maxDepth) folders.push_back ({ full, depth + 1 }); }
            else if (e.size > headerBytes) files.push_back ({ full, e });
        }
    }
    if (! anyAnswer)
    {
        setStatus ("No answer from the device (connect it by USB, connection Auto-detect or Choose MIDI ports)");
        return;
    }
    // keep only the sample files: their header says 48 kHz
    for (size_t i = 0; i < files.size() && ! threadShouldExit(); ++i)
    {
        progress.store ((float) i / (float) files.size());
        const auto& [path, e] = files[i];
        Sample s { path, e.name, e.hash, e.size, 0 };
        juce::MemoryBlock head;
        if (fs.readFile (path, 0, headerBytes, head)
            && headerOk (static_cast<const juce::uint8*> (head.getData()), head.getSize(), s.numSamples))
            found.push_back (s);
    }
    std::sort (found.begin(), found.end(), [] (const Sample& a, const Sample& b) { return a.path.compareNatural (b.path) < 0; });
    {
        const juce::ScopedLock sl (lock);
        samples = std::move (found);
    }
    setStatus (juce::String ((int) getSamples().size()) + " samples on the +Drive");
}

void SampleLibrary::doWave (juce::uint32 hash)
{
    if (auto cached = loadCache (hash))
    {
        const juce::ScopedLock sl (lock);
        waves[hash] = cached;
        ++serial;
        return;
    }
    Sample s;
    if (findByHash (hash, s) == nullptr)
    {
        // not in the list (the list is read once, or the sample came after): read it again, at most every 30 s,
        // so a track whose sample isn't on the +Drive doesn't keep the list reading forever
        const auto now = juce::Time::getMillisecondCounter();
        if (lastAutoRefresh == 0 || now - lastAutoRefresh > 30000)
        {
            lastAutoRefresh = now;
            doRefresh();
        }
        if (findByHash (hash, s) == nullptr)
        {
            const juce::ScopedLock sl (lock);
            failed[hash] = 2;
            status = "Sample " + hex (hash) + " is not on the +Drive";
            ++serial;
            return;
        }
    }
    setStatus ("Reading " + s.name + "...");
    std::shared_ptr<Wave> w;
    auto keepGoing = [this] (float p) { progress.store (p); return ! threadShouldExit(); };
    if (s.size <= wholeLimit)
    {
        juce::MemoryBlock file;
        if (fs.readFile (s.path, 0, s.size, file, keepGoing))
            w = waveFromFile (static_cast<const juce::uint8*> (file.getData()), file.getSize());
    }
    else
    {
        // a long sample: one window of 1 024 samples per column, min / max of each
        w = std::make_shared<Wave>();
        w->numSamples = s.numSamples;
        w->lo.assign (columns, 0);
        w->hi.assign (columns, 0);
        const juce::uint32 pcmBytes = (juce::uint32) s.numSamples * 2;
        juce::uint32 id = 0, fileSize = 0;
        const bool opened = fs.openRead (s.path, id, fileSize);
        if (! opened) w.reset();
        for (int c = 0; c < columns && w != nullptr; ++c)
        {
            if (! keepGoing ((float) c / columns)) { w.reset(); break; }
            const juce::uint32 at = headerBytes + (juce::uint32) ((juce::uint64) pcmBytes * (juce::uint64) c / columns) / 2 * 2;
            const juce::uint32 end = juce::jmin (headerBytes + pcmBytes, fileSize);
            juce::MemoryBlock win;
            if (at >= end || ! fs.readChunk (id, at, juce::jmin (windowBytes, end - at), win)) { w.reset(); break; }
            const auto* p = static_cast<const juce::uint8*> (win.getData());
            juce::int16 lo = 32767, hi = -32768;
            for (size_t i = 0; i + 1 < win.getSize(); i += 2) { lo = juce::jmin (lo, be16 (p + i)); hi = juce::jmax (hi, be16 (p + i)); }
            w->lo[(size_t) c] = lo;
            w->hi[(size_t) c] = hi;
        }
        if (opened) fs.closeRead (id);
    }
    const juce::ScopedLock sl (lock);
    if (w != nullptr)
    {
        waves[hash] = w;
        saveCache (hash, *w);
        status = s.name + " read";
    }
    else
    {
        ++failed[hash];
        status = "Could not read " + s.name;
    }
    ++serial;
}

// Cache file: "MCW1", samples, columns, lo[], hi[], pcm count, pcm[] (all little-endian)
bool SampleLibrary::saveCache (juce::uint32 hash, const Wave& w) const
{
    if (! cacheDir.createDirectory()) return false;
    juce::FileOutputStream o (cacheDir.getChildFile (hex (hash) + ".mcw"));
    if (! o.openedOk()) return false;
    o.setPosition (0);
    o.truncate();
    o.write ("MCW1", 4);
    o.writeInt (w.numSamples);
    o.writeInt ((int) w.lo.size());
    for (auto v : w.lo) o.writeShort (v);
    for (auto v : w.hi) o.writeShort (v);
    o.writeInt ((int) w.pcm.size());
    for (auto v : w.pcm) o.writeShort (v);
    return o.getStatus().wasOk();
}

std::shared_ptr<SampleLibrary::Wave> SampleLibrary::loadCache (juce::uint32 hash) const
{
    juce::FileInputStream in (cacheDir.getChildFile (hex (hash) + ".mcw"));
    if (! in.openedOk()) return {};
    char magic[4] {};
    if (in.read (magic, 4) != 4 || std::memcmp (magic, "MCW1", 4) != 0) return {};
    auto w = std::make_shared<Wave>();
    w->numSamples = in.readInt();
    const int cols = in.readInt();
    if (cols <= 0 || cols > 65536 || w->numSamples <= 0) return {};
    w->lo.resize ((size_t) cols);
    w->hi.resize ((size_t) cols);
    for (auto& v : w->lo) v = in.readShort();
    for (auto& v : w->hi) v = in.readShort();
    const int n = in.readInt();
    if (n < 0 || n > (1 << 24)) return {};
    w->pcm.resize ((size_t) n);
    for (auto& v : w->pcm) v = in.readShort();
    return in.getPosition() == in.getTotalLength() ? w : nullptr;
}
