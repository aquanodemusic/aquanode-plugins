#include "SpectralCore.h"

using namespace aquanode;

//==============================================================================
// drawable spectral curve: log-frequency axis, live input spectrum behind,
// drag paints the 64 curve points (interpolating between mouse positions),
// double-click resets the whole curve to the module's default.
//==============================================================================
class SpectralCurveEditor : public juce::Component,
                            private juce::Timer
{
public:
    explicit SpectralCurveEditor (SpectralModuleBase& m) : module (&m)
    {
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<SpectralModuleBase*> (module.get());
        if (m == nullptr)
            return;

        const float w = (float) getWidth();
        const float h = (float) getHeight();

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        // live spectrum (filled, log magnitude)
        juce::Path spec;
        spec.startNewSubPath (0.0f, h);
        for (int p = 0; p < SpectralModuleBase::kDisplayPoints; ++p)
        {
            const float mag = m->getDisplayMagnitude (p);
            const float db = 20.0f * std::log10 (juce::jmax (mag * (1.0f / 64.0f), 1.0e-5f));
            const float y = juce::jmap (juce::jlimit (-80.0f, 0.0f, db), -80.0f, 0.0f, h, 0.0f);
            spec.lineTo (w * (float) p / (SpectralModuleBase::kDisplayPoints - 1), y);
        }
        spec.lineTo (w, h);
        spec.closeSubPath();
        g.setColour (juce::Colour (0xff3b8ea5).withAlpha (0.45f));
        g.fillPath (spec);

        // the drawn curve
        juce::Path curve;
        for (int i = 0; i < SpectralModuleBase::kCurvePoints; ++i)
        {
            const float v = m->getParameter ("c" + juce::String (i));
            const float x = w * (float) i / (SpectralModuleBase::kCurvePoints - 1);
            const float y = (1.0f - juce::jlimit (0.0f, 1.0f, v)) * (h - 4.0f) + 2.0f;
            if (i == 0) curve.startNewSubPath (x, y);
            else        curve.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffd970b0));
        g.strokePath (curve, juce::PathStrokeType (1.8f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        lastPoint = -1;
        paintAt (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e); }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (auto* m = dynamic_cast<SpectralModuleBase*> (module.get()))
        {
            for (int i = 0; i < SpectralModuleBase::kCurvePoints; ++i)
                m->setParameter ("c" + juce::String (i), m->getCurveDefault());
            repaint();
        }
    }

private:
    void paintAt (const juce::MouseEvent& e)
    {
        auto* m = dynamic_cast<SpectralModuleBase*> (module.get());
        if (m == nullptr)
            return;

        const int pt = juce::jlimit (0, SpectralModuleBase::kCurvePoints - 1,
                                     (int) std::round ((float) e.x / (float) juce::jmax (1, getWidth())
                                                       * (SpectralModuleBase::kCurvePoints - 1)));
        const float v = juce::jlimit (0.0f, 1.0f,
                                      1.0f - ((float) e.y - 2.0f) / ((float) getHeight() - 4.0f));

        if (lastPoint < 0 || lastPoint == pt)
        {
            m->setParameter ("c" + juce::String (pt), v);
        }
        else
        {
            // interpolate across skipped points so fast drags leave no gaps
            const int lo = juce::jmin (lastPoint, pt), hi = juce::jmax (lastPoint, pt);
            const float vLo = lastPoint < pt ? lastValue : v;
            const float vHi = lastPoint < pt ? v : lastValue;
            for (int i = lo; i <= hi; ++i)
            {
                const float t = (float) (i - lo) / (float) juce::jmax (1, hi - lo);
                m->setParameter ("c" + juce::String (i), vLo + t * (vHi - vLo));
            }
        }

        lastPoint = pt;
        lastValue = v;
        repaint();
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
    int lastPoint { -1 };
    float lastValue { 0.0f };
};

std::unique_ptr<juce::Component> SpectralModuleBase::createExtraContentComponent()
{
    return std::make_unique<SpectralCurveEditor> (*this);
}
