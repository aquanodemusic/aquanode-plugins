#include "AudioThruModule.h"

static aquanode::ModuleDescriptor audioThruDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.thru";
    d.displayName = "Audio Thru";
    d.description =
        "Does nothing at all: audio in, the identical audio out. It exists for cable housekeeping "
        "- bunch several cables into one junction, or give a long run a visual waypoint. Cables "
        "into its Audio In sum, as everywhere in the graph. Flexible and stateless, so inside a "
        "per-voice chain it never forces an early voice sum.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 12;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    // deliberately no parameters: this module IS a piece of wire
    return d;
}

AQUANODE_REGISTER_MODULE (AudioThruModule, audioThruDescriptor)
