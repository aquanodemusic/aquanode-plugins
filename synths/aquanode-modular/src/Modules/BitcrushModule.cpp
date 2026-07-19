#include "BitcrushModule.h"

static aquanode::ModuleDescriptor bitcrushDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.bitcrush";
    d.displayName = "Bitcrush";
    d.description =
        "Bit-depth reduction, from subtle 12-bit warmth down to 1-bit destruction; Bits is "
        "continuous, so it crossfades between step sizes. Flexible: crushes each voice separately "
        "when patched per-voice.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 17;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("bits",   "Bits",    1.0f, 16.0f, 8.0f, 0),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BitcrushModule, bitcrushDescriptor)
