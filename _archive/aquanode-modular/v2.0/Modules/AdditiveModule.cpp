#include "AdditiveModule.h"

using namespace aquanode;

//==============================================================================
// a shared sine table: 64 partials x many voices is far too much work for
// std::sin per partial per sample
//==============================================================================
namespace
{
    struct SineTable
    {
        static constexpr int kSize = 4096;
        float t[kSize + 1];

        SineTable()
        {
            for (int i = 0; i <= kSize; ++i)
                t[i] = (float)std::sin(juce::MathConstants<double>::twoPi * i / kSize);
        }

        inline float get (double p01) const noexcept
        {
            p01 -= std::floor (p01);
            const double x = p01 * kSize;
            const int i = juce::jlimit (0, kSize - 1, (int) x);
            const float f = (float) (x - i);
            return t[i] + (t[i + 1] - t[i]) * f;
        }
    };

    const SineTable sineTable;
}

//==============================================================================
// drawable partial bars: one per harmonic, drag to shape, double-click resets
//==============================================================================
class AdditiveDisplay : public juce::Component,
                        private juce::Timer
{
public:
    explicit AdditiveDisplay (AdditiveModule& m) : module (&m) { startTimerHz (12); }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<AdditiveModule*> (module.get());
        if (m == nullptr)
            return;

        const int mode = m->currentMode();
        const int n = AdditiveModule::partialCount (mode);
        const float w = (float) getWidth() / (float) n;
        const float h = (float) getHeight();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        for (int i = 0; i < n; ++i)
        {
            const float a = juce::jlimit (0.0f, 1.0f,
                m->getParameter (AdditiveModule::partialParamId (mode, i)));
            const float barH = a * (h - 3.0f);

            // the fundamental and every octave above it are tinted, so you can
            // still tell where you are at 64 bars wide
            const bool octave = (i & (i + 1)) == 0;   // i+1 is a power of two
            g.setColour (juce::Colour (octave ? 0xffe58ac4 : 0xffd970b0)
                            .withAlpha (a > 0.001f ? 0.95f : 0.25f));
            g.fillRect (i * w + 0.6f, h - barH - 1.5f, juce::jmax (1.0f, w - 1.2f), barH + 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { lastBar = -1; paintAt (e); }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e); }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (auto* m = dynamic_cast<AdditiveModule*> (module.get()))
        {
            m->resetPartials();
            repaint();
        }
    }

private:
    void paintAt (const juce::MouseEvent& e)
    {
        auto* m = dynamic_cast<AdditiveModule*> (module.get());
        if (m == nullptr)
            return;

        const int mode = m->currentMode();
        const int n = AdditiveModule::partialCount (mode);
        const int bar = juce::jlimit (0, n - 1, e.x * n / juce::jmax (1, getWidth()));
        const float value = juce::jlimit (0.0f, 1.0f,
            1.0f - (float) e.y / (float) juce::jmax (1, getHeight()));

        if (lastBar < 0 || lastBar == bar)
        {
            m->setParameter (AdditiveModule::partialParamId (mode, bar), value);
        }
        else
        {
            // fill in bars skipped by a fast drag
            const int lo = juce::jmin (lastBar, bar), hi = juce::jmax (lastBar, bar);
            const float vLo = lastBar < bar ? lastValue : value;
            const float vHi = lastBar < bar ? value : lastValue;
            for (int i = lo; i <= hi; ++i)
            {
                const float t = (float) (i - lo) / (float) juce::jmax (1, hi - lo);
                m->setParameter (AdditiveModule::partialParamId (mode, i), vLo + t * (vHi - vLo));
            }
        }

        lastBar = bar;
        lastValue = value;
        repaint();
    }

    void timerCallback() override { repaint(); }   // follows the Partials combo

    juce::WeakReference<SynthModule> module;
    int lastBar { -1 };
    float lastValue { 0.0f };
};

std::unique_ptr<juce::Component> AdditiveModule::createExtraContentComponent()
{
    return std::make_unique<AdditiveDisplay> (*this);
}

//==============================================================================
void AdditiveModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void AdditiveModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double f0 = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                         ! pool.isMuted (v), sampleRate));

    const int mode = juce::jlimit (0, 2, (int) param (pPartials));
    const int count = partialCount (mode);
    const int base = partialParamBase (mode);

    const float tiltDbPerOct = param (pTilt);
    const float oddEven = param (pOddEven) * 0.01f;
    const float stretchExp = 1.0f + param(pStretch) * 0.01f * 0.6f;
    const double nyquist = sampleRate * 0.48;

    // Odd/Even: 0 keeps only odd partials, 1 only even, 0.5 both at full level
    const float evenGain = juce::jlimit (0.0f, 1.0f, oddEven * 2.0f);
    const float oddGain  = juce::jlimit (0.0f, 1.0f, (1.0f - oddEven) * 2.0f);

    float sum = 0.0f;
    float norm = 0.0f;

    for (int i = 0; i < count; ++i)
    {
        float amp = param (base + i);
        if (amp <= 0.0005f)
            continue;                       // silent bar: skip the work entirely

        const int partialNumber = i + 1;    // 1 = fundamental

        // spectral tilt in dB per octave above the fundamental
        if (i > 0 && std::abs (tiltDbPerOct) > 0.01f)
            amp *= std::pow (10.0f, (tiltDbPerOct * std::log2 ((float) partialNumber)) / 20.0f);

        amp *= (partialNumber % 2 == 1) ? oddGain : evenGain;
        if (amp <= 0.0005f)
            continue;

        const double ratio = stretchExp == 1.0 ? (double) partialNumber
                                               : std::pow ((double) partialNumber, stretchExp);
        const double freq = f0 * ratio;
        if (freq >= nyquist)
            break;                          // everything above this is higher still

        phase[v][i] += freq / sampleRate;
        phase[v][i] -= std::floor (phase[v][i]);

        sum += sineTable.get (phase[v][i]) * amp;
        norm += amp;
    }

    const float inv = norm > 0.0001f ? 1.0f / juce::jmax (1.0f, norm * 0.7f) : 0.0f;

    const bool envConnected = isInputConnected (0);
    const float gain = gate.next (v, envConnected, inputs[0][0], sampleRate);

    const float out = sum * inv * gain * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor additiveDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.additive";
    d.displayName = "Additive";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 16;
    d.sockets = {
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.8f, 0),
        makeCombo  ("partials", "Partials", { "16", "32", "64" }, 1, 0, 2),
        makeRotary ("tilt",     "Tilt",     -12.0f, 6.0f, 0.0f, 0, "dB/oct"),
        makeRotary ("oddEven",  "Odd/Even", 0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("stretch",  "Stretch",  0.0f, 100.0f, 0.0f, 1, "%"),
        // 64 partials across a full voice pool is heavy, so this one starts modest
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, 8.0f, 1, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 1, "ms").visibleWhen ("voices", 1.0f)
    };

    // three independent banks of bars; switching Partials swaps which one is
    // heard and drawn, and leaves the others untouched in the patch
    for (int mode = 0; mode < 3; ++mode)
        for (int i = 0; i < AdditiveModule::partialCount (mode); ++i)
            d.params.push_back (makeRotary (AdditiveModule::partialParamId (mode, i),
                                            "P" + juce::String (i + 1),
                                            0.0f, 1.0f,
                                            AdditiveModule::defaultPartial (i), 9).hide());
    return d;
}

AQUANODE_REGISTER_MODULE (AdditiveModule, additiveDescriptor)
