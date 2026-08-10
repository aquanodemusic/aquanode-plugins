#include "SpecGateModule.h"

static aquanode::ModuleDescriptor specGateDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specgate";
    d.displayName = "Spec Gate";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 18;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("invert", "Invert", { "Off", "On" }, 0, 0, 1),
        makeRotary ("smooth", "Smooth", 0.0f, 500.0f, 30.0f, 0, "ms", true),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    SpectralModuleBase::addCurveParams (d, 0.3f);
    return d;
}

AQUANODE_REGISTER_MODULE (SpecGateModule, specGateDescriptor)
