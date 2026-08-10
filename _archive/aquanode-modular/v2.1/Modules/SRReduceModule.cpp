#include "SRReduceModule.h"

static aquanode::ModuleDescriptor srReduceDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.srreduce";
    d.displayName = "SR Reduce";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 18;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("rate",   "Rate",    200.0f, 44100.0f, 8000.0f, 0, "Hz", true),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SRReduceModule, srReduceDescriptor)
