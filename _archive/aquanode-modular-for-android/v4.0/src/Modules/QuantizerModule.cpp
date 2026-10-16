#include "QuantizerModule.h"

static aquanode::ModuleDescriptor quantizerDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.quantize";
    d.displayName = "Quantize";
    d.description =
        "Snaps a pitch-scaled modulation signal to the nearest note of a scale. Mod In expects "
        "the semitones/60 convention - KeyTrack, a Step Seq's Pitch Out, or an S&H. The 60 is "
        "because the filters sweep +-5 octaves at full modulation, and 5 octaves is 60 "
        "semitones: dividing by it makes one octave of pitch equal one octave of cutoff, so the "
        "same cable tracks a filter and transposes a Pluck without any scaling in between. The "
        "classic use: random S&H into Quantize into Pluck's Pitch In is an instant melody.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 15;
    d.sockets = {
        modIn  ("modIn",  "Mod In"),
        modOut ("modOut", "Mod Out")
    };
    d.params = {
        makeCombo ("scale", "Scale",
                   { "Chromatic", "Major", "Minor", "Harm Minor",
                     "Major Pent", "Minor Pent", "Blues", "Fifths" }, 5, 0, 2),
        makeSteppedList ("root", "Root",
                         { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (QuantizerModule, quantizerDescriptor)
