#pragma once

#include "PresetCommon.h"

// Growl - Random: growls whose movement is the Warp Osc's Random Amp morph.
// A unison body and a layer an octave or two up both run Random Amp, and a
// per-note sweep (a Multi LFO, usually one-shot like an envelope) drives the
// morph amount through the note, so each hit walks through a scramble of
// harmonic patterns; a sine sub is pulsed by its own tempo LFO under it. An
// ADSR swells the layers in and out, and chorus / flanger / delay / reverb
// give the space. Every seed draws its own shapes, timings and amounts.
namespace aquanode::presets
{
    juce::String buildRandomAmpGrowl (AquanodeModularAudioProcessor& p, juce::uint32 seed);
}
