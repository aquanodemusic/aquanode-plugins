#include "DCOffsetModule.h"

static aquanode::ModuleDescriptor dcOffsetDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.dcoffset";
    d.displayName = "DC Offset";
    d.description =
        "Shifts and scales a signal: Out = In * Gain + Offset. The classic use is making a "
        "bipolar source unipolar (an LFO at Gain 50%, Offset 0.5 sweeps 0..1), or inverting a mod "
        "signal with a negative Gain. With nothing patched in it emits a constant, which makes it "
        "a manual DC source for any Mod In.";
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
