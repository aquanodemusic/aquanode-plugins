#pragma once

#include "PatchKit.h"

// 83ChorusVerb's four effect modules, for the patch versions that carry this
// effects section: 83ChorusVerb itself and VirtualDX7.
namespace aquanode::plugins::chorusverb
{
    struct Chain { int chorus { -1 }, delay { -1 }, phaser { -1 }, reverb { -1 }; };

    // The four stages in series at (x, y) relative to the builder's origin,
    // each set from the original's parameters (map: module param id -> the
    // original's id, for a plugin that names them differently).
    Chain buildChain (kit::Builder& b, juce::AudioProcessor& original, int x, int y,
                      const std::function<juce::String (const juce::String&)>& map);
}
