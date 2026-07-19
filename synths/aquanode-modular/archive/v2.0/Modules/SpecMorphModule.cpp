#include "SpecMorphModule.h"

static aquanode::ModuleDescriptor specMorphDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specmorph";
    d.displayName = "Spectral Morph";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 23;
    d.sockets = {
        audioIn  ("mainIn",   "Main In"),
        audioIn  ("morphIn",  "Morph In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("morph",  "Morph",  0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("smooth", "Smooth", 1.0f, 64.0f, 8.0f, 0, {}, true),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    SpectralModuleBase::addCurveParams (d, 1.0f);
    return d;
}

AQUANODE_REGISTER_MODULE (SpecMorphModule, specMorphDescriptor)
