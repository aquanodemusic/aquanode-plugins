#pragma once
#include <JuceHeader.h>

namespace aquanova {

//==============================================================================
/** Supernova II LFO.

    Square / Saw / Triangle / S&H, three speed ranges (the Fast range runs well
    into audio rate, which is half the fun), key sync or freewheel, a delay or
    fade in, a DC offset and a Soften control that slews the output - that last
    one is what turns the square into a usable filter wobble.
*/
class Lfo
{
public:
    enum Wave { Square = 0, Saw, Triangle, SampleHold };
    enum Range { Slow = 0, Normal, Fast };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0f;
        shValue = 0.0f;
        slewed = 0.0f;
        needsFirstSample = true;
        delaySamples = 0;
        fade = 0.0f;
    }

    void noteOn (bool keySync, bool resetDelay) noexcept
    {
        if (keySync)
            phase = 0.0f;

        if (resetDelay)
        {
            delaySamples = 0;
            fade = 0.0f;
        }
    }

    /** Hardware speed 0..127 to Hz, per range. */
    static float speedToHz (int speed, int range) noexcept
    {
        const float t = (float) juce::jlimit (0, 127, speed) / 127.0f;

        switch (range)
        {
            case Slow:   return 0.01f * std::pow (1000.0f,  t);     // 0.01 .. 10 Hz
            case Fast:   return 1.0f  * std::pow (4000.0f,  t);     // 1 Hz .. 4 kHz
            case Normal:
            default:     return 0.1f  * std::pow (1000.0f,  t);     // 0.1 .. 100 Hz
        }
    }

    /**
        @param hz       final speed after modulation
        @param wave     waveform
        @param delay    0..127 delay / fade time
        @param fadeMode true = fade in, false = hard delay
        @param offset   -1..+1 DC offset
        @param soften   0..1 slew amount
        @returns -1..+1
    */
    inline float process (float hz, int wave, int delay, bool fadeMode,
                          float offset, float soften) noexcept
    {
        const float inc = juce::jlimit (0.0f, 0.49f, hz / (float) sampleRate);

        phase += inc;
        const bool wrapped = phase >= 1.0f;
        if (wrapped) phase -= 1.0f;

        float v;

        switch (wave)
        {
            case Saw:        v = 1.0f - 2.0f * phase; break;
            case Triangle:   v = 4.0f * std::abs (phase - 0.5f) - 1.0f; break;

            case SampleHold:
                if (wrapped || needsFirstSample)
                {
                    shValue = rng.nextFloat() * 2.0f - 1.0f;
                    needsFirstSample = false;
                }
                v = shValue;
                break;

            case Square:
            default:         v = phase < 0.5f ? 1.0f : -1.0f; break;
        }

        // Soften slews the output, rounding steps into ramps.
        if (soften > 0.001f)
        {
            const float a = juce::jlimit (0.00002f, 1.0f,
                                          1.0f - soften * 0.9995f);
            slewed += a * (v - slewed);
            v = slewed;
        }
        else
        {
            slewed = v;
        }

        // Delay or fade in.
        float env = 1.0f;
        const int delaySamps = (int) (0.001f * std::pow (10000.0f, (float) delay / 127.0f)
                                      * (float) sampleRate);

        if (delay > 0)
        {
            if (fadeMode)
            {
                if (fade < 1.0f)
                    fade += 1.0f / juce::jmax (1.0f, (float) delaySamps);
                env = juce::jlimit (0.0f, 1.0f, fade);
            }
            else
            {
                if (delaySamples < delaySamps) { ++delaySamples; env = 0.0f; }
            }
        }

        return juce::jlimit (-1.0f, 1.0f, v * env + offset);
    }

private:
    juce::Random rng;
    double sampleRate = 44100.0;
    float phase = 0.0f, shValue = 0.0f, slewed = 0.0f, fade = 0.0f;
    bool needsFirstSample = true;
    int delaySamples = 0;
};

} // namespace aquanova
