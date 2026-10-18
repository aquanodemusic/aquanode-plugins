#pragma once

#include <JuceHeader.h>
#include "../Modules/ModuleCore.h"

//==============================================================================
// Small building blocks shared by the patch versions' modules: a drawn curve
// (FlowEQ's flow curves, Vocode's band curves, ...) and the display that
// draws it on a module card.
//==============================================================================
namespace aquanode::plugins
{
    //==========================================================================
    // A curve of N points in lo..hi. The message thread draws, the audio
    // thread reads; each point is its own atomic, so a read can never see a
    // torn float, only (for one sample) a point from right before the stroke.
    class PointCurve
    {
    public:
        PointCurve (int numPoints, float lo, float hi, float initial);

        int size() const { return (int) points.size(); }
        float lo() const { return low; }
        float hi() const { return high; }

        float get (int i) const { return points[(size_t) juce::jlimit (0, size() - 1, i)].load (std::memory_order_relaxed); }
        void set (int i, float v);
        void fill (float v);

        // a straight line between two points, so a fast stroke leaves no gaps
        void drawSegment (int fromIndex, float fromValue, int toIndex, float toValue);

        // a 1-2-3-2-1 moving average, periodic (FlowEQ's Smooth) or open
        void smooth (bool periodic);

        // periodic Catmull-Rom between the points, phase 0..1 (FlowEQ's playback)
        float interpolatedPeriodic (float phase) const;

        bool isFlatAt (float v, float tolerance = 1.0e-4f) const;

        juce::String toBase64() const;
        bool fromBase64 (const juce::String& s);

    private:
        std::vector<std::atomic<float>> points;
        float low, high;
    };

    //==========================================================================
    // Draws one or more curves and lets the player paint the active one. With
    // several layers, small tabs at the top switch which one the mouse edits.
    class CurveDisplay : public juce::Component,
                         private juce::Timer
    {
    public:
        struct Layer
        {
            PointCurve* curve { nullptr };
            juce::String name;
            juce::Colour colour { 0xff6fd9c4 };
            float restValue { std::numeric_limits<float>::quiet_NaN() };   // drawn as a dim line
        };

        // module keeps the curves alive (they belong to it); phase, if given,
        // returns the playhead 0..1 to draw, or < 0 for none
        CurveDisplay (SynthModule& owner, std::vector<Layer> layers,
                      std::function<float()> phase = {});

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

        // anything that should follow a stroke (Vocode rebuilds its bands)
        std::function<void()> onEdited;

    private:
        void timerCallback() override { repaint(); }

        juce::Rectangle<float> plotArea() const;
        int xToIndex (float x, const PointCurve& c) const;
        float yToValue (float y, const PointCurve& c) const;
        float valueToY (float v, const PointCurve& c) const;
        juce::Rectangle<int> tabArea (int i) const;

        juce::WeakReference<SynthModule> module;
        std::vector<Layer> layers;
        std::function<float()> phaseFn;
        int active { 0 };
        int lastIndex { 0 };
        float lastValue { 0.0f };
    };
}
