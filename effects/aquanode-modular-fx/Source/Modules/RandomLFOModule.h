#pragma once

#include "ModuleCore.h"

// Random LFO - four flavours of wandering modulation, per voice:
//   Smooth  random points joined by a cubic spline (organic drift)
//   Sine    random points joined by half-cosines (rounder, more "breathing")
//   Steps   sample & hold, with Smooth gliding between the steps
//   Random  a Lorenz attractor - never repeats, swings in lopsided loops
// Rate in Hz, or locked to the tempo. Note retriggers each voice to a fresh
// random path at note-on; Free lets every voice wander on its own path.
// Amount scales the output (bipolar -1..1, or 0..1 when Unipolar).
// No inputs. Output: 0 = Mod Out.
class RandomLFOModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMode = 0, pRate, pAmount, pSync, pDivision, pSmooth, pRetrig, pPolarity };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    RandomLFOModule();

    void prepare (double sr) override;
    void reset() override;
    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 40; }

    // recent output of the first active voice, for the display (message thread)
    float historyAt (int i) const { return history[(size_t) ((historyPos.load (std::memory_order_relaxed) + i) % kHistory)].load (std::memory_order_relaxed); }
    static constexpr int kHistory = 120;

private:
    struct State
    {
        double phase { 0.0 };
        float pts[4] {};          // the last four random points (spline)
        float held { 0.0f }, smoothed { 0.0f };
        double lx { 0.1 }, ly { 0.0 }, lz { 0.0 };   // Lorenz
        juce::uint32 seed { 1 };
    };

    float nextRandom (State& s);
    void scatter (State& s, juce::uint32 seed);

    State st[aquanode::kMaxVoices];
    juce::uint32 noteCounter { 1 };

    std::array<std::atomic<float>, kHistory> history {};
    std::atomic<int> historyPos { 0 };
    int historyTick { 0 };
};
