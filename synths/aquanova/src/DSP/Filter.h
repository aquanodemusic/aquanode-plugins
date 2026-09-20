#pragma once
#include <JuceHeader.h>

namespace aquanova {

//==============================================================================
/** One TPT state variable filter stage. Zero-delay-feedback, stable when the
    cutoff is swept fast, which matters a lot with audio-rate LFO on cutoff.
*/
struct SvfStage
{
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    inline void setCoeffs (float g_, float k_) noexcept
    {
        g = g_;
        k = k_;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float in) noexcept
    {
        const float v3 = in - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
    }

    inline float lp() const noexcept    { return v2; }
    inline float bp() const noexcept    { return v1; }
    inline float hp() const noexcept    { return (v1 * -k) - v2 + lastIn; }
    inline float notch() const noexcept { return lastIn - k * v1; }

    inline float process (float in, int mode) noexcept
    {
        lastIn = in;
        tick (in);

        switch (mode)
        {
            case 1:  return bp();
            case 2:  return hp();
            case 3:  return notch();
            default: return lp();
        }
    }

    float g = 0.1f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f, v1 = 0.0f, v2 = 0.0f, lastIn = 0.0f;
};

//==============================================================================
/** The Supernova II filter block.

    Types, in the hardware's own order:
      0 LPF   1 BPF   2 HPF
      3 Res LPF  4 Res BPF  5 Res HPF     (resonance-emphasised voicings)
      6 Notch
      7 LPF+LPF  8 BPF+BPF  9 HPF+HPF  10 LPF+BPF  11 BPF+HPF

    The last five are the split/formant designs: two independent filters spaced
    apart by the Filter Width control, summed. That pairing is where the vocal
    and formant character comes from.
*/
class Filter
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : a) s.reset();
        for (auto& s : b) s.reset();
        onePoleA = onePoleB = 0.0f;
        cacheA = cacheB = { };
    }

    /**
        @param in        input sample
        @param cutoffHz  post-modulation cutoff in Hz
        @param resonance 0..1
        @param type      0..11
        @param slope     0 = 12dB, 1 = 18dB, 2 = 24dB
        @param qNorm     0..1, trades resonance boost against constant output
        @param overdrive 0..1 pre-filter saturation
        @param width     0..1 split spacing for the paired types
        @param curve     0..1 overdrive curve shape
    */
    inline float process (float in, float cutoffHz, float resonance, int type,
                          int slope, float qNorm, float overdrive, float width,
                          float curve) noexcept
    {
        if (overdrive > 0.0005f)
            in = saturate (in, overdrive, curve);

        const bool split = type >= 7;
        const float nyq = (float) sampleRate * 0.49f;

        // Resonance maps to 2 - 2*damping. Near the top it self-oscillates,
        // as it should.
        const float res = juce::jlimit (0.0f, 0.995f, resonance);
        float k = 2.0f - 1.96f * res;

        // The "Res" voicings run hotter and narrower than the plain ones.
        if (type >= 3 && type <= 5)
            k *= 0.55f;

        float fA = juce::jlimit (15.0f, nyq, cutoffHz);
        float fB = fA;

        if (split)
        {
            // Width pushes the pair apart symmetrically in pitch, up to ~2 octaves.
            const float spread = 1.0f + width * 3.0f;
            fA = juce::jlimit (15.0f, nyq, cutoffHz / spread);
            fB = juce::jlimit (15.0f, nyq, cutoffHz * spread);
        }

        const int modeA = modeForA (type);
        const int modeB = modeForB (type);

        float outA = runChain (a, onePoleA, cacheA, in, fA, k, modeA, slope);
        float out;

        if (split)
        {
            const float outB = runChain (b, onePoleB, cacheB, in, fB, k, modeB, slope);
            out = 0.7f * (outA + outB);
        }
        else
        {
            out = outA;
        }

        // QNorm: at 0 the resonance peak adds level the way an analogue filter
        // does; at 1 the output level stays put as resonance rises.
        if (qNorm > 0.0f)
            out *= juce::jmap (qNorm, 1.0f, 1.0f / (1.0f + res * 2.5f));

        return out;
    }

private:
    static inline int modeForA (int type) noexcept
    {
        switch (type)
        {
            case 1: case 4: case 8: case 11: return 1;   // BPF
            case 2: case 5: case 9:          return 2;   // HPF
            case 6:                          return 3;   // Notch
            default:                         return 0;   // LPF
        }
    }

    static inline int modeForB (int type) noexcept
    {
        switch (type)
        {
            case 7:  return 0;   // LPF + LPF
            case 8:  return 1;   // BPF + BPF
            case 9:  return 2;   // HPF + HPF
            case 10: return 1;   // LPF + BPF
            case 11: return 2;   // BPF + HPF
            default: return 0;
        }
    }

    /** Cached tan()/exp() results. Cutoff is modulated every sample, but it
        moves slowly, so recomputing the transcendentals only when the frequency
        has actually shifted by a meaningful amount saves a great deal of CPU
        with no audible difference. The threshold below is about a sixteenth of
        a semitone. */
    struct CoeffCache
    {
        float freq = -1.0f, g = 0.0f, onePoleA = 0.0f;
    };

    inline void updateCache (CoeffCache& cache, float freq) noexcept
    {
        if (cache.freq > 0.0f && std::abs (freq - cache.freq) < cache.freq * 0.004f)
            return;

        cache.freq = freq;
        cache.g = std::tan (juce::MathConstants<float>::pi * freq / (float) sampleRate);
        cache.onePoleA = std::exp (-juce::MathConstants<float>::twoPi
                                   * juce::jlimit (0.0005f, 0.49f, freq / (float) sampleRate));
    }

    inline float runChain (SvfStage* stages, float& onePole, CoeffCache& cache, float in,
                           float freq, float k, int mode, int slope) noexcept
    {
        updateCache (cache, freq);

        stages[0].setCoeffs (cache.g, k);

        float x = stages[0].process (in, mode);

        if (slope >= 2)                      // 24 dB: two full stages
        {
            stages[1].setCoeffs (cache.g, 1.4f);
            x = stages[1].process (x, mode);
        }
        else if (slope == 1)                 // 18 dB: one stage plus a single pole
        {
            const float a1 = cache.onePoleA;

            if (mode == 2)                   // high pass single pole
            {
                const float lpNew = x + a1 * (onePole - x);
                const float hp = x - lpNew;
                onePole = lpNew;
                x = hp;
            }
            else
            {
                onePole = x + a1 * (onePole - x);
                x = onePole;
            }
        }

        return x;
    }

    inline float saturate (float x, float amount, float curve) noexcept
    {
        const float drive = 1.0f + amount * 24.0f;
        const float hard = juce::jlimit (0.0f, 1.0f, curve);

        const float soft = std::tanh (x * drive);
        const float fold = std::sin (juce::jlimit (-6.0f, 6.0f, x * drive));

        // Curve morphs from a valve-ish soft knee to a harder, folded edge.
        const float y = soft * (1.0f - hard) + fold * hard;

        // Partial gain compensation so turning it up thickens rather than
        // simply gets louder.
        return y / (1.0f + amount * 2.0f);
    }

    double sampleRate = 44100.0;
    SvfStage a[2], b[2];
    float onePoleA = 0.0f, onePoleB = 0.0f;
    CoeffCache cacheA, cacheB;
};

} // namespace aquanova
