#pragma once
//==============================================================================
//  AquaVibrioRandom.h
//
//  Usable random patches rather than random noise, in the same spirit as
//  Aquanova's generator: randomising 400-odd parameters freely gives silence
//  or a scream almost every time, so every character here is described by a
//  profile - register, oscillator model, filter window, envelope shapes,
//  effect balance and a handful of behavioural flags - and the generator
//  randomises INSIDE those windows. What comes out still sounds like the
//  thing named on the button.
//==============================================================================

#include <JuceHeader.h>
#include "AquaVibrioParameters.h"

namespace aquavibrio
{

class RandomPreset
{
public:
    enum Character
    {
        // --- low end
        Bass = 0,
        SubBass,
        AcidBass,
        ReeseBass,
        FmBass,
        WobbleBass,
        // --- melodic
        Lead,
        SyncLead,
        PsyLead,
        HooverLead,
        Brass,
        Strings,
        Organ,
        Clav,
        ElectricPiano,
        // --- sustained
        Pad,
        WarmPad,
        GlassPad,
        Choir,
        Drone,
        // --- struck and short
        Pluck,
        Bell,
        Metallic,
        Stab,
        Blip,
        // --- movement and effects
        Arp,
        ResoSweep,
        Riser,
        Formant,
        NoiseFX,
        Atmosphere,
        Granular,
        // --- catch-all
        Anything,
        NumCharacters
    };

    /** The character names, in menu order, with "Super Random" appended. */
    static juce::StringArray characterNames();

    /** Generates a patch of the given character into the state. */
    static void generate (juce::AudioProcessorValueTreeState& state,
                          Character character, juce::Random& rng);

    /** Randomises every parameter across its full range, with only the few
        guards needed to keep the result audible and not deafening. Most
        results are unusable; the occasional one is worth keeping. */
    static void superRandomise (juce::AudioProcessorValueTreeState& state, juce::Random& rng);

    /** A patch name that matches the character. */
    static juce::String nameFor (Character character, juce::Random& rng);

    /** A patch name for a super-randomised patch. */
    static juce::String wildName (juce::Random& rng);
};

} // namespace aquavibrio
