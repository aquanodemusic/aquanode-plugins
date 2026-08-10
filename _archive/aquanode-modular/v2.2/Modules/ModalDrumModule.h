#pragma once

#include "ModuleCore.h"

// Modal Drum - physical-modelling percussion: a short noise/impulse exciter
// feeding 6 parallel two-pole resonators. Inharm morphs the partial ratios
// from harmonic (marimba/tom) to stretched (bells, metal); Bright tilts the
// mode amplitudes; high modes decay faster like a real body. Pitch In offsets
// Tune in the KeyTrack scaling (semitones / 60) - Step Seq melodies on toms!
// Fires on Trig In edges and on MIDI note-on.
// Inputs: 0 = Trig In, 1 = Pitch In. Output: 0 = Audio Out.
class ModalDrumModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumModes = 6;
    enum ParamIndex { pTune = 0, pDecay, pInharm, pBright, pHit };

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
        pool.resetVoice (v);
        for (int m = 0; m < kNumModes; ++m)
            y1[v][m] = y2[v][m] = 0.0f;
        exciteSamples[v] = 0;
        lastTrig[v] = 0.0f;
        noteOffset[v] = 0.0f;
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        pool.noteOn (v, voiceLimit());
        noteOffset[v] = (float) (note - 60);   // keyboard transposes around Tune
        trigger (v);
    }

    double voiceTailSeconds() const override { return param (pDecay) * 0.001 + 0.2; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    aquanode::ModuleVoicePool pool;

    void trigger (int v)
    {
        exciteSamples[v] = juce::jmax (8, (int) (param (pHit) * 0.01f * 0.008f * sampleRate));
    }

    float y1 [aquanode::kMaxVoices][kNumModes] {};
    float y2 [aquanode::kMaxVoices][kNumModes] {};
    int exciteSamples [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    float noteOffset [aquanode::kMaxVoices] {};
    juce::Random random;
};
