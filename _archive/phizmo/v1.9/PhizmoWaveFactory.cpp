/*
    PhizmoWaveFactory.cpp
    ---------------------
    Drop-in wave generation for Phizmo. Adds two ways to fill a wavetable slot
    without needing a pre-made wavetable file:

        generateRandomWavetable() - a Transwave from a seed
        transwavifyFile()         - turn any audio file into a Transwave

    Both write straight into the existing wt[slot] structure at the slot's
    current cycle size, so nothing downstream changes: the same sampleFrameRaw()
    path, the same visualiser, the same preset embedding.

    Generated tables are also rendered out as an ordinary wavetable WAV into
    originalFileData, so the existing preset zip embeds them exactly like a
    loaded file: a shared .phizmo opens on a machine that has never seen the
    wave, and the descriptor stored alongside lets it be re-rolled.
*/

#include "PluginProcessor.h"
#include <random>
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr int kSynthLength = 2048;   // frames are built here, then resampled

    // -----------------------------------------------------------------------
    // Spectrum -> single cycle
    // -----------------------------------------------------------------------
    // Everything in this file describes a wave as a harmonic spectrum and then
    // inverse-FFTs it. Harmonics are capped at cycleSamples/2 - 1 so the stored
    // frame cannot alias against its own cycle length, whatever cycle size the
    // user has dialled in.
    void spectrumToCycle(const std::vector<float>& amp, const std::vector<float>& phase,
        std::vector<float>& out, int cycleSamples)
    {
        static thread_local juce::dsp::FFT fft(11);   // 2048
        std::vector<float> buf((size_t)(2 * kSynthLength), 0.0f);

        const int maxH = juce::jmin((int) amp.size() - 1,
            juce::jmin(kSynthLength / 2 - 1,
                juce::jmax(1, cycleSamples / 2 - 1)));

        for (int h = 1; h <= maxH; ++h)
        {
            const float a = amp[(size_t)h];
            if (a <= 0.0f) continue;
            const float p = (h < (int)phase.size()) ? phase[(size_t)h] : 0.0f;
            const float re = a * std::cos(p);
            const float im = -a * std::sin(p);
            buf[(size_t)(2 * h)] = re;
            buf[(size_t)(2 * h) + 1] = im;
            const int mirror = kSynthLength - h;
            buf[(size_t)(2 * mirror)] = re;
            buf[(size_t)(2 * mirror) + 1] = -im;
        }

        fft.performRealOnlyInverseTransform(buf.data());

        out.resize((size_t)cycleSamples);
        if (cycleSamples == kSynthLength)
        {
            std::copy(buf.begin(), buf.begin() + cycleSamples, out.begin());
        }
        else
        {
            // Safe to interpolate: the content is already limited to
            // cycleSamples/2 harmonics.
            for (int i = 0; i < cycleSamples; ++i)
            {
                const double src = (double)i * kSynthLength / (double)cycleSamples;
                const int i0 = (int)src;
                const int i1 = (i0 + 1) % kSynthLength;
                const float f = (float)(src - i0);
                out[(size_t)i] = buf[(size_t)i0] * (1.0f - f) + buf[(size_t)i1] * f;
            }
        }
    }

    // Constant-RMS rather than constant-peak. A peak-normalised resonant frame
    // is roughly 12 dB quieter than a peak-normalised saw, which makes the
    // level dip audibly as the evolution curve sweeps across the table.
    void normaliseFrame(std::vector<float>& f)
    {
        float peak = 1.0e-9f, sumSq = 0.0f;
        for (float v : f) { peak = juce::jmax(peak, std::abs(v)); sumSq += v * v; }
        const float rms = std::sqrt(sumSq / juce::jmax(1.0f, (float)f.size())) + 1.0e-9f;
        const float g = juce::jmin(0.30f / rms, 0.97f / peak);
        for (auto& v : f) v *= g;
    }

    // -----------------------------------------------------------------------
    // Random Transwave: randomise spectra, not samples
    // -----------------------------------------------------------------------
    // The parameter ranges below were derived by measuring a set of Fizmo
    // wavetables: per-frame spectral tilt, harmonic centroid and how far it
    // travels, odd/even balance, frame-to-frame motion, how monotonic the scan
    // is, spectral peakiness and comb depth. What is reproduced here is the
    // *statistical behaviour* of those categories, not any of their data - no
    // sampled material of any kind is embedded in this plugin.
    //
    // Measured across that set: tilt 0.4-2.4, centroid travel 2-185 harmonics,
    // motion 0.01-1.6, sweep correlation 0.01-1.0, peakiness 10-312, comb
    // 0.42-0.97. Each archetype below occupies a different corner of that space.

    enum class Archetype
    {
        ResonantSweep = 0,  // a broad resonant band travelling far up the series
        Formant,            // two or three vocal formants, steep tilt
        BellStack,          // sparse inharmonic partials
        AnalogSmooth,       // few harmonics, barely moves
        Unrelated,          // unrelated frames, very high motion
        Hollow,             // odd harmonics only, square/pulse family
        CombGroove,         // deep comb filtering, rhythmic frame stepping
        OrganDraw,          // a handful of drawbar-spaced partials
        NumArchetypes,
        Auto = NumArchetypes
    };

    struct Range { float lo, hi; };

    struct ArchSpec
    {
        Range tilt, even, top;
        float combProb;
        Range combPeriod, combDepth;
        int   peaksMin, peaksMax;
        Range peakOct, peakWidth, peakGain;
        Range frames;
        int   keysMin, keysMax;
        Range motion, jitter;
        int   sparse;        // 0 = dense, else keep only N partials
        bool  monotonic;     // peak centre travels linearly across all frames
    };

    const ArchSpec kArchetypes[(int)Archetype::NumArchetypes] = {
        // tilt        even        top          cProb cPeriod     cDepth
        // peaks  peakOct      peakWidth    peakGain     frames        keys   motion       jitter      sparse mono
        { {0.2f,0.9f},{0.4f,1.0f},{220.f,512.f}, 0.85f,{2.f,9.f}, {0.5f,0.9f},
          1,2, {0.5f,8.0f}, {1.2f,2.6f}, {6.f,20.f},  {96.f,200.f},  3,5,  {0.85f,1.0f},{0.0f,0.2f}, 0,  true  },
        { {2.0f,2.5f},{0.6f,1.0f},{24.f,90.f},   0.20f,{3.f,12.f},{0.2f,0.5f},
          2,3, {1.5f,5.5f}, {0.4f,1.0f}, {4.f,12.f},  {96.f,140.f},  4,7,  {0.5f,0.9f}, {0.0f,0.3f}, 0,  true  },
        { {0.4f,1.2f},{0.3f,1.0f},{40.f,200.f},  0.10f,{2.f,7.f}, {0.3f,0.7f},
          2,3, {2.0f,7.0f}, {0.5f,1.6f}, {3.f,10.f},  {120.f,200.f}, 5,9,  {0.4f,0.8f}, {0.3f,0.9f}, 14, false },
        { {1.4f,2.0f},{0.0f,1.0f},{6.f,20.f},    0.15f,{2.f,6.f}, {0.2f,0.5f},
          1,1, {0.0f,3.0f}, {0.6f,1.4f}, {2.f,6.f},   {96.f,140.f},  3,4,  {0.15f,0.45f},{0.0f,0.2f},0,  false },
        { {0.7f,2.0f},{0.2f,1.0f},{30.f,300.f},  0.60f,{2.f,14.f},{0.3f,0.9f},
          1,3, {0.0f,7.0f}, {0.25f,1.2f},{4.f,18.f},  {96.f,200.f},  30,60,{1.0f,1.0f}, {0.2f,0.8f}, 0,  false },
        { {1.4f,2.2f},{0.0f,0.05f},{20.f,160.f}, 0.50f,{2.f,10.f},{0.3f,0.8f},
          1,2, {0.5f,6.0f}, {0.3f,1.0f}, {5.f,16.f},  {96.f,140.f},  3,5,  {0.6f,1.0f}, {0.0f,0.3f}, 0,  true  },
        { {1.5f,2.3f},{0.4f,1.0f},{30.f,200.f},  1.00f,{2.f,6.f}, {0.6f,0.95f},
          1,2, {1.0f,6.5f}, {0.3f,0.8f}, {4.f,14.f},  {120.f,200.f}, 8,16, {0.8f,1.0f}, {0.1f,0.5f}, 0,  false },
        { {0.6f,1.2f},{0.5f,1.0f},{8.f,24.f},    0.00f,{0.f,0.f}, {0.f,0.f},
          1,2, {0.0f,4.0f}, {0.5f,1.2f}, {2.f,7.f},   {96.f,140.f},  3,5,  {0.2f,0.5f}, {0.0f,0.15f},9,  false },
    };

    const char* archetypeName(int i)
    {
        static const char* n[] = { "Resonant Sweep","Formant","Bell Stack","Analog Smooth",
                                   "Unrelated","Hollow","Comb Groove","Organ Drawbar" };
        return (i >= 0 && i < (int)Archetype::NumArchetypes) ? n[i] : "Auto";
    }

    float pick(juce::Random& rng, Range r) { return juce::jmap(rng.nextFloat(), r.lo, r.hi); }

    struct Peak { float centreOct, width, gain; };

    struct Keyframe
    {
        float tilt = 1.0f, evenGain = 1.0f, combPeriod = 0.0f, combDepth = 0.0f;
        float topHarmonic = 256.0f;
        std::array<Peak, 3> peaks{};
        int numPeaks = 0;
    };

    Keyframe makeKeyframe(juce::Random& rng, const ArchSpec& a)
    {
        Keyframe k;
        k.tilt = pick(rng, a.tilt);
        k.evenGain = pick(rng, a.even);
        k.topHarmonic = pick(rng, a.top);

        if (rng.nextFloat() < a.combProb)
        {
            k.combPeriod = juce::jmax(2.0f, pick(rng, a.combPeriod));
            k.combDepth = pick(rng, a.combDepth);
        }

        k.numPeaks = juce::jlimit(0, 3, a.peaksMin + rng.nextInt(a.peaksMax - a.peaksMin + 1));
        for (int i = 0; i < k.numPeaks; ++i)
            k.peaks[(size_t)i] = { pick(rng, a.peakOct), pick(rng, a.peakWidth), pick(rng, a.peakGain) };
        return k;
    }

    // peakShift >= 0 overrides the first peak's centre, which is how the
    // monotonic archetypes get a resonance that travels steadily across the
    // whole table rather than wandering between keyframes.
    void evaluateKeyframe(const Keyframe& k, std::vector<float>& out, int maxH,
        float peakShift = -1.0f, int sparse = 0, juce::Random* rng = nullptr)
    {
        out.assign((size_t)maxH + 1, 0.0f);
        for (int h = 1; h <= maxH; ++h)
        {
            float a = std::pow((float)h, -k.tilt);
            if ((h & 1) == 0) a *= k.evenGain;

            const float lh = std::log2((float)h);
            for (int i = 0; i < k.numPeaks; ++i)
            {
                const auto& pk = k.peaks[(size_t)i];
                const float c = (i == 0 && peakShift >= 0.0f) ? peakShift : pk.centreOct;
                const float d = (lh - c) / pk.width;
                a *= 1.0f + pk.gain * std::exp(-d * d);
            }

            if (k.combDepth > 0.0f)
                a *= juce::jmax(0.0f, 1.0f + k.combDepth
                    * std::cos(juce::MathConstants<float>::twoPi * (float)h / k.combPeriod));

            if ((float)h > k.topHarmonic)
                a *= std::exp(-((float)h - k.topHarmonic) / (k.topHarmonic * 0.3f + 4.0f));

            out[(size_t)h] = a;
        }

        // Sparse archetypes keep only a handful of partials, weighted towards
        // the loud ones. That is what separates a bell or an organ from a pad.
        if (sparse > 0 && rng != nullptr)
        {
            double total = 0.0;
            for (int h = 1; h <= maxH; ++h) total += out[(size_t)h];
            if (total > 1.0e-9)
            {
                std::vector<float> kept((size_t)maxH + 1, 0.0f);
                for (int n = 0; n < sparse; ++n)
                {
                    double target = rng->nextDouble() * total, acc = 0.0;
                    for (int h = 1; h <= maxH; ++h)
                    {
                        acc += out[(size_t)h];
                        if (acc >= target) { kept[(size_t)h] = out[(size_t)h]; break; }
                    }
                }
                out.swap(kept);
            }
        }
    }

    float smoothstep(float t)
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        return t * t * (3.0f - 2.0f * t);
    }

    // -----------------------------------------------------------------------
    // Pitch detection (normalised square difference, McLeod-style)
    // -----------------------------------------------------------------------
    float detectPitch(const float* data, int numSamples, double sampleRate, float& clarityOut)
    {
        clarityOut = 0.0f;
        if (numSamples < 1024) return 0.0f;

        const int minLag = juce::jmax(16, (int)(sampleRate / 1600.0));
        const int maxLag = juce::jmin(numSamples / 2, (int)(sampleRate / 25.0));
        if (maxLag <= minLag + 4) return 0.0f;

        const int W = juce::jmin(numSamples - maxLag, 8192);

        std::vector<float> nsdf((size_t)(maxLag + 1), 0.0f);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acf = 0.0, m = 0.0;
            for (int i = 0; i < W; ++i)
            {
                const double a = data[i], b = data[i + lag];
                acf += a * b;
                m += a * a + b * b;
            }
            nsdf[(size_t)lag] = (m > 1.0e-12) ? (float)(2.0 * acf / m) : 0.0f;
        }

        float globalMax = 0.0f;
        for (int lag = minLag; lag <= maxLag; ++lag)
            globalMax = juce::jmax(globalMax, nsdf[(size_t)lag]);
        if (globalMax <= 0.0f) return 0.0f;

        // First peak above 85% of the maximum, which dodges the octave-down
        // errors that plain autocorrelation is prone to.
        const float threshold = globalMax * 0.85f;
        int best = -1;
        for (int lag = minLag + 1; lag < maxLag; ++lag)
            if (nsdf[(size_t)lag] > threshold
                && nsdf[(size_t)lag] >= nsdf[(size_t)(lag - 1)]
                && nsdf[(size_t)lag] >= nsdf[(size_t)(lag + 1)])
            {
                best = lag; break;
            }

        if (best < 0) return 0.0f;

        const float y0 = nsdf[(size_t)(best - 1)];
        const float y1 = nsdf[(size_t)best];
        const float y2 = nsdf[(size_t)(best + 1)];
        const float den = y0 - 2.0f * y1 + y2;
        const float delta = (std::abs(den) > 1.0e-9f) ? 0.5f * (y0 - y2) / den : 0.0f;

        clarityOut = juce::jlimit(0.0f, 1.0f, y1);
        return (float)(sampleRate / ((double)best + (double)delta));
    }

    // Harmonic amplitudes and phases at one point in a pitched recording.
    void analyseHarmonics(const std::vector<float>& x, int centre, double sampleRate,
        float f0, int maxHarmonics,
        std::vector<float>& amp, std::vector<float>& phase)
    {
        const double period = sampleRate / juce::jmax(1.0f, f0);
        const int window = juce::jlimit(64, 8192, (int)(period * 6.0));
        const int start = juce::jlimit(0, juce::jmax(0, (int)x.size() - window),
            centre - window / 2);

        const int maxH = juce::jlimit(1, maxHarmonics, (int)(sampleRate * 0.45 / f0));

        amp.assign((size_t)maxH + 1, 0.0f);
        phase.assign((size_t)maxH + 1, 0.0f);

        std::vector<float> w((size_t)window);
        double wSum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            w[(size_t)i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                * (float)i / (float)(window - 1));
            wSum += w[(size_t)i];
        }

        for (int h = 1; h <= maxH; ++h)
        {
            const double omega = juce::MathConstants<double>::twoPi * (double)h * f0 / sampleRate;
            double re = 0.0, im = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const double s = (double)x[(size_t)(start + i)] * w[(size_t)i];
                re += s * std::cos(omega * i);
                im -= s * std::sin(omega * i);
            }
            amp[(size_t)h] = (float)(2.0 * std::sqrt(re * re + im * im) / wSum);
            phase[(size_t)h] = (float)std::atan2(im, re);
        }

        // Lock every frame to the fundamental's phase. Without this the frames
        // drift out of alignment, and frame interpolation thins the sound out
        // instead of morphing it.
        const float p1 = phase[1];
        for (int h = 1; h <= maxH; ++h)
            phase[(size_t)h] = std::remainder(phase[(size_t)h] - (float)h * p1,
                juce::MathConstants<float>::twoPi);
    }

    // Slice-and-resample: for material with no usable fundamental.
    void analyseSlice(const std::vector<float>& x, int start, int length,
        std::vector<float>& cycleOut, int cycleSamples)
    {
        cycleOut.assign((size_t)cycleSamples, 0.0f);
        for (int i = 0; i < cycleSamples; ++i)
        {
            const double src = start + (double)i * (double)length / (double)cycleSamples;
            const int i0 = juce::jlimit(0, (int)x.size() - 2, (int)src);
            const float f = (float)(src - i0);
            cycleOut[(size_t)i] = x[(size_t)i0] * (1.0f - f) + x[(size_t)(i0 + 1)] * f;
        }

        // Taper the ends into each other, so forcing the slice to be periodic
        // does not inject a click at the wrap point.
        const int fade = juce::jmax(2, cycleSamples / 16);
        for (int i = 0; i < fade; ++i)
        {
            const float t = (float)i / (float)fade;
            const float a = cycleOut[(size_t)i];
            const float b = cycleOut[(size_t)(cycleSamples - 1 - i)];
            cycleOut[(size_t)i] = a * t + b * (1.0f - t);
            cycleOut[(size_t)(cycleSamples - 1 - i)] = b * t + a * (1.0f - t);
        }
    }
}

//==============================================================================
bool PhizmoAudioProcessor::generateRandomWavetable(int slot, juce::int64 seed, int numFrames,
    int cycleSamples, juce::String& statusOut,
    int archetypeOverride)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) { statusOut = "Bad slot"; return false; }

    cycleSamples = juce::jlimit(16, 4096, cycleSamples);
    if (seed == 0) seed = juce::Random::getSystemRandom().nextInt64();

    juce::Random rng(seed);

    // Archetype: taken from the parameter, or rolled if it is set to Auto.
    int archIdx = archetypeOverride;
    if (archIdx < 0)
    {
        if (auto* p = apvts.getRawParameterValue("genArchetype"))
            archIdx = (int)p->load() - 1;                   // 0 in the choice list = Auto
    }
    if (archIdx < 0 || archIdx >= (int)Archetype::NumArchetypes)
        archIdx = rng.nextInt((int)Archetype::NumArchetypes);

    const ArchSpec& A = kArchetypes[archIdx];

    // numFrames <= 0 means "let the archetype decide" - the measured tables run
    // from roughly 70 to 210 frames depending on the kind of wave.
    if (numFrames <= 0) numFrames = juce::roundToInt(pick(rng, A.frames));
    numFrames = juce::jlimit(4, 256, numFrames);

    const int numKeys = juce::jlimit(2, 64, A.keysMin + rng.nextInt(A.keysMax - A.keysMin + 1));
    const float motion = pick(rng, A.motion);
    const float jitter = pick(rng, A.jitter);

    const int maxH = juce::jmax(1, juce::jmin(kSynthLength / 2 - 1, cycleSamples / 2 - 1));

    std::vector<Keyframe> keys((size_t)numKeys);
    keys[0] = makeKeyframe(rng, A);
    for (int i = 1; i < numKeys; ++i)
    {
        keys[(size_t)i] = makeKeyframe(rng, A);
        // 'motion' is how far later keyframes are allowed to drift from the
        // first. Low motion gives a slowly evolving pad; high motion gives the
        // unrelated-frames character of the groove and random tables.
        auto& k = keys[(size_t)i];
        const auto& b = keys[0];
        k.tilt = juce::jmap(motion, b.tilt, k.tilt);
        k.evenGain = juce::jmap(motion, b.evenGain, k.evenGain);
        k.topHarmonic = juce::jmap(motion, b.topHarmonic, k.topHarmonic);
    }

    // Monotonic archetypes travel their first resonance steadily from one end
    // of the table to the other. A minimum span of 2.5 octaves keeps a "sweep"
    // from rolling a start and end that sit almost on top of each other.
    float octStart = 0.0f, octEnd = 0.0f;
    if (A.monotonic)
    {
        octStart = pick(rng, A.peakOct);
        octEnd = pick(rng, A.peakOct);
        if (std::abs(octEnd - octStart) < 2.5f)
        {
            const float dir = (octEnd >= octStart) ? 1.0f : -1.0f;
            octEnd = juce::jlimit(A.peakOct.lo, A.peakOct.hi, octStart + dir * 2.5f);
            if (std::abs(octEnd - octStart) < 2.5f)
                octEnd = juce::jlimit(A.peakOct.lo, A.peakOct.hi, octStart - dir * 2.5f);
        }
    }

    // One phase set for the whole table. Constant phases keep adjacent frames
    // coherent, which is what lets FRAME INTERP morph rather than cancel.
    std::vector<float> phase((size_t)maxH + 1, 0.0f);
    for (int h = 1; h <= maxH; ++h)
        phase[(size_t)h] = juce::MathConstants<float>::pi * (float)(h * h) / 1024.0f
        + jitter * rng.nextFloat() * juce::MathConstants<float>::twoPi;

    // Non-monotonic archetypes can evaluate their keyframes once up front.
    std::vector<std::vector<float>> keySpectra;
    if (!A.monotonic)
    {
        keySpectra.resize((size_t)numKeys);
        for (int i = 0; i < numKeys; ++i)
        {
            juce::Random sparseRng(seed + 977 * i);
            evaluateKeyframe(keys[(size_t)i], keySpectra[(size_t)i], maxH, -1.0f,
                A.sparse, &sparseRng);
        }
    }

    std::vector<std::vector<float>> frames((size_t)numFrames);
    std::vector<float> amp ((size_t)maxH + 1, 0.0f), sa, sb;

    for (int f = 0; f < numFrames; ++f)
    {
        const float t = (numFrames == 1) ? 0.0f
            : (float)f / (float)(numFrames - 1) * (float)(numKeys - 1);
        const int kA = juce::jlimit(0, numKeys - 1, (int)t);
        const int kB = juce::jmin(numKeys - 1, kA + 1);
        const float mix = smoothstep(t - (float)kA);

        const std::vector<float>* pA;
        const std::vector<float>* pB;

        if (A.monotonic)
        {
            const float ps = octStart + (octEnd - octStart)
                * (float)f / (float)juce::jmax(1, numFrames - 1);
            juce::Random sparseRng(seed + 977 * kA);
            evaluateKeyframe(keys[(size_t)kA], sa, maxH, ps, A.sparse, &sparseRng);
            juce::Random sparseRngB(seed + 977 * kB);
            evaluateKeyframe(keys[(size_t)kB], sb, maxH, ps, A.sparse, &sparseRngB);
            pA = &sa; pB = &sb;
        }
        else
        {
            pA = &keySpectra[(size_t)kA];
            pB = &keySpectra[(size_t)kB];
        }

        // Interpolating in the log-amplitude domain makes a resonant peak slide
        // across the spectrum. Linear interpolation instead fades one peak out
        // while another fades in, which sounds like a crossfade rather than a
        // Transwave.
        for (int h = 1; h <= maxH; ++h)
        {
            const float a = std::log((*pA)[(size_t)h] + 1.0e-7f);
            const float b = std::log((*pB)[(size_t)h] + 1.0e-7f);
            amp[(size_t)h] = std::exp(a + mix * (b - a));
        }

        spectrumToCycle(amp, phase, frames[(size_t)f], cycleSamples);
        normaliseFrame(frames[(size_t)f]);
    }

    const juce::String label = juce::String(archetypeName(archIdx)).substring(0, 10)
        + " " + juce::String::toHexString(seed)
        .getLastCharacters(4).toUpperCase();

    statusOut = juce::String(archetypeName(archIdx))
        + ": " + juce::String(numFrames) + " frames x " + juce::String(cycleSamples)
        + ", " + juce::String(numKeys) + " keyframes, seed "
        + juce::String::toHexString(seed).getLastCharacters(8).toUpperCase();

    if (!installGeneratedFrames(slot, std::move(frames), cycleSamples, label,
        1, seed, numFrames, false, statusOut))
        return false;

    {
        juce::ScopedLock l(wt[slot].lock);
        wt[slot].genArchetype = archIdx;
    }
    return true;
}

//==============================================================================
bool PhizmoAudioProcessor::transwavifyFile(int slot, const juce::File& file, int numFrames,
    int cycleSamples, bool forceUnpitched,
    juce::String& statusOut)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) { statusOut = "Bad slot"; return false; }

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
    if (reader == nullptr)
    {
        statusOut = "Could not read " + file.getFileName();
        return false;
    }

    const int maxSamples = (int)juce::jmin((juce::int64)(reader->sampleRate * 30.0),
        reader->lengthInSamples);
    if (maxSamples < 512) { statusOut = "Sample too short to analyse"; return false; }

    juce::AudioBuffer<float> buf((int)juce::jmin(2u, reader->numChannels), maxSamples);
    reader->read(&buf, 0, maxSamples, 0, true, true);

    // Mono-sum, remove DC, normalise.
    std::vector<float> x((size_t)maxSamples, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        const float* p = buf.getReadPointer(c);
        for (int i = 0; i < maxSamples; ++i) x[(size_t)i] += p[i];
    }
    double mean = 0.0;
    for (float v : x) mean += v;
    mean /= (double)maxSamples;
    float peak = 1.0e-9f;
    for (auto& v : x) {
        v = v / (float)buf.getNumChannels() - (float)(mean / buf.getNumChannels());
        peak = juce::jmax(peak, std::abs(v));
    }
    for (auto& v : x) v /= peak;

    // Trim silence.
    int first = 0, last = maxSamples - 1;
    while (first < last && std::abs(x[(size_t)first]) < 0.01f) ++first;
    while (last > first && std::abs(x[(size_t)last]) < 0.01f) --last;
    const int usableStart = first;
    const int usableLen = juce::jmax(512, last - first + 1);

    numFrames = juce::jlimit(4, 256, numFrames);
    cycleSamples = juce::jlimit(16, 4096, cycleSamples);

    float clarity = 0.0f, f0 = 0.0f;
    if (!forceUnpitched)
        f0 = detectPitch(x.data() + usableStart, usableLen, reader->sampleRate, clarity);

    const bool pitched = (!forceUnpitched) && clarity > 0.55f && f0 > 20.0f && f0 < 2000.0f;

    std::vector<std::vector<float>> frames((size_t)numFrames);

    if (pitched)
    {
        // Read harmonic amplitudes and phases straight out of the recording at
        // N points along its length, then rebuild each as one cycle.
        const int maxH = juce::jmax(1, juce::jmin(512, cycleSamples / 2 - 1));
        std::vector<float> amp, phase;
        for (int k = 0; k < numFrames; ++k)
        {
            const int centre = usableStart + (int)(((double)k + 0.5) / numFrames * usableLen);
            analyseHarmonics(x, centre, reader->sampleRate, f0, maxH, amp, phase);
            spectrumToCycle(amp, phase, frames[(size_t)k], cycleSamples);
            normaliseFrame(frames[(size_t)k]);
        }

        const int note = juce::roundToInt(69.0 + 12.0 * std::log2(f0 / 440.0));
        statusOut = "Pitched " + juce::String(f0, 1) + " Hz ("
            + juce::MidiMessage::getMidiNoteName(note, true, true, 3)
            + "), clarity " + juce::String(clarity, 2) + " - "
            + juce::String(numFrames) + " frames x " + juce::String(cycleSamples);
    }
    else
    {
        // No usable fundamental (drums, noise, texture): slice the file into N
        // chunks and resample each to one cycle. Closer to wavescanning than to
        // resynthesis, and usually the more interesting of the two on
        // percussive material.
        const int sliceLen = juce::jmax(64, usableLen / numFrames);
        for (int k = 0; k < numFrames; ++k)
        {
            analyseSlice(x, usableStart + k * sliceLen, sliceLen,
                frames[(size_t)k], cycleSamples);
            normaliseFrame(frames[(size_t)k]);
        }

        statusOut = forceUnpitched
            ? "Sliced: " + juce::String(numFrames) + " frames x " + juce::String(cycleSamples)
            : "No stable pitch (clarity " + juce::String(clarity, 2) + ") - sliced into "
            + juce::String(numFrames) + " frames";
    }

    return installGeneratedFrames(slot, std::move(frames), cycleSamples,
        file.getFileNameWithoutExtension(),
        2, 0, numFrames, !pitched, statusOut);
}

//==============================================================================
bool PhizmoAudioProcessor::installGeneratedFrames(int slot,
    std::vector<std::vector<float>>&& frames,
    int cycleSamples,
    const juce::String& displayName,
    int origin, juce::int64 seed, int genFrames,
    bool genUnpitched, const juce::String& status)
{
    if (frames.empty()) return false;

    // Write the generated table out as a plain wavetable WAV and keep the bytes
    // in originalFileData. That way the existing preset zip embeds it exactly
    // like a loaded file, and a shared .phizmo still opens on a machine that has
    // never seen this wave.
    juce::MemoryBlock wavBytes;
    {
        juce::AudioBuffer<float> flat(1, (int)frames.size() * cycleSamples);
        float* dst = flat.getWritePointer(0);
        for (size_t f = 0; f < frames.size(); ++f)
            std::copy(frames[f].begin(), frames[f].end(), dst + f * (size_t)cycleSamples);

        juce::WavAudioFormat wav;
        auto stream = std::make_unique<juce::MemoryOutputStream>(wavBytes, false);
#if JUCE_MSVC
#pragma warning (push)
#pragma warning (disable: 4996)
#endif
        if (auto* writer = wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0))
        {
            stream.release();
            writer->writeFromAudioSampleBuffer(flat, 0, flat.getNumSamples());
            delete writer;
        }
#if JUCE_MSVC
#pragma warning (pop)
#endif
    }

    // Match the 16-bit quantisation the file-loading path applies, so a
    // generated table and a saved-then-reloaded one are bit-identical.
    auto q16 = [](float s) { return juce::jlimit(-1.0f, 1.0f, std::round(s * 32767.0f) / 32767.0f); };
    for (auto& f : frames)
        for (auto& s : f) s = q16(s);

    {
        juce::ScopedLock sl(wt[slot].lock);
        wt[slot].numFrames = (int)frames.size();
        wt[slot].frames = std::move(frames);
        wt[slot].cycleSamples = cycleSamples;
        wt[slot].loaded = true;
        wt[slot].name = displayName;
        wt[slot].filePath = {};                          // generated, not on disk
        wt[slot].fileName = displayName + ".wav";
        wt[slot].originalFileData = std::move(wavBytes);

        wt[slot].origin = origin;
        wt[slot].genSeed = seed;
        wt[slot].genFrames = genFrames;
        wt[slot].genUnpitched = genUnpitched;
        wt[slot].genStatus = status;
    }

    // Silence first, then rebuild: no voice can be reading the old levels.
    for (auto& v : voices) { v.active = false; v.envStage = PhizmoVoice::Env::Idle; }
    buildMips(slot);
    setCycleSizeParam(slot, cycleSamples);
    return true;
}

//==============================================================================
// Re-run whatever produced this slot. Random slots get a fresh seed; sampled
// ones are re-analysed from the embedded bytes, so it works even if the
// original file has since moved.
bool PhizmoAudioProcessor::rerollSlot(int slot, juce::String& statusOut)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) { statusOut = "Bad slot"; return false; }

    int origin, genFrames, cycleSamples;
    bool unpitched;
    juce::String name;
    juce::MemoryBlock bytes;
    {
        juce::ScopedLock l(wt[slot].lock);
        if (!wt[slot].loaded) { statusOut = "Slot is empty"; return false; }
        origin = wt[slot].origin;
        genFrames = wt[slot].genFrames;
        unpitched = wt[slot].genUnpitched;
        cycleSamples = wt[slot].cycleSamples;
        name = wt[slot].name;
        bytes = wt[slot].originalFileData;
    }

    if (origin == 1)
        return generateRandomWavetable(slot, juce::Random::getSystemRandom().nextInt64(),
            genFrames, cycleSamples, statusOut);

    if (origin == 2)
    {
        // Re-analyse from the embedded bytes, flipping the slice/pitched choice
        // so Re-roll on a Transwavified slot gives you the other reading of it.
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("phizmo_reroll.wav");
        tmp.replaceWithData(bytes.getData(), bytes.getSize());
        const bool ok = transwavifyFile(slot, tmp, genFrames, cycleSamples, !unpitched, statusOut);
        tmp.deleteFile();
        return ok;
    }

    statusOut = "Nothing to re-roll: " + name + " was loaded from a file";
    return false;
}

//==============================================================================
// Export a slot's rendered wavetable audio as an ordinary .wav on disk. Every
// loaded slot - file-loaded, random recipe, or transwavified - keeps a
// rendered WAV in originalFileData (installGeneratedFrames renders it for
// generated slots; loadWavetableFromReader keeps the source bytes for file
// slots). The preset zip chooses to skip embedding it for recipe slots to
// stay small, but the bytes exist here regardless, so export works the same
// way for all three origins.
bool PhizmoAudioProcessor::exportSlotAsWav(int slot, const juce::File& dest, juce::String& statusOut) const
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) { statusOut = "Bad slot"; return false; }

    juce::MemoryBlock bytes;
    juce::String name;
    {
        juce::ScopedLock l(wt[slot].lock);
        if (!wt[slot].loaded) { statusOut = "Slot is empty"; return false; }
        bytes = wt[slot].originalFileData;
        name = wt[slot].name;
    }

    if (bytes.getSize() == 0) { statusOut = "Nothing to export for " + name; return false; }

    if (!dest.replaceWithData(bytes.getData(), bytes.getSize()))
    {
        statusOut = "Could not write " + dest.getFileName();
        return false;
    }

    statusOut = "Exported " + name + " to " + dest.getFileName();
    return true;
}