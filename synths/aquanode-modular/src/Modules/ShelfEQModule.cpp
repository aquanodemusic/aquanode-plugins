#include "ShelfEQModule.h"

using namespace aquanode;

std::complex<double> ShelfEQModule::Biquad::response (double w) const
{
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

void ShelfEQModule::design (const float* v, double sr, Biquad out[3])
{
    // RBJ cookbook
    auto make = [sr] (int type, double f, double gainDb, double q) -> Biquad
    {
        f = juce::jlimit (10.0, sr * 0.45, f);
        q = juce::jmax (0.1, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type)
        {
            case 0:   // low shelf
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case 1:   // high pass (low cut)
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case 2:   // peak
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            case 3:   // notch
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case 4:   // high shelf
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            default:  // low pass (high cut)
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
        }
        Biquad bq;
        bq.b0 = b0 / a0; bq.b1 = b1 / a0; bq.b2 = b2 / a0; bq.a1 = a1 / a0; bq.a2 = a2 / a0;
        return bq;
    };

    out[0] = make (v[pLowMode] > 0.5f ? 1 : 0, v[pLowFreq], v[pLowGain], v[pLowQ]);
    out[1] = make (v[pMidMode] > 0.5f ? 3 : 2, v[pMidFreq], v[pMidGain], v[pMidQ]);
    out[2] = make (v[pHighMode] > 0.5f ? 5 : 4, v[pHighFreq], v[pHighGain], v[pHighQ]);
}

void ShelfEQModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    for (auto& lv : lastValues)
        lv = -12345.0f;
    reset();
}

void ShelfEQModule::reset()
{
    for (int b = 0; b < 3; ++b)
        for (int c = 0; c < 2; ++c)
            z1[b][c] = z2[b][c] = 0.0;
    ctrl = 0;
}

void ShelfEQModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (--ctrl < 0)
    {
        ctrl = 31;
        float v[13];
        bool changed = false;
        for (int i = 0; i < 13; ++i)
        {
            v[i] = param (i);
            changed = changed || ! juce::exactlyEqual (v[i], lastValues[i]);
        }
        if (changed)
        {
            design (v, sampleRate, band);
            std::copy (v, v + 13, lastValues);
        }
        outGain = juce::Decibels::decibelsToGain (param (pOutput));
    }

    for (int c = 0; c < 2; ++c)
    {
        double x = inputs[0][(size_t) c];
        for (int b = 0; b < 3; ++b)
        {
            const auto& q = band[b];
            const double y = q.b0 * x + z1[b][c];
            z1[b][c] = q.b1 * x - q.a1 * y + z2[b][c];
            z2[b][c] = q.b2 * x - q.a2 * y;
            x = y;
        }
        outputs[0][(size_t) c] = (float) x * outGain;
    }
}

//==============================================================================
class ShelfEQDisplay : public juce::Component, private juce::Timer
{
public:
    explicit ShelfEQDisplay (ShelfEQModule& m) : module (&m)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = module.get();
        if (m == nullptr)
            return;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 4.0f);
        r = r.reduced (4.0f);

        const double sr = 48000.0;
        ShelfEQModule::Biquad b[3];
        ShelfEQModule::design (values.data(), sr, b);

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (r.getX(), r.getCentreY(), r.getWidth(), 1.0f));
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const float x = r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
            g.fillRect (juce::Rectangle<float> (x, r.getY(), 1.0f, r.getHeight()));
        }

        juce::Path p;
        const int n = 120;
        for (int i = 0; i < n; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (n - 1));
            const double w = juce::MathConstants<double>::twoPi * f / sr;
            double db = values[ShelfEQModule::pOutput];
            for (auto& q : b)
                db += 20.0 * std::log10 (std::max (1.0e-6, std::abs (q.response (w))));
            const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float y = r.getCentreY() - (float) juce::jlimit (-24.0, 24.0, db) / 24.0f * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffffc46b));
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

private:
    void timerCallback() override
    {
        auto* m = module.get();
        if (m == nullptr)
            return;
        static const char* ids[13] = { "lowMode", "lowFreq", "lowGain", "lowQ", "midMode", "midFreq", "midGain", "midQ",
                                       "highMode", "highFreq", "highGain", "highQ", "output" };
        bool changed = false;
        for (int i = 0; i < 13; ++i)
        {
            const float v = m->getParameter (ids[i]);
            changed = changed || ! juce::exactlyEqual (v, values[(size_t) i]);
            values[(size_t) i] = v;
        }
        if (changed)
            repaint();
    }

    juce::WeakReference<SynthModule> module;
    std::array<float, 13> values {};
};

std::unique_ptr<juce::Component> ShelfEQModule::createExtraContentComponent()
{
    return std::make_unique<ShelfEQDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor shelfEqDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.shelfeq";
    d.displayName = "Shelf EQ";
    d.description =
        "A three-band tone shaper: Low (shelf or low cut), Mid (peak or notch) and High (shelf or high cut), "
        "each with frequency, gain and Q, plus an output trim. The broad strokes - warmth, body, air - where "
        "3-Bell EQ is surgical. The curve shows the summed response.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 13;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo ("lowMode", "Low", { "Low Shelf", "Low Cut" }, 0, 0, 2),
        makeRotary ("lowFreq", "Low Hz",   20.0f, 2000.0f, 120.0f, 0, "Hz", true, 1.0f),
        makeRotary ("lowGain", "Low dB",   -24.0f, 24.0f, 0.0f, 0, "dB"),
        makeRotary ("lowQ",    "Low Q",    0.3f, 4.0f, 0.71f, 0),

        makeCombo ("midMode", "Mid", { "Peak", "Notch" }, 0, 1, 2),
        makeRotary ("midFreq", "Mid Hz",   40.0f, 16000.0f, 1000.0f, 1, "Hz", true, 1.0f),
        makeRotary ("midGain", "Mid dB",   -24.0f, 24.0f, 0.0f, 1, "dB"),
        makeRotary ("midQ",    "Mid Q",    0.2f, 16.0f, 0.8f, 1, {}, true),

        makeCombo ("highMode", "High", { "Hi Shelf", "Hi Cut" }, 0, 2, 2),
        makeRotary ("highFreq", "High Hz", 500.0f, 20000.0f, 6000.0f, 2, "Hz", true, 1.0f),
        makeRotary ("highGain", "High dB", -24.0f, 24.0f, 0.0f, 2, "dB"),
        makeRotary ("highQ",    "High Q",  0.3f, 4.0f, 0.71f, 2),

        makeRotary ("output", "Output", -24.0f, 24.0f, 0.0f, 3, "dB")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ShelfEQModule, shelfEqDescriptor)
