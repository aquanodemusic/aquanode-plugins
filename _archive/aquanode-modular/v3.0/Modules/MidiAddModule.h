#pragma once

#include "ModuleCore.h"

// Midi Add - a MIDI-domain utility: for every note it hears it emits four
// extra notes at the chosen semitone offsets. Patch its Midi Out into the
// "Add Midi In" socket of an Oscillator, Sampler or Granulator and that
// generator plays the note you pressed PLUS the four offsets - instant
// chords, octave doubling or fifths from single keys.
//
// By default it listens to the host's MIDI directly (no cable needed on the
// way in). Patch a note driver - Always Midi or Arp - into "Always Midi In"
// and it chords THAT driver's notes instead of the played keys: Always Midi
// -> Midi Add -> Oscillator drones a whole chord with nobody at the
// keyboard, and the driver's takeover of the played keys is handed on to the
// generator, exactly as a direct Always Midi cable would.
//
// The offsets are plain rotaries, so they take knob-modulation cables like
// any other knob. They are read at note-on time, which is what makes that
// musical: an LFO or S&H into Note 1 revoices the chord on every keypress
// while notes already sounding keep their pitch.
//
// An offset of 0 is a no-op: the generator already plays that pitch, so the
// slot is simply skipped rather than doubling the note. Each key press
// therefore costs up to five voices from the global pool.
class MidiAddModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumSlots = 4;
    enum ParamIndex { pNote1 = 0, pNote2, pNote3, pNote4 };

    // read by the engine when a note arrives (audio thread); includes any
    // knob modulation, sampled at that instant
    int offsetSemitones (int slot) const
    {
        return (int) std::lround (param (juce::jlimit (0, kNumSlots - 1, slot)));
    }

    // no audio passes through this module; the base class zeroes the outputs
};
