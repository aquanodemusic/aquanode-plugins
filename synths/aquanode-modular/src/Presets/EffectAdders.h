#pragma once

#include "PresetCommon.h"

// The "Add Effect" rows of the Random Preset section. Unlike the generators
// they do not replace the patch: each adds a small, randomly varied chain to
// whatever is loaded. The effect rows go in front of Audio Out (whatever fed
// Audio Out now feeds the new chain); Midi Add goes at the start, feeding the
// Add Midi In of every generator in the patch.
namespace aquanode::presets
{
    struct EffectAdder
    {
        juce::String id;            // "add.Reverb"
        juce::String label;         // "Reverb"
        juce::String tooltipBody;
        // adds to the current patch; returns a short note for the patch name
        std::function<juce::String (AquanodeModularAudioProcessor&, juce::uint32 seed)> apply;
    };

    const std::vector<EffectAdder>& effectAdders();
    const EffectAdder* findEffectAdder (const juce::String& id);
}
