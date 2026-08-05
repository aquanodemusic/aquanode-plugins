#include "StereoWidthModule.h"

static aquanode::ModuleDescriptor stereoWidthDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.stereowidth";
    d.displayName = "Stereo Width";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 20;
    d.sockets = {
        audioIn  ("audioIn",   "Audio In"),
        modIn    ("widthMod",  "Width Mod"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("width",    "Width",     0.0f, 200.0f, 100.0f, 0, "%"),
        makeRotary ("haas",     "Haas",      0.0f, 25.0f, 0.0f, 0, "ms"),
        makeRotary ("bassMono", "Bass Mono", 0.0f, 500.0f, 0.0f, 0, "Hz")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (StereoWidthModule, stereoWidthDescriptor)
