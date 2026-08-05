#include "DCOffsetModule.h"

static aquanode::ModuleDescriptor dcOffsetDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.dcoffset";
    d.displayName = "DC Offset";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 20;
    d.sockets = {
        modIn  ("modIn",  "Mod In"),
        modOut ("modOut", "Mod Out")
    };
    d.params = {
        makeRotary ("offset", "Offset", -1.0f, 1.0f, 0.0f, 0),
        makeRotary ("gain",   "Gain",   -2.0f, 2.0f, 1.0f, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DCOffsetModule, dcOffsetDescriptor)
