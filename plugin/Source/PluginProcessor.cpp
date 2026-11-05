#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace cycles;

static const juce::Identifier propMidiIn ("midiInput"), propMidiOut ("midiOutput"), propLinkMode ("linkMode"),
                              propClockOffset ("clockOffsetMs"), propStateKnown ("stateKnown"),
                              propAudioTrim ("audioTrimMs");

static juce::Identifier channelProp (int slot)   { return juce::Identifier ("channel" + juce::String (slot)); }
static juce::Identifier optionProp (int option)  { return juce::Identifier ("option" + juce::String (option)); }
static constexpr bool optionDefaults[] = { true /*receive*/, true /*recall*/, false /*clock*/, true /*follow*/, true /*audio*/ };

static juce::uint32 msNow() { return juce::Time::getMillisecondCounter(); }

// Like Overbridge: the main mix plus one output per track, for the DAW's multi-out routing.
juce::AudioProcessor::BusesProperties CyclesProcessor::makeBuses()
{
    auto buses = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int t = 0; t < numTracks; ++t)
        buses = buses.withOutput ("Track " + juce::String (t + 1), juce::AudioChannelSet::stereo(), true);
    return buses;
}

//==============================================================================
CyclesProcessor::CyclesProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "ModelCyclesCC", createLayout()),
      lastValue (allParams().size()),
      partner (allParams().size(), -1),
      gestureLastTouch (allParams().size(), 0.0),
      gestureOpen (allParams().size(), false)
{
    const auto& infos = allParams();
    for (size_t i = 0; i < infos.size(); ++i)
    {
        auto* p = apvts.getParameter (infos[i].id);
        jassert (p != nullptr);
        params.push_back (p);
        idToIndex[infos[i].id] = (int) i;
        apvts.addParameterListener (infos[i].id, this);
    }

    for (size_t i = 0; i < infos.size(); ++i)
        if (infos[i].def->key == "fxon")
        {
            const int type = idToIndex[paramId (infos[i].slot, *infos[i].def).replace ("fxon", "fxtype")];
            partner[i] = type;
            partner[(size_t) type] = (int) i;
        }

    for (size_t i = 0; i < infos.size(); ++i)
        if (infos[i].def->nrpn >= 0)
            nrpnParams[infos[i].def->nrpn].push_back ((int) i);

    for (int s = 0; s <= numTracks; ++s)
        channels[(size_t) s].store (s + 1); // T1-T6 -> ch 1-6, FX -> ch 7
    for (int o = 0; o < numOptions; ++o)
        options[(size_t) o].store (optionDefaults[o]);

    syncLastValues();
    rebuildLookup();

    for (int t = 0; t < numTracks; ++t)
    {
        panParams[(size_t) t] = apvts.getRawParameterValue ("t" + juce::String (t + 1) + "_pan");
        panL[(size_t) t] = panR[(size_t) t] = 1.0f;
    }

    link.onMessage   = [this] (const juce::MidiMessage& m) { onDeviceMessage (m); };
    link.onSysEx     = [this] (const juce::uint8* d, int n) { return samples.fs.handleSysEx (d, n); };
    samples.fs.send  = [this] (const juce::MidiMessage& m) { link.sendNow (m); };
    link.onConnected = [this]
    {
        // Total recall: a project that holds a sound puts it back on the device when it (re)connects. A project
        // without one reads the device's sound instead (GET).
        if (getOption (optRecall) && stateKnown.load())
            recallPending.store (true);
        else if (! stateKnown.load())
            getPending.store (true);
        requestSampleReport();
        projectListPending.store (true);
    };

    audio.onFormatChanged = [this] { updateLatency(); };
    audio.setEnabled (getOption (optAudio));

    juce::Timer::startTimerHz (60);
    juce::HighResolutionTimer::startTimer (1); // ~1 CC per ms to the device (safe even over 5-pin DIN)
}

CyclesProcessor::~CyclesProcessor()
{
    samples.fs.send = nullptr;
    link.onSysEx = nullptr;
    audio.shutdown();
    link.shutdown();
    cancelPendingUpdate();
    juce::Timer::stopTimer();
    juce::HighResolutionTimer::stopTimer();

    for (auto& info : allParams())
        apvts.removeParameterListener (info.id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout CyclesProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (auto& info : allParams())
    {
        const auto* d = info.def;
        if (d->kind == Kind::Dial)
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { info.id, 1 }, info.name, 0, d->maxValue, info.defaultValue,
                juce::AudioParameterIntAttributes().withStringFromValueFunction ([d] (int v, int) { return dialText (*d, v); })));
        else
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { info.id, 1 }, info.name,
                                                                      d->choices, info.defaultValue));
    }
    return layout;
}

void CyclesProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    clock.reset();
    streamBuffer.setSize (StreamBuffer::maxChannels, juce::jmax (blockSize, 8192));
    audio.prepare (sampleRate, blockSize);
    updateLatency();
}

bool CyclesProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void CyclesProcessor::updateLatency()
{
    // The DAW delays everything else by this much, so the device's audio lands on the grid (Overbridge's
    // delay compensation). Clock and notes then go out as soon as the DAW renders them.
    const bool streaming = audio.getStatus() == AudioLink::Status::Streaming;
    audioAligned.store (streaming);
    const int samples = streaming ? juce::jmax (0, juce::roundToInt ((audio.getLatencySeconds() + audioTrimMs.load() / 1000.0)
                                                                    * currentSampleRate))
                                  : 0;
    if (samples != getLatencySamples())
        setLatencySamples (samples);
}

//==============================================================================
int CyclesProcessor::insertCc (int index) const
{
    const bool isOn = allParams()[(size_t) index].def->key == "fxon";
    auto* on   = params[(size_t) (isOn ? index : partner[(size_t) index])];
    auto* type = params[(size_t) (isOn ? partner[(size_t) index] : index)];
    if (on->getValue() < 0.5f) return 0;
    return juce::roundToInt (type->convertFrom0to1 (type->getValue())) + 1;
}

int CyclesProcessor::nrpnValue (int index) const
{
    const auto& d = *allParams()[(size_t) index].def;
    auto valueOf = [this] (int i) { auto* p = params[(size_t) i]; return juce::roundToInt (p->convertFrom0to1 (p->getValue())); };
    // One toggle per track sent together as a mask (bit 0 = T1): the sidechain's Duck T1-T6 (1:51) and each
    // track's LFO Polarity (1:56)
    const bool duck = d.key.startsWith ("scduck");
    if (! duck && d.key != "lfopol") return toCc (d, valueOf (index)); // a list index or a dial value

    int mask = 0;
    for (int t = 0; t < numTracks; ++t)
    {
        const auto n = juce::String (t + 1);
        if (valueOf (idToIndex.at (duck ? "fx_scduck" + n : "t" + n + "_lfopol")) > 0) mask |= 1 << t;
    }
    return mask;
}

void CyclesProcessor::queueNrpn (int lsb, int value)
{
    const int ch = getChannel (fxSlot) - 1;
    if (outgoing.pushAll ({ MidiQueue::pack (ch, 99, 1), MidiQueue::pack (ch, 98, lsb),
                            MidiQueue::pack (ch, 6, value), MidiQueue::pack (ch, 38, 0) }))
        txTime[(size_t) fxSlot].store (msNow());
}

void CyclesProcessor::queueNrpnMsb (int lsb, int value)
{
    const int ch = getChannel (fxSlot) - 1;
    if (outgoing.pushAll ({ MidiQueue::pack (ch, 99, 1), MidiQueue::pack (ch, 98, lsb), MidiQueue::pack (ch, 6, value & 0x7f) }))
        txTime[(size_t) fxSlot].store (msNow());
}

void CyclesProcessor::queueNrpn14 (int lsb, int value)
{
    const int ch = getChannel (fxSlot) - 1;
    if (outgoing.pushAll ({ MidiQueue::pack (ch, 99, 1), MidiQueue::pack (ch, 98, lsb),
                            MidiQueue::pack (ch, 6, (value >> 7) & 0x7f), MidiQueue::pack (ch, 38, value & 0x7f) }))
        txTime[(size_t) fxSlot].store (msNow());
}

bool CyclesProcessor::isSampleMachine (int machine)
{
    if (machine < 0 || machine >= modded::numMachines) return false;
    const juce::String n (modded::machines[machine].name);
    return n == "SAMPLER" || n == "GRANULAR" || n == "WAVETABLE";
}

// NRPN on the FX channel (firmware notes/93): 1:59 = hash bits 31..18, 1:60 = bits 17..4 (14-bit values), then
// 1:61 = bits 3..0 << 3 | track, top 7 bits only (CC 6): the firmware acts on it as soon as CC 6 arrives.
void CyclesProcessor::pickSample (int track, juce::uint32 hash)
{
    if (track < 0 || track >= numTracks || hash == 0) return;
    queueNrpn14 (59, (int) (hash >> 18) & 0x3fff);
    queueNrpn14 (60, (int) (hash >> 4) & 0x3fff);
    queueNrpn (61, (int) ((hash & 15) << 3 | (juce::uint32) track));
    trackSample[(size_t) track].store (hash);
    trackSampleDirty.store (true);
    stateKnown.store (true);
}

void CyclesProcessor::requestSampleReport()
{
    queueNrpn (62, 0);
}

void CyclesProcessor::requestProjectList()
{
    {
        const juce::SpinLock::ScopedLockType l (projectLock);
        projectListing = true;
        projectsRx.clear();
        rxProjectSlot = -1;
    }
    queueNrpnMsb (projListNrpn, 0);
}

void CyclesProcessor::loadProject (int slot, LoadHow how)
{
    if (slot < 0 || slot >= numProjectSlots) return;
    samples.fs.holdOff (15000); // the Cycles is busy loading (and may ask to save first): no +Drive reads meanwhile
    projectLoading.store (true);
    projectLoadTime = (juce::uint32) msNow();
    ++projectSerial;
    queueNrpnMsb (how == LoadHow::dontSave ? projDiscardNrpn : how == LoadHow::saveFirst ? projSaveNrpn : projLoadNrpn, slot);
}

std::vector<std::pair<int, juce::String>> CyclesProcessor::getProjects() const
{
    const juce::SpinLock::ScopedLockType l (projectLock);
    return { projects.begin(), projects.end() };
}

void CyclesProcessor::onProjectNrpn (int number, int value)
{
    if (number == projSlotNrpn)
    {
        const juce::SpinLock::ScopedLockType l (projectLock);
        rxProjectSlot = value < numProjectSlots ? value : -1;
        if (rxProjectSlot >= 0) projectsRx[rxProjectSlot] = {};
    }
    else if (number == projCharNrpn)
    {
        const juce::SpinLock::ScopedLockType l (projectLock);
        if (rxProjectSlot >= 0 && value >= 32)
            projectsRx[rxProjectSlot] += juce::String::charToString ((juce::juce_wchar) value);
    }
    else if (number == projDirtyNrpn)
    {
        if (projectDirty.exchange (value != 0 ? 1 : 0) != (value != 0 ? 1 : 0))
            ++projectSerial;
    }
    else if (number == projCurrentNrpn)
    {
        bool listed = false;
        {
            const juce::SpinLock::ScopedLockType l (projectLock);
            if (projectListing)
            {
                projects.swap (projectsRx);
                projectsRx.clear();
                projectListing = false;
                listed = true;
            }
        }
        const int before = currentProject.exchange (value - 1);
        const bool asked = projectLoading.exchange (false);
        // A project loaded or saved on the device (another slot, maybe a new name): read the names again
        if (! listed && ! asked && before != -2)
            projectListPending.store (true);
        if (asked)
            samples.fs.holdOff (2000); // loaded: the sample reports follow
        ++projectSerial;
    }
}

void CyclesProcessor::onSampleNrpn (int number, int value, bool lsbPart)
{
    samples.fs.holdOff (3000); // the device's samples are changing (a project or kit load): read the +Drive later
    if (number == 59 && lsbPart) rxHi = (juce::uint32) value;
    else if (number == 60 && lsbPart) rxMid = (juce::uint32) value;
    else if (number == 61 && ! lsbPart)
    {
        const int m = (value >> 7) & 0x7f, t = m & 7;
        if (t < numTracks)
        {
            trackSample[(size_t) t].store (rxHi << 18 | rxMid << 4 | (juce::uint32) (m >> 3));
            trackSampleDirty.store (true);
        }
    }
}

int CyclesProcessor::currentCcValue (int index) const
{
    if (allParams()[(size_t) index].def->nrpn >= 0) return nrpnValue (index);
    if (partner[(size_t) index] >= 0) return insertCc (index);
    auto* p = params[(size_t) index];
    const int value = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
    return toCc (*allParams()[(size_t) index].def, value);
}

void CyclesProcessor::queueParam (int index, int ccValue)
{
    const auto& info = allParams()[(size_t) index];
    if (info.def->nrpn >= 0) return queueNrpn (info.def->nrpn, ccValue);
    const auto msg = MidiQueue::pack (getChannel (info.slot) - 1, info.def->cc, ccValue);
    if (outgoing.push (msg))
    {
        lastSent.store (msg);
        txTime[(size_t) info.slot].store (msNow());
    }
}

void CyclesProcessor::parameterChanged (const juce::String& id, float newValue)
{
    auto it = idToIndex.find (id);
    if (it == idToIndex.end()) return;

    const int index = it->second;
    if (partner[(size_t) index] >= 0 && applyingInsert.load()) return;
    if (applyingNrpn.load()) return; // receiveNrpn() records the values once they're all set
    const auto& def = *allParams()[(size_t) index].def;
    const int cc = def.nrpn >= 0 ? nrpnValue (index)
                 : partner[(size_t) index] >= 0 ? insertCc (index)
                                                : toCc (def, juce::roundToInt (newValue));
    if (partner[(size_t) index] >= 0) lastValue[(size_t) partner[(size_t) index]].store (cc);

    // Same value as the device already has (it came from the device, or from loading a project): don't echo.
    if (lastValue[(size_t) index].exchange (cc) == cc || loadingState.load())
        return;

    stateKnown.store (true);
    queueParam (index, cc);
}

void CyclesProcessor::sendAll()
{
    for (int i = 0; i < (int) params.size(); ++i)
    {
        const int cc = currentCcValue (i);
        lastValue[(size_t) i].store (cc);
        queueParam (i, cc);
    }
    // each sample track's sample, after its machine
    for (int t = 0; t < numTracks; ++t)
        if (const auto h = trackSample[(size_t) t].load(); h != 0
            && isSampleMachine ((int) apvts.getRawParameterValue ("t" + juce::String (t + 1) + "_machine")->load()))
            pickSample (t, h);
}

void CyclesProcessor::requestGet()
{
    getNrpnMsb = getNrpnLsb = -1;
    getCount.store (0);
    getStartTime = msNow();
    getState.store (getWaiting);
    queueNrpn (modded::kGetNrpnLsb, 0);
    queueNrpn (62, 0); // the master EQ (1:63-1:65, notes/101) isn't in the GET dump: 1:62 has it sent too
}

void CyclesProcessor::receiveNrpn (int lsb, int value)
{
    if (lsb == modded::kGetDoneNrpnLsb)
    {
        if (getState.load() == getWaiting)
        {
            getState.store (getDone);
            ++getSerial;
            stateKnown.store (true);
        }
        return;
    }
    auto it = nrpnParams.find (lsb);
    if (it == nrpnParams.end()) return;

    // A mask (the sidechain's Duck T1-T6, each track's LFO Polarity): one toggle per track, bit 0 = T1
    const auto& list = it->second;
    applyingNrpn.store (true);
    for (size_t k = 0; k < list.size(); ++k)
    {
        const int i = list[k];
        const auto& d = *allParams()[(size_t) i].def;
        const int v = list.size() > 1 ? (value >> (int) k) & 1 : fromCc (d, value);
        params[(size_t) i]->setValueNotifyingHost (params[(size_t) i]->convertTo0to1 ((float) v));
    }
    applyingNrpn.store (false);
    for (int i : list)
        lastValue[(size_t) i].store (currentCcValue (i));
    stateKnown.store (true);
    if (getState.load() == getWaiting) getCount += (int) list.size();
    rxTime[(size_t) fxSlot].store (msNow());
}

void CyclesProcessor::syncLastValues()
{
    for (int i = 0; i < (int) params.size(); ++i)
        lastValue[(size_t) i].store (currentCcValue (i));
}

//==============================================================================
void CyclesProcessor::setOption (Option o, bool value)
{
    options[(size_t) o].store (value);
    apvts.state.setProperty (optionProp (o), value, nullptr);
    if (o == optReceive && ! value) incoming.clear();
    if (o == optAudio) audio.setEnabled (value);
}

void CyclesProcessor::requestUsbAudioMode (bool sixChannels)
{
    queueNrpn (47, sixChannels ? 1 : 0);
    usbModeRequestTime.store (msNow());
}

void CyclesProcessor::setAudioTrimMs (int ms)
{
    audioTrimMs.store (juce::jlimit (-20, 50, ms));
    apvts.state.setProperty (propAudioTrim, audioTrimMs.load(), nullptr);
    updateLatency();
}

void CyclesProcessor::setChannel (int slot, int channel)
{
    channels[(size_t) slot].store (juce::jlimit (1, 16, channel));
    apvts.state.setProperty (channelProp (slot), getChannel (slot), nullptr);
    rebuildLookup();
}

void CyclesProcessor::setClockOffsetMs (int ms)
{
    clockOffsetMs.store (juce::jlimit (0, 250, ms));
    apvts.state.setProperty (propClockOffset, clockOffsetMs.load(), nullptr);
}

void CyclesProcessor::setLinkMode (DeviceLink::Mode m)
{
    apvts.state.setProperty (propLinkMode, (int) m, nullptr);
    link.setMode (m);
}

void CyclesProcessor::setManualPorts (const juce::String& in, const juce::String& out)
{
    apvts.state.setProperty (propMidiIn, in, nullptr);
    apvts.state.setProperty (propMidiOut, out, nullptr);
    link.setManualPorts (in, out);
}

void CyclesProcessor::rebuildLookup()
{
    for (auto& row : lookup) row.fill (-1);

    const auto& infos = allParams();
    for (size_t i = 0; i < infos.size(); ++i)
        if (infos[i].def->cc >= 0) // NRPN-only parameters (sidechain) are write-only
            lookup[(size_t) getChannel (infos[i].slot) - 1][(size_t) infos[i].def->cc] = (int) i;
}

bool CyclesProcessor::isCyclesChannel (int channel) const noexcept
{
    for (auto& c : channels)
        if (c.load() == channel) return true;
    return false;
}

//==============================================================================
void CyclesProcessor::onDeviceMessage (const juce::MidiMessage& m)
{
    // The firmware's sample report: NRPN 1:59..1:61 on the FX channel (followed even with RECEIVE off)
    if (m.isController() && m.getChannel() == getChannel (fxSlot))
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 99) rxNrpnMsb = v;
        else if (cc == 98) rxNrpnLsb = v;
        else if ((cc == 6 || cc == 38) && rxNrpnMsb == 1 && rxNrpnLsb >= 59 && rxNrpnLsb <= 61)
        {
            rxValue = cc == 6 ? v << 7 : (rxValue & 0x3f80) | v;
            onSampleNrpn (rxNrpnLsb, rxValue, cc == 38);
            return;
        }
        // The project list and the current project (NRPN 1:67-1:69, notes/109), also with RECEIVE off
        else if (cc == 6 && rxNrpnMsb == 1 && rxNrpnLsb >= projSlotNrpn && rxNrpnLsb <= projDirtyNrpn && rxNrpnLsb != projLoadNrpn)
        {
            onProjectNrpn (rxNrpnLsb, v);
            return;
        }
        // The master EQ (NRPN 1:63-1:65, notes/101): the device sends CC 99, 98, 6 when its EQ knobs turn (also
        // followed with RECEIVE off, like turning the EQ on a DJ mixer); applied on the message thread
        else if (cc == 6 && rxNrpnMsb == 1 && rxNrpnLsb >= modded::kMasterEqNrpnLsb && rxNrpnLsb < modded::kMasterEqNrpnLsb + 3)
        {
            rxEq[(size_t) (rxNrpnLsb - modded::kMasterEqNrpnLsb)].store (v);
            return;
        }
    }
    if (m.isController() && (getOption (optReceive) || getState.load() == getWaiting))
        incoming.push (MidiQueue::pack (m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue()));
}

void CyclesProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // The Cycles' USB audio pauses while it loads a project: no +Drive reads then, nor for 3 s after
    if (audio.isStalled())
        samples.fs.holdOff (3000);

    for (int b = 0; b < 3; ++b)
        if (const int v = rxEq[(size_t) b].exchange (-1); v >= 0)
            receiveNrpn (modded::kMasterEqNrpnLsb + b, v);

    juce::uint32 msg;
    while (incoming.pop (msg))
    {
        const bool fromHost = (msg & MidiQueue::fromHost) != 0;
        const bool getting = getState.load() == getWaiting;
        if (! fromHost && ! getOption (optReceive) && ! getting) continue;

        // NRPN from a modded firmware on the FX channel (sidechain, LFO polarity, the end of a GET): CC 99, 98, 6, 38
        if (! fromHost && MidiQueue::channelOf (msg) == getChannel (fxSlot))
        {
            const int cc = MidiQueue::ccOf (msg), v = MidiQueue::valueOf (msg);
            if (cc == 99) { getNrpnMsb = v; continue; }
            if (cc == 98) { getNrpnLsb = v; continue; }
            if (cc == 6)  { getDataMsb = v; continue; }
            if (cc == 38)
            {
                if (getNrpnMsb == 1 && getNrpnLsb >= 0) receiveNrpn (getNrpnLsb, getDataMsb);
                continue;
            }
        }

        // Track select from a modded firmware: CC 119 on the FX channel, value 0-5 = T1-T6. FOLLOW shows that track.
        if (! fromHost && MidiQueue::ccOf (msg) == trackSelectCc && MidiQueue::channelOf (msg) == getChannel (fxSlot))
        {
            if (const int t = MidiQueue::valueOf (msg); t < numTracks)
            {
                rxTime[(size_t) t].store (msNow());
                lastIncomingSlot.store (t);
                ++incomingSerial;
            }
            continue;
        }

        const int index = lookup[(size_t) MidiQueue::channelOf (msg) - 1][(size_t) MidiQueue::ccOf (msg)];
        if (index < 0) continue;

        const auto& info = allParams()[(size_t) index];
        const int raw = MidiQueue::valueOf (msg);

        // Parameter updates for this CC: normally one; CC 27 (track FX) sets FX On, and FX Type unless it's Off.
        std::pair<int, int> updates[2] = { { index, fromCc (*info.def, raw) }, { -1, 0 } };
        if (partner[(size_t) index] >= 0)
        {
            const int on = info.def->key == "fxon" ? index : partner[(size_t) index];
            const int type = partner[(size_t) on];
            updates[0] = { on, raw > 0 ? 1 : 0 };
            if (raw > 0) updates[1] = { type, juce::jlimit (0, allParams()[(size_t) type].def->choices.size() - 1, raw - 1) };
            lastValue[(size_t) on].store (raw);
            lastValue[(size_t) type].store (raw);
        }
        else
        {
            // Record first, so parameterChanged() doesn't send it back to the device.
            lastValue[(size_t) index].store (toCc (*info.def, updates[0].second));
        }

        struct InsertGuard
        {
            std::atomic<bool>& flag;
            InsertGuard (std::atomic<bool>& f, bool on) : flag (f) { flag.store (on); }
            ~InsertGuard() { flag.store (false); }
        } insertGuard { applyingInsert, partner[(size_t) index] >= 0 };

        if (fromHost)
        {
            // A CC from the DAW's MIDI track, already on its way to the device: just show it.
            for (auto [i, v] : updates)
                if (i >= 0) params[(size_t) i]->setValueNotifyingHost (params[(size_t) i]->convertTo0to1 ((float) v));
            stateKnown.store (true);
            txTime[(size_t) info.slot].store (msNow());
            continue;
        }

        // Hold a gesture open while the hardware knob moves, so the DAW records it as automation. A GET's answer is
        // the device's sound, not a knob move: no gesture.
        for (auto [i, v] : updates)
        {
            if (i < 0) continue;
            auto* p = params[(size_t) i];
            if (getting)
            {
                p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
                ++getCount;
                continue;
            }
            if (! gestureOpen[(size_t) i])
            {
                p->beginChangeGesture();
                gestureOpen[(size_t) i] = true;
            }
            gestureLastTouch[(size_t) i] = now;
            p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
        }

        stateKnown.store (true);
        lastReceived.store (msg);
        rxTime[(size_t) info.slot].store (msNow());
        lastIncomingSlot.store (info.slot);
        ++incomingSerial;
    }

    for (size_t i = 0; i < gestureOpen.size(); ++i)
        if (gestureOpen[i] && now - gestureLastTouch[i] > 350.0)
        {
            params[i]->endChangeGesture();
            gestureOpen[i] = false;
        }

    if (getState.load() == getWaiting && msNow() - getStartTime > 2000)
    {
        getState.store (getNoAnswer); // firmware without GET, FX channel off, or no MIDI from the device
        ++getSerial;
    }

    if (getPending.load())
    {
        const auto status = link.getStatus();
        if (status == DeviceLink::Status::Connected || status == DeviceLink::Status::Host)
        {
            getPending.store (false);
            requestGet();
        }
    }

    if (projectListPending.load())
    {
        const auto status = link.getStatus();
        if (status == DeviceLink::Status::Connected || status == DeviceLink::Status::Host)
        {
            projectListPending.store (false);
            requestProjectList();
        }
    }
    // No answer to a load (the load was cancelled on the device, or a firmware without it): stop waiting
    if (projectLoading.load() && (juce::uint32) msNow() - projectLoadTime > 20000)
    {
        projectLoading.store (false);
        ++projectSerial;
    }

    if (trackSampleDirty.exchange (false))
        for (int t = 0; t < numTracks; ++t)
            apvts.state.setProperty ("sample" + juce::String (t + 1),
                                     juce::String::toHexString ((juce::int64) trackSample[(size_t) t].load()), nullptr);

    if (recallPending.load())
    {
        const auto status = link.getStatus();
        if (status == DeviceLink::Status::Connected || status == DeviceLink::Status::Host)
        {
            recallPending.store (false);
            sendAll();
        }
    }
}

void CyclesProcessor::hiResTimerCallback()
{
    if (! link.hasDirectOutput()) return;

    juce::uint32 msg;
    if (outgoing.pop (msg))
        link.sendNow (MidiQueue::toMessage (msg));
}

//==============================================================================
void CyclesProcessor::renderAudio (juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
    const int n = buffer.getNumSamples();

    auto busRange = [&] (int bus, int& first, int& count)
    {
        auto* b = getBus (false, bus);
        if (b == nullptr || ! b->isEnabled()) return false;
        first = getChannelIndexInProcessBlockBuffer (false, bus, 0);
        count = b->getNumberOfChannels();
        return count > 0 && first + count <= buffer.getNumChannels();
    };

    std::array<float, 2 + numTracks> peaks {};
    const int got = n <= streamBuffer.getNumSamples()
                  ? audio.read (streamBuffer.getArrayOfWritePointers(), n, currentSampleRate) : 0;
    int mainFirst = 0, mainCount = 0;
    const bool hasMain = busRange (0, mainFirst, mainCount);

    if (got == 2)
    {
        // Stereo USB audio (stock, or 2CH): the device's full mix on the main output.
        if (hasMain)
            for (int c = 0; c < mainCount; ++c)
                buffer.copyFrom (mainFirst + c, 0, streamBuffer, juce::jmin (c, 1), 0, n);
    }
    else if (got >= numTracks)
    {
        // 6CH: one mono stem per track, before pan and the delay/reverb. Pan them like the device does
        // and sum them on the main output.
        for (int t = 0; t < numTracks; ++t)
        {
            const float* stem = streamBuffer.getReadPointer (t);
            const float p = juce::jlimit (-1.0f, 1.0f, (panParams[(size_t) t]->load() - 64.0f) / 63.0f);
            const float gl = p > 0.0f ? 1.0f - p : 1.0f, gr = p < 0.0f ? 1.0f + p : 1.0f;

            int first = 0, count = 0;
            if (busRange (1 + t, first, count))
            {
                if (count == 1) buffer.copyFrom (first, 0, stem, n);
                else
                {
                    buffer.addFromWithRamp (first,     0, stem, n, panL[(size_t) t], gl);
                    buffer.addFromWithRamp (first + 1, 0, stem, n, panR[(size_t) t], gr);
                }
            }
            if (hasMain)
            {
                buffer.addFromWithRamp (mainFirst, 0, stem, n, panL[(size_t) t], gl);
                if (mainCount > 1) buffer.addFromWithRamp (mainFirst + 1, 0, stem, n, panR[(size_t) t], gr);
            }
            panL[(size_t) t] = gl;
            panR[(size_t) t] = gr;
            peaks[(size_t) (2 + t)] = juce::FloatVectorOperations::findMaximum (stem, n);
            peaks[(size_t) (2 + t)] = juce::jmax (peaks[(size_t) (2 + t)], -juce::FloatVectorOperations::findMinimum (stem, n));
        }
    }

    if (hasMain)
        for (int c = 0; c < juce::jmin (2, mainCount); ++c)
            peaks[(size_t) c] = buffer.getMagnitude (mainFirst + c, 0, n);

    const float decay = std::exp (-(float) n / (float) (currentSampleRate * 0.25));
    for (size_t i = 0; i < peaks.size(); ++i)
        meters[i].store (juce::jmax (peaks[i], meters[i].load() * decay));
}

void CyclesProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    renderAudio (buffer);

    const int numSamples = buffer.getNumSamples();
    const bool direct = link.hasDirectOutput();
    juce::MidiBuffer out, toDevice;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();

        if (direct)
        {
            // Like Overbridge, the plugin's MIDI track plays the device: notes, CCs, bends and program
            // changes go straight to it, timed with the clock. CCs also move the plugin's knobs.
            if (getOption (optClock) && (m.isMidiClock() || m.isMidiStart() || m.isMidiStop()
                                         || m.isMidiContinue() || m.isSongPositionPointer()))
                continue; // the plugin sends its own clock
            toDevice.addEvent (m, meta.samplePosition);
            if (m.isController() && isCyclesChannel (m.getChannel()))
                incoming.push (MidiQueue::pack (m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue())
                               | MidiQueue::fromHost);
            continue;
        }

        // Host MIDI routing: the track's MIDI in is the device. CCs on Model:Cycles channels are captured,
        // everything else passes through.
        if (m.isController() && isCyclesChannel (m.getChannel()))
        {
            if (getOption (optReceive))
                incoming.push (MidiQueue::pack (m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue()));
            continue;
        }
        out.addEvent (m, meta.samplePosition);
    }

    // Transport sync: clock, SPP, start/continue/stop from the DAW's playhead.
    juce::MidiBuffer clockOut;
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (getOption (optClock))
        if (auto* ph = getPlayHead())
            pos = ph->getPosition();
    clock.process (pos, numSamples, currentSampleRate, clockOut);
    clockRunning.store (getOption (optClock) && pos.hasValue() && pos->getIsPlaying());

    if (direct)
    {
        // Without audio streaming, delay by one block so the clock lines up with the audio the DAW is about
        // to play. With streaming the DAW compensates the reported delay instead.
        toDevice.addEvents (clockOut, 0, numSamples, 0);
        const double blockMs = audioAligned.load() ? 0.0 : 1000.0 * currentBlockSize / currentSampleRate;
        link.sendBlock (toDevice, currentSampleRate, blockMs + clockOffsetMs.load());
    }
    else
    {
        out.addEvents (clockOut, 0, numSamples, 0);

        // CCs through the host's MIDI output, ~1 ms apart.
        const int spacing = juce::jmax (1, (int) (currentSampleRate / 1000.0));
        juce::uint32 msg;
        for (int p = 0; p < numSamples; p += spacing)
        {
            if (! outgoing.pop (msg)) break;
            out.addEvent (MidiQueue::toMessage (msg), p);
        }
    }

    midi.swapWith (out);
}

//==============================================================================
void CyclesProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty (propStateKnown, stateKnown.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void CyclesProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    loadingState.store (true);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    syncLastValues();
    loadingState.store (false);

    triggerAsyncUpdate(); // settings and ports are applied on the message thread
}

void CyclesProcessor::handleAsyncUpdate()
{
    const auto& s = apvts.state;

    for (int o = 0; o < numOptions; ++o)
        options[(size_t) o].store ((bool) s.getProperty (optionProp (o), optionDefaults[o]));
    for (int slot = 0; slot <= numTracks; ++slot)
        channels[(size_t) slot].store (juce::jlimit (1, 16, (int) s.getProperty (channelProp (slot), slot + 1)));
    clockOffsetMs.store ((int) s.getProperty (propClockOffset, 0));
    audioTrimMs.store (juce::jlimit (-20, 50, (int) s.getProperty (propAudioTrim, 0)));
    audio.setEnabled (getOption (optAudio));
    updateLatency();
    rebuildLookup();

    link.setManualPorts (s.getProperty (propMidiIn).toString(), s.getProperty (propMidiOut).toString());
    link.setMode ((DeviceLink::Mode) juce::jlimit (0, 2, (int) s.getProperty (propLinkMode, 0)));

    for (int t = 0; t < numTracks; ++t)
        trackSample[(size_t) t].store ((juce::uint32) s.getProperty ("sample" + juce::String (t + 1), "0").toString().getHexValue64());

    // Total recall on project load.
    stateKnown.store ((bool) s.getProperty (propStateKnown, false));
    if (getOption (optRecall) && stateKnown.load())
        recallPending.store (true);
}

juce::AudioProcessorEditor* CyclesProcessor::createEditor() { return new CyclesEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CyclesProcessor(); }
