#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>
#include "ModdedMap.h"

// Model:Cycles MIDI CC map.
// Source: Pencil Research open MIDI CC database (Elektron / Model Cycles.csv),
// which mirrors the CC table in the Model:Cycles manual appendix.
//
// Track parameters are sent on each track's MIDI channel (default 1-6).
// FX parameters are sent on the FX channel (configurable on the Setup tab).

namespace cycles
{
enum class Kind
{
    Dial,   // 0-127 knob, CC value == knob value
    Toggle, // Off/On dropdown, sent as 0 / 127, received as 0 = Off, >0 = On
    List    // dropdown, CC value == list index
};

struct Def
{
    juce::String key;       // stable parameter id suffix
    juce::String name;      // display name
    juce::String section;   // UI page
    int cc;
    Kind kind;
    int defaultValue;       // knob value or list index
    juce::StringArray choices;
    int nrpn = -1;          // >= 0: sent as NRPN 1:nrpn on the FX channel instead of a CC (cc is then -1)
    int maxValue = 127;     // top of a dial's range
    std::vector<int> listCc {}; // a list whose CC values aren't 0, 1, 2...: the CC value of each entry
};

// LFO destinations as the device lists them (by sort key), with the CC 105 value of each: the sound word it modulates.
// From the OS's parameter descriptors (flags 0x600); Filter 28-31 are the modded firmware's track filter.
inline const std::vector<std::pair<juce::String, int>>& lfoDestinations()
{
    static const std::vector<std::pair<juce::String, int>> d = {
        { "None", 0 }, { "Pitch", 10 }, { "Fine Tune", 17 }, { "Decay", 18 }, { "Color", 11 }, { "Shape", 12 },
        { "Sweep", 13 }, { "Contour", 14 }, { "Filter Cutoff", 28 }, { "Filter Reso", 29 }, { "Filter Env", 30 },
        { "Filter Env Decay", 31 }, { "Delay Send", 20 }, { "Reverb Send", 21 }, { "Vol + Dist", 19 }, { "Pan", 22 },
        { "Punch", 15 }, { "Gate", 16 },
    };
    return d;
}

inline juce::StringArray lfoDestNames()
{
    juce::StringArray a;
    for (auto& [name, cc] : lfoDestinations()) a.add (name);
    return a;
}

inline std::vector<int> lfoDestCcs()
{
    std::vector<int> v;
    for (auto& [name, cc] : lfoDestinations()) v.push_back (cc);
    return v;
}

// The modded firmware's 32 machines (ModdedMap.h, generated from the firmware build). On a stock Model:Cycles only
// the first six exist.
inline const juce::StringArray& machineNames()
{
    static const juce::StringArray m = []
    {
        juce::StringArray a;
        for (auto& mc : modded::machines) a.add (mc.name);
        return a;
    }();
    return m;
}

inline juce::StringArray splitValues (const char* values)
{
    juce::StringArray a;
    a.addTokens (values, "|", "");
    if (a.size() != 128) // 128 entries: one text per value 0-127, "" = show the number (a morphing knob between names)
        a.removeEmptyStrings();
    return a;
}

inline const juce::StringArray& filterTypes()
{
    static const juce::StringArray t = splitValues (modded::filterParams[0].values);
    return t;
}

inline const std::vector<Def>& trackDefs()
{
    // Grouped into pages, Overbridge-style: MACHINE (sound source), MIX, TRIG, LFO.
    static const std::vector<Def> defs = {
        // ---- MACHINE ----
        { "machine", "Machine",      "MACHINE", 70, Kind::List,   0, machineNames() },
        { "pitch",   "Pitch",        "MACHINE", 65, Kind::Dial,  64, {} },
        { "decay",   "Decay",        "MACHINE", 80, Kind::Dial,  64, {} },
        { "color",   "Color",        "MACHINE", 16, Kind::Dial,  64, {} },
        { "shape",   "Shape",        "MACHINE", 17, Kind::Dial,  64, {} },
        { "sweep",   "Sweep",        "MACHINE", 18, Kind::Dial,  64, {} },
        { "contour", "Contour",      "MACHINE", 19, Kind::Dial,  64, {} },
        { "punch",   "Punch",        "MACHINE", 66, Kind::Toggle, 0, { "Off", "On" } },
        { "gate",    "Gate",         "MACHINE", 67, Kind::Toggle, 0, { "Off", "On" } },
        // ---- EXTRAS (modded firmware: hold MACHINES + PITCH / DECAY / COLOR / SHAPE) ----
        { "x1",      "Extra 1",      "EXTRAS", 20, Kind::Dial,   0, {} },
        { "x2",      "Extra 2",      "EXTRAS", 21, Kind::Dial,   0, {} },
        { "x3",      "Extra 3",      "EXTRAS", 22, Kind::Dial,   0, {} },
        { "x4",      "Extra 4",      "EXTRAS", 23, Kind::Dial,   0, {} },
        // ---- FILTER (modded firmware: per-track filter, hold MACHINES + LFO SPEED / SWEEP / CONTOUR / DELAY / REVERB) ----
        { "ftype",   "Filter Type",  "FILTER", 26, Kind::List,   0, filterTypes() },
        { "fcut",    "Cutoff",       "FILTER", 74, Kind::Dial, 127, {} },
        { "freso",   "Reso",         "FILTER", 71, Kind::Dial,   0, {} },
        { "fenv",    "Env",          "FILTER", 24, Kind::Dial,  64, {} },
        { "fdec",    "Env Decay",    "FILTER", 25, Kind::Dial,  64, {} },
        // ---- TRACK FX (modded firmware: per-track insert, CC 27 type 0 = Off, 1.. = the types; CC 28 amount) ----
        // FX On and FX Type share CC 27: the plugin sends Off (0) while FX On is off and remembers the type.
        { "fxon",    "FX On",        "TRACK FX", 27, Kind::Toggle, 0, { "Off", "On" } },
        { "fxtype",  "FX Type",      "TRACK FX", 27, Kind::List,   0, { "Drive", "Bitcrush", "Fold", "Ring", "Resonator", "RAT" } },
        { "fxamt",   "FX Amount",    "TRACK FX", 28, Kind::Dial,  64, {} },
        // ---- MIX ----
        { "level",   "Track Level",  "MIX", 95, Kind::Dial, 100, {} },
        { "voldist", "Vol + Dist",   "MIX",  7, Kind::Dial, 100, {} },
        { "pan",     "Pan",          "MIX", 10, Kind::Dial,  64, {} },
        { "delay",   "Delay Send",   "MIX", 12, Kind::Dial,   0, {} },
        { "reverb",  "Reverb Send",  "MIX", 13, Kind::Dial,   0, {} },
        { "mute",    "Mute",         "MIX", 94, Kind::Toggle, 0, { "Off", "On" } },
        // ---- TRIG ----
        { "note",    "Note",         "TRIG",  3, Kind::Dial,  60, {} },
        { "swing",   "Swing",        "TRIG", 15, Kind::Dial,   0, {} },
        { "chance",  "Chance",       "TRIG", 14, Kind::Dial, 127, {} },
        // ---- LFO ---- (Polarity: the modded firmware's LFO Setup > Pol, one per track, all sent as one mask on NRPN
        // 1:56 on the FX channel, notes/81)
        { "lfospd",  "Speed",        "LFO", 102, Kind::Dial, 64, {} },
        { "lfomul",  "Multiplier",   "LFO", 103, Kind::Dial,  0, {} },
        { "lfofade", "Fade",         "LFO", 104, Kind::Dial, 64, {} },
        { "lfodest", "Destination",  "LFO", 105, Kind::List,  0, lfoDestNames(), -1, 127, lfoDestCcs() },
        { "lfowave", "Waveform",     "LFO", 106, Kind::List,   0, { "Triangle", "Sine", "Square", "Saw", "Exponential", "Ramp", "Random" } },
        { "lfophs",  "Start Phase",  "LFO", 107, Kind::Dial,   0, {} },
        { "lfomode", "Mode",         "LFO", 108, Kind::List,   0, { "Free", "Trig", "Hold", "One", "Half" } },
        { "lfopol",  "Polarity",     "LFO",  -1, Kind::List,   0, { "Bi", "Uni" }, modded::kLfoPolarityNrpnLsb },
        { "lfodep",  "Depth",        "LFO", 109, Kind::Dial,  64, {} },
    };
    return defs;
}

// Sidechain of the modded firmware (SETTINGS > Mods, notes/81): NRPN 1:50-1:55 on the FX channel, write-only.
inline const modded::SidechainParam& sidechainParam (const char* key)
{
    for (auto& p : modded::sidechainParams)
        if (juce::String (p.key) == key) return p;
    jassertfalse;
    return modded::sidechainParams[0];
}

inline juce::StringArray msChoices (const char* key)
{
    juce::StringArray a;
    for (auto& v : splitValues (sidechainParam (key).values)) a.add (v + " ms");
    return a;
}

inline const std::vector<Def>& fxDefs()
{
    static const std::vector<Def> defs = []
    {
        std::vector<Def> v = {
            { "dtime", "Delay Time",     "FX", 85, Kind::Dial, 64, {} },
            { "dfb",   "Delay Feedback", "FX", 86, Kind::Dial, 64, {} },
            { "rsize", "Reverb Size",    "FX", 87, Kind::Dial, 64, {} },
            { "rtone", "Reverb Tone",    "FX", 88, Kind::Dial, 64, {} },
        };
        // Master DJ EQ: LOW / MID / HIGH on NRPN 1:63-1:65
        int eqLsb = modded::kMasterEqNrpnLsb;
        for (auto [key, name] : { std::pair { "eqlow", "EQ Low" }, std::pair { "eqmid", "EQ Mid" }, std::pair { "eqhigh", "EQ High" } })
            v.push_back ({ key, name, "MASTER EQ", -1, Kind::Dial, 64, {}, eqLsb++ });
        const auto& src = sidechainParam ("scSrc");
        auto sources = splitValues (src.values);
        sources.set (0, "Off");
        v.push_back ({ "scsrc", "SC Source", "SIDECHAIN", -1, Kind::List, src.defaultValue, sources, src.nrpnLsb });
        const auto& depth = sidechainParam ("scDepth");
        v.push_back ({ "scdepth", "SC Depth", "SIDECHAIN", -1, Kind::Dial, depth.defaultValue, {}, depth.nrpnLsb, depth.max });
        for (auto [key, name, fw] : { std::tuple { "scatk", "SC Attack", "scAtk" }, std::tuple { "schold", "SC Hold", "scHold" },
                                      std::tuple { "screl", "SC Release", "scRel" } })
        {
            const auto& p = sidechainParam (fw);
            v.push_back ({ key, name, "SIDECHAIN", -1, Kind::List, p.defaultValue, msChoices (fw), p.nrpnLsb });
        }
        // Duck mask: one On/Off per track, all sent together as NRPN 1:51 (bit 0 = T1)
        const auto& duck = sidechainParam ("scDuck");
        for (int t = 0; t < 6; ++t)
            v.push_back ({ "scduck" + juce::String (t + 1), "Duck T" + juce::String (t + 1), "DUCKED TRACKS", -1, Kind::Toggle,
                           (duck.defaultValue >> t) & 1, { "Off", "On" }, duck.nrpnLsb });
        return v;
    }();
    return defs;
}

constexpr int numTracks = 6;
constexpr int fxSlot = 6; // "track" index used for the FX channel

struct ParamInfo
{
    juce::String id;   // e.g. "t1_color", "fx_dtime"
    juce::String name; // e.g. "T1 Color"
    int slot;          // 0..5 = track, 6 = FX
    const Def* def;
    int defaultValue;
};

inline juce::String paramId (int slot, const Def& d)
{
    return (slot == fxSlot ? juce::String ("fx") : "t" + juce::String (slot + 1)) + "_" + d.key;
}

// Index of the EMPTY machine (last in the firmware list), or 0 if the map has none
inline int emptyMachine()
{
    for (int i = 0; i < modded::numMachines; ++i)
        if (juce::String (modded::machines[i].name) == "EMPTY") return i;
    return 0;
}

inline const std::vector<ParamInfo>& allParams()
{
    static const std::vector<ParamInfo> list = []
    {
        std::vector<ParamInfo> v;
        for (int t = 0; t < numTracks; ++t)
            for (auto& d : trackDefs())
            {
                // Default kit: EMPTY on all six tracks, like a new kit on the modded firmware
                const int def = d.key == "machine" ? emptyMachine() : d.defaultValue;
                const auto prefix = "T" + juce::String (t + 1) + (d.section == "LFO" ? " LFO " : " ");
                v.push_back ({ paramId (t, d), prefix + d.name, t, &d, def });
            }
        for (auto& d : fxDefs())
            v.push_back ({ paramId (fxSlot, d), "FX " + d.name, fxSlot, &d, d.defaultValue });
        return v;
    }();
    return list;
}

// Plugin value (knob value or list index) -> CC data byte
inline int toCc (const Def& d, int value)
{
    switch (d.kind)
    {
        case Kind::Toggle: return value > 0 ? 127 : 0;
        case Kind::List:
        {
            const int i = juce::jlimit (0, d.choices.size() - 1, value);
            return d.listCc.empty() ? i : d.listCc[(size_t) i];
        }
        case Kind::Dial:   break;
    }
    return juce::jlimit (0, d.maxValue, value);
}

// Machine knobs and extras whose meaning depends on the track's machine: 0..3 = COLOR..CONTOUR, 10..13 = extras
inline int machineSlot (const Def& d)
{
    static const juce::StringArray keys { "color", "shape", "sweep", "contour" };
    if (keys.contains (d.key)) return keys.indexOf (d.key);
    if (d.section == "EXTRAS") return 10 + d.key.getTrailingIntValue() - 1;
    return -1;
}

// Name of a control on a track whose machine is `machine` ("" for an extra that machine doesn't have)
inline juce::String nameFor (const Def& d, int machine)
{
    const int s = machineSlot (d);
    if (s < 0) return d.name;
    const auto& m = modded::machines[juce::jlimit (0, modded::numMachines - 1, machine)];
    // the firmware writes "Tone/ Noise" so its screen can wrap after the slash; the plugin shows "Tone/Noise"
    return (s < 10 ? juce::String (m.knobs[s].name) : juce::String (m.extras[s - 10])).replace ("/ ", "/");
}

// List labels of a machine knob or extra that steps through a list (SMP Mode, 606 Voice...), else empty
inline juce::StringArray valuesFor (const Def& d, int machine)
{
    const int s = machineSlot (d);
    if (s < 0) return {};
    const auto& m = modded::machines[juce::jlimit (0, modded::numMachines - 1, machine)];
    return splitValues (s < 10 ? m.knobs[s].values : m.extraValues[s - 10]);
}

// Text shown for a dial value (Pan reads like the device: L64 .. C .. R63)
// Gain of a master EQ band as the firmware sets it (notes/101): (v/64)^2 up to 64, then up to +6 dB at 127; 0 = kill
inline juce::String eqText (int v)
{
    if (v <= 0) return "KILL";
    const double g = v <= 64 ? (v / 64.0) * (v / 64.0) : 1.0 + (v - 64) * 16958467.0 / 1073741824.0;
    const int db = juce::roundToInt (20.0 * std::log10 (g));
    return (db > 0 ? "+" : "") + juce::String (db) + "dB";
}

inline juce::String dialText (const Def& d, int value, int machine = 0)
{
    const auto list = valuesFor (d, machine);
    if (list.size() == 128) // per-value text (firmware notes/90): a name on a pure choice, else the number
        return list[juce::jlimit (0, 127, value)].isEmpty() ? juce::String (value) : list[juce::jlimit (0, 127, value)];
    if (! list.isEmpty())
    {
        // A knob's list is stepped by the device (value = index); an extra's spans 0-127, as the firmware reads it
        int i = value;
        if (machineSlot (d) >= 10)
            i = juce::String (modded::machines[juce::jlimit (0, modded::numMachines - 1, machine)].name) == "NOISE"
                  ? (value <= 0 ? 0 : value <= 42 ? 1 : value <= 85 ? 2 : 3)   // AUT, then RATE / FOLD / CRUSH thirds
                  : value * list.size() >> 7;                                    // e.g. CLSTR Mode: value x 9 >> 7
        return list[juce::jlimit (0, list.size() - 1, i)];
    }
    if (d.key == "scdepth")
        return juce::String (value / 5 * 5) + "%";
    if (d.section == "MASTER EQ")
        return eqText (value);
    if (d.key == "pan")
        return value == 64 ? juce::String ("C") : value < 64 ? "L" + juce::String (64 - value) : "R" + juce::String (value - 64);
    return juce::String (value);
}

inline const juce::StringArray& trackPages()
{
    static const juce::StringArray p { "MACHINE", "EXTRAS", "FILTER", "MIX", "TRIG", "LFO" };
    return p;
}

// CC data byte -> plugin value
inline int fromCc (const Def& d, int cc)
{
    switch (d.kind)
    {
        case Kind::Toggle: return cc > 0 ? 1 : 0;
        case Kind::List:
        {
            if (d.listCc.empty()) return juce::jlimit (0, d.choices.size() - 1, cc);
            const auto it = std::find (d.listCc.begin(), d.listCc.end(), cc);
            return it == d.listCc.end() ? 0 : (int) (it - d.listCc.begin());
        }
        case Kind::Dial:   break;
    }
    return juce::jlimit (0, d.maxValue, cc);
}
} // namespace cycles
