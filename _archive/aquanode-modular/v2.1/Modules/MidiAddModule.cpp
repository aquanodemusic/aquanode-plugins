#include "MidiAddModule.h"

static aquanode::ModuleDescriptor midiAddDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.midiadd";
    d.displayName = "Midi Add";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 19;
    d.sockets = {
        midiOut ("midiOut", "Midi Out")
    };
    // defaults spell a major triad plus an octave below: +4, +7, +12, -12
    d.params = {
        makeSteppedList ("note1", "Note 1", MidiAddModule::offsetChoices(),
                         MidiAddModule::semitonesToChoice (4),   0),
        makeSteppedList ("note2", "Note 2", MidiAddModule::offsetChoices(),
                         MidiAddModule::semitonesToChoice (7),   0),
        makeSteppedList ("note3", "Note 3", MidiAddModule::offsetChoices(),
                         MidiAddModule::semitonesToChoice (12),  1),
        makeSteppedList ("note4", "Note 4", MidiAddModule::offsetChoices(),
                         MidiAddModule::semitonesToChoice (-12), 1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MidiAddModule, midiAddDescriptor)
