#pragma once

#include "ModuleCore.h"

// Droplets - the physics-based water droplet generator from Laurent's
// standalone Droplets plugin, stripped to a module: a stream of randomised
// sine pings whose frequency, amplitude and decay all follow bubble
// acoustics (freq = 3 / radius, amplitude ~ radius^1.5), with an upward
// pitch chirp per drip and per-droplet stereo placement.
//
// The stream runs while a note is held OR while Gate In sits high, so it
// works both from the keyboard (polyphonic, one stream per voice) and from
// a Clock / Euclid / Step Seq gate. Droplets ring out naturally when the
// stream stops. Env In follows the Oscillator convention: patched, the
// envelope IS the level; unpatched, the drips just play at full level and
// stop when the stream does. Velocity always applies.
//
// Simplified from the plugin: the mod-ADSR is gone (knob-mod cables onto
// Rate do the same job), the volume-ADSR is Env In, secondary double-drips
// and the per-droplet fade knobs are fixed at musical defaults.
//
// Flexible lane. Inputs: 0 = Gate In, 1 = Env In. Output: 0 = Audio Out.
class DropletsModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxDroplets = 32;

    enum ParamIndex { pSize = 0, pSizeVar, pRate, pRateVar,
                      pRise, pDepth, pBright, pWidth,
                      pVolume, pDensity };

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
        for (auto& d : droplets[v])
            d.active = false;
        clockPhase[v] = 0.0f;
        noteHeld[v] = false;
        vel[v] = 1.0f;
        for (int c = 0; c < 2; ++c)
            dcX1[v][c] = dcY1[v][c] = 0.0f;
    }

    void voiceNoteOn (int v, int, bool) override
    {
        pool.noteOn (v, voiceLimit());
        noteHeld[v] = true;
        clockPhase[v] = 1.0f;   // first drip lands immediately, like the plugin's noteOn
    }

    void voiceVelocity (int v, float velocity01) override { vel[v] = velocity01; }

    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        noteHeld[v] = false;    // stream stops; active droplets ring out
    }

    double voiceTailSeconds() const override { return 0.4; }   // longest droplet + fade

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    struct Droplet
    {
        float amplitude { 0.0f };
        float decay { 0.0f };
        float phase { 0.0f };
        float currentFreq { 0.0f };
        float pitchRiseRate { 0.0f };
        float pan { 0.5f };
        float fadeInSamples { 1.0f };
        float fadeOutSamples { 1.0f };
        float lifetimeSamples { 0.0f };
        int   age { 0 };
        bool  active { false };
    };

    void triggerDroplet (int v);

    // Box-Muller gaussian for the bandwidth (variation) knobs
    float gaussianRandom()
    {
        const float u1 = juce::jmax (1.0e-7f, random.nextFloat());
        const float u2 = random.nextFloat();
        return std::sqrt (-2.0f * std::log (u1))
             * std::cos (juce::MathConstants<float>::twoPi * u2);
    }

    aquanode::ModuleVoicePool pool;
    Droplet droplets [aquanode::kMaxVoices][kMaxDroplets];
    float clockPhase [aquanode::kMaxVoices] {};
    bool  noteHeld   [aquanode::kMaxVoices] {};
    float vel        [aquanode::kMaxVoices] {};
    float dcX1 [aquanode::kMaxVoices][2] {};   // DC blocker state
    float dcY1 [aquanode::kMaxVoices][2] {};
    juce::Random random;
};
