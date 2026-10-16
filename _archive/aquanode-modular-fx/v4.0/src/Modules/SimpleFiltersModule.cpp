#include "SimpleFiltersModule.h"

using namespace aquanode;

namespace
{
    std::vector<SocketSpec> filterSockets()
    {
        return {
            audioIn  ("audioIn",  "Audio In"),
            modIn    ("modIn",    "Mod In"),
            audioOut ("audioOut", "Audio Out")
        };
    }

    ParamSpec modDepthKnob (int row)
    {
        return makeRotary ("modDepth", "Mod Depth", -100.0f, 100.0f, 0.0f, row, "%");
    }
}

static ModuleDescriptor lowpassDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.lowpass";
    d.displayName = "Lowpass";
    d.description =
        "A clean, stable lowpass: 12 dB/oct (Butterworth at Resonance 0) or a steeper 24 dB/oct. "
        "Mod In sweeps the cutoff exponentially, +-5 octaves at 100% Mod Depth - an ADSR or LFO, "
        "or KeyTrack for exact tracking. Per voice when fed per voice.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 20;
    d.sockets = filterSockets();
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 2000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.0f, 0),
        makeCombo  ("slope",     "Slope",     { "12 dB/oct", "24 dB/oct" }, 0, 1, 2),
        modDepthKnob (0)
    };
    return d;
}

static ModuleDescriptor highpassDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.highpass";
    d.displayName = "Highpass";
    d.description =
        "A clean, stable highpass: 12 dB/oct (Butterworth at Resonance 0) or 24 dB/oct. Thins a "
        "sound from below; Mod In sweeps the cutoff, +-5 octaves at 100% Mod Depth.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 21;
    d.sockets = filterSockets();
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 200.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.0f, 0),
        makeCombo  ("slope",     "Slope",     { "12 dB/oct", "24 dB/oct" }, 0, 1, 2),
        modDepthKnob (0)
    };
    return d;
}

static ModuleDescriptor notchDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.notch";
    d.displayName = "Notch";
    d.description =
        "Cuts a narrow band out and leaves the rest untouched. Width is the notch in octaves; "
        "an LFO on Mod In sweeps it for a phaser-like single notch.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 22;
    d.sockets = filterSockets();
    d.params = {
        makeRotary ("freq",  "Freq",  20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("width", "Width", 0.05f, 4.0f, 1.0f, 0, "oct", true),
        modDepthKnob (0)
    };
    return d;
}

static ModuleDescriptor bellDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.bell";
    d.displayName = "Bell";
    d.description =
        "A single peaking EQ band: boosts or cuts Gain dB around Freq, as wide as Q says. One "
        "band you can sweep per voice - Mod In moves its frequency.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 23;
    d.sockets = filterSockets();
    d.params = {
        makeRotary ("freq", "Freq", 20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("gain", "Gain", -24.0f, 24.0f, 6.0f, 0, "dB"),
        makeRotary ("q",    "Q",    0.1f, 16.0f, 1.0f, 0, {}, true),
        modDepthKnob (0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LowpassModule,  lowpassDescriptor)
AQUANODE_REGISTER_MODULE (HighpassModule, highpassDescriptor)
AQUANODE_REGISTER_MODULE (NotchModule,    notchDescriptor)
AQUANODE_REGISTER_MODULE (BellModule,     bellDescriptor)
