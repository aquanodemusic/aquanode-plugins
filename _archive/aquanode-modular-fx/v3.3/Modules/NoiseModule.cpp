#include "NoiseModule.h"

static aquanode::ModuleDescriptor noiseDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "osc.noise";
    d.displayName = "Noise";
    d.description =
        "White or pink noise, per voice: each held note gets its own independent stream, so a "
        "burst gated by an ADSR behaves per note. The classic excitation source - try it into "
        "Resonator, Pluck or the Formant Filter. As a per-voice source it only sounds while notes "
        "are held.";
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
