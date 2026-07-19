#include "SampleHoldModule.h"

static aquanode::ModuleDescriptor sampleHoldDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.samplehold";
    d.displayName = "S&H";
    d.description =
        "On every rising edge at Clock In it samples Signal In and holds it; with Signal In "
        "unpatched it samples an internal random source - the classic burbling random voltage. "
        "Clock In wants a Clock or Euclid gate (unpatched, it uses its own Rate). Feed the output "
        "through Quantize for instant melodies.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 14;
    d.sockets = {
        modIn  ("sigIn",   "Signal In"),
        modIn  ("clockIn", "Clock In"),
        modOut ("modOut",  "Mod Out")
    };
    d.params = {
        makeRotary ("rate",  "Rate",  0.1f, 50.0f, 4.0f, 0, "Hz", true),
        makeRotary ("glide", "Glide", 0.0f, 500.0f, 0.0f, 0, "ms")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SampleHoldModule, sampleHoldDescriptor)
