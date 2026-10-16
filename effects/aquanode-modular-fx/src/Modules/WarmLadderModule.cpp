#include "WarmLadderModule.h"

using namespace aquanode;

static ModuleDescriptor warmLadderDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.warmladder";
    d.displayName = "Warm Ladder";
    d.description =
        "A warm, saturating 24 dB resonant low-pass after the classic polysynth chips, singing near "
        "the top of Resonance. EG In wants an Analog EG (EG Int 10 = +6 octaves), MG In a Skew LFO "
        "or LFO (MG Level 10 = +-2 octaves); Kbd Track follows the played key. Flexible: one "
        "filter per note when fed per-voice.";
    d.section = ModuleSection::Filter;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 41;
    d.sockets = {
        audioIn  ("audioIn", "Audio In"),
        modIn    ("egIn",    "EG In"),
        modIn    ("mgIn",    "MG In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    25.0f, 19000.0f, 19000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 10.0f, 0.0f, 0),
        makeRotary ("egInt",     "EG Int",    0.0f, 10.0f, 0.0f, 0),
        makeRotary ("kbdTrack",  "Kbd Track", 0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("mgLevel",   "MG Level",  0.0f, 10.0f, 0.0f, 1),
        makeRotary ("input",     "Input",     0.0f, 2.0f, 1.0f, 1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (WarmLadderModule, warmLadderDescriptor)
