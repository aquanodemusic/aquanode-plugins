#pragma once

#include "ModuleCore.h"

// Four single-purpose filters - Lowpass, Highpass, Notch and Bell - each one
// a module of its own, so a patch reads at a glance and every knob means
// exactly one thing.
//
// All four are built on the same TPT ("topology-preserving transform")
// state-variable core (Zavalishin / Simper). It is unconditionally stable at
// any cutoff and resonance, sweeps cleanly at audio rate, and its frequency
// response is exact up to Nyquist - which is what the older Chamberlin SVF in
// the SVF Filter module was not.
//
// Flexible lane: fed by a per-voice source (an Oscillator) it runs one filter
// per note, in a global chain (after a reverb) it runs once.
// Inputs: 0 = Audio In, 1 = Mod In (cutoff / frequency, exponential, +-5
// octaves at 100% Mod Depth). Output: 0 = Audio Out.
namespace aquanode
{

// One TPT state-variable stage. tick() returns band and low; high, notch and
// bell are combinations of those and the input.
struct TptSvf
{
    float ic1 { 0.0f }, ic2 { 0.0f };

    void reset() { ic1 = ic2 = 0.0f; }

    // g = tan(pi * fc / fs), k = 1/Q
    inline void tick (float in, float g, float k, float& band, float& low)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = in - ic2;
        band = a1 * ic1 + a2 * v3;
        low = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * band - ic1;
        ic2 = 2.0f * low - ic2;
    }
};

class SimpleFilterBase : public SynthModule
{
public:
    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (auto& voice : stages)
            for (auto& ch : voice)
                for (auto& st : ch)
                    st.reset();
    }

    void voiceReset (int v) override
    {
        for (auto& ch : stages[(size_t) v])
            for (auto& st : ch)
                st.reset();
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float hz = modulatedFrequency (inputs[1][0]);
        const float g = std::tan (juce::MathConstants<float>::pi * hz / (float) sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            float y = filter (inputs[0][(size_t) c], g, stages[(size_t) v][(size_t) c]);
            if (! std::isfinite (y))
            {
                for (auto& st : stages[(size_t) v][(size_t) c]) st.reset();
                y = 0.0f;
            }
            outputs[0][(size_t) c] = y;
        }
    }

protected:
    // every module's first param is its frequency, its last its Mod Depth
    float modulatedFrequency (float modIn) const
    {
        const auto& d = getDescriptor();
        const float base = param (0);
        const float depth = param ((int) d.params.size() - 1) * 0.01f;
        return juce::jlimit (10.0f, (float) (sampleRate * 0.49),
                             base * std::pow (2.0f, depth * modIn * 5.0f));
    }

    virtual float filter (float in, float g, std::array<TptSvf, 2>& st) = 0;

    // [voice][channel][stage]
    std::array<std::array<std::array<TptSvf, 2>, 2>, kMaxVoices> stages {};
};

//==============================================================================
class LowpassModule : public SimpleFilterBase
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pSlope, pModDepth };

protected:
    float filter (float in, float g, std::array<TptSvf, 2>& st) override
    {
        const float res = param (pResonance);
        float band, low;
        if (param (pSlope) < 0.5f)
        {
            // 12 dB/oct: Butterworth at Resonance 0, ringing towards 1
            st[0].tick (in, g, 1.4142f * (1.0f - res) + 0.02f * res, band, low);
            return low;
        }
        // 24 dB/oct: two Butterworth-tuned stages, resonance on the second
        st[0].tick (in, g, 1.8478f, band, low);
        const float first = low;
        st[1].tick (first, g, 0.7654f * (1.0f - res) + 0.02f * res, band, low);
        return low;
    }
};

class HighpassModule : public SimpleFilterBase
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pSlope, pModDepth };

protected:
    float filter (float in, float g, std::array<TptSvf, 2>& st) override
    {
        const float res = param (pResonance);
        float band, low;
        if (param (pSlope) < 0.5f)
        {
            const float k = 1.4142f * (1.0f - res) + 0.02f * res;
            st[0].tick (in, g, k, band, low);
            return in - k * band - low;
        }
        constexpr float k1 = 1.8478f;
        st[0].tick (in, g, k1, band, low);
        const float first = in - k1 * band - low;
        const float k2 = 0.7654f * (1.0f - res) + 0.02f * res;
        st[1].tick (first, g, k2, band, low);
        return first - k2 * band - low;
    }
};

class NotchModule : public SimpleFilterBase
{
public:
    enum ParamIndex { pFreq = 0, pWidth, pModDepth };

protected:
    float filter (float in, float g, std::array<TptSvf, 2>& st) override
    {
        // Width is the notch's bandwidth in octaves; k = 1/Q
        const float bw = param (pWidth);
        const float q = 1.0f / (2.0f * std::sinh (0.34657359f * bw));   // ln2/2 * bw
        const float k = 1.0f / juce::jmax (0.05f, q);
        float band, low;
        st[0].tick (in, g, k, band, low);
        return in - k * band;    // low + high
    }
};

class BellModule : public SimpleFilterBase
{
public:
    enum ParamIndex { pFreq = 0, pGain, pQ, pModDepth };

protected:
    float filter (float in, float g, std::array<TptSvf, 2>& st) override
    {
        // Simper's bell: boost/cut of Gain dB around Freq, width set by Q
        const float A = std::pow (10.0f, param (pGain) / 40.0f);
        const float k = 1.0f / (juce::jmax (0.05f, param (pQ)) * A);
        float band, low;
        st[0].tick (in, g, k, band, low);
        return in + k * (A * A - 1.0f) * band;
    }
};

} // namespace aquanode
