#pragma once

#include "ModuleCore.h"

// Oscillator - FM operator (true DX7-style phase modulation), 24-voice
// polyphony driven by global MIDI. Waveforms: Sine/Tri/Saw/Square/Sample.
// Inputs:  0 = FM In (Mod), 1 = Env In (Mod). Output: 0 = Audio Out.
class OscillatorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pFmRatio, pWaveform, pCycleLength, pLoadSample };

    void prepare (double sr) override;
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    void handleMidiEvent (const juce::MidiMessage& m) override;

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "loadSample")
            openSampleChooser();
    }

private:
    float readWave (int waveform, double phase01) const;   // phase 0..1
    float readSampleTable (double phase01) const;

    aquanode::VoiceAllocator voices;
    double phase   [aquanode::VoiceAllocator::maxVoices] {};
    double freqHz  [aquanode::VoiceAllocator::maxVoices] {};
    float  gateLvl [aquanode::VoiceAllocator::maxVoices] {};
    double releaseElapsed [aquanode::VoiceAllocator::maxVoices] {};

    // latched once per block (audio thread)
    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    const float* tableData { nullptr };
    int tableSourceLen { 0 };

    static constexpr double gateRampSeconds = 0.003;   // click-avoidance ramp on the on/off gate
    static constexpr double envTailSeconds  = 21.0;    // keep voices alive for external env release tails
};
