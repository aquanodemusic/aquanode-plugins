#include "ParameterTable.h"

namespace aquanova {

const ParamDesc kParams[] =
{
    { 1, "Osc1Type", "Osc1 Type", 0, 4, 0, false, true, "oscType" },
    { 2, "Osc1Octave", "Osc1 Octave", 0, 4, 2, false, true, "oscOctave" },
    { 3, "Osc1Semitone", "Osc1 Semitone", 0, 24, 12, false, true, "semitone" },
    { 4, "Osc1FineTune", "Osc1 Fine Tune", 0, 127, 64, true, false, "signed" },
    { 5, "Osc1MixLevel", "Osc1 Mix Level", 0, 127, 100, false, false, "unsignedZero" },
    { 6, "Osc1MixEnv2", "Osc1 Mix Env2", 0, 127, 64, true, false, "signed" },
    { 7, "Osc1ModKnobEnv3Mix", "Osc1 Mod Knob Env3 Mix", 0, 127, 64, true, false, "signed" },
    { 8, "Osc1ModKnobLFO1Mix", "Osc1 Mod Knob LFO1 Mix", 0, 127, 64, true, false, "signed" },
    { 9, "Osc1ModKnobLFO2Mix", "Osc1 Mod Knob LFO2 Mix", 0, 127, 64, true, false, "signed" },
    { 10, "Osc1ModKnobWhMix", "Osc1 Mod Knob Wh Mix", 0, 127, 64, true, false, "signed" },
    { 11, "Osc1PitchEnv2", "Osc1 Pitch Env2", 0, 127, 64, true, false, "signed" },
    { 12, "Osc1PitchEnv3", "Osc1 Pitch Env3", 0, 127, 64, true, false, "signed" },
    { 13, "Osc1PitchLFO1", "Osc1 Pitch LFO1", 0, 127, 64, true, false, "signed" },
    { 14, "Osc1ModKnobLFO2Pitch", "Osc1 Mod Knob LFO2 Pitch", 0, 127, 64, true, false, "signed" },
    { 15, "Osc1ModKnobWhPitch", "Osc1 Mod Knob Wh Pitch", 0, 127, 64, true, false, "signed" },
    { 16, "Osc1ManKnobPitch", "Osc1 Man Knob Pitch", 0, 127, 64, true, false, "signed" },
    { 17, "Osc1PulseWidth", "Osc1 Pulse Width", 0, 127, 64, false, false, "unsignedZero" },
    { 18, "Osc1WidthEnv2", "Osc1 Width Env2", 0, 127, 64, true, false, "signed" },
    { 19, "Osc1WidthLFO1", "Osc1 Width LFO1", 0, 127, 64, true, false, "signed" },
    { 20, "Osc1WidthLFO2", "Osc1 Width LFO2", 0, 127, 64, true, false, "signed" },
    { 21, "Osc1ModKnobEnv3Width", "Osc1 Mod Knob Env3 Width", 0, 127, 64, true, false, "signed" },
    { 22, "Osc1ModKnobWhWidth", "Osc1 Mod Knob Wh Width", 0, 127, 64, true, false, "signed" },
    { 23, "Osc1ModKnobEnv3Sync", "Osc1 Mod Knob Env3 Sync", 0, 127, 64, true, false, "signed" },
    { 24, "Osc1ModKnobLFO2Sync", "Osc1 Mod Knob LFO2 Sync", 0, 127, 64, true, false, "signed" },
    { 25, "Osc1ModKnobWhSync", "Osc1 Mod Knob Wh Sync", 0, 127, 64, true, false, "signed" },
    { 26, "Osc1ModKnobEnv3Hardness", "Osc1 Mod Knob Env3 Hardness", 0, 127, 64, true, false, "signed" },
    { 27, "Osc1ModKnobLFO1Hardness", "Osc1 Mod Knob LFO1 Hardness", 0, 127, 64, true, false, "signed" },
    { 28, "Osc1ModKnobLFO2Hardness", "Osc1 Mod Knob LFO2 Hardness", 0, 127, 64, true, false, "signed" },
    { 29, "Osc1ModKnobWhHardness", "Osc1 Mod Knob Wh Hardness", 0, 127, 64, true, false, "signed" },
    { 30, "Osc1WhLFO1Intensity", "Osc1 Wh LFO1 Intensity", 0, 127, 64, true, false, "signed" },
    { 31, "Osc1AftertouchLFO1Intensity", "Osc1 Aftertouch LFO1 Intensity", 0, 127, 64, true, false, "signed" },
    { 32, "Osc1Sync", "Osc1 Sync", 0, 127, 0, false, false, "unsignedZero" },
    { 33, "Osc1SyncEnv2", "Osc1 Sync Env2", 0, 127, 64, true, false, "signed" },
    { 34, "Osc1SyncLFO1", "Osc1 Sync LFO1", 0, 127, 64, true, false, "signed" },
    { 35, "Osc1KeySync", "Osc1 Key Sync", 0, 127, 0, false, false, "unsignedZero" },
    { 36, "Osc1SyncSkew", "Osc1 Sync Skew", 0, 127, 0, false, false, "unsignedZero" },
    { 37, "Osc1SyncFormantWidth", "Osc1 Sync Formant Width", 0, 127, 0, false, false, "unsignedZero" },
    { 38, "Osc1Soften", "Osc1 Soften", 0, 127, 127, false, false, "unsignedZero" },
    { 39, "Osc1SoftenEnv2", "Osc1 Soften Env2", 0, 127, 64, true, false, "signed" },
    { 40, "Osc1BendRange", "Osc1 Bend Range", 0, 24, 14, false, true, "bendRange" },
    { 41, "Osc2Type", "Osc2 Type", 0, 4, 0, false, true, "oscType" },
    { 42, "Osc2Octave", "Osc2 Octave", 0, 4, 2, false, true, "oscOctave" },
    { 43, "Osc2Semitone", "Osc2 Semitone", 0, 24, 12, false, true, "semitone" },
    { 44, "Osc2FineTune", "Osc2 Fine Tune", 0, 127, 64, true, false, "signed" },
    { 45, "Osc2MixLevel", "Osc2 Mix Level", 0, 127, 100, false, false, "unsignedZero" },
    { 46, "Osc2MixEnv2", "Osc2 Mix Env2", 0, 127, 64, true, false, "signed" },
    { 47, "Osc2ModKnobEnv3Mix", "Osc2 Mod Knob Env3 Mix", 0, 127, 64, true, false, "signed" },
    { 48, "Osc2ModKnobLFO1Mix", "Osc2 Mod Knob LFO1 Mix", 0, 127, 64, true, false, "signed" },
    { 49, "Osc2ModKnobLFO2Mix", "Osc2 Mod Knob LFO2 Mix", 0, 127, 64, true, false, "signed" },
    { 50, "Osc2ModKnobWhMix", "Osc2 Mod Knob Wh Mix", 0, 127, 64, true, false, "signed" },
    { 51, "Osc2PitchEnv2", "Osc2 Pitch Env2", 0, 127, 64, true, false, "signed" },
    { 52, "Osc2PitchEnv3", "Osc2 Pitch Env3", 0, 127, 64, true, false, "signed" },
    { 53, "Osc2PitchLFO1", "Osc2 Pitch LFO1", 0, 127, 64, true, false, "signed" },
    { 54, "Osc2ModKnobLFO2Pitch", "Osc2 Mod Knob LFO2 Pitch", 0, 127, 64, true, false, "signed" },
    { 55, "Osc2ModKnobWhPitch", "Osc2 Mod Knob Wh Pitch", 0, 127, 64, true, false, "signed" },
    { 56, "Osc2ManKnobPitch", "Osc2 Man Knob Pitch", 0, 127, 64, true, false, "signed" },
    { 57, "Osc2PulseWidth", "Osc2 Pulse Width", 0, 127, 64, false, false, "unsignedZero" },
    { 58, "Osc2WidthEnv2", "Osc2 Width Env2", 0, 127, 64, true, false, "signed" },
    { 59, "Osc2WidthLFO1", "Osc2 Width LFO1", 0, 127, 64, true, false, "signed" },
    { 60, "Osc2WidthLFO2", "Osc2 Width LFO2", 0, 127, 64, true, false, "signed" },
    { 61, "Osc2ModKnobEnv3Width", "Osc2 Mod Knob Env3 Width", 0, 127, 64, true, false, "signed" },
    { 62, "Osc2ModKnobWhWidth", "Osc2 Mod Knob Wh Width", 0, 127, 64, true, false, "signed" },
    { 63, "Osc2ModKnobEnv3Sync", "Osc2 Mod Knob Env3 Sync", 0, 127, 64, true, false, "signed" },
    { 64, "Osc2ModKnobLFO2Sync", "Osc2 Mod Knob LFO2 Sync", 0, 127, 64, true, false, "signed" },
    { 65, "Osc2ModKnobWhSync", "Osc2 Mod Knob Wh Sync", 0, 127, 64, true, false, "signed" },
    { 66, "Osc2ModKnobEnv3Hardness", "Osc2 Mod Knob Env3 Hardness", 0, 127, 64, true, false, "signed" },
    { 67, "Osc2ModKnobLFO1Hardness", "Osc2 Mod Knob LFO1 Hardness", 0, 127, 64, true, false, "signed" },
    { 68, "Osc2ModKnobLFO2Hardness", "Osc2 Mod Knob LFO2 Hardness", 0, 127, 64, true, false, "signed" },
    { 69, "Osc2ModKnobWhHardness", "Osc2 Mod Knob Wh Hardness", 0, 127, 64, true, false, "signed" },
    { 70, "Osc2WhLFO1Intensity", "Osc2 Wh LFO1 Intensity", 0, 127, 64, true, false, "signed" },
    { 71, "Osc2AftertouchLFO1Intensity", "Osc2 Aftertouch LFO1 Intensity", 0, 127, 64, true, false, "signed" },
    { 72, "Osc2Sync", "Osc2 Sync", 0, 127, 0, false, false, "unsignedZero" },
    { 73, "Osc2SyncEnv2", "Osc2 Sync Env2", 0, 127, 64, true, false, "signed" },
    { 74, "Osc2SyncLFO1", "Osc2 Sync LFO1", 0, 127, 64, true, false, "signed" },
    { 75, "Osc2KeySync", "Osc2 Key Sync", 0, 127, 0, false, false, "unsignedZero" },
    { 76, "Osc2SyncSkew", "Osc2 Sync Skew", 0, 127, 0, false, false, "unsignedZero" },
    { 77, "Osc2SyncFormantWidth", "Osc2 Sync Formant Width", 0, 127, 0, false, false, "unsignedZero" },
    { 78, "Osc2Soften", "Osc2 Soften", 0, 127, 127, false, false, "unsignedZero" },
    { 79, "Osc2SoftenEnv2", "Osc2 Soften Env2", 0, 127, 64, true, false, "signed" },
    { 80, "Osc2BendRange", "Osc2 Bend Range", 0, 24, 14, false, true, "bendRange" },
    { 81, "Osc3Type", "Osc3 Type", 0, 4, 0, false, true, "oscType" },
    { 82, "Osc3Octave", "Osc3 Octave", 0, 4, 2, false, true, "oscOctave" },
    { 83, "Osc3Semitone", "Osc3 Semitone", 0, 24, 12, false, true, "semitone" },
    { 84, "Osc3FineTune", "Osc3 Fine Tune", 0, 127, 64, true, false, "signed" },
    { 85, "Osc3MixLevel", "Osc3 Mix Level", 0, 127, 100, false, false, "unsignedZero" },
    { 86, "Osc3MixEnv2", "Osc3 Mix Env2", 0, 127, 64, true, false, "signed" },
    { 87, "Osc3ModKnobEnv3Mix", "Osc3 Mod Knob Env3 Mix", 0, 127, 64, true, false, "signed" },
    { 88, "Osc3ModKnobLFO1Mix", "Osc3 Mod Knob LFO1 Mix", 0, 127, 64, true, false, "signed" },
    { 89, "Osc3ModKnobLFO2Mix", "Osc3 Mod Knob LFO2 Mix", 0, 127, 64, true, false, "signed" },
    { 90, "Osc3ModKnobWhMix", "Osc3 Mod Knob Wh Mix", 0, 127, 64, true, false, "signed" },
    { 91, "Osc3PitchEnv2", "Osc3 Pitch Env2", 0, 127, 64, true, false, "signed" },
    { 92, "Osc3PitchEnv3", "Osc3 Pitch Env3", 0, 127, 64, true, false, "signed" },
    { 93, "Osc3PitchLFO1", "Osc3 Pitch LFO1", 0, 127, 64, true, false, "signed" },
    { 94, "Osc3ModKnobLFO2Pitch", "Osc3 Mod Knob LFO2 Pitch", 0, 127, 64, true, false, "signed" },
    { 95, "Osc3ModKnobWhPitch", "Osc3 Mod Knob Wh Pitch", 0, 127, 64, true, false, "signed" },
    { 96, "Osc3ManKnobPitch", "Osc3 Man Knob Pitch", 0, 127, 64, true, false, "signed" },
    { 97, "Osc3PulseWidth", "Osc3 Pulse Width", 0, 127, 64, false, false, "unsignedZero" },
    { 98, "Osc3WidthEnv2", "Osc3 Width Env2", 0, 127, 64, true, false, "signed" },
    { 99, "Osc3WidthLFO1", "Osc3 Width LFO1", 0, 127, 64, true, false, "signed" },
    { 100, "Osc3WidthLFO2", "Osc3 Width LFO2", 0, 127, 64, true, false, "signed" },
    { 101, "Osc3ModKnobEnv3Width", "Osc3 Mod Knob Env3 Width", 0, 127, 64, true, false, "signed" },
    { 102, "Osc3ModKnobWhWidth", "Osc3 Mod Knob Wh Width", 0, 127, 64, true, false, "signed" },
    { 103, "Osc3ModKnobEnv3Sync", "Osc3 Mod Knob Env3 Sync", 0, 127, 64, true, false, "signed" },
    { 104, "Osc3ModKnobLFO2Sync", "Osc3 Mod Knob LFO2 Sync", 0, 127, 64, true, false, "signed" },
    { 105, "Osc3ModKnobWhSync", "Osc3 Mod Knob Wh Sync", 0, 127, 64, true, false, "signed" },
    { 106, "Osc3ModKnobEnv3Hardness", "Osc3 Mod Knob Env3 Hardness", 0, 127, 64, true, false, "signed" },
    { 107, "Osc3ModKnobLFO1Hardness", "Osc3 Mod Knob LFO1 Hardness", 0, 127, 64, true, false, "signed" },
    { 108, "Osc3ModKnobLFO2Hardness", "Osc3 Mod Knob LFO2 Hardness", 0, 127, 64, true, false, "signed" },
    { 109, "Osc3ModKnobWhHardness", "Osc3 Mod Knob Wh Hardness", 0, 127, 64, true, false, "signed" },
    { 110, "Osc3WhLFO1Intensity", "Osc3 Wh LFO1 Intensity", 0, 127, 64, true, false, "signed" },
    { 111, "Osc3AftertouchLFO1Intensity", "Osc3 Aftertouch LFO1 Intensity", 0, 127, 64, true, false, "signed" },
    { 112, "Osc3Sync", "Osc3 Sync", 0, 127, 0, false, false, "unsignedZero" },
    { 113, "Osc3SyncEnv2", "Osc3 Sync Env2", 0, 127, 64, true, false, "signed" },
    { 114, "Osc3SyncLFO1", "Osc3 Sync LFO1", 0, 127, 64, true, false, "signed" },
    { 115, "Osc3KeySync", "Osc3 Key Sync", 0, 127, 0, false, false, "unsignedZero" },
    { 116, "Osc3SyncSkew", "Osc3 Sync Skew", 0, 127, 0, false, false, "unsignedZero" },
    { 117, "Osc3SyncFormantWidth", "Osc3 Sync Formant Width", 0, 127, 0, false, false, "unsignedZero" },
    { 118, "Osc3Soften", "Osc3 Soften", 0, 127, 127, false, false, "unsignedZero" },
    { 119, "Osc3SoftenEnv2", "Osc3 Soften Env2", 0, 127, 64, true, false, "signed" },
    { 120, "Osc3BendRange", "Osc3 Bend Range", 0, 24, 14, false, true, "bendRange" },
    { 121, "OSCsStartPhase", "Osc Start Phase", 0, 127, 0, false, false, "oscStartPhase" },
    { 122, "NoiseSoften", "Noise Soften", 0, 127, 127, false, false, "unsignedZero" },
    { 123, "NoiseMixLevel", "Noise Mix Level", 0, 127, 0, false, false, "unsignedZero" },
    { 124, "NoiseMixEnv2", "Noise Mix Env2", 0, 127, 64, true, false, "signed" },
    { 125, "NoiseModKnobEnv2Hardness", "Noise Mod Knob Env2 Hardness", 0, 127, 64, true, false, "signed" },
    { 126, "NoiseModKnobEnv3Hardness", "Noise Mod Knob Env3 Hardness", 0, 127, 64, true, false, "signed" },
    { 127, "NoiseModKnobLFO1Hardness", "Noise Mod Knob LFO1 Hardness", 0, 127, 64, true, false, "signed" },
    { 128, "NoiseModKnobLFO2Hardness", "Noise Mod Knob LFO2 Hardness", 0, 127, 64, true, false, "signed" },
    { 129, "NoiseModKnobWhHardness", "Noise Mod Knob Wh Hardness", 0, 127, 64, true, false, "signed" },
    { 130, "NoiseModKnobEnv3Mix", "Noise Mod Knob Env3 Mix", 0, 127, 64, true, false, "signed" },
    { 131, "NoiseModKnobLFO1Mix", "Noise Mod Knob LFO1 Mix", 0, 127, 64, true, false, "signed" },
    { 132, "NoiseModKnobLFO2Mix", "Noise Mod Knob LFO2 Mix", 0, 127, 64, true, false, "signed" },
    { 133, "NoiseModKnobWhMix", "Noise Mod Knob Wh Mix", 0, 127, 64, true, false, "signed" },
    { 134, "RingMod1x3MixLevel", "Ring Mod1x3 Mix Level", 0, 127, 0, false, false, "unsignedZero" },
    { 135, "ModKnobEnv2Mix1x3", "Mod Knob Env2 Mix1x3", 0, 127, 64, true, false, "signed" },
    { 136, "ModKnobEnv3Mix1x3", "Mod Knob Env3 Mix1x3", 0, 127, 64, true, false, "signed" },
    { 137, "ModKnobLFO1Mix1x3", "Mod Knob LFO1 Mix1x3", 0, 127, 64, true, false, "signed" },
    { 138, "ModKnobLFO2Mix1x3", "Mod Knob LFO2 Mix1x3", 0, 127, 64, true, false, "signed" },
    { 139, "ModKnobWhMix1x3", "Mod Knob Wh Mix1x3", 0, 127, 64, true, false, "signed" },
    { 140, "RingMod2x3MixLevel", "Ring Mod2x3 Mix Level", 0, 127, 0, false, false, "unsignedZero" },
    { 141, "ModKnobEnv2Mix2x3", "Mod Knob Env2 Mix2x3", 0, 127, 64, true, false, "signed" },
    { 142, "ModKnobEnv3Mix2x3", "Mod Knob Env3 Mix2x3", 0, 127, 64, true, false, "signed" },
    { 143, "ModKnobLFO1Mix2x3", "Mod Knob LFO1 Mix2x3", 0, 127, 64, true, false, "signed" },
    { 144, "ModKnobLFO2Mix2x3", "Mod Knob LFO2 Mix2x3", 0, 127, 64, true, false, "signed" },
    { 145, "ModKnobWhMix2x3", "Mod Knob Wh Mix2x3", 0, 127, 64, true, false, "signed" },
    { 146, "FM1x3", "FM1x3", 0, 1, 0, false, true, "unsignedZero" },
    { 147, "LFO1Speed", "LFO1 Speed", 0, 127, 0, false, false, "unsignedZero" },
    { 148, "LFO1Delay", "LFO1 Delay", 0, 127, 0, false, false, "unsignedZero" },
    { 149, "LFO1SpeedEnv3", "LFO1 Speed Env3", 0, 127, 64, true, false, "signed" },
    { 150, "LFO1Offset", "LFO1 Offset", 0, 127, 64, true, false, "signed" },
    { 151, "LFO1SpeedAftertouchDepth", "LFO1 Speed Aftertouch Depth", 0, 127, 64, true, false, "signed" },
    { 152, "LFO1SpeedWhDepth", "LFO1 Speed Wh Depth", 0, 127, 64, true, false, "signed" },
    { 153, "LFO1SoftenAmount", "LFO1 Soften Amount", 0, 127, 0, false, false, "unsignedZero" },
    { 154, "LFO1FadeMode", "LFO1 Fade Mode", 0, 127, 0, false, false, "unsignedZero" },
    { 155, "LFO1DelayMode", "LFO1 Delay Mode", 0, 127, 0, false, false, "unsignedZero" },
    { 156, "LFO1DelayTrigger", "LFO1 Delay Trigger", 0, 1, 0, false, true, "lfoDelayTrigger" },
    { 157, "LFO1Type", "LFO1 Type", 0, 3, 0, false, true, "lfoType" },
    { 158, "LFO1Trigger", "LFO1 Trigger", 0, 1, 0, false, true, "lfoTriggering" },
    { 159, "LFO1Range", "LFO1 Range", 0, 2, 0, false, true, "lfoRange" },
    { 160, "LFO1Sync", "LFO1 Sync", 0, 34, 0, false, true, "lfoSync" },
    { 161, "LFO2Speed", "LFO2 Speed", 0, 127, 0, false, false, "unsignedZero" },
    { 162, "LFO2Delay", "LFO2 Delay", 0, 127, 0, false, false, "unsignedZero" },
    { 163, "LFO2SpeedEnv3", "LFO2 Speed Env3", 0, 127, 64, true, false, "signed" },
    { 164, "LFO2Offset", "LFO2 Offset", 0, 127, 64, true, false, "signed" },
    { 165, "LFO2SpeedAftertouchDepth", "LFO2 Speed Aftertouch Depth", 0, 127, 64, true, false, "signed" },
    { 166, "LFO2SpeedWhDepth", "LFO2 Speed Wh Depth", 0, 127, 64, true, false, "signed" },
    { 167, "LFO2SoftenAmount", "LFO2 Soften Amount", 0, 127, 0, false, false, "unsignedZero" },
    { 168, "LFO2FadeMode", "LFO2 Fade Mode", 0, 127, 0, false, false, "unsignedZero" },
    { 169, "LFO2DelayMode", "LFO2 Delay Mode", 0, 127, 0, false, false, "unsignedZero" },
    { 170, "LFO2DelayTrigger", "LFO2 Delay Trigger", 0, 1, 0, false, true, "lfoDelayTrigger" },
    { 171, "LFO2Type", "LFO2 Type", 0, 3, 0, false, true, "lfoType" },
    { 172, "LFO2Trigger", "LFO2 Trigger", 0, 1, 0, false, true, "lfoTriggering" },
    { 173, "LFO2Range", "LFO2 Range", 0, 2, 0, false, true, "lfoRange" },
    { 174, "LFO2Sync", "LFO2 Sync", 0, 34, 0, false, true, "lfoSync" },
    { 175, "FilterTracking", "Filter Tracking", 0, 127, 64, true, false, "signed" },
    { 176, "FilterFreqLFO2", "Filter Freq LFO2", 0, 127, 64, true, false, "signed" },
    { 177, "FilterFreqEnv3", "Filter Freq Env3", 0, 127, 64, true, false, "signed" },
    { 178, "FilterFreqLFO1", "Filter Freq LFO1", 0, 127, 64, true, false, "signed" },
    { 179, "FilterResEnv2", "Filter Res Env2", 0, 127, 64, true, false, "signed" },
    { 180, "FilterResLFO1", "Filter Res LFO1", 0, 127, 64, true, false, "signed" },
    { 181, "FilterResEnv3", "Filter Res Env3", 0, 127, 64, true, false, "signed" },
    { 182, "FilterResLFO2", "Filter Res LFO2", 0, 127, 64, true, false, "signed" },
    { 183, "FilterOverdrive", "Filter Overdrive", 0, 127, 0, false, false, "unsignedZero" },
    { 184, "FilterCutoffFreq", "Filter Cutoff Freq", 0, 127, 127, false, false, "unsignedZero" },
    { 185, "FilterResonance", "Filter Resonance", 0, 127, 0, false, false, "unsignedZero" },
    { 186, "FilterFreqEnv2", "Filter Freq Env2", 0, 127, 64, true, false, "signed" },
    { 187, "FilterModKnobWhFrequency", "Filter Mod Knob Wh Frequency", 0, 127, 64, true, false, "signed" },
    { 188, "FilterModKnobWhResonance", "Filter Mod Knob Wh Resonance", 0, 127, 64, true, false, "signed" },
    { 189, "FilterFrequencyWhLFO2Intensity", "Filter Frequency Wh LFO2 Intensity", 0, 127, 64, true, false, "signed" },
    { 190, "FilterResonanceWhLFO2Intensity", "Filter Resonance Wh LFO2 Intensity", 0, 127, 64, true, false, "signed" },
    { 191, "FilterFreqATLFO2Intensity", "Filter Frequency Aftertouch LFO2 Intensity", 0, 127, 64, true, false, "signed" },
    { 192, "FilterResoATLFO2Intensity", "Filter Resonance Aftertouch LFO2 Intensity", 0, 127, 64, true, false, "signed" },
    { 193, "FilterAftertouchFrequency", "Filter Aftertouch Frequency", 0, 127, 64, true, false, "signed" },
    { 194, "FilterAftertouchResonance", "Filter Aftertouch Resonance", 0, 127, 64, true, false, "signed" },
    { 195, "FilterQNormalise", "Filter Q Normalise", 0, 127, 0, false, false, "unsignedZero" },
    { 196, "SpecialFilterWidth", "Special Filter Width", 0, 127, 0, false, false, "unsignedZero" },
    { 197, "FilterOverdriveCurve", "Filter Overdrive Curve", 0, 127, 0, false, false, "unsignedZero" },
    { 198, "FilterOSCsBypass", "Filter OS Cs Bypass", 0, 127, 0, false, false, "unsignedZero" },
    { 199, "FilterSlope", "Filter Slope", 0, 2, 0, false, true, "filterSlope" },
    { 200, "FilterType", "Filter Type", 0, 11, 0, false, true, "filterType" },
    { 201, "Env1Attack", "Env1 Attack", 0, 127, 0, false, false, "unsignedZero" },
    { 202, "Env1Decay", "Env1 Decay", 0, 127, 0, false, false, "unsignedZero" },
    { 203, "Env1Sustain", "Env1 Sustain", 0, 127, 127, false, false, "unsignedZero" },
    { 204, "Env1Release", "Env1 Release", 0, 127, 0, false, false, "unsignedZero" },
    { 205, "Env1Velocity", "Env1 Velocity", 0, 127, 0, false, false, "unsignedZero" },
    { 206, "Env1Wh", "Env1 Wh", 0, 127, 64, true, false, "signed" },
    { 207, "Env1Aftertouch", "Env1 Aftertouch", 0, 127, 64, true, false, "signed" },
    { 208, "Env1KeyTracking", "Env1 Key Tracking", 0, 127, 0, false, false, "unsignedZero" },
    { 209, "Env1ADRepeat", "Env1 AD Repeat", 0, 127, 0, false, false, "adRepeat" },
    { 210, "Env1SustainTime", "Env1 Sustain Time", 0, 127, 127, false, false, "unsignedZero" },
    { 211, "Env1LevelTrack", "Env1 Level Track", 0, 127, 0, false, false, "unsignedZero" },
    { 212, "Env1SustainRate", "Env1 Sustain Rate", 0, 127, 64, true, false, "signed" },
    { 213, "Env1LevelNote", "Env1 Level Note", 0, 127, 0, false, false, "unsignedZero" },
    { 214, "Env2Attack", "Env2 Attack", 0, 127, 0, false, false, "unsignedZero" },
    { 215, "Env2Decay", "Env2 Decay", 0, 127, 0, false, false, "unsignedZero" },
    { 216, "Env2Sustain", "Env2 Sustain", 0, 127, 127, false, false, "unsignedZero" },
    { 217, "Env2Release", "Env2 Release", 0, 127, 0, false, false, "unsignedZero" },
    { 218, "Env2Velocity", "Env2 Velocity", 0, 127, 0, false, false, "unsignedZero" },
    { 219, "Env2Delay", "Env2 Delay", 0, 127, 0, false, false, "unsignedZero" },
    { 220, "Env2KeyTracking", "Env2 Key Tracking", 0, 127, 0, false, false, "unsignedZero" },
    { 221, "Env2ADRepeat", "Env2 AD Repeat", 0, 127, 0, false, false, "adRepeat" },
    { 222, "Env2SustainTime", "Env2 Sustain Time", 0, 127, 127, false, false, "unsignedZero" },
    { 223, "Env2LevelTrack", "Env2 Level Track", 0, 127, 0, false, false, "unsignedZero" },
    { 224, "Env2SustainRate", "Env2 Sustain Rate", 0, 127, 64, true, false, "signed" },
    { 225, "Env2LevelNote", "Env2 Level Note", 0, 127, 0, false, false, "unsignedZero" },
    { 226, "Env3Attack", "Env3 Attack", 0, 127, 0, false, false, "unsignedZero" },
    { 227, "Env3Decay", "Env3 Decay", 0, 127, 0, false, false, "unsignedZero" },
    { 228, "Env3Sustain", "Env3 Sustain", 0, 127, 127, false, false, "unsignedZero" },
    { 229, "Env3Release", "Env3 Release", 0, 127, 0, false, false, "unsignedZero" },
    { 230, "Env3Velocity", "Env3 Velocity", 0, 127, 0, false, false, "unsignedZero" },
    { 231, "Env3Delay", "Env3 Delay", 0, 127, 0, false, false, "unsignedZero" },
    { 232, "Env3KeyTracking", "Env3 Key Tracking", 0, 127, 0, false, false, "unsignedZero" },
    { 233, "Env3ADRepeat", "Env3 AD Repeat", 0, 127, 0, false, false, "adRepeat" },
    { 234, "Env3SustainTime", "Env3 Sustain Time", 0, 127, 127, false, false, "unsignedZero" },
    { 235, "Env3LevelTrack", "Env3 Level Track", 0, 127, 0, false, false, "unsignedZero" },
    { 236, "Env3SustainRate", "Env3 Sustain Rate", 0, 127, 64, true, false, "signed" },
    { 237, "Env3LevelNote", "Env3 Level Note", 0, 127, 0, false, false, "unsignedZero" },
    { 238, "EnvsTriggering", "Envs Triggering", 0, 7, 0, false, true, "envsTriggering" },
    { 239, "ArpPatternSelect", "Arp Pattern Select", 0, 127, 0, false, false, "unsignedZero" },
    { 240, "ArpSpeed", "Arp Speed", 0, 127, 0, false, false, "arpBpm" },
    { 241, "ArpLatch", "Arp Latch", 0, 127, 0, false, false, "unsignedZero" },
    { 242, "ArpGateTime", "Arp Gate Time", 0, 127, 0, false, false, "unsignedZero" },
    { 243, "ArpSync", "Arp Sync", 0, 34, 0, false, true, "arpSync" },
    { 244, "ArpOutputRanging", "Arp Output Ranging", 0, 1, 0, false, true, "arpOutputRanging" },
    { 245, "ArpEnabled", "Arp Enabled", 0, 1, 0, false, true, "arpEnabled" },
    { 246, "ArpKeysync", "Arp Keysync", 0, 1, 0, false, true, "arpKeysync" },
    { 247, "ArpPatternBank", "Arp Pattern Bank", 0, 2, 0, false, true, "arpPatternBank" },
    { 248, "ArpLatchType", "Arp Latch Type", 0, 1, 0, false, true, "arpLatchType" },
    { 249, "ArpQuantize", "Arp Quantize", 0, 4, 0, false, true, "arpQuantize" },
    { 250, "ArpVelocity", "Arp Velocity", 0, 3, 0, false, true, "arpVelocity" },
    { 251, "ArpMute", "Arp Mute", 0, 1, 0, false, true, "arpMute" },
    { 252, "ArpRealtimeTranspose", "Arp Realtime Transpose", 0, 1, 0, false, true, "arpRealtimeTranspose" },
    { 253, "ArpEditGate", "Arp Edit Gate", 0, 3, 0, false, true, "arpEditGate" },
    { 254, "ArpOutput", "Arp Output", 0, 2, 0, false, true, "arpOutput" },
    { 255, "ArpFillInNoteOrdering", "Arp Fill In Note Ordering", 0, 5, 0, false, true, "arpFillInNoteOrdering" },
    { 256, "ArpOctaveRange", "Arp Octave Range", 0, 3, 0, false, true, "arpOctaveRange" },
    { 257, "ArpPartEditViaKlot", "Arp Part Edit Via Klot", 0, 4, 0, false, true, "arpPartEditViaKbd" },
    { 258, "ArpConstantPitch", "Arp Constant Pitch", 0, 1, 0, false, true, "arpConstantPitch" },
    { 259, "Pan", "Pan", 0, 127, 64, true, false, "signed" },
    { 260, "PanType", "Pan Type", 0, 3, 0, false, true, "panType" },
    { 261, "PanEffects", "Pan Effects", 0, 1, 0, false, true, "panEffects" },
    { 262, "MasterVolumeLevel", "Master Volume Level", 0, 127, 127, false, false, "unsignedZero" },
    { 263, "PartProgramVolume", "Part Program Volume", 0, 127, 127, false, false, "unsignedZero" },
    { 264, "PanningSpeed", "Panning Speed", 0, 127, 0, false, false, "unsignedZero" },
    { 265, "PanningDepth", "Panning Depth", 0, 127, 0, false, false, "unsignedZero" },
    { 266, "UnisonDetune", "Unison Detune", 0, 127, 0, false, false, "unsignedZero" },
    { 267, "UnisonVoices", "Unison Voices", 0, 8, 0, false, true, "unisonVoices" },
    { 268, "PortamentoType", "Portamento Type", 0, 1, 0, false, true, "portamentoType" },
    { 269, "PortamentoGlide", "Portamento Glide", 0, 1, 0, false, true, "portamentoGlide" },
    { 270, "OscTriggerMode", "Osc Trigger Mode", 0, 1, 0, false, true, "oscTriggerMode" },
    { 271, "PolyMode", "Poly Mode", 0, 1, 0, false, true, "polyMode" },
    { 272, "PortamentoTime", "Portamento Time", 0, 127, 0, false, false, "unsignedZero" },
    { 273, "DistortionLevel", "Distortion Level", 0, 127, 0, false, false, "unsignedZero" },
    { 274, "VCODrift", "VCO Drift", 0, 127, 0, false, false, "unsignedZero" },
    { 275, "DrumMapDetune", "Drum Map Detune", 0, 127, 0, false, false, "unsignedZero" },
    { 276, "GlideType", "Glide Type", 0, 9, 0, false, true, "glideType" },
    { 277, "EffectsConfigMorphAmount", "Effects Config Morph Amount", 0, 127, 0, false, false, "unsignedZero" },
    { 278, "EffectsDryLevel", "Effects Dry Level", 0, 127, 127, false, false, "unsignedZero" },
    { 279, "EffectsBypass", "Effects Bypass", 0, 127, 0, false, false, "unsignedZero" },
    { 280, "ChorusSpeed", "Chorus Speed", 0, 127, 0, false, false, "unsignedZero" },
    { 281, "ChorusModDepth", "Chorus Mod Depth", 0, 127, 0, false, false, "unsignedZero" },
    { 282, "ChorusFeedback", "Chorus Feedback", 0, 127, 0, false, false, "unsignedZero" },
    { 283, "ChorusSendLevel", "Chorus Send Level", 0, 127, 0, false, false, "unsignedZero" },
    { 284, "ChorusModWheelDepth", "Chorus Mod Wheel Depth", 0, 127, 0, false, false, "unsignedZero" },
    { 285, "ChorusDelay", "Chorus Delay", 0, 127, 0, false, false, "unsignedZero" },
    { 286, "ChorusLFOWave", "Chorus LFO Wave", 0, 127, 0, false, false, "unsignedZero" },
    { 287, "ChorusSpeed2", "Chorus Speed2", 0, 127, 0, false, false, "unsignedZero" },
    { 288, "ChorusInertia", "Chorus Inertia", 0, 127, 0, false, false, "unsignedZero" },
    { 289, "ChorusStereoWidth", "Chorus Stereo Width", 0, 127, 0, false, false, "unsignedZero" },
    { 290, "ChorusWheelMode", "Chorus Wheel Mode", 0, 127, 0, false, false, "unsignedZero" },
    { 291, "ExtraChorusTypes", "Extra Chorus Types", 0, 127, 0, false, false, "unsignedZero" },
    { 292, "ChorusType", "Chorus Type", 0, 2, 0, false, true, "chorusType" },
    { 293, "DistortionModWheelDepth", "Distortion Mod Wheel Depth", 0, 127, 64, true, false, "signed" },
    { 294, "DistortionOutputLevel", "Distortion Output Level", 0, 127, 0, false, false, "unsignedZero" },
    { 295, "DistortionGainCompensation", "Distortion Gain Compensation", 0, 127, 64, true, false, "signed" },
    { 296, "DistortionCurve", "Distortion Curve", 0, 127, 64, true, false, "signed" },
    { 297, "EQBass", "EQ Bass", 0, 127, 0, false, false, "unsignedZero" },
    { 298, "EQTreble", "EQ Treble", 0, 127, 0, false, false, "unsignedZero" },
    { 299, "CombFilterFrequency", "Comb Filter Frequency", 0, 127, 0, false, false, "unsignedZero" },
    { 300, "CombFilterBoost", "Comb Filter Boost", 0, 127, 0, false, false, "unsignedZero" },
    { 301, "CombFilterFrequencyWh", "Comb Filter Frequency Wh", 0, 127, 64, true, false, "signed" },
    { 302, "CombFilterSpeed", "Comb Filter Speed", 0, 127, 0, false, false, "unsignedZero" },
    { 303, "CombFilterDepth", "Comb Filter Depth", 0, 127, 0, false, false, "unsignedZero" },
    { 304, "CombFilterSpread", "Comb Filter Spread", 0, 127, 0, false, false, "unsignedZero" },
    { 305, "CombFilterBoostWh", "Comb Filter Boost Wh", 0, 127, 64, true, false, "signed" },
    { 306, "ReverbModWheelDepth", "Reverb Mod Wheel Depth", 0, 127, 0, false, false, "unsignedZero" },
    { 307, "ReverbSendLevel", "Reverb Send Level", 0, 127, 0, false, false, "unsignedZero" },
    { 308, "ReverbDecay", "Reverb Decay", 0, 127, 0, false, false, "unsignedZero" },
    { 309, "ReverbHFDamp", "Reverb HF Damp", 0, 127, 0, false, false, "unsignedZero" },
    { 310, "ReverbEarlyRefLevel", "Reverb Early Ref Level", 0, 7, 0, false, false, "unsignedZero" },
    { 311, "DelaySendLevel", "Delay Send Level", 0, 127, 0, false, false, "unsignedZero" },
    { 312, "DelayTime", "Delay Time", 0, 127, 0, false, false, "unsignedZero" },
    { 313, "DelayFeedback", "Delay Feedback", 0, 127, 0, false, false, "unsignedZero" },
    { 314, "DelayHFDamp", "Delay HF Damp", 0, 127, 0, false, false, "unsignedZero" },
    { 315, "DelayModWheelDepth", "Delay Mod Wheel Depth", 0, 127, 0, false, false, "unsignedZero" },
    { 316, "DelayWidth", "Delay Width", 0, 127, 0, false, false, "unsignedZero" },
    { 317, "DelaySync", "Delay Sync", 0, 34, 0, false, true, "delaySync" },
    { 319, "DelayRatio", "Delay Ratio", 0, 12, 0, false, true, "delayRatio" },
    { 320, "VocoderBalance", "Vocoder Balance", 0, 127, 0, false, false, "unsignedZero" },
    { 321, "VocSibilanceType", "Voc Sibilance Type", 0, 1, 0, false, true, "vocSibilanceType" },
    { 322, "VocoderInputAudioInput", "Vocoder Input Audio Input", 0, 1, 0, false, true, "vocoderInputAudioInput" },
    { 323, "VocoderSibilanceLevel", "Vocoder Sibilance Level", 0, 15, 0, false, true, "vocSibilanceLevel" },
    { 324, "VocoderWidth", "Vocoder Width", 0, 15, 0, false, true, "vocoderWidth" },
    { 325, "VocoderInputPart", "Vocoder Modulator Source", 0, 7, 0, false, true, "vocoderModSource" },
    { 326, "VocoderInsertPart", "Vocoder Insert Part", 0, 7, 0, false, true, "vocoderInsertPart" },
    { 327, "ConstantGate", "Constant Gate", 0, 1, 0, false, true, "constantGate" },
    { 328, "DrumOneShot", "Drum One Shot", 0, 1, 0, false, true, "drumOneShot" },
    { 329, "ProgramCategory", "Program Category", 0, 127, 0, false, false, "unsignedZero" },
    { 330, "KbdTranspose", "Kbd Transpose", 0, 127, 0, false, false, "unsignedZero" },
    { 331, "ArpTransAreaHi", "Arp Trans Area Hi", 0, 127, 0, false, false, "unsignedZero" },
    { 332, "ArpTransAreaLo", "Arp Trans Area Lo", 0, 127, 0, false, false, "unsignedZero" },
    { 333, "ArpTransAreaTune", "Arp Trans Area Tune", 0, 127, 0, false, false, "unsignedZero" },
    { 334, "Osc1DigitalWave", "Osc1 Digital Wave", 0, 127, 0, false, false, "unsignedZero" },
    { 335, "Osc2DigitalWave", "Osc2 Digital Wave", 0, 127, 0, false, false, "unsignedZero" },
    { 336, "Osc3DigitalWave", "Osc3 Digital Wave", 0, 127, 0, false, false, "unsignedZero" },
    { 337, "VocoderDigitalMod", "Vocoder Digital Mod", 0, 127, 0, false, false, "unsignedZero" },
    { 338, "SourceSelect", "Source Select", 0, 127, 0, false, false, "unsignedZero" },
    { 339, "OscModSelect", "Osc Mod Select", 0, 127, 0, false, false, "unsignedZero" },
    { 340, "FilterModSelect", "Filter Mod Select", 0, 127, 0, false, false, "unsignedZero" },
    { 341, "MiscSelect", "Misc Select", 0, 127, 0, false, false, "unsignedZero" },
    { 342, "FxDigitalRouting", "Fx Digital Routing", 0, 127, 0, false, false, "unsignedZero" },
    { 344, "Polyphony", "Polyphony", 0, 3, 0, false, true, "partPolyphony" },
    { 345, "ReverbType", "Reverb Type", 0, 15, 0, false, true, "reverbType" },
    { 346, "FxOrder", "Fx Order", 0, 18, 0, false, true, "effectsConfig" },
    { 347, "FM2x3", "FM2x3", 0, 1, 0, false, true, "unsignedZero" },
    { 348, "FMNoise", "FM Noise", 0, 1, 0, false, true, "unsignedZero" },
};

const int kNumParams = (int) (sizeof (kParams) / sizeof (kParams[0]));

static std::map<juce::String, juce::StringArray> makeValueLists()
{
    std::map<juce::String, juce::StringArray> m;
    m["adRepeat"] = juce::StringArray ({ "Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "100", "101", "102", "103", "104", "105", "106", "107", "108", "109", "110", "111", "112", "113", "114", "115", "116", "117", "118", "119", "120", "121", "122", "123", "124", "125", "126", "Inf" });
    m["arpBpm"] = juce::StringArray ({ "64 BPM", "65 BPM", "66 BPM", "67 BPM", "68 BPM", "69 BPM", "70 BPM", "71 BPM", "72 BPM", "73 BPM", "74 BPM", "75 BPM", "76 BPM", "77 BPM", "78 BPM", "79 BPM", "80 BPM", "81 BPM", "82 BPM", "83 BPM", "84 BPM", "85 BPM", "86 BPM", "87 BPM", "88 BPM", "89 BPM", "90 BPM", "91 BPM", "92 BPM", "93 BPM", "94 BPM", "95 BPM", "96 BPM", "97 BPM", "98 BPM", "99 BPM", "100 BPM", "101 BPM", "102 BPM", "103 BPM", "104 BPM", "105 BPM", "106 BPM", "107 BPM", "108 BPM", "109 BPM", "110 BPM", "111 BPM", "112 BPM", "113 BPM", "114 BPM", "115 BPM", "116 BPM", "117 BPM", "118 BPM", "119 BPM", "120 BPM", "121 BPM", "122 BPM", "123 BPM", "124 BPM", "125 BPM", "126 BPM", "127 BPM", "128 BPM", "129 BPM", "130 BPM", "131 BPM", "132 BPM", "133 BPM", "134 BPM", "135 BPM", "136 BPM", "137 BPM", "138 BPM", "139 BPM", "140 BPM", "141 BPM", "142 BPM", "143 BPM", "144 BPM", "145 BPM", "146 BPM", "147 BPM", "148 BPM", "149 BPM", "150 BPM", "151 BPM", "152 BPM", "153 BPM", "154 BPM", "155 BPM", "156 BPM", "157 BPM", "158 BPM", "159 BPM", "160 BPM", "161 BPM", "162 BPM", "163 BPM", "164 BPM", "165 BPM", "166 BPM", "167 BPM", "168 BPM", "169 BPM", "170 BPM", "171 BPM", "172 BPM", "173 BPM", "174 BPM", "175 BPM", "176 BPM", "177 BPM", "178 BPM", "179 BPM", "180 BPM", "181 BPM", "182 BPM", "183 BPM", "184 BPM", "185 BPM", "186 BPM", "187 BPM", "188 BPM", "189 BPM", "190 BPM", "191 BPM" });
    m["arpConstantPitch"] = juce::StringArray ({ "Off", "On" });
    m["arpEditGate"] = juce::StringArray ({ "Norm", "Tie", "Rest", "Glide" });
    m["arpEnabled"] = juce::StringArray ({ "Off", "On" });
    m["arpFillInNoteOrdering"] = juce::StringArray ({ "Off/Up", "On/Up", "Off/Down", "On/Down", "Off/Played", "On/Played" });
    m["arpKeysync"] = juce::StringArray ({ "On", "Off" });
    m["arpLatchType"] = juce::StringArray ({ "Constant", "Pattern" });
    m["arpMute"] = juce::StringArray ({ "Off", "On" });
    m["arpOctaveRange"] = juce::StringArray ({ "1", "2", "3", "4" });
    m["arpOutput"] = juce::StringArray ({ "Program Only", "Program & MIDI", "MIDI Only" });
    m["arpOutputRanging"] = juce::StringArray ({ "On", "Off" });
    m["arpPartEditViaKbd"] = juce::StringArray ({ "Off", "On", "Note Only", "Vel Only", "Gate Only" });
    m["arpPatternBank"] = juce::StringArray ({ "Mono", "Poly", "User(U)" });
    m["arpQuantize"] = juce::StringArray ({ "Off", "Mode 1", "Mode 2", "Mode 3", "Mode 4" });
    m["arpRealtimeTranspose"] = juce::StringArray ({ "On", "Off" });
    m["arpSync"] = juce::StringArray ({ "Off", "32nd T", "32nd", "16th T", "16th", "8th T", "16th .", "8th", "4th T", "8th .", "4th", "2nd T", "4th .", "2nd", "1 Bar T", "2nd .", "1 Bar", "2 Bar T", "1 Bar .", "2 Bars", "4 Bar T", "3 Bars", "5 Bar T", "4 Bars", "3 Bar .", "7 Bar T", "5 Bars", "8 Bar T", "6 Bars", "7 Bars", "5 Bar .", "8 Bars", "6 Bar .", "7 Bar .", "8 Bar ." });
    m["arpVelocity"] = juce::StringArray ({ "Played", "Full", "Half", "Programmed" });
    m["bendRange"] = juce::StringArray ({ "-12", "-11", "-10", "-9", "-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11", "+12" });
    m["chorusType"] = juce::StringArray ({ "Quad Chorus", "Chorus/Flanger", "Phaser" });
    m["constantGate"] = juce::StringArray ({ "Off", "On" });
    m["delayRatio"] = juce::StringArray ({ "1:1", "1:0.75", "0.75:1", "1:0.66", "0.66:1", "1:0.5", "0.5:1", "1:0.33", "0.33:1", "1:0.25", "0.25:1", "1:Off", "Off:1" });
    m["delaySync"] = juce::StringArray ({ "Off", "32nd T", "32nd", "16th T", "16th", "8th T", "16th .", "8th", "4th T", "8th .", "4th", "2nd T", "4th .", "2nd", "1 Bar T", "2nd .", "1 Bar", "2 Bar T", "1 Bar .", "2 Bars", "4 Bar T", "3 Bars", "5 Bar T", "4 Bars", "3 Bar .", "7 Bar T", "5 Bars", "8 Bar T", "6 Bars", "7 Bars", "5 Bar .", "8 Bars", "6 Bar .", "7 Bar .", "8 Bar ." });
    m["drumOneShot"] = juce::StringArray ({ "Off", "On" });
    m["effectsConfig"] = juce::StringArray ({ "D+R+C", "D>R>C", "D>C>R", "R>D>C", "R>C>D", "C>D>R", "C>R>D", "D+[R>C]", "D+[C>R]", "R+[D>C]", "R+[C>D]", "C+[D>R]", "C+[R>D]", "D>[R+C]", "R>[D+C]", "C>[D+R]", "[R+C]>D", "[D+C]>R", "[D+R]>C" });
    m["envsTriggering"] = juce::StringArray ({ "M/M/M", "M/M/S", "M/S/M", "M/S/S", "S/M/M", "S/M/S", "S/S/M", "S/S/S" });
    m["filterSlope"] = juce::StringArray ({ "12dB", "18dB", "24dB" });
    m["filterType"] = juce::StringArray ({ "LPF", "BPF", "HPF", "Res LPF", "Res BPF", "Res HPF", "Notch", "LPF+LPF", "BPF+BPF", "HPF+HPF", "LPF+BPF", "BPF+HPF" });
    m["glideType"] = juce::StringArray ({ "Normal", "Auto", "2 Semi Down", "2 Semi Up", "5 Semi Down", "5 Semi Up", "7 Semi Down", "7 Semi Up", "12 Semi Down", "12 Semi Up" });
    m["lfoDelayTrigger"] = juce::StringArray ({ "Multi", "Single" });
    m["lfoRange"] = juce::StringArray ({ "Slow", "Normal", "Fast" });
    m["lfoSync"] = juce::StringArray ({ "Off", "32nd T", "32nd", "16th T", "16th", "8th T", "16th .", "8th", "4th T", "8th .", "4th", "2nd T", "4th .", "2nd", "1 Bar T", "2nd .", "1 Bar", "2 Bar T", "1 Bar .", "2 Bars", "4 Bar T", "3 Bars", "5 Bar T", "4 Bars", "3 Bar .", "7 Bar T", "5 Bars", "8 Bar T", "6 Bars", "7 Bars", "5 Bar .", "8 Bars", "6 Bar .", "7 Bar .", "8 Bar ." });
    m["lfoTriggering"] = juce::StringArray ({ "Freewheel", "Keysync" });
    m["lfoType"] = juce::StringArray ({ "Square", "Saw", "Triangle", "S/H" });
    m["oscOctave"] = juce::StringArray ({ "-2", "-1", "0", "+1", "+2" });
    // Degrees, spelled out: a degree sign is not ASCII and JUCE asserts on
    // non-ASCII characters in a plain const char* string literal.
    m["oscStartPhase"] = juce::StringArray ({ "0 deg", "3 deg", "6 deg", "8 deg", "11 deg", "14 deg", "17 deg", "20 deg", "22 deg", "25 deg", "28 deg", "31 deg", "34 deg", "37 deg", "39 deg", "42 deg", "45 deg", "48 deg", "51 deg", "53 deg", "56 deg", "59 deg", "62 deg", "65 deg", "68 deg", "70 deg", "73 deg", "76 deg", "79 deg", "82 deg", "84 deg", "87 deg", "90 deg", "93 deg", "96 deg", "98 deg", "101 deg", "104 deg", "107 deg", "110 deg", "112 deg", "115 deg", "118 deg", "121 deg", "124 deg", "127 deg", "129 deg", "132 deg", "135 deg", "138 deg", "141 deg", "143 deg", "146 deg", "149 deg", "152 deg", "155 deg", "158 deg", "160 deg", "163 deg", "166 deg", "169 deg", "172 deg", "174 deg", "177 deg", "180 deg", "183 deg", "186 deg", "188 deg", "191 deg", "194 deg", "197 deg", "200 deg", "202 deg", "205 deg", "208 deg", "211 deg", "214 deg", "217 deg", "219 deg", "222 deg", "225 deg", "228 deg", "231 deg", "233 deg", "236 deg", "239 deg", "242 deg", "245 deg", "248 deg", "250 deg", "253 deg", "256 deg", "259 deg", "262 deg", "264 deg", "267 deg", "270 deg", "273 deg", "276 deg", "278 deg", "281 deg", "284 deg", "287 deg", "290 deg", "292 deg", "295 deg", "298 deg", "301 deg", "304 deg", "307 deg", "309 deg", "312 deg", "315 deg", "318 deg", "321 deg", "323 deg", "326 deg", "329 deg", "332 deg", "335 deg", "338 deg", "340 deg", "343 deg", "346 deg", "349 deg", "352 deg", "354 deg", "357 deg" });
    m["oscTriggerMode"] = juce::StringArray ({ "Percussive", "Ensemble" });
    m["oscType"] = juce::StringArray ({ "Square", "Saw", "Audio In 1", "Audio In 2", "Double Saw" });
    m["panEffects"] = juce::StringArray ({ "No", "Yes" });
    m["panType"] = juce::StringArray ({ "Autopan", "Tremolo", "L-R", "R-L" });
    m["partPolyphony"] = juce::StringArray ({ "Off", "Mono", "Poly", "Prog" });
    m["polyMode"] = juce::StringArray ({ "Mode 1", "Mode 2" });
    m["portamentoGlide"] = juce::StringArray ({ "Linear", "Exponential" });
    m["portamentoType"] = juce::StringArray ({ "Portamento", "Glissando" });
    m["reverbType"] = juce::StringArray ({ "Gated Reverse", "Gated Rising", "Gated Gentle", "Gated Falling", "Dry Chamber", "Echo Chamber", "Small Room", "Big Room", "Medium 1", "Medium 2", "Medium Plate 1", "Medium Plate 2", "Large 1", "Large 2", "Large Plate 1", "Large Plate 2" });
    m["semitone"] = juce::StringArray ({ "-12", "-11", "-10", "-9", "-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11", "+12" });
    m["signed"] = juce::StringArray ({ "-64", "-63", "-62", "-61", "-60", "-59", "-58", "-57", "-56", "-55", "-54", "-53", "-52", "-51", "-50", "-49", "-48", "-47", "-46", "-45", "-44", "-43", "-42", "-41", "-40", "-39", "-38", "-37", "-36", "-35", "-34", "-33", "-32", "-31", "-30", "-29", "-28", "-27", "-26", "-25", "-24", "-23", "-22", "-21", "-20", "-19", "-18", "-17", "-16", "-15", "-14", "-13", "-12", "-11", "-10", "-9", "-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11", "+12", "+13", "+14", "+15", "+16", "+17", "+18", "+19", "+20", "+21", "+22", "+23", "+24", "+25", "+26", "+27", "+28", "+29", "+30", "+31", "+32", "+33", "+34", "+35", "+36", "+37", "+38", "+39", "+40", "+41", "+42", "+43", "+44", "+45", "+46", "+47", "+48", "+49", "+50", "+51", "+52", "+53", "+54", "+55", "+56", "+57", "+58", "+59", "+60", "+61", "+62", "+63" });
    m["unisonVoices"] = juce::StringArray ({ "Off", "On", "2", "3", "4", "5", "6", "7", "8" });
    m["unsignedZero"] = juce::StringArray ({ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "100", "101", "102", "103", "104", "105", "106", "107", "108", "109", "110", "111", "112", "113", "114", "115", "116", "117", "118", "119", "120", "121", "122", "123", "124", "125", "126", "127" });
    m["vocSibilanceLevel"] = juce::StringArray ({ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" });
    m["vocSibilanceType"] = juce::StringArray ({ "Hpass", "Noise" });
    m["vocoderInputAudioInput"] = juce::StringArray ({ "Audio In 1", "Audio In 3" });
    // Repurposed for this monotimbral build: on the hardware the modulator can
    // be another Part, here it is the audio input or the oscillators themselves.
    m["vocoderModSource"] = juce::StringArray ({ "Audio In", "Osc 1", "Osc 2", "Osc 3",
                                                 "Osc 1+2", "Osc 1+3", "Osc 2+3", "Noise" });
    m["vocoderInsertPart"] = juce::StringArray ({ "Part 1", "Part 2", "Part 3", "Part 4", "Part 5", "Part 6", "Part 7", "Part 8" });
    m["vocoderWidth"] = juce::StringArray ({ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" });
    return m;
}

const juce::StringArray& getValueList (const juce::String& key)
{
    static const auto lists = makeValueLists();
    static const juce::StringArray empty;
    auto it = lists.find (key);
    return it != lists.end() ? it->second : empty;
}

int paramSlotForIndex (int hardwareIndex)
{
    for (int i = 0; i < kNumParams; ++i)
        if (kParams[i].index == hardwareIndex) return i;
    return -1;
}

} // namespace aquanova