#pragma once

#include "ModuleCore.h"

// Wavetable - load any audio file and it is read as a stack of single-cycle
// frames laid end to end. Position scans through those frames and crossfades
// between neighbours, so sweeping it morphs the timbre continuously without
// touching the pitch. That morph is the thing the Oscillator's Sample mode
// cannot do: it only ever reads the first cycle.
//
// Frame Size must match how the file was made (2048 is the near-universal
// convention for wavetable files; Serum-style tables are 2048). Warp bends the
// read phase inside each frame - a cheap formant/pulse-width-ish twist.
// Position In is the socket to reach for first: an ADSR or LFO here is what
// makes a wavetable patch move.
// Inputs: 0 = FM In, 1 = Pos In, 2 = Env In, 3 = Add Midi In.
// Output: 0 = Audio Out.
class WavetableModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pFrameSize, pPosition, pPosMod, pWarp, pVoices, pGlide, pLoadSample };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        glide.resetVoice (v);
        gate.resetVoice (v);
        phase[v] = 0.0;
    }

    void blockStart() override;

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        if (! retrigger)
            phase[v] = 0.0;
    }

    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        gate.noteOff (v);
    }

    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.02; }

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "loadSample")
            openSampleChooser();
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

private:
    float readFrame (int frame, double phase01) const;

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase [aquanode::kMaxVoices] {};

    // latched once per block so the audio thread never races the loader
    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    const float* tableData { nullptr };
    int tableLen { 0 };
};
