#include "DualVcoModule.h"

using namespace aquanode;

static ModuleDescriptor dualVcoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.dualvco";
    d.displayName = "Dual VCO";
    d.description =
        "Two analog-style VCOs and their mixer: VCO 1 Triangle/Saw/Pulse/Noise, VCO 2 Saw/Pulse/Ring "
        "Mod with its own Pitch, in feet (8' plays the key). It never gates - the VCOs "
        "run while the voice lives - so put the VCA after the filter: Volume with an Analog EG "
        "in Vol Mod. Pitch In takes an MG or envelope; Pitch Mod sets semitones per unit.";
    d.section = ModuleSection::Oscillator;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 40;
    d.sockets = {
        modIn    ("pitchIn",   "Pitch In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeCombo  ("wave1",  "VCO 1 Wave", { "Triangle", "Saw", "Pulse", "Noise" }, 1, 0, 2),
        makeRotary ("pw1",    "VCO 1 PW",    0.0f, 10.0f, 0.0f, 0).visibleWhen ("wave1", 2.0f),
        makeSteppedList ("scale1", "VCO 1 Feet", { "32'", "16'", "8'", "4'" }, 2, 1),
        makeCombo  ("wave2",  "VCO 2 Wave", { "Saw", "Pulse 50%", "Pulse 10%", "Ring Mod" }, 0, 2, 2),
        makeRotary ("pitch2", "VCO 2 Pitch", -12.0f, 12.0f, 0.0f, 2, "st"),
        makeSteppedList ("scale2", "VCO 2 Feet", { "16'", "8'", "4'", "2'" }, 1, 2),
        makeRotary ("fine",   "Fine",       -50.0f, 50.0f, 0.0f, 1, "ct"),
        makeRotary ("level1", "VCO 1 Level", 0.0f, 10.0f, 10.0f, 3),
        makeRotary ("level2", "VCO 2 Level", 0.0f, 10.0f, 0.0f, 3),
        makeRotary ("analog", "Analog",      0.0f, 100.0f, 30.0f, 3, "%"),
        makeRotary ("pitchMod", "Pitch Mod", 0.0f, 24.0f, 1.0f, 4, "st"),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 4, {}, false, 1.0f).noMod(),
        makeRotary ("glide",  "Glide", 0.0f, 1000.0f, 0.0f, 4, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DualVcoModule, dualVcoDescriptor)
