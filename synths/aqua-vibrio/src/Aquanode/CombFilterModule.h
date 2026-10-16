#pragma once

#include "ModuleCore.h"

// Comb Filter - an actual delay-line comb filter, the Vital kind: a delay one
// wavelength long, fed back on itself, so the peaks land on the harmonic
// series of Freq and the thing rings at a pitch. (The older "Center Comb"
// module is a bank of peaking EQs around a centre - useful, but no delay
// line, no ringing, no pitch.)
//
//   * Feedback is signed, which is the whole character of a comb: positive
//     reinforces every harmonic, negative cancels the even ones and leaves
//     the hollow, clarinet-like odd-harmonic sound. At the extremes it
//     self-oscillates.
//   * Damping is a one-pole lowpass INSIDE the feedback loop, so each pass
//     round the delay loses more top end - the difference between a metallic
//     buzz and a plucked string.
//   * Blend crossfades feedback comb (resonant, ringing) against feedforward
//     comb (the classic phasey notches, no ring).
//   * Pitch In takes the same semitones/60 CV as KeyTrack, so patching
//     KeyTrack straight in makes the comb track the keyboard and turn any
//     noise source into a Karplus-Strong string.
//
// Flexible lane: inside a per-voice chain every voice gets its own delay
// line and rings at its own pitch; in a global chain it runs once.
// Inputs: 0 = Audio In, 1 = Pitch In. Output: 0 = Audio Out.
class CombFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pFreq = 0, pFeedback, pDamping, pBlend, pPitchDepth, pDryWet };

    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 8000.0f;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override;
    void reset() override;
    void voiceReset (int voice) override { clearLane (voice); }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        renderLane (voice, inputs, outputs);
    }

    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override
    {
        renderLane (aquanode::kMaxVoices, inputs, outputs);   // the global lane
    }

private:
    // one comb per voice, plus one for the global lane
    static constexpr int kNumLanes = aquanode::kMaxVoices + 1;

    struct Lane
    {
        int writePos { 0 };
        float loopLp [2] {};    // damping state, per channel
    };

    void renderLane (int lane, const aquanode::StereoFrame* inputs,
                     aquanode::StereoFrame* outputs);
    void clearLane (int lane);

    float readLine (const std::vector<float>& line, int lane, int ch, float delaySamples) const;

    // [lane][channel] interleaved into one flat block each
    std::vector<float> fbLine, ffLine;   // feedback line stores y, feedforward line stores x
    Lane lanes [kNumLanes];
    int lineLength { 2 };

    int lineIndex (int lane, int ch, int pos) const
    {
        return (lane * 2 + ch) * lineLength + pos;
    }
};
