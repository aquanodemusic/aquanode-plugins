#pragma once

#include "PianoModelCore.h"

// Mid Piano - a brighter felt piano: more hammer, more air on top.
// The physics is the shared piano model (PianoModelCore.h); this module brings
// its voicing, measured on a recording of a brighter felt piano from C#2 to C8.
// Per-voice generator. Inputs: 0 = Pitch In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
class MidPianoModule : public PianoModel
{
public:
    MidPianoModule();
};
