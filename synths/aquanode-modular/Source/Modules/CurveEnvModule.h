#pragma once

#include "ModuleCore.h"

// Curve Env - a per-voice DAHDSR envelope with a curve on every moving
// segment. Delay waits, Attack rises, Hold stays at the top, Decay falls to
// Sustain, Release falls to zero. A Curve of 0 is a straight line; negative
// curves move fast first and settle slowly (the natural, "exponential"
// look), positive ones start slow and finish fast. Very negative decay
// curves make the classic kick-drum pitch drop.
//
// Amount scales (and with a negative value flips) the output, so the same
// envelope can drive a Pitch In (semitones / 60: 20% = an octave), a cutoff
// or an amplitude. Velocity sets how much the key velocity scales it.
// Legato keeps a running envelope going when a voice is re-struck.
// No inputs. Output: 0 = Env Out.
class CurveEnvModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pDelay = 0, pAttack, pHold, pDecay, pSustain,
        pRelease, pACurve, pDCurve, pRCurve, pAmount,
        pVelocity, pMode
    };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        stage[v] = Stage::Idle;
        level[v] = 0.0f;
        t[v] = 0.0;
        from[v] = 0.0f;
    }

    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override;
    void voiceVelocity (int v, float velocity01) override { vel[v] = velocity01; }
    double voiceTailSeconds() const override { return param (pRelease) * 0.001 + 0.01; }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 58; }

    // y of the curve family at x (0..1): 0 = straight
    static float curve (float x, float c)
    {
        if (std::abs (c) < 0.01f)
            return x;
        return (std::exp (c * x) - 1.0f) / (std::exp (c) - 1.0f);
    }

    // the last level any voice produced, for the display's dot
    float displayLevel() const { return shown.load (std::memory_order_relaxed); }

private:
    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    Stage stage[aquanode::kMaxVoices] {};
    float level[aquanode::kMaxVoices] {};
    float from[aquanode::kMaxVoices] {};       // level the current segment started at
    double t[aquanode::kMaxVoices] {};         // seconds into the current segment
    float vel[aquanode::kMaxVoices] {};
    std::atomic<float> shown { 0.0f };
};
