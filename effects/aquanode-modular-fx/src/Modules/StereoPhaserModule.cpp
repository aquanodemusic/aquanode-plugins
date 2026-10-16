#include "StereoPhaserModule.h"

using namespace aquanode;

static ModuleDescriptor stereoPhaserDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.stereophaser";
    d.displayName = "Stereo Phaser";
    d.description =
        "A stereo phaser: allpass chains per side, the right LFO up to 180 degrees "
        "behind the left so the notches move apart and the image swims. Manual sets the sweep "
        "centre, Resonance is bipolar feedback, High Damp darkens the feedback. Very slow Rates "
        "with high Manual make an airy shimmer on pads.";
    d.section = ModuleSection::Effect;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 41;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    // order = StereoPhaserModule::ParamIndex
    d.params = {
        makeRotary ("rate",      "LFO Freq",  0.02f, 20.0f, 0.5f, 0, "Hz", true),
        makeRotary ("lrPhase",   "L-R Phase", 0.0f, 180.0f, 90.0f, 0, "deg"),
        makeRotary ("manual",    "Manual",    50.0f, 19000.0f, 1000.0f, 1, "Hz", true),
        makeRotary ("depth",     "Depth",     0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("resonance", "Resonance", -100.0f, 100.0f, 30.0f, 1, "%"),
        makeRotary ("highDamp",  "High Damp", 1000.0f, 20000.0f, 20000.0f, 2, "Hz", true),
        makeRotary ("dryWet",    "Dry/Wet",   0.0f, 100.0f, 50.0f, 2, "%"),
        makeSteppedList ("stages", "Stages", { "4", "6", "8", "12" }, 1, 2),
        makeCombo  ("shape",     "LFO Wave", { "Triangle", "Sine" }, 0, 3, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (StereoPhaserModule, stereoPhaserDescriptor)
