#include "RingModModule.h"

static aquanode::ModuleDescriptor ringModDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.ringmod";
    d.displayName = "Ring Mod";
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
