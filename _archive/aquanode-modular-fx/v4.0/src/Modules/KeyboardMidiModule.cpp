#include "KeyboardMidiModule.h"

static aquanode::ModuleDescriptor keyboardMidiDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.keyboardmidi";
    d.displayName = "Keyboard Midi";
    d.description =
        "The notes you play - DAW track, controller or on-screen keys - as a cable. Generators "
        "hear the keyboard anyway; this is for the drums and Droplets, whose Midi In turns every "
        "key into a hit. Like Discard Midi it replaces the raw keyboard for whatever it feeds.";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 5;
    d.sockets = {
        midiOut ("midiOut", "Midi Out")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (KeyboardMidiModule, keyboardMidiDescriptor)
