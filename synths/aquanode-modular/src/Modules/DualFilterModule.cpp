#include "DualFilterModule.h"

using namespace aquanode;

static ModuleDescriptor dualFilterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.dualfilter";
    d.displayName = "Dual Filter";
    d.description =
        "A resonant 12 dB high-pass into a resonant 12 dB low-pass, with "
        "the soft-clipped Peak that makes it scream (above ~9 it self-oscillates). Each filter "
        "has an MG and an Ext CV input with its own depth (10 = 3 octaves per unit): a Skew LFO "
        "Tri Out, an Analog EG, or S&H fed by pink noise for the classic random filter jumps.";
    d.section = ModuleSection::Filter;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 40;
    d.sockets = {
        audioIn  ("audioIn", "Audio In"),
        modIn    ("hpfMg",   "HPF MG"),
        modIn    ("hpfExt",  "HPF Ext"),
        modIn    ("lpfMg",   "LPF MG"),
        modIn    ("lpfExt",  "LPF Ext"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("hpfCutoff", "HPF Cutoff",  25.0f, 19000.0f, 25.0f, 0, "Hz", true),
        makeRotary ("hpfPeak",   "HPF Peak",    0.0f, 10.0f, 0.0f, 0),
        makeRotary ("hpfMg",     "HPF MG",      0.0f, 10.0f, 0.0f, 0),
        makeRotary ("hpfExt",    "HPF EG2/Ext", 0.0f, 10.0f, 0.0f, 0),
        makeRotary ("lpfCutoff", "LPF Cutoff",  25.0f, 19000.0f, 19000.0f, 1, "Hz", true),
        makeRotary ("lpfPeak",   "LPF Peak",    0.0f, 10.0f, 0.0f, 1),
        makeRotary ("lpfMg",     "LPF MG",      0.0f, 10.0f, 0.0f, 1),
        makeRotary ("lpfExt",    "LPF EG2/Ext", 0.0f, 10.0f, 0.0f, 1),
        makeRotary ("output",    "Output",      0.0f, 2.0f, 1.0f, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DualFilterModule, dualFilterDescriptor)
