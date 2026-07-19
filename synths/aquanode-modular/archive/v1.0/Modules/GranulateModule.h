#pragma once

#include <atomic>
#include "ModuleCore.h"

// Granulate - granular sampler (adapted from the standalone Granulate VST).
// PerVoice: each note runs its own cloud of grains, pitched relative to C4.
// Grain volume uses a Hann window; note-level shaping comes from Env In
// (patch an ADSR there), matching how the Oscillator/Sampler behave.
// Inputs: 0 = Env In (Mod). Output: 0 = Audio Out.
class GranulateModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pGrains, pSize, pPosition, pSpray,
                      pPitchDisp, pStereo, pLoadSample };
    static constexpr int maxGrains = 32;

    GranulateModule();

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

    // Thread-safe snapshot of every currently active grain's normalised (0..1)
    // read position within the loaded sample - lets the waveform display draw
    // playhead markers. Written every sample from the audio thread; safe to
    // poll from the message thread. Returns how many positions were written
    // into `out` (capped at maxOut).
    int getActiveGrainPositions (float* out, int maxOut) const;

private:
    struct Grain
    {
        bool active { false };
        double pos { 0.0 };
        double inc { 1.0 };
        int age { 0 };
        int length { 1 };
        float gainL { 1.0f }, gainR { 1.0f };
    };

    Grain grains [aquanode::kMaxVoices][maxGrains];
    double spawnCounter [aquanode::kMaxVoices] {};
    double noteRate [aquanode::kMaxVoices] {};
    float  gateLvl  [aquanode::kMaxVoices] {};
    bool   gateOn   [aquanode::kMaxVoices] {};
    float  vel      [aquanode::kMaxVoices] {};

    // mirrors `grains` for the UI: normalised (0..1) read position per grain
    // slot, or -1 when that slot is inactive. Audio thread writes, UI thread
    // reads - relaxed ordering is fine since this is a purely visual hint.
    std::atomic<float> grainDisplayPos [aquanode::kMaxVoices][maxGrains];

    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    double latchedSourceRate { 44100.0 };
    juce::Random random;

    static constexpr double rootHz = 261.6255653005986;   // C4
    static constexpr double gateRampSeconds = 0.003;
};