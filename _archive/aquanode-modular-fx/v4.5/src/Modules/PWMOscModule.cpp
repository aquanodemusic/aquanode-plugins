#include "PWMOscModule.h"

using namespace aquanode;

static ModuleDescriptor pwmOscDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.pwmosc";
    d.displayName = "PWM Osc";
    d.description =
        "A polysynth VCO: Saw, PW or PWM at 16' 8' 4', with the square sub-oscillator one or two "
        "octaves down. In PWM the PW/PWM knob is the depth of its own PWM LFO. It never gates - "
        "follow it with Warm Ladder and a Volume whose Vol Mod takes an Analog EG.";
    d.section = ModuleSection::Oscillator;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 41;
    d.sockets = {
        modIn    ("pitchIn",   "Pitch In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeSteppedList ("octave", "Octave", { "16'", "8'", "4'" }, 1, 0),
        makeCombo  ("wave",     "Wave", { "Saw", "PW", "PWM" }, 0, 0, 2),
        makeRotary ("pw",       "PW/PWM",    0.0f, 10.0f, 5.0f, 1),
        makeRotary ("pwmSpeed", "PWM Speed", 0.1f, 10.0f, 1.0f, 1, "Hz", true).visibleWhen ("wave", 2.0f),
        makeCombo  ("sub",      "Sub Osc", { "Off", "1 Oct", "2 Oct" }, 0, 2, 2),
        makeRotary ("subLevel", "Sub Level", 0.0f, 1.0f, 0.6f, 2),
        makeRotary ("tune",     "Tune",     -100.0f, 100.0f, 0.0f, 3, "ct"),
        makeRotary ("level",    "Level",     0.0f, 1.0f, 1.0f, 3),
        makeRotary ("analog",   "Analog",    0.0f, 100.0f, 30.0f, 3, "%"),
        makeRotary ("pitchMod", "Pitch Mod", 0.0f, 24.0f, 1.0f, 4, "st"),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 4, {}, false, 1.0f).noMod(),
        makeRotary ("glide",  "Glide", 0.0f, 1000.0f, 0.0f, 4, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PWMOscModule, pwmOscDescriptor)
