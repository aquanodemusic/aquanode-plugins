#include "VolumeModule.h"

static aquanode::ModuleDescriptor volumeDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.volume";
    d.displayName = "Volume";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 3;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("volMod", "Vol Mod"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = { makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0) };
    return d;
}

AQUANODE_REGISTER_MODULE (VolumeModule, volumeDescriptor)
