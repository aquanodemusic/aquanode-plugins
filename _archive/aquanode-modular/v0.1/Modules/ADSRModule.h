#pragma once

#include "ModuleCore.h"

// ADSR - global-MIDI-triggered envelope generator. Manages its own 24-voice
// pool (one envelope per active voice) and sums them at the output, so its
// Modulation Out can meaningfully drive any polyphonic Oscillator/Sampler.
// No inputs. Output: 0 = Modulation Out (unipolar).
class ADSRModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pAttack = 0, pDecay, pSustain, pRelease };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        voices.reset();
        for (auto& l : level) l = 0.0f;
        for (auto& s : stage) s = Stage::Idle;
    }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;
    void handleMidiEvent (const juce::MidiMessage& m) override;

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    aquanode::VoiceAllocator voices;
    Stage stage [aquanode::VoiceAllocator::maxVoices] {};
    float level [aquanode::VoiceAllocator::maxVoices] {};
    float releaseStep [aquanode::VoiceAllocator::maxVoices] {};
};
