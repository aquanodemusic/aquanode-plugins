#include "PanningModule.h"

static aquanode::ModuleDescriptor panningDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.panning";
    d.displayName = "Panning";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 4;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("panMod", "Pan Mod"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = { makeRotary ("pan", "Pan", -100.0f, 100.0f, 0.0f, 0, "%") };
    return d;
}

AQUANODE_REGISTER_MODULE (PanningModule, panningDescriptor)
