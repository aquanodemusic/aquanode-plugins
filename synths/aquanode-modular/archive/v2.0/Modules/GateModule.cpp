#include "GateModule.h"

static aquanode::ModuleDescriptor gateDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.gate";
    d.displayName = "Gate";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 7;
    d.sockets = { modOut ("modOut", "Mod Out") };
    return d;
}

AQUANODE_REGISTER_MODULE (GateModule, gateDescriptor)
