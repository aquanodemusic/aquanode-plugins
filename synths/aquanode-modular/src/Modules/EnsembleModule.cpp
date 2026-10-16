#include "EnsembleModule.h"

using namespace aquanode;

static ModuleDescriptor ensembleDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.ensemble";
    d.displayName = "Ensemble";
    d.description =
        "A vintage polysynth effect section: Chorus, Phase, or the three-phase BBD Ensemble with its slow "
        "sweep and faster shimmer. Mono in, wide stereo out. Put it after the voices "
        "are summed (a Poly Sum), before any other effect.";
    d.section = ModuleSection::Effect;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 40;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("mode",   "Mode", { "Chorus", "Phase", "Ensemble" }, 2, 0, 2),
        makeRotary ("speed",  "Speed",  0.0f, 10.0f, 3.5f, 0),
        makeRotary ("depth",  "Depth",  0.0f, 100.0f, 85.0f, 1, "%"),
        makeRotary ("spread", "Spread", 0.0f, 100.0f, 80.0f, 1, "%"),
        makeRotary ("mix",    "Mix",    0.0f, 100.0f, 100.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (EnsembleModule, ensembleDescriptor)
