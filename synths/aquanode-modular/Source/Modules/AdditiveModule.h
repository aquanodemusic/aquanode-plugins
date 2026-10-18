#pragma once

#include "ModuleCore.h"

// Additive - build a tone by drawing its harmonics directly, one bar per
// partial. Where a subtractive voice starts rich and carves away, this starts
// silent and adds: whatever you draw IS the spectrum.
//
// The Partials chooser (16/32/64) swaps between three completely separate sets
// of bars. Switching never wipes anything - each set keeps its own values and
// is still there when you come back - and only the visible set is sounding.
//
// On top of the drawn shape:
//   Tilt     - a spectral slope in dB per octave (-6 is roughly a sawtooth)
//   Odd/Even - 0% leaves only odd partials (hollow, clarinet-like), 100% only
//              even, 50% both
//   Stretch  - pushes the partials off the harmonic series, so the tone stops
//              being a "note" and turns bell- or gong-like
//
// Cost note: 64 partials on many voices is real work. The Voices knob is the
// throttle, which is why this one defaults to 8 rather than the full pool.
// Inputs: 0 = Env In, 1 = Add Midi In. Output: 0 = Audio Out.
class AdditiveModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxPartials = 64;
    static constexpr int kFirstPartialParam = 7;   // index of "a16_0"

    enum ParamIndex { pVolume = 0, pPartials, pTilt, pOddEven, pStretch, pVoices, pGlide };

    static int partialCount (int mode) { return mode == 0 ? 16 : mode == 1 ? 32 : 64; }
    static int partialParamBase (int mode)
    {
        return mode == 0 ? kFirstPartialParam
             : mode == 1 ? kFirstPartialParam + 16
                         : kFirstPartialParam + 16 + 32;
    }
    static juce::String partialParamId (int mode, int index)
    {
        return "a" + juce::String (partialCount (mode)) + "_" + juce::String (index);
    }
    // a decaying series: sounds like a sawtooth straight away
    static float defaultPartial (int index) { return 1.0f / (float) (index + 1); }

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        glide.resetVoice (v);
        gate.resetVoice (v);
        for (int p = 0; p < kMaxPartials; ++p)
            phase[v][p] = 0.0;
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        if (! retrigger)
            for (int p = 0; p < kMaxPartials; ++p)
                phase[v][p] = 0.0;
    }

    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        gate.noteOff (v);
    }

    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.02; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 72; }

    int currentMode() const { return juce::jlimit (0, 2, (int) getParameter ("partials")); }

    // used by the display's double-click
    void resetPartials()
    {
        const int mode = currentMode();
        for (int i = 0; i < partialCount (mode); ++i)
            setParameter (partialParamId (mode, i), defaultPartial (i));
    }

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase [aquanode::kMaxVoices][kMaxPartials] {};
};
