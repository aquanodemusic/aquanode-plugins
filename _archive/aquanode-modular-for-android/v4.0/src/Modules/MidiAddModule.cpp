#include "MidiAddModule.h"

static aquanode::ModuleDescriptor midiAddDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.midiadd";
    d.displayName = "Midi Add";
    d.description =
        "For every note it hears it emits four extra notes at the chosen semitone offsets - "
        "instant chords of any scale from single keys. Its Midi Out goes into a generator's "
        "Midi In or another processor; its own Midi In takes any stream (Piano Roll, Always "
        "Midi, Arp, another Midi Add...) and chords that instead of the played keys. The offsets are modulatable and read at "
        "note-on, so an LFO or S&H on Note 1 revoices the chord on every keypress while sounding "
        "notes hold their pitch.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 19;
    d.sockets = {
        midiIn  ("alwaysMidiIn", "Midi In"),   // id kept so old patches still connect
        midiOut ("midiOut", "Midi Out")
    };
    // defaults spell a major triad plus an octave below: +4, +7, +12, -12
    d.params = {
        makeRotary ("note1", "Note 1", -24.0f, 24.0f,   4.0f, 0, "st", false, 1.0f),
        makeRotary ("note2", "Note 2", -24.0f, 24.0f,   7.0f, 0, "st", false, 1.0f),
        makeRotary ("note3", "Note 3", -24.0f, 24.0f,  12.0f, 1, "st", false, 1.0f),
        makeRotary ("note4", "Note 4", -24.0f, 24.0f, -12.0f, 1, "st", false, 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MidiAddModule, midiAddDescriptor)
