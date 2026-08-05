#include "SplitterModule.h"

static aquanode::ModuleDescriptor splitterDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.splitter";
    d.displayName = "L/R Splitter";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 2;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("leftOut",  "Left Out"),
        audioOut ("rightOut", "Right Out")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SplitterModule, splitterDescriptor)
