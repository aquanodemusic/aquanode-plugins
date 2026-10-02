#pragma once

#include "ModuleCore.h"

// Granulation - live granular effect (adapted from the standalone Granulate FX
// VST's live-input mode). Unlike the Granulator oscillator, this has no
// loaded sample: it continuously records the incoming audio into a rolling
// buffer and spins up a cloud of Hann-windowed grains read from whatever has
// recently passed through it, then blends that granulated cloud back in with
// the dry signal. Input: 0 = Audio In. Output: 0 = Audio Out.
class GranulationModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pGrains = 0, pSize, pPosition, pSpray, pWindow,
                      pPitch, pPitchDisp, pStereo, pDryWet };
    static constexpr int maxGrains = 24;
    static constexpr double maxWindowSeconds = 5.0;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    struct Grain
    {
        bool active { false };
        double pos { 0.0 };
        double inc { 1.0 };
        int age { 0 };
        int length { 1 };
        float gainL { 1.0f }, gainR { 1.0f };
    };

    std::vector<float> bufL, bufR;
    juce::int64 writePos { 0 };
    double spawnCounter { 0.0 };
    Grain grains [maxGrains];
    juce::Random random;
};
