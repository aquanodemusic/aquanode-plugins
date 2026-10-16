#include "AudioInModule.h"

static aquanode::ModuleDescriptor audioInDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.audioIn";
    d.displayName = "Audio In";
   #if AQUANODE_FX_BUILD
    d.description =
        "Brings the host's audio input into the patch - the signal on the track this effect sits "
        "on. Start an effect patch here, run it through whatever modules you like, and end at Audio "
        "Out. (Not needed when you're using Aquanode as a sound generator - the synth modules can "
        "drive themselves from their own MIDI.)";
   #else
    d.description = "Passes host audio input into the patch if available. ";
   #endif
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 0;
    d.sockets = { audioOut ("audioOut", "Audio Out") };
    d.params = { makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0) };
    return d;
}

AQUANODE_REGISTER_MODULE (AudioInModule, audioInDescriptor)
