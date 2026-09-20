#pragma once
#include <JuceHeader.h>
#include "Parameters.h"

namespace aquanova {

//==============================================================================
/** Makes usable random patches rather than random noise.

    Randomising 346 parameters freely gives you silence or a scream almost every
    time. Instead every character is described by a profile - register, waveform
    bias, filter window, envelope shapes, effect balance and a few behavioural
    flags - and the generator randomises inside those windows. The result stays
    recognisably the thing named on the button.
*/
class RandomPreset
{
public:
    enum Character
    {
        // --- low end
        Bass = 0,
        SubBass,
        AcidBass,
        TB303,
        WobbleBass,
        // --- melodic
        Lead,
        SyncLead,
        Flute,
        SineFlute,
        FMClassic,
        Brass,
        Strings,
        Organ,
        Clav,
        ElectricPiano,
        // --- sustained
        Pad,
        GlassPad,
        WarmPad,
        Drone,
        Choir,
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
        // --- catch-all
        Anything,
        NumCharacters
    };

    static juce::StringArray characterNames();

    /** Generates a patch of the given character into the state. */
    static void generate (juce::AudioProcessorValueTreeState& state,
                          Character character,
                          juce::Random& rng);

    /** A name that matches the character, for the patch name field. */
    static juce::String nameFor (Character character, juce::Random& rng);

    /** Randomises every parameter across its full range, with only the few
        guards needed to guarantee the result is audible and not deafening.
        This is the deliberately reckless one - most results are unusable and
        the occasional one is worth keeping. */
    static void superRandomise (juce::AudioProcessorValueTreeState& state, juce::Random& rng);

    /** A name for a super-randomised patch. */
    static juce::String wildName (juce::Random& rng);
};

} // namespace aquanova
