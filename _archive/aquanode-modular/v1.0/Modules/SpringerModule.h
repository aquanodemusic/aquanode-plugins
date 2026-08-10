#pragma once

#include "ModuleCore.h"

// Springer - spring reverb (adapted from the Springer Spring Reverb VST).
// Up to 7 parallel "coils": each is a feedback loop of delay -> density
// allpass -> N-stage allpass dispersion chain -> lowpass damping -> tanh,
// which produces the characteristic springy "chirp". Chirp shifts the
// dispersion coefficients like the original.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpringerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pSizeMs = 0, pCoils, pStages, pResonance, pDamping, pChirp, pDryWet };
    static constexpr int maxCoils = 7;
    static constexpr int maxStages = 128;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    struct Coil
    {
        std::vector<float> delayBuf;
        int writePos { 0 };
        std::vector<float> densityBuf;
        int densityPos { 0 };
        float apMem[maxStages] {};
        float coeffs[maxStages] {};
        float dampingMem { 0.0f };
        float hpMem { 0.0f };
        float lastOut { 0.0f };
    };

    Coil coils[maxCoils];
};
