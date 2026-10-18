#pragma once
#include <JuceHeader.h>

namespace aquanova {

//==============================================================================
/** The Supernova II style oscillator.

    Square / Saw / Double Saw, band-limited with PolyBLEP, plus the four things
    that give the machine its voice:

      Soften       - a per-oscillator one-pole low pass that rounds the corners
                     off the waveform before it ever reaches the filter. 127 is
                     fully open (hard), 0 is fully rounded.
      Sync         - a virtual master oscillator hard-syncs the audible slave.
      Sync Skew    - bends the slave's phase ramp, moving the formant peak
                     without changing the sync ratio.
      Formant Width- scales the slave's run length inside the master period, so
                     the burst narrows and the formant widens.
*/
class Oscillator
{
public:
    enum Type { Square = 0, Saw, AudioIn1, AudioIn2, DoubleSaw };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0f;
        masterPhase = 0.0f;
        subPhase = 0.0f;
        softState = 0.0f;
    }

    void setStartPhase (float p) noexcept
    {
        phase = masterPhase = juce::jlimit (0.0f, 0.999f, p);
        subPhase = phase;
    }

    /** All modulation is applied by the voice before this is called.

        @param hz        final oscillator pitch in Hz
        @param type      waveform
        @param pulse     0..1 pulse width (0.5 = square)
        @param syncAmt   0..1, 0 = sync off
        @param skew      0..1 phase skew
        @param formant   0..1 formant width
        @param soften    0..1, 1 = fully open
    */
    inline float process (float hz, int type, float pulse, float syncAmt,
                          float skew, float formant, float soften) noexcept
    {
        const float inc = juce::jlimit (0.0f, 0.49f, hz / (float) sampleRate);

        float out;

        if (syncAmt > 0.0005f)
        {
            // The master runs at the played pitch; the slave runs faster and is
            // reset every time the master wraps. That ratio is what you hear as
            // the metallic sync formant.
            const float ratio = 1.0f + syncAmt * 23.0f;        // up to ~24:1
            const float slaveInc = inc * ratio * (0.25f + formant * 1.75f);

            masterPhase += inc;
            bool wrapped = false;
            float frac = 0.0f;

            if (masterPhase >= 1.0f)
            {
                masterPhase -= 1.0f;
                wrapped = true;
                frac = masterPhase / juce::jmax (1.0e-9f, inc);
            }

            phase += slaveInc;
            if (phase >= 1.0f) phase -= 1.0f;

            if (wrapped)
                phase = skew * 0.999f;      // skew offsets the restart point

            out = shape (phase, slaveInc, type, pulse);

            if (wrapped)
            {
                // Soften the discontinuity the hard reset creates, scaled by how
                // far into the sample it happened.
                out -= polyBlep (juce::jlimit (0.0f, 1.0f, frac), slaveInc) * 0.5f;
            }
        }
        else
        {
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            masterPhase = phase;
            out = shape (phase, inc, type, pulse);
        }

        return applySoften (out, soften, inc);
    }

private:
    //==============================================================================
    static inline float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;

        if (t < dt)             { t /= dt;      return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt)      { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    static inline float sawAt (float p, float inc) noexcept
    {
        return (2.0f * p - 1.0f) - polyBlep (p, inc);
    }

    static inline float squareAt (float p, float inc, float pulse) noexcept
    {
        const float w = juce::jlimit (0.02f, 0.98f, pulse);
        float v = p < w ? 1.0f : -1.0f;
        v += polyBlep (p, inc);

        float p2 = p - w;
        if (p2 < 0.0f) p2 += 1.0f;
        v -= polyBlep (p2, inc);

        // Keep the level roughly constant as the pulse narrows, the way the
        // hardware does - otherwise extreme PWM just goes quiet.
        return v * (1.0f / (1.0f + 2.0f * std::abs (w - 0.5f)));
    }

    inline float shape (float p, float inc, int type, float pulse) noexcept
    {
        switch (type)
        {
            case Saw:       return sawAt (p, inc);

            case DoubleSaw:
            {
                // Two saws an octave apart from one phase accumulator - the
                // hollow, slightly detuned edge the SN2 calls Double Saw.
                subPhase += inc * 2.0f;
                while (subPhase >= 1.0f) subPhase -= 1.0f;
                return 0.6f * (sawAt (p, inc) + 0.7f * sawAt (subPhase, inc * 2.0f));
            }

            case AudioIn1:
            case AudioIn2:  return 0.0f;    // filled in by the voice from the input bus

            case Square:
            default:        return squareAt (p, inc, pulse);
        }
    }

    inline float applySoften (float x, float soften, float inc) noexcept
    {
        if (soften >= 0.999f)
            return x;

        // Cutoff tracks pitch so the tone stays constant across the keyboard,
        // which is what makes Soften feel like a waveform control rather than
        // a second filter.
        const float rel = 0.02f + soften * soften * 6.0f;
        const float fc = juce::jlimit (0.0005f, 0.45f, inc * rel);
        const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc);

        softState += a * (x - softState);

        // Give back a little of the lost level so Soften does not just duck.
        return softState * (1.0f + (1.0f - soften) * 0.6f);
    }

    double sampleRate = 44100.0;
    float phase = 0.0f, masterPhase = 0.0f, subPhase = 0.0f, softState = 0.0f;
};

//==============================================================================
/** White noise with the same Soften rounding as the oscillators. */
class NoiseSource
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; state = 0.0f; }

    inline float process (float soften) noexcept
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;

        if (soften >= 0.999f)
            return w;

        const float fc = juce::jlimit (0.0005f, 0.45f,
                                       (20.0f + soften * soften * 18000.0f) / (float) sampleRate);
        const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc);
        state += a * (w - state);
        return state * (1.0f + (1.0f - soften) * 2.0f);
    }

private:
    juce::Random rng;
    double sampleRate = 44100.0;
    float state = 0.0f;
};

} // namespace aquanova
