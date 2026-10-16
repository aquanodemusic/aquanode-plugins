#include "MultiLFOModule.h"

using namespace aquanode;

namespace
{
    // exponential segment curvature: 0 = straight, + = slow start, - = fast start
    float powerScale (float t, float power)
    {
        if (std::abs (power) < 0.01f)
            return t;
        return (std::exp (power * t) - 1.0f) / (std::exp (power) - 1.0f);
    }

    void renderShape (const MultiLFOModule::Shape& s, std::array<float, MultiLFOModule::kTableSize>& out)
    {
        const int n = (int) s.size();
        for (int i = 0; i < MultiLFOModule::kTableSize; ++i)
        {
            const float x = (float) i / (float) MultiLFOModule::kTableSize;
            float y = n > 0 ? s.back().y : 0.0f;
            for (int k = 0; k + 1 < n; ++k)
            {
                const auto& a = s[(size_t) k];
                const auto& b = s[(size_t) k + 1];
                if (x >= a.x && x < b.x)
                {
                    const float t = (x - a.x) / juce::jmax (1.0e-6f, b.x - a.x);
                    y = a.y + (b.y - a.y) * powerScale (t, a.power);
                    break;
                }
            }
            out[(size_t) i] = juce::jlimit (0.0f, 1.0f, y);
        }
    }
}

//==============================================================================
const juce::StringArray& MultiLFOModule::shapeChoices()
{
    static const juce::StringArray c { "Saw Down", "Saw Up", "Triangle", "Hold Decay", "Spike", "Pump",
                                       "Square", "Gate 3/4", "Double Pluck", "Quad Chop", "Stairs Down",
                                       "Exp Decay", "Rolled" };
    return c;
}

const juce::StringArray& MultiLFOModule::divisionChoices()
{
    static const juce::StringArray c { "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    return c;
}

double MultiLFOModule::divisionBeats (int index, int syncMode)
{
    static const double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    double b = beats[juce::jlimit (0, 8, index)];
    if (syncMode == 1) b *= 1.5;              // dotted
    else if (syncMode == 2) b *= 2.0 / 3.0;   // triplet
    return b;
}

MultiLFOModule::Shape MultiLFOModule::builtInShape (int index)
{
    switch (index)
    {
        case 0:  return { { 0, 1, 0 }, { 1, 0, 0 } };                                       // Saw Down (pluck chop)
        case 1:  return { { 0, 0, 0 }, { 1, 1, 0 } };                                       // Saw Up
        case 2:  return { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } };                       // Triangle
        case 3:  return { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } };                       // Hold Decay
        case 4:  return { { 0, 0, 0 }, { 0.1f, 1, 0 }, { 1, 0, 0 } };                       // Spike
        case 5:  return { { 0, 0, 0 }, { 0.6f, 1, -3.0f }, { 1, 1, 0 } };                   // Pump (sidechain)
        case 6:  return { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } };       // Square
        case 7:  return { { 0, 1, 0 }, { 0.75f, 1, 0 }, { 0.75f, 0, 0 }, { 1, 0, 0 } };     // Gate 3/4
        case 8:  return { { 0, 1, 0 }, { 0.5f, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } };       // Double Pluck
        case 9:  return { { 0, 1, 0 }, { 0.25f, 0, 0 }, { 0.25f, 1, 0 }, { 0.5f, 0, 0 },    // Quad Chop
                          { 0.5f, 1, 0 }, { 0.75f, 0, 0 }, { 0.75f, 1, 0 }, { 1, 0, 0 } };
        case 10: return { { 0, 1, 0 }, { 0.25f, 1, 0 }, { 0.25f, 0.75f, 0 }, { 0.5f, 0.75f, 0 },     // Stairs Down
                          { 0.5f, 0.5f, 0 }, { 0.75f, 0.5f, 0 }, { 0.75f, 0.25f, 0 }, { 1, 0.25f, 0 } };
        case 11: return { { 0, 1, 4.0f }, { 1, 0, 0 } };                                    // Exp Decay
        default: break;
    }
    return { { 0, 1, 0 }, { 1, 0, 0 } };
}

// Random shapes in three generic families: step patterns, chops, free curves
MultiLFOModule::Shape MultiLFOModule::randomShape (juce::uint32 s)
{
    juce::Random r ((juce::int64) ((s * 2654435761u) ^ (s >> 15) ^ 0x51ED27u));
    Shape sh;
    const int family = r.nextInt (3);

    if (family == 0)
    {
        // step pattern: 4, 8 or 16 steps of random height, some of them off
        const int steps = r.nextBool() ? 8 : (r.nextBool() ? 4 : 16);
        for (int i = 0; i < steps; ++i)
        {
            const float x0 = (float) i / (float) steps, x1 = (float) (i + 1) / (float) steps;
            const float y = r.nextFloat() < 0.2f ? 0.0f : juce::jmap (r.nextFloat(), 0.15f, 1.0f);
            sh.push_back ({ x0, y, 0 });
            sh.push_back ({ x1, y, 0 });
        }
    }
    else if (family == 1)
    {
        // chop pattern: a few plucks (saw-down segments) on an eighth/sixteenth grid
        const int grid = r.nextBool() ? 8 : 16;
        float x = 0.0f;
        while (x < 0.999f)
        {
            const int len = 1 + r.nextInt (grid >= 16 ? 4 : 2);
            const float x1 = juce::jmin (1.0f, x + (float) len / (float) grid);
            if (r.nextFloat() < 0.15f && x > 0.0f)
            {
                sh.push_back ({ x, 0, 0 });
                sh.push_back ({ x1, 0, 0 });
            }
            else
            {
                const float top = juce::jmap (r.nextFloat(), 0.6f, 1.0f);
                const float tail = r.nextFloat() < 0.5f ? 0.0f : juce::jmap (r.nextFloat(), 0.0f, 0.4f);
                sh.push_back ({ x, top, juce::jmap (r.nextFloat(), -2.0f, 3.0f) });
                sh.push_back ({ x1, tail, 0 });
            }
            x = x1;
        }
    }
    else
    {
        // free curve: 3 to 7 points with curvature
        const int n = 3 + r.nextInt (5);
        std::vector<float> xs { 0.0f, 1.0f };
        for (int i = 0; i < n - 2; ++i) xs.push_back (r.nextFloat());
        std::sort (xs.begin(), xs.end());
        for (size_t i = 0; i < xs.size(); ++i)
            sh.push_back ({ xs[i], r.nextFloat(), juce::jmap (r.nextFloat(), -4.0f, 4.0f) });
        sh.front().y = r.nextBool() ? 0.0f : 1.0f;
    }
    if (sh.empty()) sh = builtInShape (0);
    return sh;
}

//==============================================================================
MultiLFOModule::MultiLFOModule()
{
    rebuildTable();
}

void MultiLFOModule::rebuildTable()
{
    const int idx = (int) std::lround (getParameter ("shape"));
    const Shape s = (idx == shapeChoices().size() - 1) ? randomShape (seed) : builtInShape (idx);

    auto t = std::make_shared<std::array<float, kTableSize>>();
    renderShape (s, *t);

    std::shared_ptr<const std::array<float, kTableSize>> old;
    {
        const juce::SpinLock::ScopedLockType sl (tableLock);
        old = std::move (published);
        published = std::move (t);
    }
    retired.push_back (std::move (old));
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                       [] (const auto& p) { return p == nullptr || p.use_count() == 1; }),
                   retired.end());
}

std::shared_ptr<const std::array<float, MultiLFOModule::kTableSize>> MultiLFOModule::currentTable() const
{
    const juce::SpinLock::ScopedLockType sl (tableLock);
    return published;
}

void MultiLFOModule::rollShape (juce::uint32 newSeed)
{
    while (newSeed == 0)
        newSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    seed = newSeed;
    SynthModule::setParameter ("shape", (float) (shapeChoices().size() - 1));
    rebuildTable();
}

void MultiLFOModule::setParameter (const juce::String& id, float value)
{
    SynthModule::setParameter (id, value);
    if (id == "shape")
        rebuildTable();
}

void MultiLFOModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "roll")
        rollShape();
}

juce::String MultiLFOModule::saveCustomState() const
{
    return "CBL1 " + juce::String ((juce::int64) seed);
}

void MultiLFOModule::loadCustomState (const juce::String& state)
{
    auto t = juce::StringArray::fromTokens (state, " ", {});
    if (t.size() >= 2 && t[0] == "CBL1")
    {
        const auto s = (juce::uint32) t[1].getLargeIntValue();
        if (s != 0) seed = s;
    }
    rebuildTable();
}

//==============================================================================
void MultiLFOModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void MultiLFOModule::voiceReset (int v)
{
    phase[v] = 0.0;
    slew[v] = 0.0f;
    done[v] = false;
}

void MultiLFOModule::voiceNoteOn (int v, int, bool)
{
    // every note restarts the shape, as the plain LFO does
    phase[v] = (double) param (pPhase);
    done[v] = false;
    if (audioTable != nullptr)
    {
        const float lo = param (pMin), hi = param (pMax);
        slew[v] = lo + (hi - lo) * (*audioTable)[(size_t) juce::jlimit (0, kTableSize - 1, (int) (phase[v] * kTableSize))];
    }
}

void MultiLFOModule::blockStart()
{
    {
        const juce::SpinLock::ScopedLockType sl (tableLock);
        if (audioTable != published)
            audioTable = published;
    }
    const double bpm = hostTransport.hasBpm ? hostTransport.bpm : (double) param (pBpm);
    beatsPerSecond = juce::jmax (1.0, bpm) / 60.0;
}

void MultiLFOModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    if (audioTable == nullptr)
    {
        outputs[0] = outputs[1] = { 0.0f, 0.0f };
        return;
    }

    const int sync = (int) std::lround (param (pSync));
    double hz;
    if (sync == 3)
        hz = (double) param (pRate);
    else
        hz = beatsPerSecond / divisionBeats ((int) std::lround (param (pDivision)), sync);

    const bool oneShot = (int) std::lround (param (pMode)) == 1;

    if (! done[v])
    {
        phase[v] += hz / sampleRate;
        if (phase[v] >= 1.0)
        {
            if (oneShot) { phase[v] = 0.99999; done[v] = true; }
            else phase[v] -= std::floor (phase[v]);
        }
    }

    const double x = phase[v] * kTableSize;
    const int i0 = juce::jlimit (0, kTableSize - 1, (int) x);
    const int i1 = oneShot ? juce::jmin (kTableSize - 1, i0 + 1) : (i0 + 1) % kTableSize;
    const float fr = (float) (x - (double) i0);
    const float shape = (*audioTable)[(size_t) i0] + ((*audioTable)[(size_t) i1] - (*audioTable)[(size_t) i0]) * fr;

    const float lo = param (pMin), hi = param (pMax);
    const float target = lo + (hi - lo) * shape;

    const float ms = param (pSmooth);
    if (ms < 0.05f)
        slew[v] = target;
    else
        slew[v] += (1.0f - std::exp ((float) (-1.0 / (ms * 0.001 * sampleRate)))) * (target - slew[v]);

    if (v == 0 || ! done[v])
        uiPhase.store ((float) phase[v], std::memory_order_relaxed);

    const float out = slew[v];
    outputs[0] = { out, out };
    const float inv = lo + hi - out;
    outputs[1] = { inv, inv };
}

//==============================================================================
class MultiLFODisplay : public juce::Component, private juce::Timer
{
public:
    explicit MultiLFODisplay (MultiLFOModule& m) : module (&m) { startTimerHz (20); }

    void paint (juce::Graphics& g) override
    {
        auto* lfo = dynamic_cast<MultiLFOModule*> (module.get());
        auto box = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141218));
        g.fillRoundedRectangle (box, 4.0f);
        if (lfo == nullptr) return;
        auto t = lfo->currentTable();
        if (t == nullptr) return;

        auto area = box.reduced (6.0f, 5.0f);
        juce::Path p;
        const int n = juce::jmax (2, (int) area.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const float y01 = (*t)[(size_t) juce::jlimit (0, MultiLFOModule::kTableSize - 1, i * MultiLFOModule::kTableSize / n)];
            const float x = area.getX() + (float) i;
            const float y = area.getBottom() - y01 * area.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xff9b7cf0));
        g.strokePath (p, juce::PathStrokeType (1.4f));

        const float px = area.getX() + area.getWidth() * lfo->getUiPhase();
        g.setColour (juce::Colour (0xff00e5ff).withAlpha (0.8f));
        g.drawVerticalLine ((int) px, area.getY(), area.getBottom());
    }

private:
    void timerCallback() override { repaint(); }
    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> MultiLFOModule::createExtraContentComponent()
{
    return std::make_unique<MultiLFODisplay> (*this);
}

//==============================================================================
static ModuleDescriptor multiLfoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.multilfo";
    d.displayName = "Multi LFO";
    d.description =
        "A tempo-synced shape LFO for chops and sweeps: saw-down plucks, gates, stairs, sixteenth chops "
        "or a Rolled random shape. Divisions follow the host tempo (the BPM knob stands in without a host). "
        "Retrigger restarts it on every note, One Shot runs once like an envelope. Patch Mod Out into a "
        "Chord Wavetable's Level In for the rhythmic chop, or into Morph In for the shimmer.";
    d.section = ModuleSection::Utility;
    d.family = ModuleFamily::Colorbass;
    d.sidebarOrder = 2;
    d.sockets = {
        modOut ("modOut", "Mod Out"),
        modOut ("invOut", "Inv Out")
    };
    d.params = {
        makeCombo       ("shape",    "Shape",    MultiLFOModule::shapeChoices(), 0, 0, 3),
        makeButton      ("roll",     "Roll",     0, 1),
        makeCombo       ("sync",     "Sync",     { "Tempo", "Dotted", "Triplet", "Hz" }, 0, 1, 2),
        makeSteppedList ("division", "Division", MultiLFOModule::divisionChoices(), 5, 1),
        makeRotary      ("rate",     "Rate",     0.02f, 40.0f, 2.0f, 1, "Hz", true).visibleWhen ("sync", 3.0f),
        makeCombo       ("mode",     "Mode",     { "Retrigger", "One Shot" }, 0, 2, 2),
        makeRotary      ("min",      "Min",      0.0f, 1.0f, 0.0f, 2),
        makeRotary      ("max",      "Max",      0.0f, 1.0f, 1.0f, 2),
        makeRotary      ("smooth",   "Smooth",   0.0f, 50.0f, 2.0f, 3, "ms"),
        makeRotary      ("phase",    "Phase",    0.0f, 0.99f, 0.0f, 3),
        makeRotary      ("bpm",      "BPM",      40.0f, 240.0f, 140.0f, 3, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MultiLFOModule, multiLfoDescriptor)
