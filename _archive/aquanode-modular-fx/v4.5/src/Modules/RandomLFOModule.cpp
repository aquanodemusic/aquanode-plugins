#include "RandomLFOModule.h"

using namespace aquanode;

namespace
{
    const juce::StringArray& divisionChoices()
    {
        static const juce::StringArray c { "1/32", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
        return c;
    }

    double divisionBeats (int i)
    {
        static const double beats[] = { 0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return beats[juce::jlimit (0, 10, i)];
    }
}

RandomLFOModule::RandomLFOModule()
{
    for (int v = 0; v < kMaxVoices; ++v)
        scatter (st[v], 0x9E3779B9u * (juce::uint32) (v + 1));
}

void RandomLFOModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void RandomLFOModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void RandomLFOModule::voiceReset (int v)
{
    scatter (st[v], 0x9E3779B9u * (juce::uint32) (v + 1) + noteCounter);
}

void RandomLFOModule::voiceNoteOn (int v, int, bool retrigger)
{
    if (param (pRetrig) > 0.5f && ! retrigger)
        scatter (st[v], 0x85EBCA6Bu * ++noteCounter);
}

float RandomLFOModule::nextRandom (State& s)
{
    // xorshift: cheap, deterministic per voice, no shared state
    s.seed ^= s.seed << 13;
    s.seed ^= s.seed >> 17;
    s.seed ^= s.seed << 5;
    return (float) (s.seed & 0xffffff) / 8388608.0f - 1.0f;
}

void RandomLFOModule::scatter (State& s, juce::uint32 seed)
{
    s.seed = seed == 0 ? 1u : seed;
    for (int i = 0; i < 8; ++i)
        nextRandom (s);
    for (auto& p : s.pts)
        p = nextRandom (s);
    s.held = s.smoothed = s.pts[1];
    s.phase = 0.0;
    s.lx = 0.1 + 0.5 * nextRandom (s);
    s.ly = 0.5 * nextRandom (s);
    s.lz = 20.0 + 5.0 * nextRandom (s);
}

void RandomLFOModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    auto& s = st[v];
    double hz = param (pRate);
    if (param (pSync) > 0.5f)
        hz = (tempoBpm / 60.0) / divisionBeats ((int) std::lround (param (pDivision)));
    hz = juce::jlimit (0.001, 200.0, hz);
    const double inc = hz / sampleRate;
    const int mode = (int) std::lround (param (pMode));

    float y = 0.0f;
    if (mode == 3)
    {
        // Lorenz (sigma 10, rho 28, beta 8/3); Rate sets how fast it is run
        const double dt = juce::jmin (0.02, inc * 2.0);
        const double dx = 10.0 * (s.ly - s.lx);
        const double dy = s.lx * (28.0 - s.lz) - s.ly;
        const double dz = s.lx * s.ly - 8.0 / 3.0 * s.lz;
        s.lx += dx * dt;
        s.ly += dy * dt;
        s.lz += dz * dt;
        y = (float) juce::jlimit (-1.0, 1.0, s.lx / 20.0);
    }
    else
    {
        s.phase += inc;
        if (s.phase >= 1.0)
        {
            s.phase -= std::floor (s.phase);
            s.pts[0] = s.pts[1];
            s.pts[1] = s.pts[2];
            s.pts[2] = s.pts[3];
            s.pts[3] = nextRandom (s);
            s.held = s.pts[1];
        }
        const float t = (float) s.phase;
        const float p0 = s.pts[0], p1 = s.pts[1], p2 = s.pts[2], p3 = s.pts[3];
        if (mode == 0)
        {
            // Catmull-Rom between p1 and p2
            const float t2 = t * t, t3 = t2 * t;
            y = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
            y = juce::jlimit (-1.0f, 1.0f, y);
        }
        else if (mode == 1)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t);
            y = p1 + (p2 - p1) * w;
        }
        else
        {
            // steps, with Smooth as a glide of up to one step long
            const float smooth = param (pSmooth) * 0.01f;
            if (smooth < 0.001f)
                s.smoothed = s.held;
            else
            {
                const float coef = 1.0f - std::exp (-(float) inc * 5.0f / (smooth * smooth + 1.0e-4f));
                s.smoothed += coef * (s.held - s.smoothed);
            }
            y = s.smoothed;
        }
    }

    if (param (pPolarity) > 0.5f)
        y = 0.5f * (y + 1.0f);
    const float out = y * param (pAmount) * 0.01f;
    outputs[0][0] = out;
    outputs[0][1] = out;

    // a coarse trace for the card, from voice 0's lane
    if (v == 0 && ++historyTick >= 400)
    {
        historyTick = 0;
        const int pos = historyPos.load (std::memory_order_relaxed);
        history[(size_t) pos].store (y, std::memory_order_relaxed);
        historyPos.store ((pos + 1) % kHistory, std::memory_order_relaxed);
    }
}

//==============================================================================
class RandomLFODisplay : public juce::Component, private juce::Timer
{
public:
    explicit RandomLFODisplay (RandomLFOModule& m) : module (&m)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<RandomLFOModule*> (module.get());
        if (m == nullptr)
            return;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 4.0f);
        r = r.reduced (4.0f);
        const bool uni = m->getParameter ("polarity") > 0.5f;
        juce::Path p;
        for (int i = 0; i < RandomLFOModule::kHistory; ++i)
        {
            const float y = m->historyAt (i);
            const float x = r.getX() + r.getWidth() * (float) i / (float) (RandomLFOModule::kHistory - 1);
            const float yy = uni ? r.getBottom() - y * r.getHeight() : r.getCentreY() - y * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, yy); else p.lineTo (x, yy);
        }
        g.setColour (juce::Colour (0xffb4f08c));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

private:
    void timerCallback() override { repaint(); }
    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> RandomLFOModule::createExtraContentComponent()
{
    return std::make_unique<RandomLFODisplay> (*this);
}

//==============================================================================
static ModuleDescriptor randomLfoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.randomlfo";
    d.displayName = "Random LFO";
    d.description =
        "Wandering modulation, per voice. Smooth joins random points with a spline, Sine with half-cosines, "
        "Steps is sample & hold (Smooth glides between steps), Chaos runs a Lorenz attractor that never "
        "repeats. Rate in Hz or locked to the tempo; Note gives every new note a fresh path, Free lets each "
        "voice keep wandering. Amount scales the output.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 12;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeCombo ("mode", "Mode", { "Smooth", "Sine", "Steps", "Chaos" }, 0, 0, 2),
        makeRotary ("rate",   "Rate",   0.1f, 50.0f, 1.0f, 0, "Hz", true).visibleWhen ("sync", 0.0f),
        makeRotary ("amount", "Amount", -100.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
        makeCombo ("sync", "Sync", { "Hz", "Tempo" }, 0, 1, 2),
        makeSteppedList ("division", "Division", divisionChoices(), 5, 0).visibleWhen ("sync", 1.0f),
        makeRotary ("smooth", "Smooth", 0.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),
        makeCombo ("retrig", "Retrig", { "Free", "Note" }, 1, 2, 2),
        makeCombo ("polarity", "Polarity", { "Bipolar", "Unipolar" }, 0, 2, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RandomLFOModule, randomLfoDescriptor)
