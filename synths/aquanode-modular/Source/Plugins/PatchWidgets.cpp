#include "PatchWidgets.h"

namespace aquanode::plugins
{

//==============================================================================
PointCurve::PointCurve (int numPoints, float lo, float hi, float initial)
    : points ((size_t) juce::jmax (2, numPoints)), low (lo), high (hi)
{
    fill (initial);
}

void PointCurve::set (int i, float v)
{
    if (i >= 0 && i < size())
        points[(size_t) i].store (juce::jlimit (low, high, v), std::memory_order_relaxed);
}

void PointCurve::fill (float v)
{
    for (int i = 0; i < size(); ++i)
        set (i, v);
}

void PointCurve::drawSegment (int fromIndex, float fromValue, int toIndex, float toValue)
{
    if (fromIndex == toIndex)
    {
        set (toIndex, toValue);
        return;
    }
    const int step = toIndex > fromIndex ? 1 : -1;
    const int span = std::abs (toIndex - fromIndex);
    for (int k = 0; k <= span; ++k)
        set (fromIndex + k * step, fromValue + (toValue - fromValue) * (float) k / (float) span);
}

void PointCurve::smooth (bool periodic)
{
    const int n = size();
    std::vector<float> src ((size_t) n), dst ((size_t) n);
    for (int i = 0; i < n; ++i)
        src[(size_t) i] = get (i);

    auto at = [&] (int i)
    {
        if (periodic)
            return src[(size_t) (((i % n) + n) % n)];
        return src[(size_t) juce::jlimit (0, n - 1, i)];
    };

    for (int i = 0; i < n; ++i)
        dst[(size_t) i] = (at (i - 2) + 2.0f * at (i - 1) + 3.0f * at (i) + 2.0f * at (i + 1) + at (i + 2)) / 9.0f;

    for (int i = 0; i < n; ++i)
        set (i, dst[(size_t) i]);
}

float PointCurve::interpolatedPeriodic (float phase) const
{
    const int n = size();
    const float p = juce::jlimit (0.0f, 0.999999f, phase) * (float) n;
    const int i1 = (int) p;
    const float t = p - (float) i1;
    auto wrap = [n] (int idx) { return ((idx % n) + n) % n; };

    const float p0 = get (wrap (i1 - 1)), p1 = get (wrap (i1)), p2 = get (wrap (i1 + 1)), p3 = get (wrap (i1 + 2));
    const float t2 = t * t, t3 = t2 * t;
    const float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float a1 =         p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float a2 = -0.5f * p0 + 0.5f * p2;
    return juce::jlimit (low, high, a0 * t3 + a1 * t2 + a2 * t + p1);
}

bool PointCurve::isFlatAt (float v, float tolerance) const
{
    for (int i = 0; i < size(); ++i)
        if (std::abs (get (i) - v) > tolerance)
            return false;
    return true;
}

juce::String PointCurve::toBase64() const
{
    std::vector<float> raw ((size_t) size());
    for (int i = 0; i < size(); ++i)
        raw[(size_t) i] = get (i);
    return juce::MemoryBlock (raw.data(), raw.size() * sizeof (float)).toBase64Encoding();
}

bool PointCurve::fromBase64 (const juce::String& s)
{
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (s) || mb.getSize() != (size_t) size() * sizeof (float))
        return false;
    const auto* raw = static_cast<const float*> (mb.getData());
    for (int i = 0; i < size(); ++i)
        set (i, raw[i]);
    return true;
}

//==============================================================================
CurveDisplay::CurveDisplay (SynthModule& owner, std::vector<Layer> l, std::function<float()> phase)
    : module (&owner), layers (std::move (l)), phaseFn (std::move (phase))
{
    startTimerHz (phaseFn != nullptr ? 30 : 10);
}

juce::Rectangle<float> CurveDisplay::plotArea() const
{
    auto r = getLocalBounds().toFloat();
    if (layers.size() > 1)
        r.removeFromTop (18.0f);
    return r;
}

juce::Rectangle<int> CurveDisplay::tabArea (int i) const
{
    const int w = juce::jmax (40, getWidth() / juce::jmax (1, (int) layers.size()));
    return { i * w, 0, w - 2, 16 };
}

int CurveDisplay::xToIndex (float x, const PointCurve& c) const
{
    const auto a = plotArea();
    const float t = juce::jlimit (0.0f, 1.0f, (x - a.getX()) / juce::jmax (1.0f, a.getWidth()));
    return juce::jlimit (0, c.size() - 1, (int) (t * (float) c.size()));
}

float CurveDisplay::yToValue (float y, const PointCurve& c) const
{
    const auto a = plotArea();
    const float t = juce::jlimit (0.0f, 1.0f, (y - a.getY()) / juce::jmax (1.0f, a.getHeight()));
    return c.hi() - t * (c.hi() - c.lo());
}

float CurveDisplay::valueToY (float v, const PointCurve& c) const
{
    const auto a = plotArea();
    return a.getY() + (c.hi() - v) / juce::jmax (1.0e-6f, c.hi() - c.lo()) * a.getHeight();
}

void CurveDisplay::paint (juce::Graphics& g)
{
    if (module.get() == nullptr)
        return;

    if (layers.size() > 1)
        for (int i = 0; i < (int) layers.size(); ++i)
        {
            const auto t = tabArea (i).toFloat();
            const bool on = i == active;
            g.setColour (layers[(size_t) i].colour.withAlpha (on ? 0.85f : 0.25f));
            g.fillRoundedRectangle (t, 3.0f);
            g.setColour (on ? juce::Colours::black : juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawText (layers[(size_t) i].name, t, juce::Justification::centred);
        }

    const auto a = plotArea();
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (a, 3.0f);

    // draw the inactive layers first, dim, then the one being edited
    auto drawLayer = [&] (const Layer& layer, bool isActive)
    {
        const auto& c = *layer.curve;
        if (! std::isnan (layer.restValue))
        {
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawHorizontalLine ((int) valueToY (layer.restValue, c), a.getX(), a.getRight());
        }

        juce::Path path;
        const int w = juce::jmax (1, (int) a.getWidth());
        for (int x = 0; x < w; ++x)
        {
            const int idx = juce::jlimit (0, c.size() - 1, (int) ((float) x / (float) w * (float) c.size()));
            const float y = valueToY (c.get (idx), c);
            if (x == 0) path.startNewSubPath (a.getX() + (float) x, y);
            else        path.lineTo (a.getX() + (float) x, y);
        }
        g.setColour (layer.colour.withAlpha (isActive ? 1.0f : 0.35f));
        g.strokePath (path, juce::PathStrokeType (isActive ? 1.6f : 1.1f));
    };

    for (int i = 0; i < (int) layers.size(); ++i)
        if (i != active)
            drawLayer (layers[(size_t) i], false);
    if (! layers.empty())
        drawLayer (layers[(size_t) active], true);

    if (phaseFn != nullptr && ! layers.empty())
    {
        const float ph = phaseFn();
        if (ph >= 0.0f)
        {
            const auto& layer = layers[(size_t) active];
            const float x = a.getX() + juce::jlimit (0.0f, 1.0f, ph) * a.getWidth();
            const float y = valueToY (layer.curve->interpolatedPeriodic (ph), *layer.curve);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (juce::Colours::white);
            g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (a.reduced (0.5f), 3.0f, 1.0f);
}

void CurveDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || layers.empty())
        return;

    if (layers.size() > 1)
        for (int i = 0; i < (int) layers.size(); ++i)
            if (tabArea (i).contains (e.getPosition()))
            {
                active = i;
                repaint();
                return;
            }

    if (! plotArea().contains (e.position))
        return;

    auto& c = *layers[(size_t) active].curve;
    lastIndex = xToIndex (e.position.x, c);
    lastValue = yToValue (e.position.y, c);
    c.set (lastIndex, lastValue);
    if (onEdited) onEdited();
    repaint();
}

void CurveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || layers.empty() || ! plotArea().expanded (40.0f).contains (e.position))
        return;

    auto& c = *layers[(size_t) active].curve;
    const int index = xToIndex (e.position.x, c);
    const float value = yToValue (e.position.y, c);
    c.drawSegment (lastIndex, lastValue, index, value);
    lastIndex = index;
    lastValue = value;
    if (onEdited) onEdited();
    repaint();
}

} // namespace aquanode::plugins
