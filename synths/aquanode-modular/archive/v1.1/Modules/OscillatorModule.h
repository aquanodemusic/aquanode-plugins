#pragma once

#include "ModuleCore.h"

// Oscillator - FM operator (true DX7-style phase modulation). PerVoice: the
// engine calls processVoiceSample once per active global voice, and both FM In
// and Env In arrive as that voice's OWN signals - so a modulator oscillator
// and an ADSR patched here act per note, exactly like a real polysynth.
// Inputs:  0 = FM In (Mod), 1 = Env In (Mod). Output: 0 = Audio Out.
class OscillatorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pFmRatio, pWaveform, pCycleLength, pLoadSample };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void voiceNoteOn (int voice, int note, bool retrigger) override;
    void voiceVelocity (int voice, float velocity01) override { vel[voice] = velocity01; }
    void voiceNoteOff (int voice) override;
    void voiceReset (int voice) override;
    double voiceTailSeconds() const override { return 0.02; }   // covers the gate ramp

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "loadSample")
            openSampleChooser();
    }

private:
    float readWave (int waveform, double phase01) const;   // phase 0..1
    float readSampleTable (double phase01) const;

    double phase   [aquanode::kMaxVoices] {};
    double freqHz  [aquanode::kMaxVoices] {};
    float  gateLvl [aquanode::kMaxVoices] {};
    bool   gateOn  [aquanode::kMaxVoices] {};
    float  vel     [aquanode::kMaxVoices] {};   // note-on velocity, applied per voice

    // latched once per block (audio thread)
    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    const float* tableData { nullptr };
    int tableSourceLen { 0 };

    static constexpr double gateRampSeconds = 0.003;   // click-avoidance ramp on the on/off gate
};
