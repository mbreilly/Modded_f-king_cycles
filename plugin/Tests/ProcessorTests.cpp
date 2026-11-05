// Headless checks of the sync logic. Build with -DBUILD_TESTS=ON, run Modded Cycles Techno Edition tests (ModelCyclesCC_Tests) [snapshotDir]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <map>

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

struct Cc { int ch, cc, val; };

static std::vector<Cc> ccsIn (const juce::MidiBuffer& b)
{
    std::vector<Cc> out;
    for (const auto m : b)
    {
        auto msg = m.getMessage();
        if (msg.isController()) out.push_back ({ msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue() });
    }
    return out;
}

static std::vector<Cc> runBlock (CyclesProcessor& p, juce::MidiBuffer in = {}, int samples = 4096)
{
    juce::AudioBuffer<float> audio (2, samples);
    p.processBlock (audio, in);
    return ccsIn (in);
}

// 250 ms: the macOS CI runner missed async updates within 60 ms
static void pump (int ms = 250) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void setParam (CyclesProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static float getParam (CyclesProcessor& p, const juce::String& id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

struct TestPlayHead : juce::AudioPlayHead
{
    juce::AudioPlayHead::PositionInfo info;
    juce::Optional<PositionInfo> getPosition() const override { return info; }
};

struct GestureCounter : juce::AudioProcessorListener
{
    int begins = 0, ends = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
};

static std::unique_ptr<CyclesProcessor> makeProcessor()
{
    auto p = std::make_unique<CyclesProcessor>();
    p->setLinkMode (DeviceLink::Mode::Host); // talk through processBlock in tests
    p->prepareToPlay (48000.0, 4096);
    return p;
}

static juce::String snapshotDir;

int main (int argc, char** argv)
{
    if (argc > 1) snapshotDir = argv[1];
    juce::ScopedJuceInitialiser_GUI init;

    auto pp = makeProcessor();
    auto& p = *pp;

    CHECK (cycles::allParams().size() == 6 * 39 + 4 + 3 + 11); // + master EQ low, mid, high; sidechain: source, depth, attack, hold, release, duck T1-T6
    {   // list names: a knob's list is stepped, an extra's spans 0-127 (CLSTR Mode: value x 9 >> 7)
        const auto& defs = cycles::trackDefs();
        auto def = [&] (const char* key) -> const cycles::Def& { return *std::find_if (defs.begin(), defs.end(), [&] (auto& d) { return d.key == key; }); };
        CHECK (modded::numMachines == 25 && juce::String (modded::machines[13].name) == "BIAS" && juce::String (modded::machines[14].name) == "ACID");
        CHECK (cycles::dialText (def ("shape"), 2, 9) == "HT"); // 909 Voice
        CHECK (cycles::dialText (def ("shape"), 9, 9) == "SD"); // the 909 snare
        CHECK (juce::String (modded::machines[7].knobs[3].name) == "Accent" && juce::String (modded::machines[7].extras[0]) == "Drift"); // 808BD
        // per-value names (notes/90): BASSLINE Wave zones, XMOD ratio steps, names only on the pure spots of a morph
        CHECK (juce::String (modded::machines[18].name) == "XMOD" && juce::String (modded::machines[6].name) == "909BD");
        CHECK (cycles::dialText (def ("shape"), 0, 18) == "0.25" && cycles::dialText (def ("shape"), 127, 18) == "7");
        // a new plugin starts with EMPTY on every track, like a new kit on the modded firmware
        CHECK (cycles::emptyMachine() == 24 && juce::String (modded::machines[24].name) == "EMPTY");
        for (auto& info : cycles::allParams())
            if (info.def->key == "machine") CHECK (info.defaultValue == 24);
    }
    pump();
    CHECK (runBlock (p).empty()); // fresh plugin: nothing sent, even with RECALL on

    // 1. Turning a dial sends that CC on the track's channel
    setParam (p, "t3_color", 99);
    auto out = runBlock (p);
    CHECK (out.size() == 1 && out[0].ch == 3 && out[0].cc == 16 && out[0].val == 99);

    // 2. Dropdowns: machine index, toggles 0/127; pan text
    setParam (p, "t2_machine", 4);
    setParam (p, "t2_mute", 1);
    out = runBlock (p);
    CHECK (out.size() == 2 && out[0].ch == 2 && out[0].cc == 70 && out[0].val == 4);
    CHECK (out.size() == 2 && out[1].cc == 94 && out[1].val == 127);
    CHECK (p.apvts.getParameter ("t1_pan")->getText (p.apvts.getParameter ("t1_pan")->convertTo0to1 (60), 8) == "L4");

    // 3. FX on the FX channel
    setParam (p, "fx_rsize", 10);
    out = runBlock (p);
    CHECK (out.size() == 1 && out[0].ch == 7 && out[0].cc == 87 && out[0].val == 10);

    // 4. Receive: moves the parameter, isn't echoed, opens/closes an automation gesture
    GestureCounter gestures;
    p.addListener (&gestures);
    juce::MidiBuffer in;
    in.addEvent (juce::MidiMessage::controllerEvent (5, 80, 33), 0);
    in.addEvent (juce::MidiMessage::controllerEvent (5, 80, 34), 1);  // same knob still moving
    in.addEvent (juce::MidiMessage::controllerEvent (1, 106, 3), 2);
    in.addEvent (juce::MidiMessage::controllerEvent (6, 67, 1), 3);
    in.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 4);
    juce::MidiBuffer copy (in);
    juce::AudioBuffer<float> audio (2, 4096);
    p.processBlock (audio, copy);
    int notes = 0;
    for (const auto m : copy) if (m.getMessage().isNoteOn()) ++notes;
    CHECK (notes == 1 && ccsIn (copy).empty());
    pump();
    CHECK ((int) getParam (p, "t5_decay") == 34);
    CHECK ((int) getParam (p, "t1_lfowave") == 3);
    CHECK ((int) getParam (p, "t6_gate") == 1);
    CHECK (gestures.begins == 3 && gestures.ends == 0);
    CHECK (runBlock (p).empty()); // no echo
    pump (500);
    CHECK (gestures.ends == 3);
    p.removeListener (&gestures);

    // 5. Receive off
    p.setOption (CyclesProcessor::optReceive, false);
    juce::MidiBuffer in2;
    in2.addEvent (juce::MidiMessage::controllerEvent (5, 80, 100), 0);
    p.processBlock (audio, in2);
    pump();
    CHECK ((int) getParam (p, "t5_decay") == 34);
    p.setOption (CyclesProcessor::optReceive, true);

    // 6. Send All
    p.sendAll();
    std::vector<Cc> all;
    for (int i = 0; i < 10; ++i) { auto b = runBlock (p); all.insert (all.end(), b.begin(), b.end()); }
    CHECK (all.size() == cycles::allParams().size() + 20 * 3); // each master EQ, sidechain and LFO Polarity NRPN is four CCs

    // 6a. Track FX: On and Type share CC 27 (0 = Off, type + 1); Amount is CC 28
    setParam (p, "t2_fxtype", 2);           // Fold, while FX is off: nothing to send
    CHECK (runBlock (p).empty());
    setParam (p, "t2_fxon", 1);
    out = runBlock (p);
    CHECK (out.size() == 1 && out[0].ch == 2 && out[0].cc == 27 && out[0].val == 3);
    setParam (p, "t2_fxtype", 4);           // Resonator
    out = runBlock (p);
    CHECK (out.size() == 1 && out[0].cc == 27 && out[0].val == 5);
    setParam (p, "t2_fxon", 0);
    out = runBlock (p);
    CHECK (out.size() == 1 && out[0].cc == 27 && out[0].val == 0);
    CHECK ((int) getParam (p, "t2_fxtype") == 4); // type remembered
    setParam (p, "t2_fxamt", 90);
    out = runBlock (p);
    CHECK (out.size() == 1 && out[0].cc == 28 && out[0].val == 90);
    {
        juce::MidiBuffer fxin;
        fxin.addEvent (juce::MidiMessage::controllerEvent (3, 27, 2), 0);   // device: track 3 Bitcrush
        fxin.addEvent (juce::MidiMessage::controllerEvent (4, 27, 0), 1);   // device: track 4 Off
        juce::AudioBuffer<float> a (2, 64);
        p.processBlock (a, fxin);
        pump();
        CHECK ((int) getParam (p, "t3_fxon") == 1 && (int) getParam (p, "t3_fxtype") == 1);
        CHECK ((int) getParam (p, "t4_fxon") == 0);
        CHECK (runBlock (p).empty()); // no echo
        pump (500);
    }

    // 6b. 2CH/6CH switch: NRPN 1:47 on the FX channel
    p.requestUsbAudioMode (true);
    out = runBlock (p);
    CHECK (out.size() == 4 && out[0].ch == 7 && out[0].cc == 99 && out[0].val == 1 && out[1].cc == 98 && out[1].val == 47
           && out[2].cc == 6 && out[2].val == 1 && out[3].cc == 38 && out[3].val == 0);
    p.requestUsbAudioMode (false);
    out = runBlock (p);
    CHECK (out.size() == 4 && out[2].val == 0);

    // 6e. LFO Destination is a list whose CC 105 values are the device's destinations (Filter Cutoff = 28)
    setParam (p, "t1_lfodest", 8);
    out = runBlock (p);
    CHECK (out.size() == 1 && out[0].ch == 1 && out[0].cc == 105 && out[0].val == 28);
    {
        juce::MidiBuffer din;
        din.addEvent (juce::MidiMessage::controllerEvent (2, 105, 13), 0);   // device: T2 LFO -> Sweep
        juce::AudioBuffer<float> a (2, 64);
        p.processBlock (a, din);
        pump();
        CHECK ((int) getParam (p, "t2_lfodest") == 6);
        CHECK (runBlock (p).empty());
        pump (500);
    }

    // 6d. Track select from the device (CC 119 on the FX channel): FOLLOW's slot, nothing sent back
    {
        const int serial = p.incomingSerial.load();
        juce::MidiBuffer sel;
        sel.addEvent (juce::MidiMessage::controllerEvent (7, 119, 4), 0);   // T5
        juce::AudioBuffer<float> a (2, 64);
        p.processBlock (a, sel);
        pump();
        CHECK (p.incomingSerial.load() == serial + 1 && p.lastIncomingSlot.load() == 4);
        sel.clear();
        sel.addEvent (juce::MidiMessage::controllerEvent (7, 119, 9), 0);   // out of range: ignored
        p.processBlock (a, sel);
        pump();
        CHECK (p.incomingSerial.load() == serial + 1);
        CHECK (runBlock (p).empty());
    }

    // 6c. Sidechain: NRPN 1:50-1:55 on the FX channel; the six Duck toggles send one mask on 1:51
    auto isNrpn = [] (const std::vector<Cc>& o, int lsb, int value)
    {
        return o.size() == 4 && o[0].ch == 7 && o[0].cc == 99 && o[0].val == 1 && o[1].cc == 98 && o[1].val == lsb
            && o[2].cc == 6 && o[2].val == value && o[3].cc == 38 && o[3].val == 0;
    };
    setParam (p, "fx_scsrc", 1);            // T1
    CHECK (isNrpn (runBlock (p), 50, 1));
    setParam (p, "fx_scduck1", 0);          // stop ducking T1: mask 63 -> 62
    CHECK (isNrpn (runBlock (p), 51, 62));
    setParam (p, "fx_scduck4", 0);          // and T4: 62 -> 54
    CHECK (isNrpn (runBlock (p), 51, 54));
    setParam (p, "fx_scdepth", 100);
    CHECK (isNrpn (runBlock (p), 52, 100));
    setParam (p, "fx_screl", 13);           // 800 ms
    CHECK (isNrpn (runBlock (p), 55, 13));
    // 6d. LFO Polarity: one Bi/Uni per track, sent together as a mask on NRPN 1:56 on the FX channel
    setParam (p, "t2_lfopol", 1);           // T2 Uni: mask 2
    CHECK (isNrpn (runBlock (p), 56, 2));
    setParam (p, "t5_lfopol", 1);           // and T5: 2 -> 18
    CHECK (isNrpn (runBlock (p), 56, 18));
    setParam (p, "t2_lfopol", 0);           // T2 back to Bi: 18 -> 16
    CHECK (isNrpn (runBlock (p), 56, 16));
    setParam (p, "t5_lfopol", 0);
    CHECK (isNrpn (runBlock (p), 56, 0));
    // 6e. Master EQ (firmware notes/101): NRPN 1:63-1:65 both ways on the FX channel
    setParam (p, "fx_eqlow", 0);            // kill LOW
    CHECK (isNrpn (runBlock (p), 63, 0));
    setParam (p, "fx_eqhigh", 127);
    CHECK (isNrpn (runBlock (p), 65, 127));
    CHECK (cycles::eqText (0) == "KILL" && cycles::eqText (16) == "-24dB" && cycles::eqText (64) == "0dB"
           && cycles::eqText (100) == "+4dB" && cycles::eqText (127) == "+6dB");
    {
        // the device's report (test_master_eq.py: CC 99, 98, 6, no 38): MID = 20, applied even with RECEIVE off
        p.setOption (CyclesProcessor::optReceive, false);
        for (auto [cc, v] : { std::pair { 99, 1 }, std::pair { 98, 64 }, std::pair { 6, 20 } })
            p.link.onMessage (juce::MidiMessage::controllerEvent (7, cc, v));
        pump();
        p.setOption (CyclesProcessor::optReceive, true);
        CHECK ((int) getParam (p, "fx_eqmid") == 20);
        CHECK (runBlock (p).empty());       // not sent back
    }
    setParam (p, "fx_eqlow", 64);
    setParam (p, "fx_eqmid", 64);
    setParam (p, "fx_eqhigh", 64);
    runBlock (p);
    {
        juce::MidiBuffer scin;                // a CC 98 or 6 from the device doesn't move a sidechain control
        scin.addEvent (juce::MidiMessage::controllerEvent (7, 98, 50), 0);
        scin.addEvent (juce::MidiMessage::controllerEvent (7, 6, 3), 1);
        juce::AudioBuffer<float> a (2, 64);
        p.processBlock (a, scin);
        pump();
        CHECK ((int) getParam (p, "fx_scsrc") == 1);
    }

    // 7. Transport sync: 120 BPM, 48 kHz, 0.1 s blocks
    {
        auto cp = makeProcessor();
        cp->setOption (CyclesProcessor::optClock, true);
        TestPlayHead ph;
        cp->setPlayHead (&ph);
        ph.info.setIsPlaying (true);
        ph.info.setBpm (120.0);

        int ticks = 0, starts = 0, stops = 0, conts = 0, spp = -1;
        auto block = [&] (double ppq)
        {
            ph.info.setPpqPosition (ppq);
            juce::MidiBuffer b;
            juce::AudioBuffer<float> a (2, 4800);
            cp->processBlock (a, b);
            for (const auto m : b)
            {
                auto msg = m.getMessage();
                if (msg.isMidiClock()) ++ticks;
                if (msg.isMidiStart()) ++starts;
                if (msg.isMidiStop()) ++stops;
                if (msg.isMidiContinue()) ++conts;
                if (msg.isSongPositionPointer()) spp = msg.getSongPositionPointerMidiBeat();
            }
        };
        for (int i = 0; i < 10; ++i) block (i * 0.2);     // 2 beats
        CHECK (starts == 1 && spp == 0 && ticks == 48 && stops == 0);
        block (4.0);                                        // locate to bar 2
        CHECK (stops == 1 && conts == 1 && spp == 16);
        ph.info.setIsPlaying (false);
        block (4.2);
        CHECK (stops == 2);
        cp->setPlayHead (nullptr);
    }

    // 8. Total recall: a saved sound is pushed on load; a fresh one isn't
    {
        juce::MemoryBlock state;
        p.setChannel (0, 10);
        p.getStateInformation (state);

        auto p2 = makeProcessor();
        p2->setStateInformation (state.getData(), (int) state.getSize());
        pump();
        p2->setLinkMode (DeviceLink::Mode::Host);
        pump();
        std::vector<Cc> recalled;
        for (int i = 0; i < 10; ++i) { auto b = runBlock (*p2); recalled.insert (recalled.end(), b.begin(), b.end()); }
        CHECK (recalled.size() == cycles::allParams().size() + 20 * 3); // master EQ, sidechain and LFO Polarity NRPNs are four CCs each
        CHECK ((int) getParam (*p2, "t3_color") == 99);
        CHECK (p2->getChannel (0) == 10);
        bool colorOnCh3 = false;
        for (auto& c : recalled) if (c.ch == 3 && c.cc == 16 && c.val == 99) colorOnCh3 = true;
        CHECK (colorOnCh3);

        p.setOption (CyclesProcessor::optRecall, false);
        p.getStateInformation (state);
        auto p3 = makeProcessor();
        p3->setStateInformation (state.getData(), (int) state.getSize());
        pump();
        p3->setLinkMode (DeviceLink::Mode::Host);
        pump();
        CHECK (runBlock (*p3).empty());
    }

    // 9. Port-name matching for auto-detect
    CHECK (DeviceLink::looksLikeModelCycles ("Elektron Model:Cycles"));
    CHECK (DeviceLink::looksLikeModelCycles ("Model:Cycles MIDI 1"));
    CHECK (! DeviceLink::looksLikeModelCycles ("Elektron Model:Samples"));

    // 10. Audio stream: a device clock 150 ppm fast at 48 kHz into a 44.1 kHz DAW (and 120 ppm slow into 48 kHz), 5 min. The FIFO holds its
    //     target (no underrun or overrun after it fills), the drift is tracked, and a 1 kHz tone stays 1 kHz.
    for (auto [hostRate, ppm] : { std::pair { 44100.0, 150.0 }, std::pair { 48000.0, -120.0 } })
    {
        StreamBuffer sb;
        sb.configure (hostRate, 512);
        sb.reset (6, 48000.0, 128);
        const double devRate = 48000.0 * (1.0 + ppm * 1.0e-6);
        juce::AudioBuffer<float> dev (6, 128), host (6, 512);
        double phase = 0.0, tDev = 0.0, tHost = 0.0;
        int crossings = 0, played = 0, driftReads = 0;
        double driftSum = 0.0;
        float prev = 0.0f;
        bool started = false;
        while (tHost < 300.0)
        {
            if (tDev <= tHost)
            {
                for (int i = 0; i < 128; ++i, phase += 1000.0 / 48000.0)
                    for (int c = 0; c < 6; ++c) dev.setSample (c, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase) / (float) (c + 1));
                sb.write (dev.getArrayOfReadPointers(), 6, 128);
                tDev += 128.0 / devRate;
            }
            else
            {
                const bool ok = sb.read (host.getArrayOfWritePointers(), 512, hostRate);
                if (ok && ! started) { started = true; sb.underruns = 0; sb.overruns = 0; }
                if (ok && tHost > 240.0) { driftSum += sb.getDrift(); ++driftReads; }
                if (ok && tHost > 299.0)
                    for (int i = 0; i < 512; ++i, ++played)
                    {
                        const float v = host.getSample (0, i);
                        if (prev < 0.0f && v >= 0.0f) ++crossings;
                        prev = v;
                    }
                tHost += 512.0 / hostRate;
            }
        }
        const double seconds = played / hostRate;
        CHECK (started && sb.underruns.load() == 0 && sb.overruns.load() == 0);
        CHECK (driftReads > 0 && std::abs (driftSum / driftReads - ppm * 1.0e-6) < 30.0e-6);
        CHECK (std::abs (crossings / seconds - 1000.0) < 3.0);
    }

    // 11. 6CH multi-out: each stem on its track output, panned, and summed on the main output; latency reported
    {
        auto ap = makeProcessor();
        ap->setOption (CyclesProcessor::optAudio, false);
        CHECK (ap->getLatencySamples() == 0);
        setParam (*ap, "t2_pan", 127); // hard right
        ap->audio.startTestStream (6, 48000.0, 128);
        const int lat = ap->getLatencySamples();
        CHECK (lat > 48 && lat < 8192);
        juce::AudioBuffer<float> dev (6, 128);
        for (int c = 0; c < 6; ++c) dev.clear (c, 0, 128);
        juce::AudioBuffer<float> out (ap->getTotalNumOutputChannels(), 4096);
        CHECK (out.getNumChannels() == 14);
        float t2l = 0, t2r = 0, t1l = 0, t1r = 0, mainR = 0, t3 = 0;
        for (int b = 0; b < 40; ++b)
        {
            for (int k = 0; k < 32; ++k)
            {
                for (int i = 0; i < 128; ++i) { dev.setSample (0, i, 0.25f); dev.setSample (1, i, 0.5f); }
                ap->audio.getStream().write (dev.getArrayOfReadPointers(), 6, 128);
            }
            juce::MidiBuffer none;
            ap->processBlock (out, none);
        }
        t1l = out.getSample (2, 4000); t1r = out.getSample (3, 4000);
        t2l = out.getSample (4, 4000); t2r = out.getSample (5, 4000);
        t3 = out.getMagnitude (6, 0, 4096);
        mainR = out.getSample (1, 4000);
        CHECK (std::abs (t1l - 0.25f) < 0.01f && std::abs (t1r - 0.25f) < 0.01f);
        CHECK (std::abs (t2l) < 0.01f && std::abs (t2r - 0.5f) < 0.01f);
        CHECK (t3 < 1.0e-6f);
        CHECK (std::abs (mainR - 0.75f) < 0.02f);
        CHECK (ap->meters[3].load() > 0.4f);
        ap->audio.shutdown();
    }


    // Samples (firmware notes/93): the file-transfer framing, the +Drive list and file reads against a fake device,
    // the sample NRPNs both ways, the overlay formulas
    {
        // 7-bit packing: 8 bytes for 7, top bits first
        const juce::uint8 raw[] = { 0x80, 0x01, 0xff, 0x7f, 0x00, 0x81, 0x40, 0xc3, 0x12 };
        auto enc = ElektronFs::encode7 (raw, sizeof (raw));
        CHECK (enc.getSize() == 9 + 2);
        auto dec = ElektronFs::decode7 (static_cast<const juce::uint8*> (enc.getData()), enc.getSize());
        CHECK (dec.getSize() == sizeof (raw) && std::memcmp (dec.getData(), raw, sizeof (raw)) == 0);
        const auto* e = static_cast<const juce::uint8*> (enc.getData());
        CHECK (e[0] == 0x52 && e[1] == 0x00 && e[3] == 0x7f); // 0x80, 0xff, 0x81 have the top bit: 1010010

        // a fake Model:Cycles: one folder, a sample (48 kHz header + PCM), a preset (not a sample)
        std::vector<juce::int16> pcm (30000);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (juce::int16) (20000.0 * std::sin ((double) i * 0.01) * (1.0 - (double) i / 30000.0));
        juce::MemoryBlock smp (64, true);
        auto* h = static_cast<juce::uint8*> (smp.getData());
        const juce::uint32 bytes = (juce::uint32) pcm.size() * 2;
        h[4] = (juce::uint8) (bytes >> 24); h[5] = (juce::uint8) (bytes >> 16); h[6] = (juce::uint8) (bytes >> 8); h[7] = (juce::uint8) bytes;
        h[10] = 0xbb; h[11] = 0x80; // 48000
        for (auto v : pcm) { const juce::uint8 b[] = { (juce::uint8) ((juce::uint16) v >> 8), (juce::uint8) v }; smp.append (b, 2); }
        juce::MemoryBlock preset (300, true);
        std::map<juce::String, juce::MemoryBlock> files { { "/kicks/boom", smp }, { "/a preset", preset } };

        ElektronFs fs;
        ElektronFs* answerTo = &fs;
        int requests = 0, lists = 0;
        juce::uint32 cap = 0xffffffff; // the most data the fake device sends in one read answer
        fs.send = [&] (const juce::MidiMessage& m)
        {
            ++requests;
            auto req = ElektronFs::decode7 (m.getSysExData() + 5, (size_t) m.getSysExDataSize() - 5);
            const auto* q = static_cast<const juce::uint8*> (req.getData());
            auto be = [] (const juce::uint8* x) { return (juce::uint32) x[0] << 24 | (juce::uint32) x[1] << 16 | (juce::uint32) x[2] << 8 | x[3]; };
            juce::MemoryBlock ans;
            juce::MemoryOutputStream o (ans, false);
            const juce::uint8 hd[] = { 0, 0, q[0], q[1], (juce::uint8) (q[4] | 0x80) };
            o.write (hd, 5);
            auto put32 = [&] (juce::uint32 v) { o.writeByte ((char) (v >> 24)); o.writeByte ((char) (v >> 16)); o.writeByte ((char) (v >> 8)); o.writeByte ((char) v); };
            auto entry = [&] (juce::uint32 hash, juce::uint32 size, char type, const char* name) { put32 (hash); put32 (size); o.writeByte (0); o.writeByte (type); o.write (name, strlen (name) + 1); };
            const juce::String path (reinterpret_cast<const char*> (q + 5));
            static juce::String open;
            if (q[4] == 0x14) ++lists;
            if (q[4] == 0x14 && path == "/") { entry (0, 0, 'D', "kicks"); entry (0x0badf00d, (juce::uint32) preset.getSize(), 'F', "a preset"); }
            else if (q[4] == 0x14 && path == "/kicks") entry (0x9a3c51e7, (juce::uint32) smp.getSize(), 'F', "boom");
            else if (q[4] == 0x33) { open = path; o.writeByte (files.count (path) ? 1 : 0); put32 (7); put32 (files.count (path) ? (juce::uint32) files[path].getSize() : 0); }
            else if (q[4] == 0x35)
            {
                const juce::uint32 n = juce::jmin (cap, be (q + 9)), at = be (q + 13); // id, size, start
                o.writeByte (1); put32 (be (q + 5)); put32 (be (q + 9)); put32 (at); put32 (n);
                o.write (static_cast<const char*> (files[open].getData()) + at, n);
            }
            else if (q[4] == 0x34) o.writeByte (1);
            o.flush();
            const auto reply = ElektronFs::frame (ans);
            answerTo->handleSysEx (reply.getSysExData(), reply.getSysExDataSize());
        };
        std::vector<ElektronFs::Entry> list;
        {
            // the device is busy (a project loading): requests wait, then go out
            fs.holdOff (300);
            const auto t0 = juce::Time::getMillisecondCounter();
            const int before = requests;
            CHECK (fs.listFolder ("/", list) && requests == before + 1 && juce::Time::getMillisecondCounter() - t0 >= 250);
        }
        CHECK (fs.listFolder ("/", list) && list.size() == 2 && list[0].folder && list[0].name == "kicks"
               && ! list[1].folder && list[1].hash == 0x0badf00d && list[1].size == 300);
        juce::MemoryBlock got;
        CHECK (fs.readFile ("/kicks/boom", 0, (juce::uint32) smp.getSize(), got) && got == smp);
        CHECK (requests >= 2 + (int) (smp.getSize() / ElektronFs::maxChunk));
        {
            // a device that answers at most 512 bytes a read: the header's length comes from the first full answer
            ElektronFs small;
            answerTo = &small;
            small.send = fs.send;
            cap = 512;
            juce::MemoryBlock head, all;
            CHECK (small.readFile ("/kicks/boom", 0, 64, head) && head.getSize() == 64
                   && std::memcmp (head.getData(), smp.getData(), 64) == 0);
            CHECK (small.readFile ("/kicks/boom", 0, (juce::uint32) smp.getSize(), all) && all == smp);
            cap = 0xffffffff;
            answerTo = &fs;
        }
        auto w = SampleLibrary::waveFromFile (static_cast<const juce::uint8*> (got.getData()), got.getSize());
        CHECK (w != nullptr && w->numSamples == 30000 && (int) w->lo.size() == SampleLibrary::columns && w->pcm == pcm);
        CHECK (w != nullptr && *std::max_element (w->hi.begin(), w->hi.end()) > 15000 && *std::min_element (w->lo.begin(), w->lo.end()) < -15000
               && w->hi[1000] < w->hi[10]);
        CHECK (SampleLibrary::waveFromFile (static_cast<const juce::uint8*> (preset.getData()), preset.getSize()) == nullptr);

        // the whole library against the fake device: only the sample is listed, its waveform read, then cached
        auto cache = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mcw-test");
        cache.deleteRecursively();
        {
            SampleLibrary lib (cache);
            answerTo = &lib.fs;
            lib.fs.send = fs.send;
            lib.refresh();
            for (int i = 0; i < 100 && (lib.getSamples().empty() || lib.isBusy()); ++i) juce::Thread::sleep (20);
            const auto found = lib.getSamples();
            CHECK (found.size() == 1 && found[0].path == "/kicks/boom" && found[0].hash == 0x9a3c51e7 && found[0].numSamples == 30000);
            std::shared_ptr<const SampleLibrary::Wave> lw;
            for (int i = 0; i < 100 && (lw = lib.getWave (0x9a3c51e7)) == nullptr; ++i) juce::Thread::sleep (20);
            CHECK (lw != nullptr && lw->pcm == pcm && lw->lo == w->lo);
            CHECK (cache.getChildFile ("9A3C51E7.mcw").existsAsFile());
            // a track whose sample isn't on the +Drive: the list is read again once, not forever
            const int before = lists;
            for (int i = 0; i < 60; ++i) { lib.getWave (0xdeadbeef); juce::Thread::sleep (20); }
            CHECK (lists - before <= 2 && lib.getStatus().contains ("not on the +Drive"));
            lib.fs.send = nullptr;
        }
        {
            SampleLibrary again (cache); // no device: the waveform comes from the cache
            again.addForTests ({ "/kicks/boom", "boom", 0x9a3c51e7, (juce::uint32) smp.getSize(), 30000 }, nullptr);
            std::shared_ptr<const SampleLibrary::Wave> cw;
            for (int i = 0; i < 100 && (cw = again.getWave (0x9a3c51e7)) == nullptr; ++i) juce::Thread::sleep (20);
            CHECK (cw != nullptr && cw->pcm == pcm && cw->hi == w->hi);
        }
        cache.deleteRecursively();
        answerTo = &fs;

        // NRPN: picking a sample sends 1:59, 1:60 (14 bits), 1:61 (track, top 7 bits) on the FX channel
        setParam (p, "t3_machine", 21); // SAMPLER (machine 22)
        runBlock (p);
        CHECK (juce::String (modded::machines[21].name) == "SAMPLER" && CyclesProcessor::isSampleMachine (21) && ! CyclesProcessor::isSampleMachine (20));
        p.pickSample (2, 0x9a3c51e7);
        out = runBlock (p);
        const juce::uint32 hh = 0x9a3c51e7;
        const std::vector<std::array<int, 2>> want {
            { 99, 1 }, { 98, 59 }, { 6, (int) (hh >> 25) & 127 }, { 38, (int) (hh >> 18) & 127 },
            { 99, 1 }, { 98, 60 }, { 6, (int) (hh >> 11) & 127 }, { 38, (int) (hh >> 4) & 127 },
            { 99, 1 }, { 98, 61 }, { 6, (int) ((hh & 15) << 3 | 2) }, { 38, 0 } };
        bool same = out.size() == want.size();
        for (size_t i = 0; same && i < want.size(); ++i)
            same = out[i].ch == 7 && out[i].cc == want[i][0] && out[i].val == want[i][1];
        CHECK (same);
        CHECK (p.getTrackSample (2) == hh);

        // the firmware's report (as test_smp_nrpn.py checks it sends): T5 has 0x12345678
        juce::MidiBuffer rep;
        const juce::uint32 r5 = 0x12345678;
        int at = 0;
        for (auto [cc, v] : std::vector<std::pair<int, int>> {
                 { 99, 1 }, { 98, 59 }, { 6, (int) (r5 >> 25) & 127 }, { 38, (int) (r5 >> 18) & 127 },
                 { 99, 1 }, { 98, 60 }, { 6, (int) (r5 >> 11) & 127 }, { 38, (int) (r5 >> 4) & 127 },
                 { 99, 1 }, { 98, 61 }, { 6, (int) ((r5 & 15) << 3 | 4) } })
            rep.addEvent (juce::MidiMessage::controllerEvent (7, cc, v), at++);
        // processBlock (host mode) hands device messages to the same path as the direct port
        for (const auto m : rep) p.handleDeviceMessageForTests (m.getMessage());
        CHECK (p.getTrackSample (4) == r5 && p.getTrackSample (2) == hh);
        pump (100);
        CHECK (p.apvts.state.getProperty ("sample5").toString() == "12345678");

        // SEND ALL puts each sample track's sample back after the parameters
        p.sendAll();
        out.clear();
        for (int i = 0; i < 20; ++i) for (auto c : runBlock (p)) out.push_back (c);
        CHECK (out.size() >= 12 && out[out.size() - 3].cc == 98 && out[out.size() - 3].val == 61 && out.back().cc == 38
               && out[out.size() - 2].val == (int) ((hh & 15) << 3 | 2));

        // overlay formulas = the firmware's (smp_view.c)
        auto m = SampleView::marks (SampleView::Kind::Sampler, 48000, 64, 95, 2, 0, 64);
        CHECK (m.start == 24000 && m.end == 36000 && m.mode == 2 && m.loop == 30000);
        m = SampleView::marks (SampleView::Kind::Sampler, 48000, 64, 95, 0, 0, 64);
        CHECK (m.loop == -1);
        m = SampleView::marks (SampleView::Kind::Granular, 48000, 127, 0, 127, 127, 10);
        CHECK (m.pos == 47625 && m.spray == 11906 && m.grains == 8 && m.scan == 10);
        m = SampleView::marks (SampleView::Kind::Wavetable, 2048 * 16, 127, 127, 0, 0, 0);
        CHECK (m.frames == 16 && m.pos == 15 * 256 && m.warp == 1024);

        // dragging the SAMPLER bars: the knob value that puts a bar where it is dropped (every value round-trips)
        {
            using H = SampleView::Handle;
            bool round = true;
            for (int v = 0; v < 128 && round; ++v)
            {
                const auto mv = SampleView::marks (SampleView::Kind::Sampler, 48000, v, v, 2, 0, v);
                round = SampleView::valueFor (H::Start, 48000, mv.start, mv) == v
                     && SampleView::valueFor (H::End, 48000, mv.end, mv) == v;
            }
            CHECK (round);
            const auto ml = SampleView::marks (SampleView::Kind::Sampler, 48000, 0, 127, 2, 0, 64);
            CHECK (SampleView::valueFor (H::Loop, 48000, ml.loop, ml) == 64);
            CHECK (SampleView::valueFor (H::Start, 48000, -500, ml) == 0 && SampleView::valueFor (H::End, 48000, 99999, ml) == 127);
        }

        // the window: a fake sample on T3 for the snapshot
        SampleLibrary::Sample s { "/kicks/boom", "boom", hh, (juce::uint32) smp.getSize(), 30000 };
        p.samples.addForTests (s, w);
        p.samples.addForTests ({ "/loops/amen 170", "amen 170", 0x11, 400000, 199968 }, nullptr);
        setParam (p, "t3_color", 20);
        setParam (p, "t3_shape", 100);
        setParam (p, "t3_sweep", 2);
        setParam (p, "t3_x3", 40);
    }

    // Optional: render each view to PNG
    if (snapshotDir.isNotEmpty())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* ce = dynamic_cast<CyclesEditor*> (ed.get());
        const char* names[] = { "track", "", "track3_sample", "", "", "", "fx", "mixer", "setup" };
        for (int v : { 0, 2, 6, 7, 8 })
        {
            ce->showView (v);
            pump (150);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (snapshotDir + "/ui_" + names[v] + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        // GRANULAR and WAVETABLE on the same sample
        for (auto [machine, name] : { std::pair { 22, "track3_granular" }, std::pair { 23, "track3_wavetable" } })
        {
            setParam (p, "t3_machine", (float) machine);
            setParam (p, "t3_contour", 60);
            ce->showView (0);
            ce->showView (2);
            pump (150);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (snapshotDir + "/ui_" + name + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "FAILURES: " + std::to_string (failures) + "\n");
    // 12. GET: asks the device (NRPN 1:57 on the FX channel), takes its answer even with RECEIVE off, without echo or
    //     gestures, until NRPN 1:58; no answer -> getNoAnswer
    {
        auto g = makeProcessor();
        g->setLinkMode (DeviceLink::Mode::Host);
        pump();
        for (int i = 0; i < 4; ++i) runBlock (*g); // a fresh project asks by itself when it connects
        g->setOption (CyclesProcessor::optReceive, false);
        GestureCounter gestures;
        g->addListener (&gestures);
        g->requestGet();
        auto ask = runBlock (*g);
        CHECK (ask.size() == 8 && ask[0].ch == 7 && ask[0].cc == 99 && ask[0].val == 1 && ask[1].cc == 98 && ask[1].val == 57
               && ask[2].cc == 6 && ask[2].val == 0 && ask[3].cc == 38
               && ask[5].cc == 98 && ask[5].val == 62);   // then 1:62: the master EQ (not in the GET dump)
        const int serial = g->getSerial.load();
        auto dev = [&] (int ch, int cc, int v) { g->link.onMessage (juce::MidiMessage::controllerEvent (ch, cc, v)); };
        auto nrpn = [&] (int lsb, int v) { dev (7, 99, 1); dev (7, 98, lsb); dev (7, 6, v); dev (7, 38, 0); };
        dev (2, 16, 77);                                   // T2 COLOR
        dev (4, 94, 1);                                    // T4 muted
        nrpn (modded::kLfoPolarityNrpnLsb, 0b100101);      // T1, T3, T6 unipolar
        pump();
        CHECK (g->getState.load() == CyclesProcessor::getWaiting);
        nrpn (modded::kGetDoneNrpnLsb, 0);
        pump();
        CHECK (g->getState.load() == CyclesProcessor::getDone && g->getSerial.load() == serial + 1);
        CHECK (g->getCount.load() == 2 + 6);
        CHECK ((int) getParam (*g, "t2_color") == 77 && (int) getParam (*g, "t4_mute") == 1);
        CHECK ((int) getParam (*g, "t1_lfopol") == 1 && (int) getParam (*g, "t2_lfopol") == 0
               && (int) getParam (*g, "t3_lfopol") == 1 && (int) getParam (*g, "t6_lfopol") == 1);
        CHECK (runBlock (*g).empty());                     // no echo
        CHECK (gestures.begins == 0);
        dev (2, 16, 10);                                   // after the GET, RECEIVE off applies again
        pump();
        CHECK ((int) getParam (*g, "t2_color") == 77);
        g->removeListener (&gestures);

        g->requestGet();
        runBlock (*g);
        pump (2300);
        CHECK (g->getState.load() == CyclesProcessor::getNoAnswer && g->getSerial.load() == serial + 2);

        // Projects (firmware notes/109, as test_smp_nrpn.py checks the device side): 1:66 asks, CC 6 only
        g->requestProjectList();
        auto pl = runBlock (*g);
        CHECK (pl.size() == 3 && pl[0].ch == 7 && pl[0].cc == 99 && pl[1].val == 66 && pl[2].cc == 6 && pl[2].val == 0);
        auto msb = [&] (int lsb, int v) { dev (7, 99, 1); dev (7, 98, lsb); dev (7, 6, v); };
        const int projSerial = g->projectSerial.load();
        msb (67, 0);
        for (auto c : juce::String ("TECHNO 01")) msb (68, (int) c);
        msb (67, 5);
        for (auto c : juce::String ("HARD")) msb (68, (int) c);
        CHECK (g->getProjects().empty());                  // shown only once complete
        msb (69, 6);                                       // end of the list: current slot 5
        pump();
        auto list = g->getProjects();
        CHECK (list.size() == 2 && list[0].first == 0 && list[0].second == "TECHNO 01" && list[1].first == 5
               && list[1].second == "HARD" && g->currentProject.load() == 5 && g->projectSerial.load() > projSerial);
        CHECK (runBlock (*g).empty());                     // nothing asked again
        g->loadProject (17);
        auto ld = runBlock (*g);
        CHECK (ld.size() == 3 && ld[1].val == 70 && ld[2].cc == 6 && ld[2].val == 17 && g->projectLoading.load());
        msb (69, 18);                                      // loaded
        pump();
        CHECK (! g->projectLoading.load() && g->currentProject.load() == 17);
        CHECK (runBlock (*g).empty());
        g->loadProject (96);
        CHECK (runBlock (*g).empty() && ! g->projectLoading.load());
        msb (69, 1);                                       // a project loaded on the device: the list is read again
        pump();
        auto re = runBlock (*g);
        CHECK (g->currentProject.load() == 0 && re.size() == 3 && re[1].val == 66);
        msb (69, 1);                                       // the (empty) answer
        pump();
        CHECK (g->getProjects().empty() && g->currentProject.load() == 0);

        // Unsaved changes (1:71) and the two loads that answer the save question in the plugin (1:72, 1:73)
        CHECK (g->projectDirty.load() == -1);
        const int dSerial = g->projectSerial.load();
        msb (71, 1);
        pump();
        CHECK (g->projectDirty.load() == 1 && g->projectSerial.load() > dSerial);
        g->loadProject (3, CyclesProcessor::LoadHow::dontSave);
        auto d1 = runBlock (*g);
        CHECK (d1.size() == 3 && d1[1].val == 72 && d1[2].val == 3 && g->projectLoading.load());
        msb (71, 0);
        msb (69, 4);
        pump();
        CHECK (g->projectDirty.load() == 0 && g->currentProject.load() == 3 && ! g->projectLoading.load());
        g->loadProject (9, CyclesProcessor::LoadHow::saveFirst);
        auto d2 = runBlock (*g);
        CHECK (d2.size() == 3 && d2[1].val == 73 && d2[2].val == 9);
        msb (70, 5);                                       // 1:70 is the plugin's own: ignored if it comes back
        pump();
        CHECK (g->currentProject.load() == 3);
    }

    return failures == 0 ? 0 : 1;
}
