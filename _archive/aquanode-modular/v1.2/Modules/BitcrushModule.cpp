#include "BitcrushModule.h"

static aquanode::ModuleDescriptor bitcrushDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.bitcrush";
    d.displayName = "Bitcrush";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 17;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("bits",   "Bits",    1.0f, 16.0f, 8.0f, 0),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BitcrushModule, bitcrushDescriptor)
