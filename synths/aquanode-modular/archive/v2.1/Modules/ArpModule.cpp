#include "ArpModule.h"

static aquanode::ModuleDescriptor arpDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.arp";
    d.displayName = "Arp";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 22;
    d.sockets = {
        midiOut ("midiOut", "Midi Out")
    };
    d.params = {
        makeSteppedList ("division", "Rate", seqDivisionChoices(), 8, 0),
        makeCombo  ("mode",    "Mode",    { "Up", "Down", "Up/Down", "Random", "As Played" }, 0, 0, 2),
        makeRotary ("octaves", "Octaves", 1.0f, 4.0f, 1.0f, 1, {}, false, 1.0f),
        makeRotary ("gate",    "Gate",    5.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("swing",   "Swing",   0.0f, 75.0f, 0.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ArpModule, arpDescriptor)
