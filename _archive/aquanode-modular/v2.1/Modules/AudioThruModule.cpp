#include "AudioThruModule.h"

static aquanode::ModuleDescriptor audioThruDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.thru";
    d.displayName = "Audio Thru";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 12;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    // deliberately no parameters: this module IS a piece of wire
    return d;
}

AQUANODE_REGISTER_MODULE (AudioThruModule, audioThruDescriptor)
