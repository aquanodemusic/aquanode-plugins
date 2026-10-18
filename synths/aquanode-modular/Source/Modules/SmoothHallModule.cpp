#include "SmoothHallModule.h"

using namespace aquanode;

static ModuleDescriptor smoothHallDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.smoothhall";
    d.displayName = "Smooth Hall";
    d.description =
        "A smooth hall reverb: an 8-line feedback network with gently moving line lengths, so "
        "long pads decay without metallic ringing. Reverb Time is the RT60; Low and High Gain "
        "shelve what goes in, as on classic studio reverbs. At 100 % wet it works as a send effect "
        "fed from a Volume.";
    d.section = ModuleSection::Effect;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 43;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("time",      "Reverb Time", 0.3f, 10.0f, 2.5f, 0, "s", true),
        makeRotary ("preDelay",  "Pre Delay",   0.0f, 200.0f, 20.0f, 0, "ms"),
        makeRotary ("highDamp",  "High Damp",   1000.0f, 20000.0f, 8000.0f, 0, "Hz", true),
        makeRotary ("lowGain",   "Low Gain",    -15.0f, 15.0f, 0.0f, 1, "dB"),
        makeRotary ("highGain",  "High Gain",   -15.0f, 15.0f, 0.0f, 1, "dB"),
        makeRotary ("dryWet",    "Dry/Wet",     0.0f, 100.0f, 30.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SmoothHallModule, smoothHallDescriptor)
