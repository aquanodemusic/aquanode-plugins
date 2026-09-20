#pragma once
#include <JuceHeader.h>

namespace aquanova {

//==============================================================================
/** The vocoder.

    Adapted from the Aquanode Modular FX VocoderModule, widened to the 42 bands
    the Supernova II specifies and given the hardware's own controls.

    The modulator's per-band envelope shapes the same bands of the carrier. The
    carrier is always the synth's own output; the modulator is whatever the
    Modulator Source parameter points at - the audio input, or one or two of the
    oscillators taken straight from the voices, before the filter. Feeding it
    oscillators rather than a microphone is what turns it into a formant/comb
    machine instead of a robot-voice box.
*/
class Vocoder
{
public:
    static constexpr int kBands = 42;
    static constexpr int kOrder = 2;      // cascaded bandpasses per path

    //==========================================================================
    /** Transposed direct form II bandpass biquad. */
    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setBandpass (float fc, float q, float sr) noexcept
        {
            fc = juce::jlimit (20.0f, sr * 0.49f, fc);
            q  = juce::jmax (0.05f, q);

            const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
            const float alpha = std::sin (w0) / (2.0f * q);
            const float inv = 1.0f / (1.0f + alpha);

            b0 =  alpha * inv;
            b1 =  0.0f;
            b2 = -alpha * inv;
            a1 = (-2.0f * std::cos (w0)) * inv;
            a2 = (1.0f - alpha) * inv;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    //==========================================================================
    void prepare (double sr)
    {
        sampleRate = sr;
        buildBank();
        reset();
    }

    void reset() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            for (int o = 0; o < kOrder; ++o)
            {
                modFilters[b][o].reset();
                carFiltersL[b][o].reset();
                carFiltersR[b][o].reset();
            }
            envelope[b] = 0.0f;
        }

        sibHpState = 0.0f;
    }

    /**
        @param carrierL/R  the synth output
        @param modulator   the modulator signal, already selected by the engine
        @param balance     0..1, dry to fully vocoded
        @param speed       0..1, envelope follower attack/release speed
        @param width       0..1, stereo spread of the bands
        @param sibLevel    0..1, how much sibilance is added back
        @param sibNoise    true = generate noise, false = high-passed modulator
    */
    inline void process (float& carrierL, float& carrierR, float modulator,
                         float balance, float speed, float width,
                         float sibLevel, bool sibNoise) noexcept
    {
        if (balance <= 0.001f)
            return;

        // Faster settings track consonants; slower ones smear into pads.
        const float attackMs  = 0.5f + (1.0f - speed) * 14.0f;
        const float releaseMs = 5.0f + (1.0f - speed) * 180.0f;

        const float attCoeff = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, attackMs  * 0.001f * (float) sampleRate));
        const float relCoeff = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, releaseMs * 0.001f * (float) sampleRate));

        const float dryL = carrierL, dryR = carrierR;
        float outL = 0.0f, outR = 0.0f;

        for (int b = 0; b < kBands; ++b)
        {
            // --- modulator band into an envelope
            float m = modulator;
            for (int o = 0; o < kOrder; ++o)
                m = modFilters[b][o].process (m);

            const float rectified = std::abs (m);
            envelope[b] += (rectified > envelope[b] ? attCoeff : relCoeff) * (rectified - envelope[b]);

            // --- carrier band, shaped by that envelope
            float cl = carrierL, cr = carrierR;
            for (int o = 0; o < kOrder; ++o)
            {
                cl = carFiltersL[b][o].process (cl);
                cr = carFiltersR[b][o].process (cr);
            }

            const float g = envelope[b];

            // Width fans alternate bands out to the sides, which is what makes
            // the vocoder sound wide without any actual stereo source.
            const float side = (b % 2 == 0) ? width : -width;
            const float gl = g * (1.0f - side * 0.5f);
            const float gr = g * (1.0f + side * 0.5f);

            outL += cl * gl;
            outR += cr * gr;
        }

        // --- sibilance: the bands above about 7 kHz cannot carry consonants,
        // so they are replaced, either by high-passed modulator or by noise.
        if (sibLevel > 0.001f)
        {
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                             * 4000.0f / (float) sampleRate);
            sibHpState += a * (modulator - sibHpState);
            const float hp = modulator - sibHpState;

            const float amount = std::abs (hp) * sibLevel * 6.0f;
            const float source = sibNoise ? (rng.nextFloat() * 2.0f - 1.0f) : hp;

            outL += source * amount;
            outR += source * amount;
        }

        const float makeup = 6.0f;
        const float wetL = std::tanh (outL * makeup);
        const float wetR = std::tanh (outR * makeup);

        carrierL = dryL * (1.0f - balance) + wetL * balance;
        carrierR = dryR * (1.0f - balance) + wetR * balance;
    }

private:
    void buildBank()
    {
        // Log-spaced centres across the speech range.
        const float loHz = 90.0f, hiHz = 8000.0f;
        const float ratio = std::pow (hiHz / loHz, 1.0f / (float) (kBands - 1));

        // Q proportional to band spacing, so neighbouring bands meet cleanly.
        const float q = 1.0f / (std::sqrt (ratio) - 1.0f / std::sqrt (ratio));

        float fc = loHz;

        for (int b = 0; b < kBands; ++b)
        {
            for (int o = 0; o < kOrder; ++o)
            {
                modFilters[b][o].setBandpass (fc, q, (float) sampleRate);
                carFiltersL[b][o].setBandpass (fc, q, (float) sampleRate);
                carFiltersR[b][o].setBandpass (fc, q, (float) sampleRate);
            }

            fc *= ratio;
        }
    }

    double sampleRate = 44100.0;
    Biquad modFilters  [kBands][kOrder];
    Biquad carFiltersL [kBands][kOrder];
    Biquad carFiltersR [kBands][kOrder];
    float envelope [kBands] { };
    float sibHpState = 0.0f;
    juce::Random rng;
};

} // namespace aquanova
