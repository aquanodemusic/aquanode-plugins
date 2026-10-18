#include "RingModModule.h"

static aquanode::ModuleDescriptor ringModDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.ringmod";
    d.displayName = "Ring Mod";
    d.description =
        "Multiplies In A by In B, with Depth blending from clean A to full ring modulation "
        "(classic AM in between). In B reads as silence when unpatched. Flexible: two per-voice "
        "oscillators ring-modulate per note.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 9;
    d.sockets = {
        audioIn ("inA", "In A"),
        audioIn ("inB", "In B"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("depth", "Depth", 0.0f, 100.0f, 100.0f, 0, "%"),
        makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RingModModule, ringModDescriptor)
