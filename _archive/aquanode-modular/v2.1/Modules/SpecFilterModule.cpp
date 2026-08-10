#include "SpecFilterModule.h"

static aquanode::ModuleDescriptor specFilterDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specfilter";
    d.displayName = "Spectral Filter";
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
