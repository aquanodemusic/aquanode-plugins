#include "CurveCVModule.h"

using namespace aquanode;

//==============================================================================
CurveCVModule::CurveCVModule()
{
    published = std::make_shared<const PointList>();
    resetToRamp();
    audioPoints = published;
}

const juce::StringArray& CurveCVModule::lengthChoices()
{
    static const juce::StringArray c { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
    return c;
}

double CurveCVModule::lengthBeats (int choice)
{
    static const double beats[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
    return beats[juce::jlimit (0, 8, choice)];
}

void CurveCVModule::resetToRamp()
{
    setPoints ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
}

void CurveCVModule::setPoints (PointList p)
{
    for (auto& pt : p)
    {
        pt.x = juce::jlimit (0.0f, 1.0f, pt.x);
        pt.y = juce::jlimit (0.0f, 1.0f, pt.y);
        pt.bend = juce::jlimit (-1.0f, 1.0f, pt.bend);
    }
    std::stable_sort (p.begin(), p.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
    if ((int) p.size() > kMaxPoints)
        p.resize ((size_t) kMaxPoints);

    // the curve always spans 0..1: first and last points are pinned in time
    if (p.empty())
        p = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    if (p.size() == 1)
        p.push_back ({ 1.0f, p[0].y, 0.0f });
    p.front().x = 0.0f;
    p.back().x = 1.0f;

    points = std::move (p);
    publish();
    editCounter.fetch_add (1, std::memory_order_relaxed);
}

void CurveCVModule::publish()
{
    auto snap = std::make_shared<const PointList> (points);
    std::shared_ptr<const PointList> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (published);
        published = std::move (snap);
    }
    retired.push_back (std::move (old));
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                       [] (const std::shared_ptr<const PointList>& q) { return q == nullptr || q.use_count() == 1; }),
                   retired.end());
}

float CurveCVModule::evaluate (const PointList& pts, float x, int interp)
{
    if (pts.empty())
        return 0.0f;
    if (x <= pts.front().x)
        return pts.front().y;
    if (x >= pts.back().x)
        return pts.back().y;

    size_t i = 0;
    while (i + 1 < pts.size() && pts[i + 1].x <= x)
        ++i;
    if (i + 1 >= pts.size())
        return pts.back().y;

    const auto& a = pts[i];
    const auto& b = pts[i + 1];
    const float span = b.x - a.x;
    float t = span > 1.0e-6f ? (x - a.x) / span : 1.0f;

    switch (interp)
    {
        case iStep:   return a.y;
        case iSmooth: t = t * t * (3.0f - 2.0f * t); break;
        default:
            if (std::abs (a.bend) > 1.0e-4f)
                t = std::pow (t, std::exp2 (a.bend * 3.0f));   // bend +-1 = t^8 .. t^(1/8)
            break;
    }
    return a.y + (b.y - a.y) * t;
}

juce::String CurveCVModule::saveCustomState() const
{
    juce::String s ("CV1");
    for (const auto& p : points)
        s << ' ' << juce::String (p.x, 5) << ':' << juce::String (p.y, 5) << ':' << juce::String (p.bend, 4);
    return s;
}

void CurveCVModule::loadCustomState (const juce::String& state)
{
    auto tokens = juce::StringArray::fromTokens (state, " ", {});
    if (tokens.isEmpty() || tokens[0] != "CV1")
        return;

    PointList p;
    for (int i = 1; i < tokens.size(); ++i)
    {
        auto parts = juce::StringArray::fromTokens (tokens[i], ":", {});
        if (parts.size() == 3)
            p.push_back ({ (float) parts[0].getDoubleValue(), (float) parts[1].getDoubleValue(),
                           (float) parts[2].getDoubleValue() });
    }
    setPoints (std::move (p));
}

//==============================================================================
void CurveCVModule::reset()
{
    phase = 0.0;
    direction = 1;
    holding = false;
    lastRestart = 0.0f;
    restartRequested = false;
}

void CurveCVModule::blockStart()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (published != audioPoints)
        audioPoints = published;   // the old one is still held by `retired`
}

void CurveCVModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // restart: a rising gate, or a note on Midi In
    const float gate = inputs[0][0];
    if ((gate > 0.5f && lastRestart <= 0.5f) || restartRequested)
    {
        phase = 0.0;
        direction = 1;
        holding = false;
        restartRequested = false;
    }
    lastRestart = gate;

    // seconds for one pass through the curve
    double seconds;
    if (param (pSync) > 0.5f)
        seconds = lengthBeats ((int) std::lround (param (pLength))) * 60.0 / juce::jmax (1.0, tempoBpm);
    else
        seconds = 1.0 / juce::jmax (0.001, (double) param (pRate));

    const int mode = (int) std::lround (param (pMode));

    // Leaving "Once" after it has finished starts the curve again - it was
    // stuck on its last value forever, since nothing else cleared the hold.
    // Leaving Ping-Pong mid-way back turns the curve forwards again.
    if (mode != mOnce && holding)
    {
        holding = false;
        phase = 0.0;
        direction = 1;
    }
    if (mode != mPingPong)
        direction = 1;

    if (! holding)
    {
        phase += direction / (seconds * sampleRate);

        if (phase >= 1.0)
        {
            if (mode == mLoop)          phase -= std::floor (phase);
            else if (mode == mOnce)   { phase = 1.0; holding = true; }
            else                      { phase = 2.0 - phase; direction = -1; }
        }
        else if (phase < 0.0)          // only ping-pong runs backwards
        {
            phase = -phase;
            direction = 1;
        }
    }

    const float v = audioPoints != nullptr ? evaluate (*audioPoints, (float) phase, (int) std::lround (param (pInterp)))
                                           : 0.0f;
    const float lo = param (pMin), hi = param (pMax);
    const float out = lo + (hi - lo) * v;

    outputs[0] = { out, out };
    uiPlayhead.store (phase, std::memory_order_relaxed);
    uiValue.store (v, std::memory_order_relaxed);
}

//==============================================================================
// the curve editor
//==============================================================================
class CurveCVEditor : public juce::Component,
                      private juce::Timer
{
public:
    explicit CurveCVEditor (CurveCVModule& m) : module (&m) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto* m = cv();
        const auto box = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (box, 4.0f);
        if (m == nullptr)
            return;

        const auto area = plotArea();

        // grid: quarters across, halves up
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (int i = 1; i < 4; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 4.0f), area.getY(), area.getBottom());
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        // the curve itself, sampled finely so bends and smoothing show
        const auto& pts = m->getPoints();
        const int interp = (int) std::lround (m->getParameter ("interp"));
        juce::Path curve;
        const int steps = juce::jmax (64, (int) area.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            const auto p = toScreen (x, CurveCVModule::evaluate (pts, x, interp));
            if (i == 0) curve.startNewSubPath (p);
            else        curve.lineTo (p);
        }

        juce::Path fill (curve);
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(), area.getBottom());
        fill.closeSubPath();
        g.setColour (juce::Colour (0xffd970b0).withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (juce::Colour (0xffd970b0));
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        // points
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const auto p = toScreen (pts[(size_t) i].x, pts[(size_t) i].y);
            g.setColour (i == dragged ? juce::Colours::white : juce::Colour (0xfff2c4e3));
            g.fillEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
        }

        // playhead and the live value
        const float px = area.getX() + area.getWidth() * (float) m->getPlayhead();
        g.setColour (juce::Colour (0xff00ffff).withAlpha (0.8f));
        g.drawVerticalLine ((int) px, area.getY(), area.getBottom());
        const auto dot = toScreen ((float) m->getPlayhead(), m->getLastOutput());
        g.fillEllipse (dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);

        if (pts.size() <= 2 && interactionCount == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("tap to add points - drag to move\ntap twice to delete - drag empty space to bend",
                              getLocalBounds().reduced (8), juce::Justification::centredTop, 2);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* m = cv();
        if (m == nullptr)
            return;
        ++interactionCount;

        const int hit = pointAt (e.position);
        auto pts = m->getPoints();

        if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
        {
            if (hit > 0 && hit < (int) pts.size() - 1)   // the end points stay
            {
                pts.erase (pts.begin() + hit);
                m->setPoints (std::move (pts));
            }
            dragged = -1;
            mode = Mode::none;
            return;
        }

        downPos = e.position;
        if (hit >= 0)
        {
            dragged = hit;
            mode = Mode::movePoint;
            return;
        }

        // empty space: a tap adds a point, a vertical drag bends a segment
        mode = Mode::pending;
        bendSegment = segmentAt (e.position.x);
        bendStart = (bendSegment >= 0 && bendSegment < (int) pts.size()) ? pts[(size_t) bendSegment].bend : 0.0f;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* m = cv();
        if (m == nullptr)
            return;
        auto pts = m->getPoints();

        if (mode == Mode::movePoint && dragged >= 0 && dragged < (int) pts.size())
        {
            const auto v = fromScreen (e.position);
            auto& p = pts[(size_t) dragged];
            p.y = v.y;
            if (dragged > 0 && dragged < (int) pts.size() - 1)
                p.x = juce::jlimit (pts[(size_t) dragged - 1].x + 0.001f, pts[(size_t) dragged + 1].x - 0.001f, v.x);
            m->setPoints (std::move (pts));
            return;
        }

        if (mode == Mode::pending && e.position.getDistanceFrom (downPos) > 4.0f)
            mode = Mode::bend;

        if (mode == Mode::bend && bendSegment >= 0 && bendSegment + 1 < (int) pts.size())
        {
            // dragging up bulges the segment upwards whichever way it slopes
            const auto& a = pts[(size_t) bendSegment];
            const auto& b = pts[(size_t) bendSegment + 1];
            const float slopeSign = b.y >= a.y ? -1.0f : 1.0f;
            const float dy = (downPos.y - e.position.y) / juce::jmax (20.0f, plotArea().getHeight());
            pts[(size_t) bendSegment].bend = juce::jlimit (-1.0f, 1.0f, bendStart + slopeSign * dy * 2.0f);
            m->setPoints (std::move (pts));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* m = cv();
        if (m != nullptr && mode == Mode::pending)
        {
            auto pts = m->getPoints();
            const auto v = fromScreen (e.position);
            if (v.x > 0.0f && v.x < 1.0f && (int) pts.size() < CurveCVModule::kMaxPoints)
            {
                pts.push_back ({ v.x, v.y, 0.0f });
                m->setPoints (std::move (pts));
            }
        }
        dragged = -1;
        mode = Mode::none;
        repaint();
    }

private:
    enum class Mode { none, pending, movePoint, bend };

    CurveCVModule* cv() const { return dynamic_cast<CurveCVModule*> (module.get()); }

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (8.0f, 8.0f); }

    juce::Point<float> toScreen (float x, float y) const
    {
        const auto a = plotArea();
        return { a.getX() + a.getWidth() * x, a.getBottom() - a.getHeight() * y };
    }

    juce::Point<float> fromScreen (juce::Point<float> p) const
    {
        const auto a = plotArea();
        return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
    }

    int pointAt (juce::Point<float> pos) const
    {
        auto* m = cv();
        if (m == nullptr) return -1;
        const auto& pts = m->getPoints();
        for (int i = (int) pts.size() - 1; i >= 0; --i)
            if (toScreen (pts[(size_t) i].x, pts[(size_t) i].y).getDistanceFrom (pos) < 9.0f)
                return i;
        return -1;
    }

    int segmentAt (float screenX) const
    {
        auto* m = cv();
        if (m == nullptr) return -1;
        const float x = fromScreen ({ screenX, 0.0f }).x;
        const auto& pts = m->getPoints();
        for (int i = 0; i + 1 < (int) pts.size(); ++i)
            if (x >= pts[(size_t) i].x && x <= pts[(size_t) i + 1].x)
                return i;
        return -1;
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
    Mode mode { Mode::none };
    int dragged { -1 };
    int bendSegment { -1 };
    float bendStart { 0.0f };
    int interactionCount { 0 };
    juce::Point<float> downPos;
};

std::unique_ptr<juce::Component> CurveCVModule::createExtraContentComponent()
{
    return std::make_unique<CurveCVEditor> (*this);
}

//==============================================================================
static ModuleDescriptor curveCvDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.curvecv";
    d.displayName = "Curve CV";
    d.description =
        "A drawn control curve for automating any knob: place points, pick how it runs between "
        "them (Curved - drag empty space to bend a segment - Smooth or Step) and how fast it "
        "plays, in Hz or synced bars. Loops, plays once or ping-pongs; Restart In or a note on "
        "Midi In starts it over. A cable into a knob takes it over: 0 to 1 is the knob's whole "
        "range, wherever the knob is set, and Min/Max narrow that. Cable depth blends between "
        "the knob's own setting and the curve (100%, the default, is the curve alone); a "
        "negative depth turns it upside down.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 5;
    d.sockets = {
        modIn  ("restartIn", "Restart In"),
        midiIn ("midiIn",    "Midi In"),
        modOut ("cvOut",     "CV Out")
    };
    d.params = {
        makeRotary ("rate",   "Rate",   0.01f, 20.0f, 0.5f, 0, "Hz", true).visibleWhen ("sync", 0.0f),
        makeCombo  ("sync",   "Sync",   { "Free (Hz)", "Tempo" }, 0, 1, 2),
        makeSteppedList ("length", "Length", CurveCVModule::lengthChoices(), 4, 0).visibleWhen ("sync", 1.0f),
        makeCombo  ("mode",   "Mode",   { "Loop", "Once", "Ping-Pong" }, 0, 1, 2),
        makeCombo  ("interp", "Interp", { "Curved", "Smooth", "Step" }, 0, 2, 2),
        makeRotary ("min",    "Min",    0.0f, 1.0f, 0.0f, 0),
        makeRotary ("max",    "Max",    0.0f, 1.0f, 1.0f, 0),
        makeButton ("reset",  "Reset",  2, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CurveCVModule, curveCvDescriptor)
