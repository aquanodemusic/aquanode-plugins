#include "AudioInModule.h"

static aquanode::ModuleDescriptor audioInDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.audioIn";
    d.displayName = "Audio In";
    d.description = "Passes host audio input into the patch if available. ";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 0;
    d.sockets = { audioOut ("audioOut", "Audio Out") };
    d.params = { makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0) };
    return d;
}

AQUANODE_REGISTER_MODULE (AudioInModule, audioInDescriptor)
