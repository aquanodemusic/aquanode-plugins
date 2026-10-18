#include "AnalogEGModule.h"

using namespace aquanode;

static ModuleDescriptor analogEgDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.analogeg";
    d.displayName = "Analog EG";
    d.description =
        "An RC envelope like a vintage analog synth's: Delay, Attack, Hold, Decay, Sustain, "
        "Release, with a capacitor's curved attack and exponential decay and release. Mod Out "
        "wants a Volume's Vol Mod (the VCA) or a filter's EG input; Rev Out is the inverted "
        "envelope.";
    d.section = ModuleSection::Utility;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 41;
    d.sockets = {
        modOut ("modOut", "Mod Out"),
        modOut ("revOut", "Rev Out")
    };
    d.params = {
        makeRotary ("delay",    "Delay",    0.0f, 5000.0f, 0.0f, 0, "ms"),
        makeRotary ("attack",   "Attack",   0.5f, 10000.0f, 5.0f, 0, "ms", true),
        makeRotary ("hold",     "Hold",     0.0f, 5000.0f, 0.0f, 0, "ms"),
        makeRotary ("decay",    "Decay",    1.0f, 20000.0f, 300.0f, 1, "ms", true),
        makeRotary ("sustain",  "Sustain",  0.0f, 1.0f, 0.7f, 1),
        makeRotary ("release",  "Release",  1.0f, 20000.0f, 300.0f, 1, "ms", true),
        makeRotary ("velocity", "Velocity", 0.0f, 100.0f, 0.0f, 2, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AnalogEGModule, analogEgDescriptor)
