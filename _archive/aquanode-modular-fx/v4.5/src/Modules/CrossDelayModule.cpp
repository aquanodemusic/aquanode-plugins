#include "CrossDelayModule.h"

using namespace aquanode;

static ModuleDescriptor crossDelayDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.crossdelay";
    d.displayName = "Cross Delay";
    d.description =
        "A stereo / cross delay: separate left and right times and "
        "bipolar feedback; Cross mode sends each repeat to the other side. High and Low Damp "
        "shape the repeats, Spread narrows them. At 100 % wet it is a send effect - feed it from "
        "a Volume used as the send level and mix its output back in.";
    d.section = ModuleSection::Effect;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 42;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("timeL",     "L Time",     1.0f, 2700.0f, 375.0f, 0, "ms", true),
        makeRotary ("timeR",     "R Time",     1.0f, 2700.0f, 500.0f, 0, "ms", true),
        makeRotary ("feedbackL", "L Feedback", -100.0f, 100.0f, 30.0f, 1, "%"),
        makeRotary ("feedbackR", "R Feedback", -100.0f, 100.0f, 30.0f, 1, "%"),
        makeCombo  ("mode",      "Mode", { "Stereo", "Cross" }, 0, 1, 2),
        makeRotary ("highDamp",  "High Damp",  1000.0f, 20000.0f, 8000.0f, 2, "Hz", true),
        makeRotary ("lowDamp",   "Low Damp",   20.0f, 1000.0f, 20.0f, 2, "Hz", true),
        makeRotary ("spread",    "Spread",     0.0f, 100.0f, 100.0f, 2, "%"),
        makeRotary ("dryWet",    "Dry/Wet",    0.0f, 100.0f, 30.0f, 3, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CrossDelayModule, crossDelayDescriptor)
