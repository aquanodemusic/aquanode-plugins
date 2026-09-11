#pragma once

#include "ModuleCore.h"

// Tape Wobble - what a worn tape machine does to whatever passes through it.
// The global-lane sibling of Analog Drift: that module wobbles an oscillator
// from the inside, this one wobbles a finished signal from the outside.
//
//   * Wow is the slow pitch sag of an off-centre reel (~0.5 Hz) - seasickness.
//   * Flutter is the fast jitter of the capstan (~7 Hz) - a shimmer that
//     makes sustained notes sound recorded rather than rendered.
//   * Both ride a smoothed random walk on top of their sine, so the wobble
//     never settles into an obviously periodic pattern.
//   * Drive is tape compression and soft saturation, gain-compensated so
//     turning it up thickens rather than just gets louder.
//   * Dropouts are worn oxide: at random intervals the level dips and the top
//     end goes with it, gently, the way a real dropout sounds.
//   * Hiss is the noise floor, ducked under loud material like real tape.
//
// The wobble is a modulated delay, so it costs a few ms of latency-free delay
// rather than any pitch shifting.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class TapeWobbleModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pWow = 0, pFlutter, pDrive, pDropouts, pHiss, pDryWet };

    void prepare (double sr) override;
    void reset() override;

    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override;

private:
    static constexpr double kMaxDelaySeconds = 0.020;   // the wobble's swing room

    float readDelay (int ch, float delaySamples) const;
    void advanceWalk (float& value, float& target, double& countdown, double rateHz);

    std::vector<float> delayLine;   // [channel][pos], flat
    int lineLength { 2 };
    int writePos { 0 };

    double wowPhase { 0.0 }, flutterPhase { 0.0 };

    // smoothed random walks that keep the wobble from sounding like an LFO
    float wowWalk { 0.0f }, wowTarget { 0.0f };
    double wowCount { 0.0 };
    float flutWalk { 0.0f }, flutTarget { 0.0f };
    double flutCount { 0.0 };
    float walkSmooth { 0.001f };

    // dropouts
    double dropoutTimer { 0.0 };
    float dropoutDepth { 0.0f };     // 0 = none, 1 = full dip
    float dropoutEnv { 0.0f };
    float dropoutLp [2] {};

    float hissEnv { 0.0f };
    juce::Random rng;
};
