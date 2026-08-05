#include "AudioOutModule.h"

static aquanode::ModuleDescriptor audioOutDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.audioOut";
    d.displayName = "Audio Out";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 1;
    d.sockets = { audioIn ("audioIn", "Audio In") };
    d.params = { makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0) };
    return d;
}

AQUANODE_REGISTER_MODULE (AudioOutModule, audioOutDescriptor)
