#pragma once

#include "ModuleCore.h"

// Analog Drift - the FM Oscillator with the digital precision sanded off.
// Same engine as Oscillator (true DX7-style phase modulation, FM In / Env In
// per voice, waveform set incl. sample wavetable, voices + mono glide), plus
// the things that make a VCO sound like hardware:
//
//   * Drift / Drift Rate - every oscillator wanders off pitch on its own
//     smoothed random walk (in cents), like temperature-drifting analog
//     cores. Each unison partial and each voice drifts INDEPENDENTLY.
//   * Phase Drift - a slow random wobble on the read phase, blurring the
//     rock-solid digital phase relationship (subtle chorus-like motion even
//     on a single oscillator).
//   * Unison / Detune / Spread - up to 7 stacked copies per voice, detuned
//     symmetrically in cents and panned across the stereo field.
//   * Free-running start phases - notes start at random phase per partial,
//     the way analog oscillators never wait for a key press.
//
// Drift and Phase Drift are ordinary rotaries, so they take knob-modulation
// cables (LFO, DAW Mod) like anything else.
// Inputs: 0 = FM In (Mod), 1 = Env In (Mod). Output: 0 = Audio Out.
class AnalogDriftModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pFmRatio, pWaveform, pCycleLength, pLoadSample,
                      pDrift, pDriftRate, pPhaseDrift, pUnison, pDetune, pSpread };

    static constexpr int kMaxUnison = 7;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

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
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    float readWave (int waveform, double phase01) const;   // phase 0..1
    float readSampleTable (double phase01) const;
    void advanceDrift (float& value, float& target, double& countdown, double rateHz);

    // per voice, per unison partial
    double phase        [aquanode::kMaxVoices][kMaxUnison] {};
    float  drift        [aquanode::kMaxVoices][kMaxUnison] {};   // smoothed -1..1 pitch walk
    float  driftTarget  [aquanode::kMaxVoices][kMaxUnison] {};
    double driftCount   [aquanode::kMaxVoices][kMaxUnison] {};   // samples to next target
    float  phWobble     [aquanode::kMaxVoices][kMaxUnison] {};   // smoothed -1..1 phase walk
    float  phTarget     [aquanode::kMaxVoices][kMaxUnison] {};
    double phCount      [aquanode::kMaxVoices][kMaxUnison] {};

    double freqHz  [aquanode::kMaxVoices] {};
    float  gateLvl [aquanode::kMaxVoices] {};
    bool   gateOn  [aquanode::kMaxVoices] {};
    float  vel     [aquanode::kMaxVoices] {};

    float driftSmoothCoeff { 0.001f };   // latched from Drift Rate once per block
    juce::Random rng;                    // audio thread only

    // latched once per block (audio thread)
    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    const float* tableData { nullptr };
    int tableSourceLen { 0 };

    static constexpr double gateRampSeconds = 0.003;
};
