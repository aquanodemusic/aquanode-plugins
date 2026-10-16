#include "TranswaveModule.h"

#include <random>

using namespace aquanode;

//==============================================================================
// The transwave generator, taken verbatim from Phizmo (PhizmoWaveFactory.cpp,
// the "Random Transwave" part). Only the output stage differs: Phizmo writes
// one table at the slot's cycle size, this module renders each frame at
// several band-limited sizes for clean playback across the keyboard.
//==============================================================================
namespace twgen
{
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

        // The four below are the "dense" family: they keep real energy out to
        // harmonic 200-700 rather than dying above 50, which is what separates
        // a table that stays interesting at the top of the keyboard from one
        // that turns into a sine. See the note above kArchetypes.
        MetallicSweep,      // wide resonance travelling far, gentle skirt
        InharmonicBell,     // stretched partial series, jagged, gong/chime
        SpectralGrit,       // dense and rough, energy right to the top
        VocalMorph,         // four formants moving independently, breathy top
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

        // ---- upper-register shaping -----------------------------------------
        // The four fields below are what let a table stay complex above
        // harmonic 50. They are properties of the whole table rather than of a
        // single keyframe: a wave does not change how rough or how stretched it
        // is halfway through, so interpolating them between keyframes would
        // only smear the character out.
        //
        // skirtDbOct  rolloff above `top`, in dB per octave. Measured on the
        //             reference set at -7 to -15; the original steep
        //             exponential cutoff was the main reason generated tables
        //             sounded thin next to them. 0 = use the old exponential.
        // rough       per-harmonic level scatter in dB, fixed for the table so
        //             it reads as timbre rather than noise. 0 = smooth.
        // roughHi     how much that scatter grows with harmonic number.
        // stretch     inharmonic partial stretching, Fletcher-style: partial n
        //             sits at n*(1+stretch)^log2(n). 0 = perfectly harmonic.
        //             0.0016 is roughly a struck-bar/chime amount.
        // plateau     a shallow noise-like floor under the spectrum, as a
        //             fraction of the fundamental. Gives breath and air at the
        //             top without needing the tilt to go flat. 0 = off.
        float skirtDbOct;
        float rough, roughHi;
        float stretch;
        float plateau;
    };

    const ArchSpec kArchetypes[(int)Archetype::NumArchetypes] = {
        // tilt        even        top          cProb cPeriod     cDepth
        // peaks  peakOct      peakWidth    peakGain     frames        keys   motion       jitter      sparse mono
        // skirt rough roughHi stretch plateau
        { {0.2f,0.9f},{0.4f,1.0f},{220.f,512.f}, 0.85f,{2.f,9.f}, {0.5f,0.9f},
          1,2, {0.5f,8.0f}, {1.2f,2.6f}, {6.f,20.f},  {96.f,200.f},  3,5,  {0.85f,1.0f},{0.0f,0.2f}, 0,  true,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {2.0f,2.5f},{0.6f,1.0f},{24.f,90.f},   0.20f,{3.f,12.f},{0.2f,0.5f},
          2,3, {1.5f,5.5f}, {0.4f,1.0f}, {4.f,12.f},  {96.f,140.f},  4,7,  {0.5f,0.9f}, {0.0f,0.3f}, 0,  true,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {0.4f,1.2f},{0.3f,1.0f},{40.f,200.f},  0.10f,{2.f,7.f}, {0.3f,0.7f},
          2,3, {2.0f,7.0f}, {0.5f,1.6f}, {3.f,10.f},  {120.f,200.f}, 5,9,  {0.4f,0.8f}, {0.3f,0.9f}, 14, false,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {1.4f,2.0f},{0.0f,1.0f},{6.f,20.f},    0.15f,{2.f,6.f}, {0.2f,0.5f},
          1,1, {0.0f,3.0f}, {0.6f,1.4f}, {2.f,6.f},   {96.f,140.f},  3,4,  {0.15f,0.45f},{0.0f,0.2f},0,  false,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {0.7f,2.0f},{0.2f,1.0f},{30.f,300.f},  0.60f,{2.f,14.f},{0.3f,0.9f},
          1,3, {0.0f,7.0f}, {0.25f,1.2f},{4.f,18.f},  {96.f,200.f},  30,60,{1.0f,1.0f}, {0.2f,0.8f}, 0,  false,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {1.4f,2.2f},{0.0f,0.05f},{20.f,160.f}, 0.50f,{2.f,10.f},{0.3f,0.8f},
          1,2, {0.5f,6.0f}, {0.3f,1.0f}, {5.f,16.f},  {96.f,140.f},  3,5,  {0.6f,1.0f}, {0.0f,0.3f}, 0,  true,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {1.5f,2.3f},{0.4f,1.0f},{30.f,200.f},  1.00f,{2.f,6.f}, {0.6f,0.95f},
          1,2, {1.0f,6.5f}, {0.3f,0.8f}, {4.f,14.f},  {120.f,200.f}, 8,16, {0.8f,1.0f}, {0.1f,0.5f}, 0,  false,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },
        { {0.6f,1.2f},{0.5f,1.0f},{8.f,24.f},    0.00f,{0.f,0.f}, {0.f,0.f},
          1,2, {0.0f,4.0f}, {0.5f,1.2f}, {2.f,7.f},   {96.f,140.f},  3,5,  {0.2f,0.5f}, {0.0f,0.15f},9,  false,
          0.0f,  0.0f, 0.0f, 0.0f,    0.0f   },

        // ---- the dense family ------------------------------------------------
        // MetallicSweep: the Rez Loops / Color Wheel corner. A wide resonance
        // crossing five or more octaves with only a -11 dB/oct skirt above it,
        // so the harmonics it has already passed are still audible behind it.
        { {1.0f,1.6f},{0.5f,1.0f},{420.f,900.f}, 0.10f,{3.f,11.f},{0.2f,0.5f},
          2,2, {2.0f,8.5f}, {1.0f,2.2f}, {8.f,26.f},  {120.f,209.f}, 4,6,  {0.7f,1.0f}, {0.0f,0.25f},0,  true,
          -11.0f, 3.5f, 2.0f, 0.0f,    0.0f   },

        // InharmonicBell: gong and tubular territory. The stretch pushes the
        // partial series off the harmonic grid, which is what stops this
        // reading as "a bright pad" and starts it reading as struck metal.
        { {1.7f,2.3f},{0.3f,1.0f},{260.f,620.f}, 0.15f,{2.f,8.f}, {0.3f,0.7f},
          3,3, {2.5f,7.0f}, {0.7f,1.8f}, {4.f,14.f},  {96.f,200.f},  6,8,  {0.5f,0.9f}, {0.2f,0.7f}, 0,  false,
          -9.0f,  9.0f, 3.0f, 0.0016f, 0.0f   },

        // SpectralGrit: dense, jagged and loud to the very top - the Brainiac /
        // TransGruv 10 corner. Roughness does the work here; the plateau keeps
        // the gaps between the rough peaks from going silent.
        { {1.5f,2.1f},{0.4f,1.0f},{420.f,860.f}, 0.30f,{2.f,9.f}, {0.3f,0.8f},
          2,2, {1.5f,7.5f}, {0.5f,1.5f}, {5.f,18.f},  {120.f,209.f}, 8,12, {0.7f,1.0f}, {0.2f,0.6f}, 0,  false,
          -7.0f, 11.0f, 1.2f, 0.0f,    0.006f },

        // VocalMorph: four formants that drift independently, over a breathy
        // plateau. Narrower peaks and a lower top than the rest of this family -
        // vowels are about where the peaks sit, not about raw brightness.
        { {1.2f,1.7f},{0.6f,1.0f},{200.f,420.f}, 0.10f,{3.f,12.f},{0.2f,0.5f},
          3,3, {1.5f,6.0f}, {0.35f,0.9f},{6.f,20.f},  {96.f,174.f},  6,8,  {0.5f,0.9f}, {0.0f,0.3f}, 0,  false,
          -13.0f, 2.5f, 1.5f, 0.0f,    0.010f },
    };

    const char* archetypeName(int i)
    {
        static const char* n[] = { "Resonant Sweep","Formant","Bell Stack","Analog Smooth",
                                   "Unrelated","Hollow","Comb Groove","Organ Drawbar",
                                   "Metallic Sweep","Inharmonic Bell","Spectral Grit","Vocal Morph" };
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
    // `shape` carries the table-level upper-register settings and `roughVec` the
    // pre-drawn per-harmonic gains; both may be null, in which case this behaves
    // exactly as it did before they existed.
    void evaluateKeyframe(const Keyframe& k, std::vector<float>& out, int maxH,
        float peakShift = -1.0f, int sparse = 0, juce::Random* rng = nullptr,
        const ArchSpec* shape = nullptr, const std::vector<float>* roughVec = nullptr)
    {
        out.assign((size_t)maxH + 1, 0.0f);

        const float stretch = (shape != nullptr) ? shape->stretch : 0.0f;
        const float skirt   = (shape != nullptr) ? shape->skirtDbOct : 0.0f;
        const float plateau = (shape != nullptr) ? shape->plateau : 0.0f;

        for (int h = 1; h <= maxH; ++h)
        {
            // Partial position. With stretch = 0 this is just h, and everything
            // below behaves exactly as it always has. With stretch > 0 the
            // series walks progressively sharp - Fletcher's piano/bar formula -
            // so the partials stop lining up with any fundamental and the ear
            // hears struck metal rather than a pitched wave.
            const float hs = (stretch > 0.0f)
                ? (float)h * std::pow(1.0f + stretch, std::log2((float)h))
                : (float)h;

            float a = std::pow(hs, -k.tilt);
            if ((h & 1) == 0) a *= k.evenGain;

            const float lh = std::log2(hs);
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

            // A shallow floor under the whole spectrum. Without it, roughness
            // and comb notches punch holes that go fully silent, which reads as
            // gaps rather than as texture; with it the quiet harmonics still
            // carry a little air. Falls at a gentle 3 dB/oct of its own.
            if (plateau > 0.0f)
                a = juce::jmax(a, plateau * std::pow(hs, -0.55f));

            // Rolloff above the top harmonic. The reference tables sit at -7 to
            // -15 dB/oct here, which is far gentler than the exponential the
            // other archetypes use - that steepness is exactly what made
            // generated tables collapse to a sine in the upper registers.
            if (skirt < 0.0f)
            {
                const float over = std::log2(juce::jmax(1.0f, (float)h / k.topHarmonic));
                if (over > 0.0f)
                    a *= std::pow(10.0f, skirt * over / 20.0f);
            }
            else if ((float)h > k.topHarmonic)
            {
                a *= std::exp(-((float)h - k.topHarmonic) / (k.topHarmonic * 0.3f + 4.0f));
            }

            // Per-harmonic level scatter, drawn once for the whole table and
            // reused by every frame. Because it is the same scatter everywhere,
            // it survives frame interpolation and reads as the fixed character
            // of this wave; a fresh draw per frame would just sound like noise.
            // Clamped asymmetrically: a harmonic may drop a long way but may
            // only rise a little, so the scatter roughens the spectrum without
            // dragging its overall tilt flat.
            if (roughVec != nullptr && h < (int)roughVec->size())
                a *= (*roughVec)[(size_t)h];

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

}

    constexpr int kNumArchetypes = (int) Archetype::NumArchetypes;

    // the gain normaliseFrame() would apply, so every band-limited copy of a
    // frame gets exactly the gain its full-bandwidth original got
    float frameGain (const std::vector<float>& f)
    {
        float peak = 1.0e-9f, sumSq = 0.0f;
        for (float v : f) { peak = juce::jmax (peak, std::abs (v)); sumSq += v * v; }
        const float rms = std::sqrt (sumSq / juce::jmax (1.0f, (float) f.size())) + 1.0e-9f;
        return juce::jmin (0.30f / rms, 0.97f / peak);
    }

    // Phizmo's generateRandomWavetable(), with its output stage replaced.
    std::shared_ptr<TranswaveModule::Table> generate (juce::uint32 seed, int archetypeSetting, int framesSetting)
    {
        auto table = std::make_shared<TranswaveModule::Table>();
        table->seed = seed;
        table->archetypeSetting = archetypeSetting;
        table->framesSetting = framesSetting;

        juce::Random rng ((juce::int64) seed);   // exactly as Phizmo seeds it

        int archIdx = archetypeSetting - 1;                    // 0 in the list = Auto
        if (archIdx < 0 || archIdx >= kNumArchetypes)
            archIdx = rng.nextInt (kNumArchetypes);
        table->archetype = archIdx;
        const ArchSpec& A = kArchetypes[archIdx];

        static const int frameCounts[] = { 0, 16, 32, 64, 128, 256 };
        int numFrames = frameCounts[juce::jlimit (0, 5, framesSetting)];
        if (numFrames <= 0) numFrames = juce::roundToInt (pick (rng, A.frames));
        numFrames = juce::jlimit (4, 256, numFrames);

        const int numKeys = juce::jlimit (2, 64, A.keysMin + rng.nextInt (A.keysMax - A.keysMin + 1));
        const float motion = pick (rng, A.motion);
        const float jitter = pick (rng, A.jitter);

        const int cycleSamples = TranswaveModule::kBaseLength;
        const int maxH = juce::jmax (1, juce::jmin (kSynthLength / 2 - 1, cycleSamples / 2 - 1));

        std::vector<Keyframe> keys ((size_t) numKeys);
        keys[0] = makeKeyframe (rng, A);
        for (int i = 1; i < numKeys; ++i)
        {
            keys[(size_t) i] = makeKeyframe (rng, A);
            auto& k = keys[(size_t) i];
            const auto& b = keys[0];
            k.tilt = juce::jmap (motion, b.tilt, k.tilt);
            k.evenGain = juce::jmap (motion, b.evenGain, k.evenGain);
            k.topHarmonic = juce::jmap (motion, b.topHarmonic, k.topHarmonic);
        }

        float octStart = 0.0f, octEnd = 0.0f;
        if (A.monotonic)
        {
            octStart = pick (rng, A.peakOct);
            octEnd = pick (rng, A.peakOct);
            if (std::abs (octEnd - octStart) < 2.5f)
            {
                const float dir = (octEnd >= octStart) ? 1.0f : -1.0f;
                octEnd = juce::jlimit (A.peakOct.lo, A.peakOct.hi, octStart + dir * 2.5f);
                if (std::abs (octEnd - octStart) < 2.5f)
                    octEnd = juce::jlimit (A.peakOct.lo, A.peakOct.hi, octStart - dir * 2.5f);
            }
        }

        std::vector<float> roughVec;
        if (A.rough > 0.0f)
        {
            juce::Random roughRng ((juce::int64) seed ^ 0x5EED6047);
            roughVec.assign ((size_t) maxH + 1, 1.0f);
            for (int h = 1; h <= maxH; ++h)
            {
                const float u1 = juce::jmax (1.0e-7f, roughRng.nextFloat());
                const float u2 = roughRng.nextFloat();
                const float g = std::sqrt (-2.0f * std::log (u1))
                              * std::cos (juce::MathConstants<float>::twoPi * u2);
                const float widen = 1.0f + A.roughHi * std::log2 ((float) h) / 10.0f;
                const float dB = juce::jlimit (-22.0f, 10.0f, g * A.rough * widen);
                roughVec[(size_t) h] = std::pow (10.0f, dB / 20.0f);
            }
        }
        const std::vector<float>* rvp = roughVec.empty() ? nullptr : &roughVec;

        std::vector<float> phase ((size_t) maxH + 1, 0.0f);
        for (int h = 1; h <= maxH; ++h)
            phase[(size_t) h] = juce::MathConstants<float>::pi * (float) (h * h) / 1024.0f
                              + jitter * rng.nextFloat() * juce::MathConstants<float>::twoPi;

        std::vector<std::vector<float>> keySpectra;
        if (! A.monotonic)
        {
            keySpectra.resize ((size_t) numKeys);
            for (int i = 0; i < numKeys; ++i)
            {
                juce::Random sparseRng ((juce::int64) seed + 977 * i);
                evaluateKeyframe (keys[(size_t) i], keySpectra[(size_t) i], maxH, -1.0f,
                                  A.sparse, &sparseRng, &A, rvp);
            }
        }

        table->numFrames = numFrames;
        table->frames.resize ((size_t) numFrames);
        std::vector<float> amp ((size_t) maxH + 1, 0.0f), sa, sb;

        for (int f = 0; f < numFrames; ++f)
        {
            const float t = (numFrames == 1) ? 0.0f
                : (float) f / (float) (numFrames - 1) * (float) (numKeys - 1);
            const int kA = juce::jlimit (0, numKeys - 1, (int) t);
            const int kB = juce::jmin (numKeys - 1, kA + 1);
            const float mix = smoothstep (t - (float) kA);

            const std::vector<float>* pA;
            const std::vector<float>* pB;
            if (A.monotonic)
            {
                const float ps = octStart + (octEnd - octStart)
                               * (float) f / (float) juce::jmax (1, numFrames - 1);
                juce::Random sparseRng ((juce::int64) seed + 977 * kA);
                evaluateKeyframe (keys[(size_t) kA], sa, maxH, ps, A.sparse, &sparseRng, &A, rvp);
                juce::Random sparseRngB ((juce::int64) seed + 977 * kB);
                evaluateKeyframe (keys[(size_t) kB], sb, maxH, ps, A.sparse, &sparseRngB, &A, rvp);
                pA = &sa; pB = &sb;
            }
            else
            {
                pA = &keySpectra[(size_t) kA];
                pB = &keySpectra[(size_t) kB];
            }

            for (int h = 1; h <= maxH; ++h)
            {
                const float a = std::log ((*pA)[(size_t) h] + 1.0e-7f);
                const float b = std::log ((*pB)[(size_t) h] + 1.0e-7f);
                amp[(size_t) h] = std::exp (a + mix * (b - a));
            }

            // full bandwidth first; its normalising gain is shared by all copies
            auto& mips = table->frames[(size_t) f];
            spectrumToCycle (amp, phase, mips[0], cycleSamples);
            const float g = frameGain (mips[0]);
            for (auto& v : mips[0]) v *= g;

            for (int m = 1; m < TranswaveModule::kNumMips; ++m)
            {
                spectrumToCycle (amp, phase, mips[(size_t) m], cycleSamples >> m);   // fewer harmonics each
                for (auto& v : mips[(size_t) m]) v *= g;
            }
        }
        return table;
    }
} // namespace twgen

//==============================================================================
// rebuilding: knob changes are coalesced into one rebuild on the message thread
//==============================================================================
struct TranswaveModule::Rebuilder : public juce::AsyncUpdater
{
    explicit Rebuilder (TranswaveModule& m) : owner (m) {}
    void handleAsyncUpdate() override { owner.regenerateIfNeeded(); }
    TranswaveModule& owner;
};

TranswaveModule::TranswaveModule()
{
    rebuilder = std::make_unique<Rebuilder> (*this);
    seed = 0x5A17u;   // a fixed default, so a fresh module always starts on the same wave

    // build the default table straight away (Auto archetype, Auto frames - the
    // descriptor defaults), so the module sounds even before any UI exists
    published = twgen::generate (seed, 0, 0);
}

TranswaveModule::~TranswaveModule()
{
    rebuilder->cancelPendingUpdate();
}

const juce::StringArray& TranswaveModule::archetypeChoices()
{
    static const juce::StringArray c = []
    {
        juce::StringArray a { "Auto" };
        for (int i = 0; i < twgen::kNumArchetypes; ++i)
            a.add (twgen::archetypeName (i));
        return a;
    }();
    return c;
}

const juce::StringArray& TranswaveModule::frameChoices()
{
    static const juce::StringArray c { "Auto", "16", "32", "64", "128", "256" };
    return c;
}

std::shared_ptr<const TranswaveModule::Table> TranswaveModule::currentTable() const
{
    const juce::SpinLock::ScopedLockType sl (tableLock);
    return published;
}

void TranswaveModule::regenerateIfNeeded()
{
    const int arch = (int) std::lround (getParameter ("archetype"));
    const int frames = (int) std::lround (getParameter ("frames"));

    if (auto cur = currentTable())
        if (cur->seed == seed && cur->archetypeSetting == arch && cur->framesSetting == frames)
            return;

    std::shared_ptr<const Table> fresh = twgen::generate (seed, arch, frames);
    std::shared_ptr<const Table> old;
    {
        const juce::SpinLock::ScopedLockType sl (tableLock);
        old = std::move (published);
        published = std::move (fresh);
    }

    // freed here once the audio thread has let go, never on the audio thread
    retired.push_back (std::move (old));
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                       [] (const std::shared_ptr<const Table>& t) { return t == nullptr || t.use_count() == 1; }),
                   retired.end());
}

void TranswaveModule::rollNewSeed()
{
    juce::uint32 s = 0;
    while (s == 0)
        s = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    seed = s;
    regenerateIfNeeded();
}

void TranswaveModule::setParameter (const juce::String& id, float value)
{
    SynthModule::setParameter (id, value);
    if (id == "archetype" || id == "frames")
        rebuilder->triggerAsyncUpdate();
}

void TranswaveModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "roll")
        rollNewSeed();
}

juce::String TranswaveModule::saveCustomState() const
{
    return "TW1 " + juce::String ((juce::int64) seed);
}

void TranswaveModule::loadCustomState (const juce::String& state)
{
    auto t = juce::StringArray::fromTokens (state, " ", {});
    if (t.size() >= 2 && t[0] == "TW1")
    {
        const auto s = (juce::uint32) t[1].getLargeIntValue();
        if (s != 0)
            seed = s;
    }
    regenerateIfNeeded();
}

//==============================================================================
// playback (audio thread)
//==============================================================================
void TranswaveModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void TranswaveModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    gate.resetVoice (v);
    phase[v] = 0.0;
    scan[v] = 0.0;
    scanDir[v] = 1;
}

void TranswaveModule::blockStart()
{
    {
        const juce::SpinLock::ScopedLockType sl (tableLock);
        if (audioTable != published)
            audioTable = published;
    }

    // No voice drew the display during the last block: show where a note
    // would start - the Position knob plus whatever cables are turning it
    // (a Curve CV keeps sweeping the picture with nothing playing). Pos In
    // and the self-scan only exist inside a sounding voice.
    if (! uiVoiceShown)
        uiPosition.store (juce::jlimit (0.0f, 1.0f, param (pPosition) * 0.01f), std::memory_order_relaxed);
    uiVoiceShown = false;
}

void TranswaveModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    gate.noteOn (v);
    if (! retrigger)
    {
        phase[v] = 0.0;
        scan[v] = 0.0;
        scanDir[v] = 1;
    }
}

void TranswaveModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gate.noteOff (v);
}

float TranswaveModule::readTable (const Table& t, float position01, double phase01, double freqHz) const
{
    if (t.numFrames <= 0)
        return 0.0f;

    // the brightest copy whose top harmonic still fits under ~0.45 x fs
    int mip = 0;
    while (mip < kNumMips - 1
           && (double) ((kBaseLength >> mip) / 2 - 1) * freqHz > sampleRate * 0.45)
        ++mip;

    const float pos = juce::jlimit (0.0f, 1.0f, position01) * (float) (t.numFrames - 1);
    const int f0 = (int) pos;
    const int f1 = juce::jmin (t.numFrames - 1, f0 + 1);
    const float ff = pos - (float) f0;

    auto read = [mip, phase01] (const std::vector<float>& cyc)
    {
        const int len = (int) cyc.size();
        const double x = phase01 * len;
        const int i0 = (int) x % len;
        const int i1 = (i0 + 1) % len;
        const float fr = (float) (x - std::floor (x));
        juce::ignoreUnused (mip);
        return cyc[(size_t) i0] + (cyc[(size_t) i1] - cyc[(size_t) i0]) * fr;
    };

    const float a = read (t.frames[(size_t) f0][(size_t) mip]);
    const float b = read (t.frames[(size_t) f1][(size_t) mip]);
    return a + (b - a) * ff;
}

void TranswaveModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v) || audioTable == nullptr)
    {
        outputs[0] = { 0.0f, 0.0f };
        return;
    }

    const double freq = midiNoteToHz ((double) (glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                                                + pitchBendSemitones()));

    // the self-scan: one full pass every Scan Time seconds
    const int mode = (int) std::lround (param (pScanMode));
    if (mode != sOff)
    {
        const double step = 1.0 / (juce::jmax (0.01, (double) param (pScanTime)) * sampleRate);
        if (mode == sPingPong)
        {
            scan[v] += step * scanDir[v];
            if (scan[v] >= 1.0) { scan[v] = 2.0 - scan[v]; scanDir[v] = -1; }
            if (scan[v] <= 0.0) { scan[v] = -scan[v];      scanDir[v] =  1; }
        }
        else if (mode == sOnce)
        {
            scan[v] = juce::jmin (1.0, scan[v] + step);
        }
        else
        {
            scan[v] += step;
            scan[v] -= std::floor (scan[v]);
        }
    }

    double travel = 0.0;
    switch (mode)
    {
        case sForward:  travel = scan[v];        break;
        case sBackward: travel = -scan[v];       break;
        case sPingPong:
        case sOnce:     travel = scan[v];        break;
        default:        break;
    }

    double pos = param (pPosition) * 0.01 + travel + inputs[0][0] * param (pPosDepth) * 0.01;
    if (mode == sForward || mode == sBackward)
        pos -= std::floor (pos);                 // looping scans wrap round the table
    const float pos01 = (float) juce::jlimit (0.0, 1.0, pos);
    if (v == 0 || gate.isOn (v) || ! uiVoiceShown)
    {
        uiPosition.store (pos01, std::memory_order_relaxed);
        uiVoiceShown = true;
    }

    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    const float y = readTable (*audioTable, pos01, phase[v], freq);
    const float amp = gate.next (v, isInputConnected (1), inputs[1][0], sampleRate);
    const float out = y * amp * param (pVolume) * voiceGain;
    outputs[0] = { out, out };
}

//==============================================================================
// display: the frame being played, its place in the table, archetype + seed
//==============================================================================
class TranswaveDisplay : public juce::Component, private juce::Timer
{
public:
    explicit TranswaveDisplay (TranswaveModule& m) : module (&m) { startTimerHz (20); }

    void paint (juce::Graphics& g) override
    {
        auto* tw = dynamic_cast<TranswaveModule*> (module.get());
        auto box = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (box, 4.0f);
        if (tw == nullptr)
            return;

        auto table = tw->currentTable();
        auto area = box.reduced (6.0f, 4.0f);
        auto caption = area.removeFromTop (13.0f);
        auto strip = area.removeFromBottom (6.0f);

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        if (table != nullptr)
        {
            g.drawText (TranswaveModule::archetypeChoices()[table->archetype + 1]
                            + "  " + juce::String (table->numFrames) + " frames",
                        caption, juce::Justification::centredLeft, false);
            g.drawText ("seed " + juce::String::toHexString ((juce::int64) table->seed).toUpperCase(),
                        caption, juce::Justification::centredRight, false);
        }
        else
        {
            g.drawText ("building...", caption, juce::Justification::centredLeft, false);
            return;
        }

        // the current frame
        const float pos = tw->getUiPosition();
        const int frame = juce::jlimit (0, table->numFrames - 1, juce::roundToInt (pos * (float) (table->numFrames - 1)));
        const auto& cyc = table->frames[(size_t) frame][0];
        juce::Path p;
        const int n = (int) area.getWidth();
        for (int i = 0; i <= n; ++i)
        {
            const float s = cyc[(size_t) juce::jlimit (0, (int) cyc.size() - 1, i * (int) cyc.size() / juce::jmax (1, n))];
            const float x = area.getX() + (float) i;
            const float y = area.getCentreY() - s * area.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xff4a86d9).brighter (0.6f));
        g.strokePath (p, juce::PathStrokeType (1.3f));

        // where in the table we are
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (strip, 2.0f);
        g.setColour (juce::Colour (0xff00ffff));
        g.fillRoundedRectangle (strip.withWidth (juce::jmax (3.0f, strip.getWidth() * pos)), 2.0f);
    }

private:
    void timerCallback() override { repaint(); }
    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> TranswaveModule::createExtraContentComponent()
{
    regenerateIfNeeded();   // a module added from the sidebar shows its wave at once
    return std::make_unique<TranswaveDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor transwaveDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.transwave";
    d.displayName = "Transwave";
    d.description =
        "A wavetable oscillator whose table is generated, using Phizmo's transwave generator: "
        "pick an archetype (Resonant Sweep, Formant, Bell Stack, Vocal Morph...) and Roll new "
        "seeds until one speaks. Position sets where a note starts in the table, Scan sweeps "
        "through it by itself, and Pos In adds modulation on top - a Curve CV or ADSR there is "
        "the classic transwave move. Band-limited, so bright tables stay clean up high.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 19;
    d.sockets = {
        modIn    ("posIn",     "Pos In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",    "Volume",    0.0f, 1.0f, 0.8f, 0),
        makeCombo  ("archetype", "Archetype", TranswaveModule::archetypeChoices(), 0, 0, 3),
        makeCombo  ("frames",    "Frames",    TranswaveModule::frameChoices(), 0, 1, 2),
        makeRotary ("position",  "Position",  0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("posDepth",  "Pos Mod",   -100.0f, 100.0f, 100.0f, 2, "%"),
        makeCombo  ("scanMode",  "Scan",      { "Scan: Off", "Forward", "Backward", "Ping-Pong", "Once" }, 1, 1, 3),
        makeRotary ("scanTime",  "Scan Time", 0.05f, 30.0f, 2.0f, 2, "s", true),
        makeRotary ("voices",    "Voices",    1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",     "Glide",     0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f),
        makeButton ("roll",      "Roll",      0, 1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (TranswaveModule, transwaveDescriptor)
