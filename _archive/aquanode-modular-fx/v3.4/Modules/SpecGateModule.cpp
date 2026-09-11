#include "SpecGateModule.h"

static aquanode::ModuleDescriptor specGateDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.specgate";
    d.displayName = "Spectral Gate";
    d.description =
        "Each bin passes only while its magnitude is above the drawn per-bin threshold, relative "
        "to the frame's loudest bin. Invert turns it into a residual/noise extractor, and Smooth "
        "ramps each bin between frames, taming the metallic musical-noise artefact of hard "
        "spectral gating.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 20;
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
