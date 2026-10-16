#pragma once

#include "ModuleCore.h"
#include "DXNativeFM.h"

// DX Operator - one operator of a six-operator FM engine, as a module.
// It carries exactly the 21 controls a DX7 operator has (the eight-stage
// envelope R1-R4 / L1-L4, output level, ratio or fixed frequency with coarse,
// fine and detune, keyboard level scaling with break point, depths and
// curves, rate scaling, amp-mod and velocity sensitivity) and runs them
// through the engine's own curves - the same level law, the same keyboard
// scaling steps, the same envelope with its accelerated attack.
//
// The algorithm is the patch: an operator's Audio Out into another one's
// FM In makes it that operator's modulator, with the DX7's modulation depth,
// so stacking DX Operators reproduces any of the 32 algorithms and lots more.
// Feedback is the DX7's operator self-feedback (0-7).
//
// AM In takes a 0..1 signal (a DX LFO's AM Out) and pulls the level down by
// up to the AMS depth - the DX7's amplitude modulation. Pitch In moves the
// pitch in the KeyTrack convention (semitones / 60): a DX Pitch EG and a DX
// LFO's Pitch Out go here. Its envelope is built in, so no ADSR is needed; its
// tail follows R4.
//
// Fb In replaces the operator's self-feedback with another operator's output,
// for the two DX7 algorithms (4 and 6) whose feedback loop runs through
// several operators: OP4 or OP5 into OP6's Fb In, with OP6's Feedback knob
// setting the amount exactly as the DX7 does (the last two samples
// averaged, scaled by 2^(fb-7)).
//
// Out / is the OPS's carrier attenuation: a DX7 divides each carrier by the
// number of carriers in the algorithm, inside the operator - so it applies
// to the feedback tap too. Set it to the carrier count on every carrier.
// Key Sync Off lets the oscillator run free, so each note starts at an
// arbitrary phase instead of at zero.
//
// Inputs: 0 = FM In (audio), 1 = Fb In (audio), 2 = AM In (mod),
//         3 = Pitch In (mod), 4 = Add Midi In.  Output: 0 = Audio Out.
class DXOperatorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pMode = 0, pCoarse, pFine, pDetune,
        pLevel, pFeedback, pVelSens, pAms, pRateScale,
        pR1, pR2, pR3, pR4, pL1, pL2, pL3, pL4,
        pBreakPoint, pLeftDepth, pRightDepth, pLeftCurve, pRightCurve,
        pVoices, pGlide, pCom, pKeySync, pOpNumber
    };

    enum InputIndex { inFm = 0, inFb, inAm, inPitch };

    // A carrier at full level comes out at this amplitude; FM In scales by the
    // inverse, so one DX Operator into another modulates exactly as the
    // engine's operators do (where full scale is a phase swing of 2.07 cycles).
    static constexpr float kOutScale = 0.5f;
    static constexpr int kCtrlBlock = 32;     // the engine's control rate

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < aquanode::kMaxVoices; ++v) voiceReset (v); }

    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override;
    void voiceVelocity (int v, float velocity01) override { velocity[v] = juce::jlimit (1, 127, juce::roundToInt (velocity01 * 127.0f)); }
    double voiceTailSeconds() const override;

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // "DX Operator 3" when a generator has numbered it (hidden Op param)
    juce::String titleSuffix() const override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 86; }

private:
    void controlUpdate (int v, float amIn);

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;

    dxnative::OperatorEnv env[aquanode::kMaxVoices];
    double phase[aquanode::kMaxVoices] {};
    float gain[aquanode::kMaxVoices] {}, gainStep[aquanode::kMaxVoices] {};
    float fb1[aquanode::kMaxVoices] {}, fb2[aquanode::kMaxVoices] {};
    float extFbPrev[aquanode::kMaxVoices] {};   // last Fb In sample, for the two-sample average
    juce::Random phaseRandom;
    int ctrlLeft[aquanode::kMaxVoices] {};
    int noteOf[aquanode::kMaxVoices] {};
    int velocity[aquanode::kMaxVoices] {};
};
