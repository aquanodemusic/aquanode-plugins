#include "KeyTrackModule.h"

static aquanode::ModuleDescriptor keyTrackDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.keytrack";
    d.displayName = "KeyTrack";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 5;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = { makeRotary ("center", "Center", 0.0f, 127.0f, 60.0f, 0, {}, false, 1.0f) };
    return d;
}

AQUANODE_REGISTER_MODULE (KeyTrackModule, keyTrackDescriptor)
