#include "PolySumModule.h"

static aquanode::ModuleDescriptor polySumDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.polysum";
    d.displayName = "Polyphony Sum";
    d.description =
        "Counts the voices actually sounding and pulls the level down to match, so a chord does "
        "not arrive several times louder than a single note. Put it just before Audio Out. Amount "
        "is the law: 0.5 is 1/sqrt(N), correct for ordinary chords where different pitches sum as "
        "power; 1.0 is 1/N, correct for unison stacks where voices sum as amplitude; 0 is bypass. "
        "Full compensation makes a six-note chord peak at exactly the level of one note, which is "
        "numerically right but often musically flat - try 0.35-0.45. Smooth ramps the gain so the "
        "staircase between voice counts does not click. Released voices keep counting until their "
        "tail has finished, so the gain does not jump mid-release. Needs a per-voice source "
        "upstream to see a count at all; with none it passes audio through untouched. Voices Out "
        "reports the live count as a modulation signal.";
    d.section = ModuleSection::Utility;
    d.sockets = {
        audioIn  ("audioIn",   "Audio In"),
        audioOut ("audioOut",  "Audio Out"),
        modOut   ("voicesOut", "Voices Out")
    };
    d.params = {
        makeRotary ("amount", "Amount", 0.0f,  1.0f,   0.5f,  0),
        makeRotary ("smooth", "Smooth", 1.0f, 500.0f, 40.0f,  0, "ms", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PolySumModule, polySumDescriptor)
