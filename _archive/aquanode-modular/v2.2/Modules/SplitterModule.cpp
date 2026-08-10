#include "SplitterModule.h"

static aquanode::ModuleDescriptor splitterDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.splitter";
    d.displayName = "L/R Splitter";
    d.description =
        "Splits a stereo signal into its channels. Each output stays a full stereo pair, but only "
        "its named channel carries signal - for processing left and right differently and mixing "
        "them back together.";
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
