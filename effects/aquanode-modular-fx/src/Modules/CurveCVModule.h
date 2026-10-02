#pragma once

#include "ModuleCore.h"

// Curve CV - a drawn control-voltage curve, in the spirit of the SignalControl
// plugin: place points, choose how the curve runs between them and how fast it
// plays, and patch the output into any knob or modulation input.
//
// Points: tap empty space to add one, drag one to move it, double-tap or
// right-click it to delete. The first and last points stay at the start and
// end of the curve (only their height moves). With Interp on "Curved",
// dragging empty space up or down bends the segment underneath.
//
// Speed is either Rate in Hz or, with Sync on Tempo, a length in beats/bars
// at the host tempo (120 bpm standalone). Mode loops, plays once and holds,
// or ping-pongs. A rising gate on Restart In, or any note on Midi In, starts
// the curve again from the beginning.
//
// The output runs Min..Max (0..1 by default). A cable into a knob maps that
// onto the knob's WHOLE range - 0 is the knob's minimum, 1 its maximum,
// wherever the knob itself is set - so Min and Max are the part of the knob's
// range the curve covers. The cable's depth blends between the knob's own
// setting (0%) and the curve (100%, the default); a negative depth flips it.
//
// Global lane: one curve for the whole patch, free-running.
// Inputs: 0 = Restart In, 1 = Midi In. Output: 0 = CV Out.
class CurveCVModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pSync, pLength, pMode, pInterp, pMin, pMax, pReset };
    enum Interp { iCurved = 0, iSmooth, iStep };
    enum Mode { mLoop = 0, mOnce, mPingPong };

    struct Point { float x { 0.0f }, y { 0.0f }, bend { 0.0f }; };   // bend: shape of the segment to the next point
    using PointList = std::vector<Point>;
    static constexpr int kMaxPoints = 64;

    CurveCVModule();

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override;
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // its cables set a knob ABSOLUTELY: 0..1 is the knob's whole range
    bool drivesKnobsAbsolutely() const override { return true; }

    // notes on Midi In restart the curve (they arrive as global-lane hits)
    bool acceptsGlobalMidiNotes() const override { return true; }
    void voiceNoteOn (int, int, bool) override { restartRequested = true; }

    //=== editing (message thread) =============================================
    const PointList& getPoints() const { return points; }
    void setPoints (PointList newPoints);
    void resetToRamp();
    static float evaluate (const PointList& pts, float x, int interp);

    double getPlayhead() const { return uiPlayhead.load (std::memory_order_relaxed); }
    float getLastOutput() const { return uiValue.load (std::memory_order_relaxed); }

    static const juce::StringArray& lengthChoices();
    static double lengthBeats (int choice);

    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "reset")
            resetToRamp();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 150; }
    int preferredModuleWidth() const override { return 340; }

    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

    int getEditCounter() const { return editCounter.load (std::memory_order_relaxed); }

private:
    void publish();

    PointList points;                                          // message thread
    std::vector<std::shared_ptr<const PointList>> retired;     // freed here, not on the audio thread
    std::atomic<int> editCounter { 0 };

    juce::SpinLock lock;
    std::shared_ptr<const PointList> published;

    // audio thread
    std::shared_ptr<const PointList> audioPoints;
    double phase { 0.0 };          // 0..1 through the curve
    int direction { 1 };           // ping-pong
    bool holding { false };        // "Once" reached the end
    float lastRestart { 0.0f };
    bool restartRequested { false };

    std::atomic<double> uiPlayhead { 0.0 };
    std::atomic<float> uiValue { 0.0f };
};
