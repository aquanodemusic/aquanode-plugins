#include "GateModule.h"

static aquanode::ModuleDescriptor gateDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.gate";
    d.displayName = "Gate";
    d.description =
        "Outputs 1 while a voice's note is held and 0 after release, with a tiny anti-click ramp. "
        "Feed it through a Slew Limiter to build custom attack/release shapes out of nothing.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 7;
    d.sockets = { modOut ("modOut", "Mod Out") };
    return d;
}

AQUANODE_REGISTER_MODULE (GateModule, gateDescriptor)
