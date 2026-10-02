#include "SpecFilterModule.h"

static aquanode::ModuleDescriptor specFilterDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specfilter";
    d.displayName = "Spectral Filter";
    d.description =
        "The drawn curve is a per-bin gain across the whole spectrum: draw notches, brick walls, "
        "spectral holes or comb shapes freehand. Curve at 0.83 is unity; the top is +12 dB and "
        "the bottom is silence.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 19;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    SpectralModuleBase::addCurveParams (d, SpecFilterModule::kUnityCurve);
    return d;
}

AQUANODE_REGISTER_MODULE (SpecFilterModule, specFilterDescriptor)
