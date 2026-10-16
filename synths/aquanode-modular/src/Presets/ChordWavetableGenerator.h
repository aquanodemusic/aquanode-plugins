#pragma once

#include <JuceHeader.h>

class AquanodeModularAudioProcessor;

namespace aquanode
{
    // Builds a complete colour-bass patch into an EMPTY processor patch:
    // modulators, Chord Wavetable layers, per-voice processing, global effects
    // and the output stage, all wired. The seed decides everything, so the
    // same seed always gives the same patch. Returns a short display name for
    // the patch (e.g. "Colorbass Chord - Minor 9").
    // forcedArchetype: -1 = drawn from the seed, otherwise one of
    // chordWavetableArchetypeNames() (Chord - a chord-stacked bass, Growl, Pluck, Pad).
    juce::String buildChordWavetablePatch (AquanodeModularAudioProcessor& processor, juce::uint32 seed,
                                      int forcedArchetype = -1);

    inline juce::StringArray chordWavetableArchetypeNames() { return { "Chord", "Growl", "Pluck", "Pad" }; }
}
