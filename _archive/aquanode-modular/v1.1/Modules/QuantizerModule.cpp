#include "QuantizerModule.h"

static aquanode::ModuleDescriptor quantizerDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.quantize";
    d.displayName = "Quantize";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 15;
    d.sockets = {
        modIn  ("modIn",  "Mod In"),
        modOut ("modOut", "Mod Out")
    };
    d.params = {
        makeCombo ("scale", "Scale",
                   { "Chromatic", "Major", "Minor", "Harm Minor",
                     "Major Pent", "Minor Pent", "Blues", "Fifths" }, 5, 0, 2),
        makeSteppedList ("root", "Root",
                         { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (QuantizerModule, quantizerDescriptor)
