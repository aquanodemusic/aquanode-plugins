#pragma once

#include "PianoModelCore.h"

// Felt Piano - a muted felt upright: dark, soft and close.
// The physics is the shared piano model (PianoModelCore.h); this module brings
// its voicing, measured on a recording of a muted felt upright from C1 to C7.
// Per-voice generator. Inputs: 0 = Pitch In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
class FeltPianoModule : public PianoModel
{
public:
    FeltPianoModule();
};
