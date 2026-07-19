#include "WaveshaperModule.h"

static aquanode::ModuleDescriptor waveshaperDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.waveshaper";
    d.displayName = "Waveshaper";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 12;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("drive",  "Drive",   1.0f, 50.0f, 2.0f, 0, {}, true),
        makeRotary ("out",    "Out",     0.0f, 2.0f, 0.8f, 0),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%"),
        makeCombo  ("shape", "Shape", { "Tanh", "Sine Fold", "Hard Clip", "Rectify" }, 0, 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (WaveshaperModule, waveshaperDescriptor)
