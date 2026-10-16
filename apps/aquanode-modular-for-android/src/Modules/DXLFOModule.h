#pragma once

#include "ModuleCore.h"
#include "DXNativeFM.h"

// DX LFO - the DX7's LFO as a module (DX family). One LFO shared by
// every note, as on the DX7: Speed and Delay follow the hardware's measured
// curves, Delay counts from the first key of a phrase (a key pressed while
// no other is held), and LFO Key Sync restarts the wave on every key.
//
// Pitch Out = wave x PMD x the PMS range, in the KeyTrack convention
// (semitones / 60): patch it into every DX Operator's Pitch In. AM Out is the
// unipolar wave x AMD (0..1) for the operators' AM In, where each operator's
// AMS decides how deep it goes. Wheel In adds to PMD - a Midi Controls'
// Wheel Out there is the DX7's mod wheel vibrato. LFO Out is the bare wave,
// faded in, -1..1.
//
// Per voice (so every note can read it), but the wave itself is shared.
// Input: 0 = Wheel In. Outputs: 0 = Pitch Out, 1 = AM Out, 2 = LFO Out.
class DXLFOModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pWave = 0, pSpeed, pDelay, pPmd, pAmd, pPms, pKeySync };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override;

    void voiceReset (int v) override { held[v] = false; }
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override { held[v] = false; }
    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 64; }

private:
    void advanceShared();
    float waveValue() const;

    bool held[aquanode::kMaxVoices] {};
    int lastVoice { aquanode::kMaxVoices };

    // the shared LFO, advanced once per sample
    float phase { 0.0f }, age { 0.0f }, fadeGain { 0.0f }, sampleHold { 0.0f };
    juce::uint32 rng { 12345u };
};
