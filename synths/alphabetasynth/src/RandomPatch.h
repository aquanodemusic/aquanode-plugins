#pragma once
#include <JuceHeader.h>

// ==============================================================
//  RandomPatch - makes usable random patches rather than noise.
//
//  Idea taken from Aquanode's AquaNova randomizer: every character
//  (Bass, Pad, Pluck, ...) is a profile - waveform pools, register,
//  filter window, envelope shapes, effect balance and a few
//  behaviour flags that wire up LFOs and the matrix. The generator
//  picks values inside those windows, so the result stays
//  recognisably the thing named in the menu.
//
//  "Anything" picks a random character; "Super Random" throws every
//  parameter across its full range with only a few guards that keep
//  the result audible and not deafening.
// ==============================================================
namespace RandomPatch {

    // All character names in menu order, followed by "Anything" and "Super Random"
    juce::StringArray characterNames();

    // Index of "Anything" / "Super Random" in characterNames()
    int anythingIndex();
    int superRandomIndex();

    // Menu section headings: returns a heading for the item at `index`, or ""
    juce::String sectionBefore(int index);

    // Writes a new patch of the given character into the state and returns
    // its name. Wavetables, bend range and master tune are left alone.
    // Must be called on the message thread.
    juce::String generate(juce::AudioProcessorValueTreeState& state, int character, juce::Random& rng);
}
