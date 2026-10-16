#pragma once

#include <JuceHeader.h>
#include "PresetCommon.h"

// The genre engines behind the Preset Generation rows: Ambient, DnB, Dub
// Techno and Psychill. Each one rebuilds a patch from a recipe - a sound
// described in Aquanode terms: Warp Osc layers with their unison and phase
// warps (custom single-cycle spectra where a basic shape will not do), the
// filters, the amplitude and modulation envelopes (Curve Env), LFOs and
// random modulators, and an effect chain (Echo, Shelf EQ, AquaChorus,
// Reverb...).
//
// randomBuilder() picks a recipe of the genre (optionally of one category)
// and mutates it gently, so no two taps sound quite the same.
namespace aquanode::presets::genre
{
    juce::StringArray genreNames();

    struct Entry
    {
        int index;              // into the recipe table
        juce::String name;
        juce::String category;  // Bass, Pad, Lead, Pluck, Keys, Bell, Kick, Perc, Chord, Arp, FX, Synth
    };

    // the recipes of a genre, sorted by category then name
    std::vector<Entry> recipesOf (int genre);

    // builds recipe `index`; a non-zero seed mutates it a little
    juce::String buildRecipePatch (AquanodeModularAudioProcessor& p, int index, juce::uint32 mutationSeed);

    // a random recipe of the genre (optionally of one category), mutated
    BuildFn randomBuilder (int genre, juce::uint32 seed, const juce::String& category = {});

    int numRecipes();
}
