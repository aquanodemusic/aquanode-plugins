#include "EnvFollowModule.h"

static aquanode::ModuleDescriptor envFollowDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.envfollow";
    d.displayName = "Env Follow";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 16;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modOut  ("modOut",  "Mod Out")
    };
    d.params = {
        makeRotary ("attack",  "Attack",  0.1f, 200.0f, 5.0f,   0, "ms", true),
        makeRotary ("release", "Release", 1.0f, 2000.0f, 150.0f, 0, "ms", true),
        makeRotary ("gain",    "Gain",    0.1f, 8.0f, 1.0f, 0, {}, true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (EnvFollowModule, envFollowDescriptor)
