#pragma once

#include "ModuleCore.h"

// Euclid - Euclidean rhythm generator: distributes 'Fills' pulses as evenly
// as possible across 'Steps' steps (Bjorklund pattern), with rotation.
// Runs from the host tempo, or advances on Clock In edges when patched.
// The little dot display shows the pattern and the playhead.
// Inputs: 0 = Clock In, 1 = Reset In. Output: 0 = Gate Out.
class EuclidModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxSteps = 16;
    enum ParamIndex { pSteps = 0, pFills, pRotate, pDivision, pGateLen };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        stepIndex = 0;
        gateLevel = 0.0f;
        lastClock = lastReset = 0.0f;
        displayStep.store (0, std::memory_order_relaxed);
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 20; }

    int getDisplayStep() const { return displayStep.load (std::memory_order_relaxed); }

    // Bjorklund via the classic Bresenham formulation
    static bool pulseAt (int step, int steps, int fills, int rotate)
    {
        if (fills <= 0) return false;
        if (fills >= steps) return true;
        const int i = ((step - rotate) % steps + steps) % steps;
        return ((i * fills) % steps) < fills;
    }

private:
    double phase { 0.0 };
    int stepIndex { 0 };
    float gateLevel { 0.0f };
    float lastClock { 0.0f }, lastReset { 0.0f };
    std::atomic<int> displayStep { 0 };
};
