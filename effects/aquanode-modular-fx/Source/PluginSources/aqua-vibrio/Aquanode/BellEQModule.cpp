#include "BellEQModule.h"

using namespace aquanode;

//==============================================================================
void BellEQModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    // ~5 ms coefficient glide: fast enough to track a swept LFO, slow enough
    // that the per-block target steps never turn into clicks
    smoothCoeff = 1.0f - std::exp ((float) (-1.0 / (0.005 * sr)));
    reset();
}

void BellEQModule::reset()
{
    for (int i = 0; i < kNumBells; ++i)
        bells[i].clearState();

    // snap straight to the current knob positions rather than sweeping in
    blockStart();
    for (int i = 0; i < kNumBells; ++i)
    {
        bells[i].b0 = targets[i].b0;  bells[i].b1 = targets[i].b1;
        bells[i].b2 = targets[i].b2;  bells[i].a1 = targets[i].a1;
        bells[i].a2 = targets[i].a2;
    }
}

BellEQModule::Coeffs BellEQModule::computeCoeffs (float freqHz, float gainDb, float q) const
{
    // RBJ peaking EQ
    const float f = juce::jlimit (kMinFreq, (float) (sampleRate * 0.45), freqHz);
    const float A = std::pow (10.0f, gainDb / 40.0f);
    const float w0 = juce::MathConstants<float>::twoPi * f / (float) sampleRate;
    const float cosw = std::cos (w0);
    const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.05f, q));

    const float a0 = 1.0f + alpha / A;

    Coeffs c;
    c.b0 = (1.0f + alpha * A) / a0;
    c.b1 = (-2.0f * cosw) / a0;
    c.b2 = (1.0f - alpha * A) / a0;
    c.a1 = (-2.0f * cosw) / a0;
    c.a2 = (1.0f - alpha / A) / a0;
    return c;
}

void BellEQModule::blockStart()
{
    // param() folds in knob-modulation cables, so a modulated bell just moves
    for (int i = 0; i < kNumBells; ++i)
        targets[i] = computeCoeffs (param (i * 3 + 0), param (i * 3 + 1), param (i * 3 + 2));
}

void BellEQModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    float l = inputs[0][0];
    float r = inputs[0][1];

    for (int i = 0; i < kNumBells; ++i)
    {
        auto& b = bells[i];
        const auto& t = targets[i];

        b.b0 += smoothCoeff * (t.b0 - b.b0);
        b.b1 += smoothCoeff * (t.b1 - b.b1);
        b.b2 += smoothCoeff * (t.b2 - b.b2);
        b.a1 += smoothCoeff * (t.a1 - b.a1);
        b.a2 += smoothCoeff * (t.a2 - b.a2);

        l = b.process (0, l);
        r = b.process (1, r);
    }

    outputs[0][0] = l;
    outputs[0][1] = r;
}

float BellEQModule::responseDbAt (float frequencyHz) const
{
    // |H(e^jw)| of each bell, multiplied together (cascade) -> dB
    const float w = juce::MathConstants<float>::twoPi * frequencyHz / (float) sampleRate;
    const float cosw = std::cos (w), cos2w = std::cos (2.0f * w);
    const float sinw = std::sin (w), sin2w = std::sin (2.0f * w);

    float totalDb = 0.0f;

    for (int i = 0; i < kNumBells; ++i)
    {
        const auto c = computeCoeffs (getParameterBase (i * 3 + 0),
                                      getParameterBase (i * 3 + 1),
                                      getParameterBase (i * 3 + 2));

        const float numRe = c.b0 + c.b1 * cosw + c.b2 * cos2w;
        const float numIm = -(c.b1 * sinw + c.b2 * sin2w);
        const float denRe = 1.0f + c.a1 * cosw + c.a2 * cos2w;
        const float denIm = -(c.a1 * sinw + c.a2 * sin2w);

        const float num = std::sqrt (numRe * numRe + numIm * numIm);
        const float den = juce::jmax (1.0e-9f, std::sqrt (denRe * denRe + denIm * denIm));

        totalDb += 20.0f * std::log10 (juce::jmax (1.0e-9f, num / den));
    }

    return totalDb;
}

//==============================================================================
// the graphic display: three draggable dots over the summed response
//==============================================================================
class BellEQDisplay : public juce::Component,
                      public aquanode::CustomParamCableTargets,
                      private juce::Timer
{
public:
    explicit BellEQDisplay (BellEQModule& m) : module (&m)
    {
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto* eq = dynamic_cast<BellEQModule*> (module.get());
        if (eq == nullptr)
            return;

        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (bounds, 3.0f);

        // 0 dB line
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

        // decade gridlines at 100 Hz / 1 kHz / 10 kHz
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
            g.drawVerticalLine ((int) freqToX (f), bounds.getY(), bounds.getBottom());

        // the real summed response
        juce::Path path;
        const int w = juce::jmax (1, getWidth());
        for (int x = 0; x < w; ++x)
        {
            const float y = dbToY (eq->responseDbAt (xToFreq ((float) x)));
            if (x == 0) path.startNewSubPath ((float) x, y);
            else        path.lineTo ((float) x, y);
        }

        g.setColour (juce::Colour (0xff6fd9c4));
        g.strokePath (path, juce::PathStrokeType (1.6f));

        // the dots
        for (int i = 0; i < BellEQModule::kNumBells; ++i)
        {
            const auto c = dotCentre (i).toFloat();
            const bool active = i == draggedBell;

            g.setColour (kDotColours[i].withAlpha (active ? 1.0f : 0.85f));
            g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
            g.setColour (juce::Colours::white.withAlpha (active ? 0.9f : 0.45f));
            g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.2f);

            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f),
                        juce::Justification::centred, false);
        }

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        draggedBell = bellAt (e.getPosition());
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* eq = dynamic_cast<BellEQModule*> (module.get());
        if (eq == nullptr || draggedBell < 0 || e.mods.isPopupMenu())
            return;

        eq->setParameter (BellEQModule::freqParamId (draggedBell),
                          juce::jlimit (BellEQModule::kMinFreq, BellEQModule::kMaxFreq,
                                        xToFreq (e.position.x)));
        eq->setParameter (BellEQModule::gainParamId (draggedBell),
                          juce::jlimit (BellEQModule::kMinGain, BellEQModule::kMaxGain,
                                        yToDb (e.position.y)));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggedBell = -1;
        repaint();
    }

    // wheel over a dot sets its Q: the one bell control with no axis left
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        auto* eq = dynamic_cast<BellEQModule*> (module.get());
        if (eq == nullptr)
            return;

        const int bell = bellAt (e.getPosition());
        if (bell < 0)
            return;

        const auto id = BellEQModule::qParamId (bell);
        eq->setParameter (id, juce::jlimit (BellEQModule::kMinQ, BellEQModule::kMaxQ,
                                            eq->getParameter (id) * (1.0f + wheel.deltaY * 0.6f)));
        repaint();
    }

    //=== CustomParamCableTargets: cables land on a dot and drive its freq ====
    // frequency is the modulation target a dot means: dropping an LFO on a
    // bell should sweep it, which is the whole point of a modulatable EQ
    juce::String paramTargetAt (juce::Point<int> localPos) const override
    {
        const int bell = bellAt (localPos);
        return bell < 0 ? juce::String() : BellEQModule::freqParamId (bell);
    }

    juce::Point<int> paramTargetCentre (const juce::String& paramId) const override
    {
        for (int i = 0; i < BellEQModule::kNumBells; ++i)
            if (BellEQModule::freqParamId (i) == paramId)
                return dotCentre (i);
        return { -1, -1 };
    }

private:
    static constexpr float kDisplayRangeDb = 26.0f;

    const juce::Colour kDotColours[BellEQModule::kNumBells] = {
        juce::Colour (0xffd970b0), juce::Colour (0xffd9b070), juce::Colour (0xff70b0d9)
    };

    float freqToX (float f) const
    {
        const float t = std::log (juce::jmax (1.0f, f) / BellEQModule::kMinFreq)
                        / std::log (BellEQModule::kMaxFreq / BellEQModule::kMinFreq);
        return juce::jlimit (0.0f, 1.0f, t) * (float) getWidth();
    }

    float xToFreq (float x) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth()));
        return BellEQModule::kMinFreq
                 * std::pow (BellEQModule::kMaxFreq / BellEQModule::kMinFreq, t);
    }

    float dbToY (float db) const
    {
        const float t = 0.5f - juce::jlimit (-1.0f, 1.0f, db / kDisplayRangeDb) * 0.5f;
        return t * (float) getHeight();
    }

    float yToDb (float y) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, y / (float) juce::jmax (1, getHeight()));
        return (0.5f - t) * 2.0f * kDisplayRangeDb;
    }

    juce::Point<int> dotCentre (int bell) const
    {
        auto* eq = dynamic_cast<BellEQModule*> (module.get());
        if (eq == nullptr)
            return { -1, -1 };

        return { (int) freqToX (eq->getParameter (BellEQModule::freqParamId (bell))),
                 (int) dbToY  (eq->getParameter (BellEQModule::gainParamId (bell))) };
    }

    int bellAt (juce::Point<int> pos) const
    {
        int best = -1;
        int bestDist = 12;   // grab radius
        for (int i = 0; i < BellEQModule::kNumBells; ++i)
        {
            const int d = (int) pos.getDistanceFrom (dotCentre (i));
            if (d < bestDist)
            {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
    int draggedBell { -1 };
};

std::unique_ptr<juce::Component> BellEQModule::createExtraContentComponent()
{
    return std::make_unique<BellEQDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor bellEQDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.belleq";
    d.displayName = "3-Bell EQ";
    d.description =
        "Three peaking bells over a graphic display: drag a dot for frequency and gain, wheel "
        "over it for Q, and the curve behind them is the real summed response. Unlike the Pitch "
        "Lock Filter every knob here is modulatable - an LFO into a bell frequency is a wah, an "
        "ADSR into gain is a dynamic EQ - and a cable dropped straight onto a dot lands on that "
        "bell's frequency.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 4;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };

    const float defaultFreq[BellEQModule::kNumBells] = { 120.0f, 1000.0f, 6000.0f };

    for (int i = 0; i < BellEQModule::kNumBells; ++i)
    {
        d.params.push_back (makeRotary (BellEQModule::freqParamId (i),
                                        "Freq " + juce::String (i + 1),
                                        BellEQModule::kMinFreq, BellEQModule::kMaxFreq,
                                        defaultFreq[i], i, "Hz", true));
        d.params.push_back (makeRotary (BellEQModule::gainParamId (i),
                                        "Gain " + juce::String (i + 1),
                                        BellEQModule::kMinGain, BellEQModule::kMaxGain,
                                        0.0f, i, "dB"));
        d.params.push_back (makeRotary (BellEQModule::qParamId (i),
                                        "Q " + juce::String (i + 1),
                                        BellEQModule::kMinQ, BellEQModule::kMaxQ,
                                        1.0f, i, {}, true));
    }
    return d;
}

AQUANODE_REGISTER_MODULE (BellEQModule, bellEQDescriptor)
