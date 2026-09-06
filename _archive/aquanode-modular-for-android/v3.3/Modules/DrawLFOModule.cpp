#include "DrawLFOModule.h"

using namespace aquanode;

//==============================================================================
void DrawLFOModule::resetTableToSine()
{
    for (int i = 0; i < kNumPoints; ++i)
        setPointAt (i, (float) std::sin (juce::MathConstants<double>::twoPi * i / kNumPoints));
}

void DrawLFOModule::smoothTable()
{
    // 5-point moving average, wrapping around the cycle so the seam between
    // the end and the start gets rounded too
    std::array<float, kNumPoints> copy {};
    for (int i = 0; i < kNumPoints; ++i)
        copy[(size_t) i] = pointAt (i);

    for (int i = 0; i < kNumPoints; ++i)
    {
        float sum = 0.0f;
        for (int k = -2; k <= 2; ++k)
            sum += copy[(size_t) ((i + k + kNumPoints) % kNumPoints)];
        setPointAt (i, sum * 0.2f);
    }
}

void DrawLFOModule::drawSegment (int fromIndex, float fromValue, int toIndex, float toValue)
{
    fromIndex = juce::jlimit (0, kNumPoints - 1, fromIndex);
    toIndex = juce::jlimit (0, kNumPoints - 1, toIndex);

    if (fromIndex == toIndex)
    {
        setPointAt (toIndex, toValue);
        return;
    }

    const int step = toIndex > fromIndex ? 1 : -1;
    const int span = std::abs (toIndex - fromIndex);

    for (int i = 0; i <= span; ++i)
    {
        const float t = (float) i / (float) span;
        setPointAt (fromIndex + i * step, fromValue + (toValue - fromValue) * t);
    }
}

void DrawLFOModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    phase[v] += param (pRate) / sampleRate;
    phase[v] -= std::floor (phase[v]);

    const double pos = phase[v] * kNumPoints;
    const int i0 = juce::jlimit (0, kNumPoints - 1, (int) pos);

    float value;
    if (param (pInterpolate) > 0.5f)
    {
        // linear read: smooth sweeps, and the only sane choice for a curve
        // that drives pitch or a filter
        const int i1 = (i0 + 1) % kNumPoints;
        const float frac = (float) (pos - (double) i0);
        value = pointAt (i0) + (pointAt (i1) - pointAt (i0)) * frac;
    }
    else
    {
        // stepped read: the drawn points become hard stages, which is what
        // makes this a step sequencer for modulation
        value = pointAt (i0);
    }

    outputs[0][0] = juce::jlimit (-1.0f, 1.0f, value * param (pLevel) + param (pOffset));
    outputs[0][1] = outputs[0][0];
}

//==============================================================================
// patch persistence: 1024 points as base64 16-bit ints (~2.7 kB of text)
//==============================================================================
juce::String DrawLFOModule::saveCustomState() const
{
    juce::MemoryOutputStream out;
    for (int i = 0; i < kNumPoints; ++i)
        out.writeShort ((short) juce::jlimit (-32767, 32767,
                                              juce::roundToInt (pointAt (i) * 32767.0f)));

    return juce::Base64::toBase64 (out.getData(), out.getDataSize());
}

void DrawLFOModule::loadCustomState (const juce::String& state)
{
    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, state.trim()))
        return;

    const auto expected = (size_t) kNumPoints * sizeof (short);
    if (decoded.getDataSize() < expected)
        return;   // truncated blob: keep whatever curve we have rather than half of one

    juce::MemoryInputStream in (decoded.getData(), decoded.getDataSize(), false);
    for (int i = 0; i < kNumPoints; ++i)
        setPointAt (i, (float) in.readShort() / 32767.0f);
}

//==============================================================================
// the drawing surface
//==============================================================================
class DrawLFODisplay : public juce::Component,
                       private juce::Timer
{
public:
    explicit DrawLFODisplay (DrawLFOModule& m) : module (&m)
    {
        startTimerHz (20);   // reflect Smooth / Reset / patch loads
    }

    void paint (juce::Graphics& g) override
    {
        auto* dm = dynamic_cast<DrawLFOModule*> (module.get());
        if (dm == nullptr)
            return;

        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (bounds, 3.0f);

        // zero line
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

        // the curve: one vertical step per pixel column, reading the table at
        // that column's point (1024 points across ~200 px, so this decimates
        // deliberately - drawing every point would just be aliasing noise)
        juce::Path path;
        const int w = juce::jmax (1, getWidth());
        for (int x = 0; x < w; ++x)
        {
            const int idx = juce::jlimit (0, DrawLFOModule::kNumPoints - 1,
                                          (int) ((float) x / (float) w * DrawLFOModule::kNumPoints));
            const float y = valueToY (dm->pointAt (idx));
            if (x == 0)
                path.startNewSubPath ((float) x, y);
            else
                path.lineTo ((float) x, y);
        }

        g.setColour (juce::Colour (0xff6fd9c4));
        g.strokePath (path, juce::PathStrokeType (1.6f));

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        lastIndex = xToIndex (e.position.x);
        lastValue = yToValue (e.position.y);
        if (auto* dm = dynamic_cast<DrawLFOModule*> (module.get()))
            dm->setPointAt (lastIndex, lastValue);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        auto* dm = dynamic_cast<DrawLFOModule*> (module.get());
        if (dm == nullptr)
            return;

        const int index = xToIndex (e.position.x);
        const float value = yToValue (e.position.y);

        // fill in everything the mouse skipped over, so a fast stroke draws a
        // line rather than a row of dots
        dm->drawSegment (lastIndex, lastValue, index, value);

        lastIndex = index;
        lastValue = value;
        repaint();
    }

private:
    int xToIndex (float x) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth()));
        return juce::jlimit (0, DrawLFOModule::kNumPoints - 1,
                             (int) (t * DrawLFOModule::kNumPoints));
    }

    float yToValue (float y) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, y / (float) juce::jmax (1, getHeight()));
        return juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * t);
    }

    float valueToY (float value) const
    {
        return (1.0f - value) * 0.5f * (float) getHeight();
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
    int lastIndex { 0 };
    float lastValue { 0.0f };
};

std::unique_ptr<juce::Component> DrawLFOModule::createExtraContentComponent()
{
    return std::make_unique<DrawLFODisplay> (*this);
}

//==============================================================================
static ModuleDescriptor drawLFODescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.drawlfo";
    d.displayName = "Draw LFO";
    d.description =
        "An LFO whose shape you draw by hand over 1024 points - drag in the display and fast "
        "strokes fill in the gaps. Smooth Read off turns the drawn points into hard stages, which "
        "makes this a step sequencer for modulation. Rate takes mod cables (another LFO there "
        "gives accelerating stutters); Smooth Read does not, and Smooth and Reset are buttons "
        "that rewrite the curve itself.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 25;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeRotary ("rate",   "Rate",   0.02f, 100.0f, 1.0f, 0, "Hz", true),
        makeRotary ("level",  "Level",  0.0f, 1.0f, 1.0f, 0),
        makeRotary ("offset", "Offset", -1.0f, 1.0f, 0.0f, 0),
        makeRotary ("interpolate", "Smooth Read", 0.0f, 1.0f, 1.0f, 1, {}, false, 1.0f).noMod(),
        makeButton ("smooth", "Smooth", 1),
        makeButton ("reset",  "Reset",  1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DrawLFOModule, drawLFODescriptor)
