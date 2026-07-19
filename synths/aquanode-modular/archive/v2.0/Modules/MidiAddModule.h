#pragma once

#include "ModuleCore.h"

// Midi Add - a MIDI-domain utility: it listens to the host's MIDI input
// directly (no cable needed on the way in) and, for every note played, emits
// four extra notes at the chosen semitone offsets. Patch its Midi Out into
// the "Add Midi In" socket of an Oscillator, Sampler or Granulator and that
// generator plays the note you pressed PLUS the four offsets - instant
// chords, octave doubling or fifths from single keys.
//
// A generator with nothing patched into Add Midi In behaves exactly as
// before: it just plays the notes you press.
//
// The offsets deliberately skip 0, since a unison copy would only double a
// note the generator already plays. Each key press therefore costs five
// voices from the global pool (the note itself plus four additions).
class MidiAddModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumSlots = 4;
    enum ParamIndex { pNote1 = 0, pNote2, pNote3, pNote4 };

    // -24..+24 excluding 0, as 48 stepped choices ("-24" .. "-1", "+1" .. "+24")
    static const juce::StringArray& offsetChoices()
    {
        static const juce::StringArray choices = []
        {
            juce::StringArray a;
            for (int i = -24; i <= 24; ++i)
                if (i != 0)
                    a.add ((i > 0 ? "+" : "") + juce::String (i));
            return a;
        }();
        return choices;
    }

    static int choiceToSemitones (int index)
    {
        index = juce::jlimit (0, 47, index);
        return index < 24 ? index - 24    // 0 -> -24 ... 23 -> -1
                          : index - 23;   // 24 -> +1 ... 47 -> +24
    }

    static int semitonesToChoice (int semis)
    {
        semis = juce::jlimit (-24, 24, semis);
        if (semis == 0) semis = 1;
        return semis < 0 ? semis + 24 : semis + 23;
    }

    // read by the engine when a note arrives (audio thread)
    int offsetSemitones (int slot) const
    {
        return choiceToSemitones ((int) param (juce::jlimit (0, kNumSlots - 1, slot)));
    }

    // no audio passes through this module; the base class zeroes the outputs
};
