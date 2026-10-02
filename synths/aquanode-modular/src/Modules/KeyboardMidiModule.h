#pragma once

#include "ModuleCore.h"

// Keyboard Midi - the notes you play (DAW track, MIDI controller, the
// on-screen keys) as a cable. Every generator already hears the keyboard
// without one, so this exists for the modules that do NOT: the drums (Clap,
// Kick, Hats, Snare, Modal Drum, Pluck) and Droplets, which otherwise only
// fire from a Trig / Gate cable. Patch its Midi Out into their Midi In and
// every key you press is a hit.
//
// Engine-wise it is a Discard Midi that discards nothing: the played keys go
// to whatever is listening, and like Discard Midi it REPLACES the raw
// keyboard for that listener, so an Oscillator patched to it hears each key
// once rather than twice.
class KeyboardMidiModule : public aquanode::SynthModule
{
public:
    bool midiSourceReplacesInput() const override { return true; }

    // a processor with no Midi In: it always hears the played keys and
    // passes them on unchanged (the base class's default)
    bool isMidiProcessor() const override { return true; }

    // no audio passes through this module; the base class zeroes the outputs
};
