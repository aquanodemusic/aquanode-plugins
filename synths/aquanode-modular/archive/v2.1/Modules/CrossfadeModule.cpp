#include "CrossfadeModule.h"

static aquanode::ModuleDescriptor crossfadeDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.crossfade";
    d.displayName = "Crossfade";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 17;
    d.sockets = {
        audioIn  ("aIn",    "A In"),
        audioIn  ("bIn",    "B In"),
        modIn    ("fadeIn", "Fade In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("fade",     "Fade",      0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("modDepth", "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CrossfadeModule, crossfadeDescriptor)
