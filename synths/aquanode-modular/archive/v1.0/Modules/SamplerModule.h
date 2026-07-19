#pragma once

#include "ModuleCore.h"

// Sampler - keyboard-mapped one-shot playback (root fixed at C4). PerVoice:
// each engine voice plays its own copy of the sample; Env In arrives as that
// voice's own envelope signal.
// Inputs: 0 = FM In (Mod, playback-rate mod), 1 = Env In (Mod). Output: 0 = Audio Out.
class SamplerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pLoadSample };

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
    double voiceTailSeconds() const override { return 0.02; }

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "loadSample")
            openSampleChooser();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 56; }

private:
    double position [aquanode::kMaxVoices] {};
    double rate     [aquanode::kMaxVoices] {};
    float  gateLvl  [aquanode::kMaxVoices] {};
    bool   gateOn   [aquanode::kMaxVoices] {};
    float  vel      [aquanode::kMaxVoices] {};

    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    double latchedSourceRate { 44100.0 };

    static constexpr double rootHz = 261.6255653005986;   // C4 (MIDI note 60)
    static constexpr double gateRampSeconds = 0.003;
};
