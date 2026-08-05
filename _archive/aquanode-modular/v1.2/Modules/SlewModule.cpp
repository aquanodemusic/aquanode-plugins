#include "SlewModule.h"

static aquanode::ModuleDescriptor slewDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.slew";
    d.displayName = "Slew Limiter";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 8;
    d.sockets = {
        modIn ("modIn", "Mod In"),
        modOut ("modOut", "Mod Out")
    };
    d.params = {
        makeRotary ("rise", "Rise", 0.1f, 5000.0f, 50.0f, 0, "ms", true),
        makeRotary ("fall", "Fall", 0.1f, 5000.0f, 50.0f, 0, "ms", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SlewModule, slewDescriptor)
