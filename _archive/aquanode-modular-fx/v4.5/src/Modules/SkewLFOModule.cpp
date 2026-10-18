#include "SkewLFOModule.h"

using namespace aquanode;

static ModuleDescriptor skewLfoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.skewlfo";
    d.displayName = "Skew LFO";
    d.description =
        "A free-running LFO whose Waveform skews the triangle from falling saw (0) to "
        "rising ramp (10) and sets the Pulse Out's duty cycle. One shared LFO for all voices, free "
        "or locked to the host tempo. Pulse Out is the classic S&H clock; Tri Out "
        "wants a Dual Filter MG input or a pitch input.";
    d.section = ModuleSection::Utility;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 40;
    d.sockets = {
        modOut ("triOut",   "Tri Out"),
        modOut ("pulseOut", "Pulse Out")
    };
    d.params = {
        makeRotary ("rate",     "Frequency", 0.05f, 30.0f, 1.0f, 0, "Hz", true),
        makeRotary ("waveform", "Waveform",  0.0f, 10.0f, 5.0f, 0),
        makeCombo  ("sync",     "Sync", { "Free", "Tempo" }, 0, 1, 2),
        makeCombo  ("division", "Division",
                    { "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3, 1, 2)
            .visibleWhen ("sync", 1.0f),
        makeRotary ("level",    "Level",     0.0f, 1.0f, 1.0f, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SkewLFOModule, skewLfoDescriptor)
