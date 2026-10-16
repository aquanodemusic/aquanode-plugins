#include "WarpOscModule.h"

using namespace aquanode;

namespace
{
    constexpr int kAnalysisOrder = 14;                     // naive shapes analysed at 16384 points
    constexpr int kTableOrder = 11;                        // 2048-point tables

    // a naive (aliasing) single cycle, analysed into harmonic coefficients
    std::vector<std::complex<float>> analyse (const std::function<float (double)>& shape, int harmonics)
    {
        const int n = 1 << kAnalysisOrder;
        juce::dsp::FFT fft (kAnalysisOrder);
        std::vector<juce::dsp::Complex<float>> in ((size_t) n), out ((size_t) n);
        for (int k = 0; k < n; ++k)
            in[(size_t) k] = { shape ((double) k / n), 0.0f };
        fft.perform (in.data(), out.data(), false);
        std::vector<std::complex<float>> c ((size_t) harmonics + 1);
        for (int h = 1; h <= harmonics; ++h)
            c[(size_t) h] = out[(size_t) h] / (float) n;
        return c;
    }

    struct BasicTables
    {
        BasicTables()
        {
            const int H = WarpOscModule::kTableSize / 2 - 1;
            const std::function<float (double)> shapes[5] =
            {
                [] (double p) { return (float) std::sin (juce::MathConstants<double>::twoPi * p); },
                [] (double p) { return (float) (p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0)); },
                [] (double p) { return (float) (p < 0.5 ? 2.0 * p : 2.0 * p - 2.0); },
                [] (double p) { return p < 0.5 ? 1.0f : -1.0f; },
                [] (double p) { return p < 0.25 ? 1.0f : -1.0f; }
            };
            for (int s = 0; s < 5; ++s)
            {
                sets[s] = std::make_unique<WarpOscModule::TableSet>();
                buildSet (*sets[s], analyse (shapes[s], H));
            }
        }

        static void buildSet (WarpOscModule::TableSet& set, const std::vector<std::complex<float>>& c)
        {
            juce::dsp::FFT fft (kTableOrder);
            const int n = WarpOscModule::kTableSize;
            std::vector<juce::dsp::Complex<float>> in ((size_t) n), out ((size_t) n);
            float scale = 1.0f;
            for (int level = 0; level < WarpOscModule::kNumMips; ++level)
            {
                const int maxH = std::min ((int) c.size() - 1, (n / 2) >> level);
                std::fill (in.begin(), in.end(), juce::dsp::Complex<float> { 0.0f, 0.0f });
                for (int h = 1; h <= maxH && h < n / 2; ++h)
                {
                    in[(size_t) h] = c[(size_t) h];
                    in[(size_t) (n - h)] = std::conj (c[(size_t) h]);
                }
                fft.perform (in.data(), out.data(), true);
                auto& t = set.mip[(size_t) level];
                for (int k = 0; k < n; ++k)
                    t[(size_t) k] = out[(size_t) k].real();
                t[(size_t) n] = t[0];

                // one gain for every level (from the fullest), so switching
                // tables never changes the loudness
                if (level == 0)
                {
                    float peak = 0.0f;
                    for (int k = 0; k < n; ++k)
                        peak = std::max (peak, std::abs (t[(size_t) k]));
                    scale = peak > 1.0e-9f ? 1.0f / peak : 1.0f;
                }
                for (auto& x : t)
                    x *= scale;
            }
        }

        std::unique_ptr<WarpOscModule::TableSet> sets[5];
    };

    const BasicTables& basicTables()
    {
        static const BasicTables tables;
        return tables;
    }

    inline float readTable (const std::array<float, WarpOscModule::kTableSize + 1>& t, double p) noexcept
    {
        const double x = p * WarpOscModule::kTableSize;
        const int i = juce::jlimit (0, WarpOscModule::kTableSize - 1, (int) x);
        const float f = (float) (x - i);
        return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
    }

    inline double wrap01 (double p) noexcept { return p - std::floor (p); }

    std::vector<std::complex<float>> sawCoefficients()
    {
        std::vector<std::complex<float>> c (WarpOscModule::kCustomHarmonics + 1);
        for (int h = 1; h <= WarpOscModule::kCustomHarmonics; ++h)
            c[(size_t) h] = { 0.0f, -1.0f / (juce::MathConstants<float>::pi * (float) h) * (h % 2 == 0 ? -1.0f : 1.0f) };
        return c;
    }
}

void WarpOscModule::buildTableSet (TableSet& set, const std::vector<std::complex<float>>& coefs, bool)
{
    BasicTables::buildSet (set, coefs);
}

//==============================================================================
WarpOscModule::WarpOscModule()
{
    basicTables();   // built once, shared by every instance
    for (auto& buf : custom)
        for (auto& f : buf)
            f = std::make_unique<TableSet>();
    setCustomSpectra (sawCoefficients(), sawCoefficients());
}

void WarpOscModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void WarpOscModule::reset()
{
    pool.resetAll();
    glide.resetAll();
    gate.resetAll();
    for (auto& v : phase)
        for (auto& p : v)
            p = 0.0;
}

void WarpOscModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    gate.resetVoice (v);
}

void WarpOscModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    gate.noteOn (v);

    if (! retrigger || isMonoVoice())
    {
        // a lone voice starts on the cycle (kicks, plucks); a stack starts
        // scattered so it blooms instead of phasing
        const int n = juce::jlimit (1, kMaxUnison, (int) std::lround (param (pUnison)));
        for (int u = 0; u < kMaxUnison; ++u)
            phase[v][u] = n == 1 ? 0.0 : rng.nextDouble();
    }
}

void WarpOscModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gate.noteOff (v);
}

//==============================================================================
void WarpOscModule::updateUnisonLayout()
{
    const int n = juce::jlimit (1, kMaxUnison, (int) std::lround (param (pUnison)));
    const float detune = param (pDetune), spread = param (pSpread) * 0.01f, blend = param (pBlend) * 0.01f;
    if (n == layoutCount && juce::exactlyEqual (detune, layoutDetune) && juce::exactlyEqual (spread, layoutSpread)
        && juce::exactlyEqual (blend, layoutBlend))
        return;
    layoutCount = n;
    layoutDetune = detune;
    layoutSpread = spread;
    layoutBlend = blend;

    float g[kMaxUnison] {}, pan[kMaxUnison] {};
    float sumSq = 0.0f;
    for (int u = 0; u < n; ++u)
    {
        const float x = n == 1 ? 0.0f : (float) u / (float) (n - 1) * 2.0f - 1.0f;   // -1..1
        ratio[u] = std::pow (2.0, (double) (x * detune * 0.5f) / 1200.0);
        // the middle voice (or the middle two) is the centre; the rest blend in
        const bool centre = n <= 2 || std::abs (x) < 1.0f / (float) (n - 1) + 1.0e-4f;
        g[u] = centre ? 1.0f : blend;
        // alternate sides so the low and high copies don't all sit left
        pan[u] = (u % 2 == 0 ? 1.0f : -1.0f) * std::abs (x) * spread;
        sumSq += g[u] * g[u];
    }
    const float norm = 1.0f / std::sqrt (juce::jmax (1.0e-6f, sumSq));
    for (int u = 0; u < n; ++u)
    {
        const float th = (pan[u] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        gainL[u] = g[u] * norm * juce::MathConstants<float>::sqrt2 * std::cos (th);
        gainR[u] = g[u] * norm * juce::MathConstants<float>::sqrt2 * std::sin (th);
    }
}

void WarpOscModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    outputs[0][0] = outputs[0][1] = 0.0f;
    if (pool.isSilent (v))
        return;

    updateUnisonLayout();
    const int n = layoutCount;

    const double semis = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                       + std::round (param (pTranspose)) + param (pFine) * 0.01
                       + (double) inputs[0][0] * 60.0 + pitchBendSemitones();
    const double f0 = 440.0 * std::exp2 ((semis - 69.0) / 12.0);
    const double invSr = 1.0 / sampleRate;
    const double fm = inputs[1][0];

    const float shape = juce::jlimit (0.0f, 100.0f, param (pShape) + inputs[2][0] * param (pShapeMod)) * 0.01f;
    const float a = juce::jlimit (0.0f, 1.0f, param (pWarp) * 0.01f);
    const int mode = juce::jlimit (0, 7, (int) std::lround (param (pWarpMode)));

    // which two tables, and how far between them
    const TableSet* A;
    const TableSet* B;
    float t;
    if (param (pWave) > 0.5f)
    {
        const int buf = activeCustom.load (std::memory_order_acquire);
        A = custom[buf][0].get();
        B = custom[buf][1].get();
        t = shape;
    }
    else
    {
        const float pos = shape * 4.0f;
        const int i = juce::jlimit (0, 3, (int) pos);
        A = basicTables().sets[i].get();
        B = basicTables().sets[i + 1].get();
        t = pos - (float) i;
    }

    // how much the warp speeds the read up, for the table choice
    double speed = 1.0, syncRatio = 1.0, width = 1.0, squeezeW = 0.5, steps = 1024.0;
    switch (mode)
    {
        case warpSync: case warpFormant: syncRatio = 1.0 + 7.0 * a; speed = syncRatio; break;
        case warpBend:     speed = 1.0 + 3.0 * a; break;
        case warpSqueeze:  squeezeW = 0.5 - 0.45 * a; speed = 0.5 / squeezeW; break;
        case warpPulse:    width = 1.0 - 0.9 * a; speed = 1.0 / width; break;
        case warpQuantize: steps = std::round (std::exp2 (10.0 - 9.0 * a)); break;
        case warpMirror:   speed = 1.0 + a; break;
        default: break;
    }
    const double maxH = 0.45 * sampleRate / juce::jmax (1.0, f0 * speed * (1.0 + layoutDetune / 1200.0));
    const int level = maxH >= 1024.0 ? 0 : juce::jlimit (0, kNumMips - 1, (int) std::ceil (std::log2 (1024.0 / juce::jmax (1.0, maxH))));
    const auto& ta = A->mip[(size_t) level];
    const auto& tb = B->mip[(size_t) level];
    const auto& sine = basicTables().sets[0]->mip[0];

    auto readAt = [&] (double p) { const float x = readTable (ta, p); return x + t * (readTable (tb, p) - x); };

    float outL = 0.0f, outR = 0.0f;
    for (int u = 0; u < n; ++u)
    {
        double& ph = phase[v][u];
        ph = wrap01 (ph + f0 * ratio[u] * invSr);
        const double p = juce::exactlyEqual (fm, 0.0) ? ph : wrap01 (ph + fm);

        float s;
        switch (mode)
        {
            case warpSync:    s = readAt (wrap01 (p * syncRatio)); break;
            case warpBend:    { const double p2 = p * p; s = readAt (p + a * (p2 * p2 - p)); break; }
            case warpSqueeze: s = readAt (p < squeezeW ? 0.5 * p / squeezeW : 0.5 + 0.5 * (p - squeezeW) / (1.0 - squeezeW)); break;
            case warpPulse:   s = p < width ? readAt (p / width) : 0.0f; break;
            case warpFormant:
            {
                // a synced copy under a raised-cosine window: no jump at the reset
                const float win = 0.5f - 0.5f * readTable (sine, wrap01 (p + 0.25));
                s = readAt (wrap01 (p * syncRatio)) * (1.0f + juce::jmin (1.0f, 4.0f * a) * (win - 1.0f));
                break;
            }
            case warpQuantize: s = readAt (std::floor (p * steps) / steps); break;
            case warpMirror:   { const double m = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p; s = readAt (wrap01 (p + a * (m - p))); break; }
            default:           s = readAt (p); break;
        }
        outL += s * gainL[u];
        outR += s * gainR[u];
    }

    const float amp = gate.next (v, isInputConnected (3), inputs[3][0], sampleRate) * param (pVolume) * voiceGain;
    outputs[0][0] = outL * amp;
    outputs[0][1] = outR * amp;
}

//==============================================================================
void WarpOscModule::setCustomSpectra (const std::vector<std::complex<float>>& frameA,
                                      const std::vector<std::complex<float>>& frameB)
{
    auto fit = [] (const std::vector<std::complex<float>>& in)
    {
        std::vector<std::complex<float>> c ((size_t) kCustomHarmonics + 1);
        for (size_t h = 1; h < c.size() && h < in.size(); ++h)
            c[h] = in[h];
        return c;
    };
    customA = fit (frameA);
    customB = frameB.size() > 1 ? fit (frameB) : customA;

    const int target = 1 - activeCustom.load();
    buildTableSet (*custom[target][0], customA, true);
    buildTableSet (*custom[target][1], customB, true);
    activeCustom.store (target, std::memory_order_release);
}

juce::String WarpOscModule::saveCustomState() const
{
    auto frame = [] (const std::vector<std::complex<float>>& c)
    {
        juce::String s;
        // trailing silent harmonics are left out
        int last = 0;
        for (int h = 1; h < (int) c.size(); ++h)
            if (std::abs (c[(size_t) h]) > 1.0e-6f) last = h;
        for (int h = 1; h <= last; ++h)
        {
            if (h > 1) s << ",";
            s << juce::String (c[(size_t) h].real(), 6) << "," << juce::String (c[(size_t) h].imag(), 6);
        }
        return s;
    };
    return "warp1;A:" + frame (customA) + ";B:" + frame (customB);
}

void WarpOscModule::loadCustomState (const juce::String& state)
{
    if (! state.startsWith ("warp1"))
        return;
    auto parse = [] (const juce::String& part)
    {
        std::vector<std::complex<float>> c (1);
        juce::StringArray nums;
        nums.addTokens (part, ",", {});
        for (int i = 0; i + 1 < nums.size(); i += 2)
            c.push_back ({ nums[i].getFloatValue(), nums[i + 1].getFloatValue() });
        return c;
    };
    const auto a = parse (state.fromFirstOccurrenceOf (";A:", false, false).upToFirstOccurrenceOf (";", false, false));
    const auto b = parse (state.fromFirstOccurrenceOf (";B:", false, false).upToFirstOccurrenceOf (";", false, false));
    if (a.size() > 1)
        setCustomSpectra (a, b.size() > 1 ? b : a);
}

//==============================================================================
static ModuleDescriptor warpOscDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.warp";
    d.displayName = "Warp Osc";
    d.description =
        "A clean, band-limited morphing oscillator with phase warps and up to 16 unison copies. Shape morphs "
        "Sine > Triangle > Saw > Square > Pulse (or, on Custom, between the patch's two stored spectra); "
        "Warp bends the phase: Sync, Bend, Squeeze, Pulse, Formant, Quantize, Mirror. Detune is the unison "
        "spread in cents, Spread the stereo width, Blend the outer copies' level. Pitch In is semitones/60, "
        "FM In is phase modulation, Shape In moves Shape by In Amt.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 2;
    d.sockets = {
        modIn    ("pitchIn",   "Pitch In"),
        modIn    ("fmIn",      "FM In"),
        modIn    ("shapeIn",   "Shape In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",    "Volume",    0.0f, 1.0f, 0.7f, 0),
        makeRotary ("shape",     "Shape",     0.0f, 100.0f, 50.0f, 0, "%", false, 0.1f),
        makeRotary ("warp",      "Warp",      0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("transpose", "Transpose", -48.0f, 48.0f, 0.0f, 0, "st", false, 1.0f),
        makeRotary ("fine",      "Fine",      -100.0f, 100.0f, 0.0f, 0, "ct"),

        makeCombo ("wave",     "Wave", { "Shapes", "Custom" }, 0, 1, 2),
        makeCombo ("warpMode", "Warp Mode", { "Warp Off", "Sync", "Bend", "Squeeze", "Pulse", "Formant", "Quantize", "Mirror" }, 0, 1, 2),
        makeRotary ("unison",  "Unison", 1.0f, (float) WarpOscModule::kMaxUnison, 1.0f, 1, {}, false, 1.0f),

        makeRotary ("detune",   "Detune",    0.0f, 200.0f, 20.0f, 2, "ct"),
        makeRotary ("spread",   "Spread",    0.0f, 100.0f, 80.0f, 2, "%", false, 1.0f),
        makeRotary ("blend",    "Blend",     0.0f, 100.0f, 80.0f, 2, "%", false, 1.0f),
        makeRotary ("shapeMod", "In Amt",    -100.0f, 100.0f, 0.0f, 2, "%", false, 1.0f),
        makeRotary ("voices",   "Voices",    1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",     0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (WarpOscModule, warpOscDescriptor)
