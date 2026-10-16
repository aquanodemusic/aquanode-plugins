#pragma once

#include "PianoModelCore.h"

// Hard Piano - hard hammers: a clear, ringing, bright piano.
// The physics is the shared piano model (PianoModelCore.h); this module brings
// its voicing, measured on a recording of a piano with hard hammers from C#1 to C8.
// Per-voice generator. Inputs: 0 = Pitch In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
class HardPianoModule : public PianoModel
{
public:
    HardPianoModule();
};
