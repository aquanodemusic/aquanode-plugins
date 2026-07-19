#include "SpecEnhanceModule.h"

static aquanode::ModuleDescriptor specEnhanceDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specenhance";
    d.displayName = "Spectral Enhance";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 21;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("mode", "Mode", { "Boost", "Attenuate" }, 0, 0, 2),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    SpectralModuleBase::addCurveParams (d, 0.25f);
    return d;
}

AQUANODE_REGISTER_MODULE (SpecEnhanceModule, specEnhanceDescriptor)
