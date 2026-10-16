#pragma once

#include "ModuleCore.h"

// Draw LFO - a low-frequency oscillator whose shape you draw by hand. The
// curve is 1024 points across one cycle; drag in the display to draw, and
// dragging quickly fills in every point between the last mouse position and
// this one, so a fast stroke leaves no gaps.
//
// Per-voice and phase-retriggered at note-on, exactly like the LFO module, so
// every note gets the same shape from its start. Rate is a normal modulatable
// knob - patch another LFO into it for accelerating stutters.
//
// Smooth rounds the drawn curve off (a 5-point moving average per press),
// which is the difference between a stepped, aliased scribble and something
// that sweeps a filter cleanly. It is a Button rather than a knob because it
// rewrites the table: repeated presses round further, and Reset restores the
// starting sine.
//
// The table lives outside the parameter system (1024 hidden params would mean
// 1024 XML elements per instance) and rides along in the patch through
// saveCustomState / loadCustomState instead.
// No inputs. Output: 0 = Mod Out.
class DrawLFOModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumPoints = 1024;

    enum ParamIndex { pRate = 0, pLevel, pOffset, pInterpolate, pSmooth, pReset };

    DrawLFOModule() { resetTableToSine(); }

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

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

    void voiceReset (int v) override { phase[v] = 0.0; }
    void voiceNoteOn (int v, int, bool) override { phase[v] = 0.0; }

    void processVoiceSample (int voice, const aquanode::StereoFrame*,
                             aquanode::StereoFrame* outputs) override;

    //=== the drawn table (message thread writes, audio thread reads) =========
    // Points are plain floats in -1..1. Tearing between a UI write and an
    // audio read is inaudible here (one sample of a slightly stale curve), so
    // the table stays lock-free rather than double-buffered.
    float pointAt (int index) const
    {
        return table[(size_t) juce::jlimit (0, kNumPoints - 1, index)].load (std::memory_order_relaxed);
    }

    void setPointAt (int index, float value)
    {
        table[(size_t) juce::jlimit (0, kNumPoints - 1, index)]
            .store (juce::jlimit (-1.0f, 1.0f, value), std::memory_order_relaxed);
    }

    // draws a straight line between two points, so a fast drag leaves no gaps
    void drawSegment (int fromIndex, float fromValue, int toIndex, float toValue);

    void resetTableToSine();
    void smoothTable();

    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "smooth")
            smoothTable();
        else if (paramId == "reset")
            resetTableToSine();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 96; }

    //=== patch persistence ==================================================
    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

private:
    std::array<std::atomic<float>, kNumPoints> table;
    double phase [aquanode::kMaxVoices] {};
};
