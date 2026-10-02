#include "MixerModule.h"

static aquanode::ModuleDescriptor mixerDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.mixer";
    d.displayName = "Mixer";
    d.description =
        "Four audio inputs with individual levels plus a master. Flexible: it can sit inside a "
        "voice chain mixing FM operators, or in the global effect chain.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 10;
    d.sockets = {
        audioIn ("in1", "In 1"),
        audioIn ("in2", "In 2"),
        audioIn ("in3", "In 3"),
        audioIn ("in4", "In 4"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("level1", "Lvl 1",  0.0f, 2.0f, 1.0f, 0),
        makeRotary ("level2", "Lvl 2",  0.0f, 2.0f, 1.0f, 0),
        makeRotary ("level3", "Lvl 3",  0.0f, 2.0f, 1.0f, 0),
        makeRotary ("level4", "Lvl 4",  0.0f, 2.0f, 1.0f, 0),
        makeRotary ("master", "Master", 0.0f, 2.0f, 1.0f, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MixerModule, mixerDescriptor)
