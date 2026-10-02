#pragma once

#include "ModuleCore.h"

// Step Seq - G2-style 16-step pitch/gate sequencer. Runs from the host tempo
// at the chosen division, or advances on rising edges at Clock In when that
// socket is patched. Pitch Out is scaled like KeyTrack (semitones / 60), so
// it drives filter cutoffs and the Pluck / Modal Drum pitch inputs 1:1.
// Steps are edited in the module's own step display (drag = pitch,
// bottom row = gate on/off, double-click = reset pitch to 0).
// Inputs: 0 = Clock In, 1 = Reset In.
// Outputs: 0 = Pitch Out (bipolar), 1 = Gate Out (unipolar).
class StepSeqModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxSteps = 16;
    enum ParamIndex { pSteps = 0, pDivision, pDirection, pGateLen };
    // hidden step params follow: s0..s15 then g0..g15

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        stepIndex = 0;
        pendulumUp = true;
        gateLevel = 0.0f;
        lastClock = lastReset = 0.0f;
        clockHigh = false;
        displayStep.store (0, std::memory_order_relaxed);
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 96; }

    int getDisplayStep() const { return displayStep.load (std::memory_order_relaxed); }

private:
    void advance (int numSteps, int direction);

    double phase { 0.0 };
    int stepIndex { 0 };
    bool pendulumUp { true };
    float gateLevel { 0.0f };
    float lastClock { 0.0f }, lastReset { 0.0f };
    bool clockHigh { false };
    juce::Random random;
    std::atomic<int> displayStep { 0 };
};
