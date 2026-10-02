#include "DawModModule.h"

static aquanode::ModuleDescriptor dawModDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.dawmod";
    d.displayName = "DAW Mod";
    d.description =
        "A modulation source the host automates: each instance claims one of twelve \"DAW "
        "Mod\" automation parameters and shows its number in its title bar. Output is the host "
        "parameter's raw 0..1 value - patch DC Offset after it (Gain 2, Offset -1) for a bipolar "
        "sweep.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 21;
    d.sockets = {
        modOut ("modOut", "Mod Out")
    };
    // Which host slot this instance claims. Hidden (the title bar shows it)
    // but saved, so a slot stays with its module across save/load and does not
    // get re-shuffled when some other DAW Mod is deleted.
    d.params = {
        makeRotary ("slot", "Slot", -1.0f, 11.0f, -1.0f, 9, {}, false, 1.0f).hide().noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DawModModule, dawModDescriptor)
