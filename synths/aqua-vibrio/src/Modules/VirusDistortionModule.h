#pragma once
//==============================================================================
//  VirusDistortionModule.h
//
//  The Virus names 26 distortion curves. `fx.waveshaper` has four shapes, so
//  until now whole families were being collapsed together - every folder
//  sounded like the same folder, and the five Overdrive flavours were all one
//  hard clipper. Each curve here is only a few lines, but they are different
//  few lines, which is the entire point of having 26 of them.
//
//  The list follows the hardware's order so a patch byte picks the curve it
//  names. Where a curve is a filter as much as a shaper - the Low Pass and
//  High Pass entries - the filter is in here too, since that is what the
//  hardware means by those.
//
//  Sockets: 0 = Audio In, out 0 = Audio Out.
//==============================================================================

#include <JuceHeader.h>
#include "../Aquanode/ModuleCore.h"

namespace aquavibrio
{

class VirusDistortionModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCurve = 0, pDrive, pTone, pHighCut, pDryWet, pLevel };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        aquanode::SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (auto& lane : state)
            lane = {};
    }

    void voiceReset (int voice) override
    {
        if (voice >= 0 && voice < aquanode::kMaxVoices)
            state[(size_t) voice] = {};
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        render (voice, inputs, outputs);
    }

    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override
    {
        render (aquanode::kMaxVoices, inputs, outputs);
    }

private:
    struct LaneState
    {
        float lowpass [2] { 0.0f, 0.0f };
        float highpass[2] { 0.0f, 0.0f };
        float hold    [2] { 0.0f, 0.0f };
        int   holdCount[2] { 0, 0 };
    };

    void render (int lane, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs);

    // One sample through one curve. `drive` has already been applied.
    float shape (int curve, float x, int channel, LaneState& s, float tone);

    std::array<LaneState, aquanode::kMaxVoices + 1> state {};
};

} // namespace aquavibrio
