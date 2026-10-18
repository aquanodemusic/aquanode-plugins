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
                WarpOscModule::buildMorphSets (sets[s], analyse (shapes[s], H));
        }

        // the wave's tables, scaled to a peak of 1 - or by fixedScale, when
        // given; returns the scale used
        static float buildSet (WarpOscModule::TableSet& set, const std::vector<std::complex<float>>& c,
                               float fixedScale = 0.0f)
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
                if (level == 0 && fixedScale > 0.0f)
                    scale = fixedScale;
                else if (level == 0)
                {
                    float peak = 0.0f;
                    for (int k = 0; k < n; ++k)
                        peak = std::max (peak, std::abs (t[(size_t) k]));
                    scale = peak > 1.0e-9f ? 1.0f / peak : 1.0f;
                }
                for (auto& x : t)
                    x *= scale;
            }
            return scale;
        }

        WarpOscModule::MorphSets sets[5];
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
// Random Amp: a fixed field of random levels, one per harmonic and knot.
// Knot 0 is the untouched wave. Each further knot draws a new random spectral
// landscape: smooth hills and valleys a third of an octave apart (so loud
// harmonics come in clusters, like formants), roughened by a random level per
// harmonic. Going up the knots, that landscape is mixed in more strongly
// (fully from about 40 % on), sharpened (a power from 1 to 4, so a few
// clusters stand out over quiet stretches) and lifted towards the top (up to
// 6 dB per octave as far as the 48th harmonic, so the upper clusters shine
// through a saw's falling spectrum). Every knot keeps the plain wave's power.
// Sweeping the amount walks from one landscape to the next. The
// fundamental takes part, but never drops below a third of its level, so the
// note keeps its pitch.
float WarpOscModule::randomAmpGain (int harmonic, int knot)
{
    static const std::vector<float> field = []
    {
        constexpr int H = kTableSize / 2;
        constexpr int nodesPerOctave = 3, octaves = 11;
        std::vector<float> f ((size_t) (kMorphKnots * (H + 1)), 1.0f);
        juce::Random r (0x52414D50);   // "RAMP"
        for (int k = 1; k < kMorphKnots; ++k)
        {
            const float x = (float) k / (float) (kMorphKnots - 1);
            const float depth = juce::jmin (1.0f, x * 2.5f);
            const float p = 1.0f + 3.0f * x;
            const float lift = x;                                       // 0 .. 1 = 0 .. +6 dB / octave

            std::array<float, nodesPerOctave * octaves + 2> hills {};
            for (auto& n : hills)
                n = r.nextFloat();

            std::vector<float> g ((size_t) H + 1, 0.0f);
            double power = 0.0, basePower = 0.0;
            for (int h = 1; h <= H; ++h)
            {
                // the landscape at this harmonic: cosine between its two nodes
                const float pos = std::log2 ((float) h) * (float) nodesPerOctave;
                const int i = juce::jlimit (0, (int) hills.size() - 2, (int) pos);
                const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (pos - (float) i));
                const float hill = hills[(size_t) i] + t * (hills[(size_t) i + 1] - hills[(size_t) i]);
                const float v = 0.75f * hill + 0.25f * r.nextFloat();
                g[(size_t) h] = std::pow (v, p) * std::pow ((float) juce::jmin (h, 48), lift);
                // keep the power of a saw-like spectrum where it was
                const double w = 1.0 / ((double) h * h);
                power += w * g[(size_t) h] * g[(size_t) h];
                basePower += w;
            }
            const float norm = (float) std::sqrt (basePower / juce::jmax (1.0e-12, power));
            for (int h = 1; h <= H; ++h)
            {
                float gain = g[(size_t) h] * norm;
                if (h == 1)
                    gain = juce::jmax (gain, 0.35f);
                f[(size_t) (k * (H + 1) + h)] = (1.0f - depth) + depth * gain;
            }
        }
        return f;
    }();
    constexpr int H = kTableSize / 2;
    if (knot <= 0 || harmonic < 1 || harmonic > H)
        return 1.0f;
    return field[(size_t) (juce::jmin (knot, kMorphKnots - 1) * (H + 1) + harmonic)];
}

std::vector<std::complex<float>> WarpOscModule::applyRandomAmp (const std::vector<std::complex<float>>& c, int knot)
{
    auto out = c;
    for (size_t h = 1; h < out.size(); ++h)
        out[h] *= randomAmpGain ((int) h, knot);
    return out;
}

void WarpOscModule::buildMorphSets (MorphSets& sets, const std::vector<std::complex<float>>& coefs)
{
    auto power = [] (const std::vector<std::complex<float>>& c)
    {
        double e = 0.0;
        for (size_t h = 1; h < c.size(); ++h)
            e += std::norm (c[h]);
        return e;
    };
    for (auto& set : sets)
        if (set == nullptr)
            set = std::make_unique<TableSet>();

    // the plain wave peaks at 1; every knot keeps its power and its scale, so
    // the morph changes the colour, not the loudness
    const double basePower = power (coefs);
    const float scale = BasicTables::buildSet (*sets[0], coefs);
    for (int k = 1; k < kMorphKnots; ++k)
    {
        auto c = applyRandomAmp (coefs, k);
        const double e = power (c);
        if (e > 1.0e-20)
            for (auto& x : c)
                x *= (float) std::sqrt (basePower / e);
        BasicTables::buildSet (*sets[(size_t) k], c, scale);
    }
}

//==============================================================================
WarpOscModule::WarpOscModule()
{
    basicTables();   // built once, shared by every instance
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

    // which two waves, and how far between them
    const MorphSets* A;
    const MorphSets* B;
    float t;
    if (param (pWave) > 0.5f)
    {
        const int buf = activeCustom.load (std::memory_order_acquire);
        A = &custom[buf][0];
        B = &custom[buf][1];
        t = shape;
    }
    else
    {
        const float pos = shape * 4.0f;
        const int i = juce::jlimit (0, 3, (int) pos);
        A = &basicTables().sets[i];
        B = &basicTables().sets[i + 1];
        t = pos - (float) i;
    }

    // the morph: the two knots around the amount, and how far between them
    const float morph = (int) std::lround (param (pMorphType)) == morphRandomAmp
                      ? juce::jlimit (0.0f, 1.0f, param (pMorph) * 0.01f) : 0.0f;
    const float mk = morph * (float) (kMorphKnots - 1);
    const int knot = juce::jlimit (0, kMorphKnots - 2, (int) mk);
    const float tm = mk - (float) knot;
    const bool morphing = morph > 0.0f;

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
    const auto& ta = (*A)[(size_t) knot]->mip[(size_t) level];
    const auto& tb = (*B)[(size_t) knot]->mip[(size_t) level];
    const auto& ta2 = (*A)[(size_t) knot + 1]->mip[(size_t) level];
    const auto& tb2 = (*B)[(size_t) knot + 1]->mip[(size_t) level];
    const auto& sine = basicTables().sets[0][0]->mip[0];

    auto readAt = [&] (double p)
    {
        float x = readTable (ta, p);
        x += t * (readTable (tb, p) - x);
        if (! morphing)
            return x;
        float y = readTable (ta2, p);
        y += t * (readTable (tb2, p) - y);
        return x + tm * (y - x);
    };

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
    buildMorphSets (custom[target][0], customA);
    buildMorphSets (custom[target][1], customB);
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
        "spread in cents, Spread the stereo width, Blend the outer copies' level. Morph Random Amp gives every "
        "harmonic a random level of its own: off at 0 %, wilder and sparser further up - "
        "put an envelope or LFO on Morph Amt to make it talk. Pitch In is semitones/60, FM In is phase "
        "modulation, Shape In moves Shape by In Amt.";
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
        makeRotary ("glide",    "Glide",     0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f),

        makeCombo ("morphType", "Morph", { "None", "Random Amp" }, 1, 3, 3),
        makeRotary ("morph",    "Morph Amt", 0.0f, 100.0f, 0.0f, 3, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (WarpOscModule, warpOscDescriptor)
