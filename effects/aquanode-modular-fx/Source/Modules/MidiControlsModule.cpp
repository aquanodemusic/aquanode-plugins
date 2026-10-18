#include "MidiControlsModule.h"

static aquanode::ModuleDescriptor midiControlsDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.midicontrols";
    d.displayName = "Midi Controls";
    d.description =
        "Pitch bend, mod wheel, aftertouch, any CC and the sustain pedal as modulation cables - "
        "mod wheel into a cutoff, aftertouch into vibrato. The pedal holds keys and bend bends "
        "every pitched generator even without this module; Bend Range here sets that range.";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 6;
    d.sockets = {
        modOut ("bendOut",    "Bend"),
        modOut ("wheelOut",   "Mod Wheel"),
        modOut ("touchOut",   "Aftertouch"),
        modOut ("ccOut",      "CC"),
        modOut ("sustainOut", "Sustain")
    };
    d.params = {
        makeRotary ("ccNumber",  "CC #",       0.0f, 127.0f, 74.0f, 0, {}, false, 1.0f).noMod(),
        makeRotary ("bendRange", "Bend Range", 0.0f, 24.0f, 2.0f, 0, "st", false, 1.0f),
        makeRotary ("smooth",    "Smooth",     0.0f, 100.0f, 5.0f, 0, "ms")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MidiControlsModule, midiControlsDescriptor)
