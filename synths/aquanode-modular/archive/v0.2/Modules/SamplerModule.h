#pragma once

#include "ModuleCore.h"

// Sampler - maps the loaded sample to the keyboard (root fixed at C4),
// 24-voice polyphony from global MIDI, one-shot playback per note.
// Inputs: 0 = FM In (Mod), 1 = Env In (Mod). Output: 0 = Audio Out.
class SamplerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pLoadSample };

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    void handleMidiEvent (const juce::MidiMessage& m) override;

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "loadSample")
            openSampleChooser();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 56; }

private:
    aquanode::VoiceAllocator voices;
    double position [aquanode::VoiceAllocator::maxVoices] {};
    double rate     [aquanode::VoiceAllocator::maxVoices] {};
    float  gateLvl  [aquanode::VoiceAllocator::maxVoices] {};
    double silentFor [aquanode::VoiceAllocator::maxVoices] {};

    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    double latchedSourceRate { 44100.0 };

    static constexpr double rootHz = 261.6255653005986;   // C4 (MIDI note 60)
    static constexpr double gateRampSeconds = 0.003;
    static constexpr double silenceReleaseSeconds = 0.05;
};
