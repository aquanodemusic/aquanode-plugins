#include "KeyTrackModule.h"

static aquanode::ModuleDescriptor keyTrackDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.keytrack";
    d.displayName = "KeyTrack";
    d.description =
        "Outputs each voice's pitch as a modulation signal, scaled so that into a filter's Mod In "
        "at 100% Mod Depth you get exact one-octave-per-octave keyboard tracking. The same "
        "semitones/60 scaling that Step Seq's Pitch Out and Pluck, Modal Drum and Comb Filter's "
        "Pitch In use.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 5;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = { makeRotary ("center", "Center", 0.0f, 127.0f, 60.0f, 0, {}, false, 1.0f) };
    return d;
}

AQUANODE_REGISTER_MODULE (KeyTrackModule, keyTrackDescriptor)
