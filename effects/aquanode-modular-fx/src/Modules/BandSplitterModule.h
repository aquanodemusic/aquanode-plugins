#pragma once

#include "ModuleCore.h"

// Band Splitter - splits one audio signal into 3 frequency bands using two
// cascaded Linkwitz-Riley 4th-order (24 dB/oct) crossovers. LR4 is the same
// family of crossover professional "clean" multiband tools use:
// each crossover is two cascaded 2-pole Butterworth stages, chosen so that
// lowpass + highpass sum back to a flat 0 dB magnitude at the split point,
// with only a well-behaved, symmetric phase rotation around it (no ripple,
// no FFT block delay). Split 2 is always kept a little above Split 1 so the
// bands can't collapse into each other.
//
// Topology is serial: Split 1 peels Band 1 (Low) off the input; everything
// above Split 1 is fed into a second LR4 crossover at Split 2, which peels
// off Band 2 (Mid) and leaves Band 3 (High). This is the same cascade shape
// as the "Natural Phase" mode in commercial multiband splitters - simple,
// cheap per sample, and free of the pre-ringing FFT-based splitters can show
// on transients.
//
// Input: 0 = Audio In. Outputs: 0 = Band 1 (Low), 1 = Band 2 (Mid), 2 = Band 3 (High).
class BandSplitterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pSplit1 = 0, pSplit2 };

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
        for (int c = 0; c < 2; ++c)
        {
            lp1[v][c].reset();
            hp1[v][c].reset();
            lp2[v][c].reset();
            hp2[v][c].reset();
        }
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    // One 2-pole Butterworth biquad stage; two of these in series (same
    // coefficients) make one LR4 (24 dB/oct) lowpass or highpass leg.
    struct BiquadStage
    {
        float b0 {}, b1 {}, b2 {}, a1 {}, a2 {};
        float x1 {}, x2 {}, y1 {}, y2 {};

        void reset() { x1 = x2 = y1 = y2 = 0.0f; }

        float process (float in)
        {
            const float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in;
            y2 = y1; y1 = out;
            return out;
        }
    };

    // LR4 leg = 2 identical cascaded Butterworth stages (same biquad twice).
    struct LR4Leg
    {
        BiquadStage a, b;
        void reset() { a.reset(); b.reset(); }
        float process (float in) { return b.process (a.process (in)); }
    };

    static void setButterworthLowpass  (BiquadStage& s, double freq, double sr);
    static void setButterworthHighpass (BiquadStage& s, double freq, double sr);

    LR4Leg lp1[aquanode::kMaxVoices][2];   // Split 1: low leg  -> Band 1
    LR4Leg hp1[aquanode::kMaxVoices][2];   // Split 1: high leg -> feeds Split 2
    LR4Leg lp2[aquanode::kMaxVoices][2];   // Split 2: low leg  -> Band 2
    LR4Leg hp2[aquanode::kMaxVoices][2];   // Split 2: high leg -> Band 3
};
