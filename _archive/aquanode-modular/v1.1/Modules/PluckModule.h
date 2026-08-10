#pragma once

#include "ModuleCore.h"

// Pluck - Karplus-Strong plucked string: a noise burst excites a tuned
// feedback delay line with a damping low-pass in the loop. Played from the
// keyboard it's fully polyphonic (per-voice strings); triggered from a
// Step Seq / Euclid it runs as a global mono string, with Pitch In taking
// the sequencer's Pitch Out directly (semitones / 60 around Note).
// Inputs: 0 = Trig In, 1 = Pitch In. Output: 0 = Audio Out.
class PluckModule : public aquanode::SynthModule
{
public:
    static constexpr int maxDelaySamples = 8192;
    enum ParamIndex { pNote = 0, pDecay, pDamp, pColor };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    PluckModule()
    {
        for (auto& l : line)
            l.assign (maxDelaySamples, 0.0f);
    }

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
        if ((int) line[v].size() == maxDelaySamples)
            std::fill (line[v].begin(), line[v].end(), 0.0f);
        writeIndex[v] = 0;
        lpfState[v] = colorState[v] = 0.0f;
        burstSamples[v] = 0;
        noteOfVoice[v] = -1;
        lastTrig[v] = 0.0f;
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        noteOfVoice[v] = note;
        trigger (v);
    }

    void voiceNoteOff (int v) override { juce::ignoreUnused (v); }   // string rings out

    double voiceTailSeconds() const override { return param (pDecay) * 0.001 + 0.2; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    void trigger (int v)
    {
        burstSamples[v] = -1;   // length resolved on the next sample (needs pitch)
    }

    std::vector<float> line [aquanode::kMaxVoices];
    int writeIndex [aquanode::kMaxVoices] {};
    float lpfState [aquanode::kMaxVoices] {};
    float colorState [aquanode::kMaxVoices] {};
    int burstSamples [aquanode::kMaxVoices] {};
    int noteOfVoice [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    juce::Random random;
};
