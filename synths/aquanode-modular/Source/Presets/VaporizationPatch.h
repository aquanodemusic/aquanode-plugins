#pragma once

#include "PresetCommon.h"

// The Vaporization Pad: a classic two-layer vintage pad rebuilt with the
// Vintage (terracotta) modules - a Dual VCO layer whose high-pass is thrown
// around by pink noise -> S&H clocked by a Skew LFO, over a PWM Osc pad with
// its Ensemble and a slow Stereo Phaser, both sent to a Cross Delay and a
// Smooth Hall. One of the Ambient engine's Pad recipes.
namespace aquanode::presets
{
    juce::String buildVaporizationPatch (AquanodeModularAudioProcessor& p);
}
