#include "CompressorModule.h"

static aquanode::ModuleDescriptor compressorDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.compress";
    d.displayName = "Compress";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 16;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, 0, "dB"),
        makeRotary ("ratio",     "Ratio",     1.0f, 20.0f, 4.0f, 0, ":1", true),
        makeRotary ("attack",    "Attack",    0.1f, 200.0f, 10.0f, 0, "ms", true),
        makeRotary ("release",   "Release",   5.0f, 2000.0f, 150.0f, 0, "ms", true),
        makeRotary ("makeup",    "Makeup",    0.0f, 24.0f, 0.0f, 0, "dB")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CompressorModule, compressorDescriptor)
