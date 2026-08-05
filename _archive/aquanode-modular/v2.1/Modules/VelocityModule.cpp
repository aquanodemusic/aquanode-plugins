#include "VelocityModule.h"

static aquanode::ModuleDescriptor velocityDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.velocity";
    d.displayName = "Velocity";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 6;
    d.sockets = { modOut ("modOut", "Mod Out") };
    return d;
}

AQUANODE_REGISTER_MODULE (VelocityModule, velocityDescriptor)
