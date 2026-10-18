#pragma once

#include "PresetCommon.h"

// 303: the acid bass line voice. One Warp Osc (saw, or square) played mono
// with a slide between held notes, into a resonant 24 dB ladder whose cutoff
// is snapped open by a short decay envelope (velocity adds a little accent),
// a VCA envelope, drive and a short delay. About half the patches come with an
// Arp in front (sixteenths over two octaves), so a held note or chord plays an
// acid line straight away; the rest are played by hand.
namespace aquanode::presets
{
    juce::String buildAcid303 (AquanodeModularAudioProcessor& p, juce::uint32 seed);
}
