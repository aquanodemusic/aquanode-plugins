#include "NoiseModule.h"

static aquanode::ModuleDescriptor noiseDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "osc.noise";
    d.displayName = "Noise";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 3;
    d.sockets = { audioOut ("audioOut", "Audio Out") };
    d.params = {
        makeCombo  ("type", "Type", { "White", "Pink" }, 0, 0, 2),
        makeRotary ("level", "Level", 0.0f, 1.0f, 0.8f, 0)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 0, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (NoiseModule, noiseDescriptor)
