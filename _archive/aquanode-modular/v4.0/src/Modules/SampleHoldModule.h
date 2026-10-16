#pragma once

#include "ModuleCore.h"

// S&H - clocked sample & hold. On every rising edge at Clock In (or from the
// internal Rate clock when Clock In is unpatched) it samples Signal In and
// holds it; with Signal In unpatched it samples an internal random source -
// the classic burbling random voltage (feed it through Quantize for melodies).
// Glide slews between held values. Flexible: per-voice when fed per-voice.
// Inputs: 0 = Signal In, 1 = Clock In. Output: 0 = Mod Out (bipolar).
class SampleHoldModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pGlide };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        held[v] = out[v] = 0.0f;
        lastClock[v] = 0.0f;
        phase[v] = 1.0;   // internal clock fires immediately
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        bool trigger = false;

        if (isInputConnected (1))
        {
            const float clockIn = inputs[1][0];
            trigger = clockIn > 0.5f && lastClock[v] <= 0.5f;
            lastClock[v] = clockIn;
        }
        else
        {
            phase[v] += param (pRate) / sampleRate;
            if (phase[v] >= 1.0)
            {
                phase[v] -= std::floor (phase[v]);
                trigger = true;
            }
        }

        if (trigger)
            held[v] = isInputConnected (0) ? inputs[0][0]
                                           : random.nextFloat() * 2.0f - 1.0f;

        // glide: one-pole toward the held value
        const float glideMs = param (pGlide);
        if (glideMs < 0.5f)
            out[v] = held[v];
        else
        {
            const float coeff = 1.0f - std::exp ((float) (-1.0 / (glideMs * 0.001 * sampleRate)));
            out[v] += coeff * (held[v] - out[v]);
        }

        outputs[0][0] = out[v];
        outputs[0][1] = out[v];
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    float held [aquanode::kMaxVoices] {};
    float out  [aquanode::kMaxVoices] {};
    float lastClock [aquanode::kMaxVoices] {};
    double phase [aquanode::kMaxVoices] {};
    juce::Random random;
};
