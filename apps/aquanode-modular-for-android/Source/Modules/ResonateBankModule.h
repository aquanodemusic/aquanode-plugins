#pragma once

#include "ModuleCore.h"
#include "ResonateModelCore.h"

// Resonate - five tuned comb resonators with a measured "Modelled" engine
// (allpass fractional delay, colour filter fitted from impulse responses, so
// upper partials stretch sharp the way a classic resonator's do).
//
// Resonator I sets the main pitch (Note) and hears the mono sum; II-V are
// tuned around it in semitones and take the left and right input in turn,
// panned per resonator and spread by Width.
// Defaults: G3, +2, +9, +9, +2. Decay and Color are global, or per resonator
// in Per Res mode. Const keeps the decay time the same at every pitch; Odd
// (Mode B) inverts the feedback for the hollow odd-harmonic tone.
//
// Midi In: notes patched here (Keyboard Midi, Piano Roll...) retune the
// resonators round-robin; a resonator keeps
// the last note it was given. The cyan readout shows what each one plays;
// clicking a resonator there switches it on or off.
//
// Global effect. Inputs: 0 = Audio In, 1 = Midi In. Output: 0 = Audio Out.
class ResonateBankModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumRes = 5;
    static constexpr int kPerResParams = 5;   // on, pitch, fine, gain, pan

    enum ParamIndex
    {
        pNote = 0, pDecay, pColor, pGain, pDryWet,
        pMode, pConst, pWidth,
        pPerRes, pFilter, pFilterFreq,
        pResBase,                                          // res i, field k: pResBase + i * 5 + k
        pResDecayBase = pResBase + kNumRes * kPerResParams,
        pResColorBase = pResDecayBase + kNumRes
    };
    enum ResField { fOn = 0, fPitch, fFine, fGain, fPan };

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // MIDI notes arriving on Midi In become "hits" (see ModuleCore): each one
    // retunes the next enabled resonator
    bool acceptsGlobalMidiNotes() const override { return true; }
    void voiceNoteOn (int slot, int note, bool retrigger) override;
    void voiceNoteOff (int slot) override;

    double voiceTailSeconds() const override { return 0.0; }

    // the note each resonator is sounding right now (knobs or MIDI), for the UI
    int soundingNote (int index) const { return shownNote[juce::jlimit (0, kNumRes - 1, index)].load (std::memory_order_relaxed); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 26; }

private:
    void updateCoefficients();
    float rp (int res, ResField f) const { return param (pResBase + res * kPerResParams + (int) f); }

    aqresonate::ResModel::Voice voices[kNumRes];
    aqresonate::ResModel::InputStage stage[2];
    aqresonate::ResModel::InputBiquad filt[2];

    float resGain[kNumRes] {}, panL[kNumRes] {}, panR[kNumRes] {};
    bool resOn[kNumRes] {};
    bool filterOn { false };
    float width { 1.0f }, masterGain { 1.0f }, mix { 1.0f };
    int ctrlLeft { 0 };

    // MIDI voice allocation (audio thread)
    int midiNote[kNumRes] { -1, -1, -1, -1, -1 };
    bool midiActive[kNumRes] {};
    juce::uint32 midiOrder[kNumRes] {};
    juce::uint32 midiCounter { 0 };
    int rrPointer { 0 };
    int slotRes[aquanode::kMaxVoices];

    std::atomic<int> shownNote[kNumRes];
};
