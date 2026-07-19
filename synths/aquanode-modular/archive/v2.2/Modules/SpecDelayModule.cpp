#include "SpecDelayModule.h"

static aquanode::ModuleDescriptor specDelayDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specdelay";
    d.displayName = "Spectral Delay";
    d.description =
        "Every bin gets its own delay line, set by the drawn curve times: draw a rising "
        "line and the highs arrive after the lows, so chords turn into arpeggios of frequency. "
        "Feedback regenerates each bin into its own delay for shimmer trails.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 22;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("maxTime",  "Max Time", 0.05f, 5.0f, 1.0f, 0, "s", true),
        makeRotary ("feedback", "Feedback", 0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 100.0f, 0, "%")
    };
    SpectralModuleBase::addCurveParams (d, 0.0f);
    return d;
}

AQUANODE_REGISTER_MODULE (SpecDelayModule, specDelayDescriptor)
