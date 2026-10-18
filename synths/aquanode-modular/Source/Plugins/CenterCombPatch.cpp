//==============================================================================
// CenterComb - Patch version
//
// One module, since CenterComb is one thing: up to 129 bell filters in series,
// mirrored around a centre frequency. "CenterComb - Comb Bank" is the plugin's
// processBlock line for line - linear or multiplicative spread, Regular / Wrap
// / Mirror frequency modes, the smoothing, Wet Only, the hard limiter and the
// safety rules for the extreme mode combinations - with every knob taking
// cables.
//
// CenterComb updates its filters once per host block; this module does it
// every 32 samples, which is what the plugin does at a 32-sample buffer.
// (The older, simpler "Center Comb" filter module stays as it was, so patches
// that use it keep sounding the same.)
//==============================================================================
#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "centercomb" };

    float wrapFrequency (double freq, float minFreq, float maxFreq)
    {
        const double range = (double) maxFreq - (double) minFreq;
        double x = std::fmod (freq - (double) minFreq, range);
        if (x < 0.0)
            x += range;
        return minFreq + (float) x;
    }

    float mirrorFrequency (double freq, float minFreq, float maxFreq)
    {
        const double range = (double) maxFreq - (double) minFreq;
        const double period = 2.0 * range;
        double x = std::fmod (freq - (double) minFreq, period);
        if (x < 0.0)
            x += period;
        if (x > range)
            x = period - x;
        return minFreq + (float) x;
    }
}

class CenterCombBankModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.centercomb.bank";
        d.displayName = kit::moduleName (kPlugin, "Comb Bank");
        d.description = "CenterComb whole: a bell filter at Center with Count more pairs mirrored around it, "
                        "spaced by Hz (linear) or by a ratio (multiplicative), Damp fading the outer ones. Wrap "
                        "and Mirror fold peaks that leave 20 Hz - 20 kHz back in. Smooth glides the knobs.";
        d.section = ModuleSection::Filter;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("freq",   "Center", 20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
            makeRotary ("gain",   "Gain",   -24.0f, 24.0f, 0.0f, 0, "dB"),
            makeRotary ("amount", "Count",  0.0f, 64.0f, 2.0f, 0, {}, false, 1.0f),
            makeRotary ("q",      "Q",      10.0f, 1000.0f, 10.0f, 0, {}, true),
            makeRotary ("damp",   "Damp",   0.0f, 1.0f, 0.5f, 0),

            makeCombo  ("spreadMode", "Spread", { "Linear", "Multiplicative" }, 0, 1, 2),
            makeRotary ("spreadHz",    "Spread Hz",    10.0f, 2000.0f, 100.0f, 1, "Hz", true).visibleWhen ("spreadMode", 0.0f),
            makeRotary ("spreadRatio", "Spread Ratio", 1.01f, 10.0f, 1.05f, 1, {}, true).visibleWhen ("spreadMode", 1.0f),
            makeRotary ("smoothTime",  "Smooth", 0.0f, 2000.0f, 1.0f, 1, "ms"),

            makeCombo ("freqMode",    "Freq Mode", { "Regular", "Wrap", "Mirror" }, 0, 2, 2),
            makeCombo ("wetOnly",     "Wet Only",  { "Off", "On" }, 0, 2, 1),
            makeCombo ("hardLimiter", "Limiter",   { "Off", "On" }, 1, 2, 1),
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        idx.freq = paramIndex ("freq");   idx.gain = paramIndex ("gain");
        idx.amount = paramIndex ("amount"); idx.q = paramIndex ("q");
        idx.damp = paramIndex ("damp");   idx.spreadMode = paramIndex ("spreadMode");
        idx.spreadHz = paramIndex ("spreadHz"); idx.spreadRatio = paramIndex ("spreadRatio");
        idx.smooth = paramIndex ("smoothTime"); idx.freqMode = paramIndex ("freqMode");
        idx.wetOnly = paramIndex ("wetOnly"); idx.limiter = paramIndex ("hardLimiter");
        currentSmoothingMs = -1.0f;
        counter = 0;
        reset();
    }

    void reset() override
    {
        for (auto& ch : state)
            for (auto& s : ch)
                s = {};
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = kBlock - 1;
            updateFilters();
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = in[0][(size_t) ch];
            float x = dry;

            for (int i = 0; i < numFilters; ++i)
            {
                const auto& c = coeffs[(size_t) i];
                auto& s = state[(size_t) ch][(size_t) i];
                if (! c.active)
                {
                    s = {};
                    continue;
                }
                // transposed direct form II, as juce::dsp::IIR::Filter
                const float y = c.b0 * x + s.s1;
                s.s1 = c.b1 * x - c.a1 * y + s.s2;
                s.s2 = c.b2 * x - c.a2 * y;
                x = y;
            }

            if (wetOnly)
                x -= dry;
            if (limit)
                x = juce::jlimit (-limitThreshold, limitThreshold, x);
            if (! std::isfinite (x))
                x = 0.0f;
            out[0][(size_t) ch] = x;
        }
    }

private:
    static constexpr int kBlock = 32;
    static constexpr int kMaxFilters = 129;

    struct Coeffs { float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; bool active { false }; };
    struct State { float s1 { 0 }, s2 { 0 }; };

    // CenterCombAudioProcessor::processBlock, steps 1 to 3, for one 32-sample block
    void updateFilters()
    {
        const float targetSmoothMs = param (idx.smooth);
        if (std::abs (targetSmoothMs - currentSmoothingMs) > 0.01f)
        {
            currentSmoothingMs = targetSmoothMs;
            const double t = currentSmoothingMs / 1000.0;
            for (auto* s : { &sGain, &sFreq, &sQ, &sDamp, &sSpreadHz, &sSpreadRatio })
                s->reset (sampleRate, t);
        }

        sGain.setTargetValue (param (idx.gain));
        sFreq.setTargetValue (param (idx.freq));
        sQ.setTargetValue (juce::jlimit (0.1f, 100.0f, param (idx.q)));
        sDamp.setTargetValue (param (idx.damp));
        sSpreadHz.setTargetValue (param (idx.spreadHz));
        sSpreadRatio.setTargetValue (param (idx.spreadRatio));

        const float gainDb = sGain.getNextValue();
        const float freq = sFreq.getNextValue();
        const float q = sQ.getNextValue();
        const float damp = sDamp.getNextValue();
        sGain.skip (kBlock - 1); sFreq.skip (kBlock - 1); sQ.skip (kBlock - 1); sDamp.skip (kBlock - 1);

        int pairs = (int) param (idx.amount);
        const bool multiplicative = param (idx.spreadMode) > 0.5f;
        wetOnly = param (idx.wetOnly) > 0.5f;
        const bool hardLimiter = param (idx.limiter) > 0.5f;
        const int freqMode = (int) param (idx.freqMode);

        if (multiplicative && freqMode == 1)
            pairs = juce::jmin (pairs, 5);   // the plugin's safety rule: max 11 tines in Wrap + Multiplicative

        const float spread = multiplicative ? sSpreadRatio.getNextValue() : sSpreadHz.getNextValue();
        (multiplicative ? sSpreadRatio : sSpreadHz).skip (kBlock - 1);

        const float lowLimit = 20.0f;
        const float highLimit = (float) sampleRate * 0.475f;
        const int total = juce::jmin (kMaxFilters, 1 + pairs * 2);
        numFilters = total;

        for (int i = 0; i < kMaxFilters; ++i)
        {
            auto& c = coeffs[(size_t) i];
            c.active = false;
            if (i >= total)
                continue;

            const int offsetIdx = i - pairs;
            double rawF = multiplicative ? (double) freq * std::pow ((double) spread, (double) offsetIdx)
                                         : (double) freq + (double) offsetIdx * (double) spread;
            rawF += (double) i * 0.0001;

            float f = freqMode == 1 ? wrapFrequency (rawF, lowLimit, highLimit)
                    : freqMode == 2 ? mirrorFrequency (rawF, lowLimit, highLimit)
                                    : (float) rawF;

            if (! (f >= lowLimit && f <= highLimit && std::isfinite (f)))
                continue;

            float weight = 1.0f - damp * ((float) std::abs (offsetIdx) / (float) (pairs + 1));
            if (multiplicative && freqMode == 2 && f > 35.0f && f < 45.0f)
                weight = 0.0f;
            weight = juce::jmax (0.0f, weight);
            if (weight <= 0.001f)
                continue;

            // juce::dsp::IIR::Coefficients::makePeakFilter
            const double A = std::sqrt (juce::jmax (0.0, (double) juce::Decibels::decibelsToGain (gainDb * weight)));
            const double omega = juce::MathConstants<double>::twoPi * f / sampleRate;
            const double alpha = std::sin (omega) / (q * 2.0);
            const double c2 = -2.0 * std::cos (omega);
            const double a0 = 1.0 + alpha / A;
            c.b0 = (float) ((1.0 + alpha * A) / a0);
            c.b1 = (float) (c2 / a0);
            c.b2 = (float) ((1.0 - alpha * A) / a0);
            c.a1 = (float) (c2 / a0);
            c.a2 = (float) ((1.0 - alpha / A) / a0);
            c.active = true;
        }

        const bool specialSafety = multiplicative && (freqMode == 1 || freqMode == 2);
        limit = hardLimiter || specialSafety;
        limitThreshold = specialSafety ? 0.80f : 0.95f;
    }

    struct { int freq, gain, amount, q, damp, spreadMode, spreadHz, spreadRatio, smooth, freqMode, wetOnly, limiter; } idx {};
    juce::LinearSmoothedValue<float> sGain, sFreq, sQ, sDamp, sSpreadHz, sSpreadRatio;
    float currentSmoothingMs { -1.0f };
    std::array<Coeffs, kMaxFilters> coeffs {};
    std::array<std::array<State, kMaxFilters>, 2> state {};
    int numFilters { 0 }, counter { 0 };
    bool wetOnly { false }, limit { true };
    float limitThreshold { 0.95f };
};

AQUANODE_REGISTER_MODULE (CenterCombBankModule, CenterCombBankModule::descriptor)

namespace aquanode::plugins::centercomb
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int bank = b.add ("plugin.centercomb.bank", 0, 0);
        b.copy (bank, original);

        PatchPorts ports;
        ports.name = "CenterComb";
        ports.audioIn = { { bank, "audioIn" } };
        ports.audioOut = { { bank, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
