#include "PrecisionUtilityModule.h"

static aquanode::ModuleDescriptor precisionDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.precision";
    d.displayName = "Precision";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 11;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volL",   "Vol L",   0.0f, 2.0f, 1.0f, 0),
        makeRotary ("volR",   "Vol R",   0.0f, 2.0f, 1.0f, 0),
        makeRotary ("delayL", "Delay L", 0.0f, 200.0f, 0.0f, 0, "ms"),
        makeRotary ("delayR", "Delay R", 0.0f, 200.0f, 0.0f, 0, "ms"),
        makeRotary ("phase",  "Phase",   0.0f, 100.0f, 0.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PrecisionUtilityModule, precisionDescriptor)
