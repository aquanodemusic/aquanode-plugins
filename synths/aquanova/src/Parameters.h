#pragma once
#include <JuceHeader.h>
#include "ParameterTable.h"

namespace aquanova {

//==============================================================================
/** Hardware parameter indices used directly by the DSP.
    These match the Supernova II's own parameter numbering, so the sysex
    reader/writer and the engine speak the same language.
*/
namespace P
{
    enum
    {
        // --- Oscillators. Osc 2 and 3 are the same block, offset by 40.
        OscStride            = 40,

        Osc1Type             = 1,
        Osc1Octave           = 2,
        Osc1Semitone         = 3,
        Osc1FineTune         = 4,
        Osc1MixLevel         = 5,
        Osc1MixEnv2          = 6,
        Osc1MixEnv3          = 7,
        Osc1MixLFO1          = 8,
        Osc1MixLFO2          = 9,
        Osc1MixWheel         = 10,
        Osc1PitchEnv2        = 11,
        Osc1PitchEnv3        = 12,
        Osc1PitchLFO1        = 13,
        Osc1PitchLFO2        = 14,
        Osc1PitchWheel       = 15,
        Osc1PitchManual      = 16,
        Osc1PulseWidth       = 17,
        Osc1WidthEnv2        = 18,
        Osc1WidthLFO1        = 19,
        Osc1WidthLFO2        = 20,
        Osc1WidthEnv3        = 21,
        Osc1WidthWheel       = 22,
        Osc1SyncEnv3         = 23,
        Osc1SyncLFO2         = 24,
        Osc1SyncWheel        = 25,
        Osc1HardnessEnv3     = 26,
        Osc1HardnessLFO1     = 27,
        Osc1HardnessLFO2     = 28,
        Osc1HardnessWheel    = 29,
        Osc1Sync             = 32,
        Osc1SyncEnv2         = 33,
        Osc1SyncLFO1         = 34,
        Osc1KeySync          = 35,
        Osc1SyncSkew         = 36,
        Osc1FormantWidth     = 37,
        Osc1Soften           = 38,
        Osc1SoftenEnv2       = 39,
        Osc1BendRange        = 40,

        OscsStartPhase       = 121,

        // --- Noise
        NoiseSoften          = 122,
        NoiseMixLevel        = 123,
        NoiseMixEnv2         = 124,
        NoiseMixEnv3         = 130,
        NoiseMixLFO1         = 131,
        NoiseMixLFO2         = 132,
        NoiseMixWheel        = 133,

        // --- Ring modulators
        Ring1x3Level         = 134,
        Ring1x3Env2          = 135,
        Ring1x3Env3          = 136,
        Ring1x3LFO1          = 137,
        Ring1x3LFO2          = 138,
        Ring1x3Wheel         = 139,
        Ring2x3Level         = 140,
        Ring2x3Env2          = 141,
        Ring2x3Env3          = 142,
        Ring2x3LFO1          = 143,
        Ring2x3LFO2          = 144,
        Ring2x3Wheel         = 145,
        FM1x3                = 146,
        FM2x3                = 347,
        FMNoise              = 348,

        // --- LFOs (LFO 2 = LFO 1 + 14)
        LfoStride            = 14,
        LFO1Speed            = 147,
        LFO1Delay            = 148,
        LFO1SpeedEnv3        = 149,
        LFO1Offset           = 150,
        LFO1SpeedAftertouch  = 151,
        LFO1SpeedWheel       = 152,
        LFO1Soften           = 153,
        LFO1FadeMode         = 154,
        LFO1DelayMode        = 155,
        LFO1DelayTrigger     = 156,
        LFO1Type             = 157,
        LFO1Trigger          = 158,
        LFO1Range            = 159,
        LFO1Sync             = 160,

        // --- Filter
        FilterTracking       = 175,
        FilterFreqLFO2       = 176,
        FilterFreqEnv3       = 177,
        FilterFreqLFO1       = 178,
        FilterResEnv2        = 179,
        FilterResLFO1        = 180,
        FilterResEnv3        = 181,
        FilterResLFO2        = 182,
        FilterOverdrive      = 183,
        FilterCutoff         = 184,
        FilterResonance      = 185,
        FilterFreqEnv2       = 186,
        FilterFreqWheel      = 187,
        FilterResWheel       = 188,
        FilterFreqAftertouch = 193,
        FilterResAftertouch  = 194,
        FilterQNormalise     = 195,
        FilterWidth          = 196,
        FilterOverdriveCurve = 197,
        FilterOscsBypass     = 198,
        FilterSlope          = 199,
        FilterType           = 200,

        // --- Envelopes (Env 2 = Env 1 + 13, Env 3 = Env 1 + 25)
        Env1Attack           = 201,
        Env1Decay            = 202,
        Env1Sustain          = 203,
        Env1Release          = 204,
        Env1Velocity         = 205,
        Env1KeyTracking      = 208,
        Env1ADRepeat         = 209,
        Env1SustainTime      = 210,
        Env1SustainRate      = 212,

        Env2Attack           = 214,
        Env2Decay            = 215,
        Env2Sustain          = 216,
        Env2Release          = 217,
        Env2Velocity         = 218,
        Env2Delay            = 219,
        Env2KeyTracking      = 220,
        Env2ADRepeat         = 221,
        Env2SustainTime      = 222,
        Env2SustainRate      = 224,

        Env3Attack           = 226,
        Env3Decay            = 227,
        Env3Sustain          = 228,
        Env3Release          = 229,
        Env3Velocity         = 230,
        Env3Delay            = 231,
        Env3KeyTracking      = 232,
        Env3ADRepeat         = 233,
        Env3SustainTime      = 234,
        Env3SustainRate      = 236,

        EnvsTriggering       = 238,

        // --- Arpeggiator
        ArpPatternSelect     = 239,
        ArpSpeed             = 240,
        ArpLatch             = 241,
        ArpGateTime          = 242,
        ArpSync              = 243,
        ArpEnabled           = 245,
        ArpKeysync           = 246,
        ArpPatternBank       = 247,
        ArpOctaveRange       = 256,

        // --- Voice / pan / global
        Pan                  = 259,
        PanType              = 260,
        PanEffects           = 261,
        MasterVolume         = 262,
        ProgramVolume        = 263,
        PanningSpeed         = 264,
        PanningDepth         = 265,
        UnisonDetune         = 266,
        UnisonVoices         = 267,
        PortamentoType       = 268,
        PortamentoGlide      = 269,
        OscTriggerMode       = 270,
        PolyMode             = 271,
        PortamentoTime       = 272,
        VCODrift             = 274,
        GlideType            = 276,
        ConstantGate         = 327,
        PartPolyphony        = 344,

        // --- Effects
        DistortionLevel      = 273,
        EffectsMorph         = 277,
        EffectsDryLevel      = 278,
        EffectsBypass        = 279,
        ChorusSpeed          = 280,
        ChorusModDepth       = 281,
        ChorusFeedback       = 282,
        ChorusSendLevel      = 283,
        ChorusDelay          = 285,
        ChorusLFOWave        = 286,
        ChorusStereoWidth    = 289,
        ChorusType           = 292,
        DistortionOutput     = 294,
        DistortionGainComp   = 295,
        DistortionCurve      = 296,
        EQBass               = 297,
        EQTreble             = 298,
        CombFrequency        = 299,
        CombBoost            = 300,
        CombSpeed            = 302,
        CombDepth            = 303,
        CombSpread           = 304,
        ReverbSendLevel      = 307,
        ReverbDecay          = 308,
        ReverbHFDamp         = 309,
        ReverbEarlyRef       = 310,
        DelaySendLevel       = 311,
        DelayTime            = 312,
        DelayFeedback        = 313,
        DelayHFDamp          = 314,
        DelayWidth           = 316,
        DelaySync            = 317,
        DelayRatio           = 319,
        ReverbType           = 345,
        FxOrder              = 346,

        // --- Vocoder
        VocoderBalance       = 320,
        VocSibilanceType     = 321,
        VocoderAudioInput    = 322,
        VocSibilanceLevel    = 323,
        VocoderWidth         = 324,
        VocoderModSource     = 325,
        VocoderSpeed         = 337
    };
}

//==============================================================================
/** Id of the one parameter that is ours rather than the hardware's: the DC
    blocker on the output. It deliberately lives outside the generated table,
    so the panel sections and the randomiser - which both walk kParams - leave
    it alone and the header owns it. */
inline constexpr const char* kDcBlockId = "DCBlock";

/** Corner frequency of that blocker, in Hz - the DC Cancel Speed slider.
    1 = slow and click-free, 5 = fast but can clack on a sudden DC-heavy
    onset. Also ours, also outside kParams, for the same reason. */
inline constexpr const char* kDcSpeedId = "DCSpeed";
inline constexpr float kDcSpeedMinHz = 1.0f;
inline constexpr float kDcSpeedMaxHz = 5.0f;

//==============================================================================
/** Fast, lock-free read access to every parameter, addressed by the hardware
    parameter index. The DSP never touches the APVTS tree directly.
*/
class ParamAccess
{
public:
    void attach (juce::AudioProcessorValueTreeState& state)
    {
        slots.fill (nullptr);

        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& d = kParams[i];
            if (juce::isPositiveAndBelow (d.index, kMaxIndex))
                slots[(size_t) d.index] = state.getRawParameterValue (d.id);
        }

        // Not hardware parameters, so they have no slot in the table above.
        dcBlock = state.getRawParameterValue (kDcBlockId);
        dcSpeed = state.getRawParameterValue (kDcSpeedId);
    }

    /** The output DC blocker, on by default. */
    inline bool dcBlockEnabled() const noexcept
    {
        return dcBlock == nullptr || dcBlock->load() > 0.5f;
    }

    /** Its corner frequency in Hz, from the DC Cancel Speed slider. */
    inline float dcBlockHz() const noexcept
    {
        return dcSpeed != nullptr ? dcSpeed->load() : kDcSpeedMinHz;
    }

    /** Raw hardware value, normally 0..127. */
    inline int raw (int hwIndex) const noexcept
    {
        auto* p = slots[(size_t) hwIndex];
        return p != nullptr ? (int) p->load() : 0;
    }

    /** 0..1 */
    inline float uni (int hwIndex) const noexcept   { return (float) raw (hwIndex) * (1.0f / 127.0f); }

    /** -1..+1, matching the hardware's -64..+63 display. */
    inline float bip (int hwIndex) const noexcept
    {
        const int v = raw (hwIndex) - 64;
        return v < 0 ? (float) v * (1.0f / 64.0f) : (float) v * (1.0f / 63.0f);
    }

    inline bool flag (int hwIndex) const noexcept   { return raw (hwIndex) != 0; }

    static constexpr int kMaxIndex = 400;

private:
    std::array<std::atomic<float>*, (size_t) kMaxIndex> slots { };
    std::atomic<float>* dcBlock = nullptr;
    std::atomic<float>* dcSpeed = nullptr;
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace aquanova
