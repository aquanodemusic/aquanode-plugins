#pragma once
//==============================================================================
//  AquaVibrioMutator.h
//
//  The preset mutator: nudges a patch's FEEL without changing what it is.
//
//  Only a curated set of knobs ever moves - levels, shapes, brightness,
//  envelope times, modulation depths and rates, effect amounts. Pitch
//  (semitones, key follow, transpose, bend, portamento) and structure
//  (oscillator models, filter modes and routing, matrix routings, effect
//  types, key mode, arp) never do: a mutation is still the same patch, just
//  a little brighter, snappier, wider, wobblier or dirtier.
//
//  It moves along a handful of musical axes at once rather than giving 70
//  knobs 70 unrelated shoves:
//
//      Brightness  cutoff, envelope amount, shape, tone, EQ top end
//      Snap        envelope times (shorter = snappier), punch
//      Movement    LFO rates and depths, phaser and chorus motion
//      Width       detune spread, unison, chorus, stereo
//      Grit        resonance, drive, ring mod, distortion
//      Space       delay and reverb sends, feedback, tails
//
//  Each mutation draws a random direction in that space plus a little
//  independent jitter per knob, so the result reads as "a bit softer and
//  more alive" rather than as noise. Knobs that do nothing in the current
//  mode are left alone, and so are elements that are switched off: a send at
//  zero stays at zero, so a mutation never conjures up an effect that was
//  not there.
//==============================================================================

#include <JuceHeader.h>
#include <vector>

namespace aquavibrio
{

struct Mutator
{
    enum Strength { Gentle = 0, Medium, Wild };

    static juce::StringArray strengthNames() { return { "Gentle", "Medium", "Wild" }; }

    // Mutates raw parameter values (indexed like allParameters(), in the
    // APVTS's own raw units) in place. Returns how many knobs moved.
    static int mutate (std::vector<float>& values, Strength strength, juce::Random& rng);

    // True when the knob is one the mutator may move.
    static bool isMutable (int parameterIndex);
};

} // namespace aquavibrio
