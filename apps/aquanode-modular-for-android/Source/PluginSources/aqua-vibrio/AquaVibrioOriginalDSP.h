#pragma once
//==============================================================================
//  AquaVibrioOriginalDSP.h
//
//  The Original engine's response curves and its voice filter. Everything in
//  here was MEASURED, not designed: a reference instrument was hosted
//  offline, fed synthetic single dumps that each isolate one parameter, and
//  its output analysed. The tables are the results, one
//  point per 8 bytes, interpolated in between.
//
//  Native Aqua Vibrio maps knobs "for musical, predictable behaviour"; this
//  file maps them the way hardware patches were programmed to be heard.
//==============================================================================

#include <JuceHeader.h>
#include <cmath>

namespace aquavibrio
{
namespace original
{
namespace curves
{
    // A table sampled every 8 bytes (0, 8 .. 120) plus a last point at 127.
    // Piecewise-linear lookup over (byte, value) points.
    template <size_t N>
    inline float lookup (const float (&x)[N], const float (&y)[N], float b)
    {
        if (b <= x[0]) return y[0];
        for (size_t k = 1; k < N; ++k)
            if (b <= x[k])
                return y[k - 1] + (y[k] - y[k - 1]) * (b - x[k - 1]) / (x[k] - x[k - 1]);
        return y[N - 1];
    }

    struct Table17
    {
        float v[17];

        float at (float byte, bool logDomain = false) const
        {
            const auto get = [this, logDomain] (int i) { return logDomain ? std::log (v[i]) : v[i]; };
            float y;
            if (byte <= 0.0f)        y = get (0) + (get (1) - get (0)) * (byte / 8.0f);       // linear extrapolation
            else if (byte >= 120.0f) y = get (15) + (get (16) - get (15)) * ((byte - 120.0f) / 7.0f);
            else
            {
                const int i = (int) (byte / 8.0f);
                const float f = (byte - 8.0f * (float) i) / 8.0f;
                y = get (i) + (get (i + 1) - get (i)) * f;
            }
            return logDomain ? std::exp (y) : y;
        }
    };

    //== envelopes ============================================================
    // Attack: a linear ramp whose whole length doubles every 8.13 bytes,
    // 98.8 ms at 64. Below 32 it shortens much faster (re-measured sample by
    // sample): 0.54 ms at 16, 0.05 ms at 8, and 0..6 start at full level.
    inline float attackMs (float byte)
    {
        byte = juce::jlimit (0.0f, 127.0f, byte);
        if (byte < 32.0f)
        {
            static const float x[] { 0.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 22.0f, 24.0f, 28.0f, 32.0f };
            static const float y[] { -6.0f, -5.0f, -4.3f, -3.47f, -2.47f, -1.64f, -0.89f, -0.22f, 0.44f, 0.96f, 1.47f, 2.21f, 2.70f };   // log2 ms
            return std::exp2 (lookup (x, y, byte));
        }
        return 98.8f * std::exp2 ((byte - 64.0f) / 8.13f);
    }

    // Decay and Release share one curve: exponential, at this many dB per
    // second (25.9 dB/s at 64, i.e. about 2.3 s to fall 60 dB).
    inline float decayDbPerSecond (float byte)
    {
        static const Table17 t { { 6000.0f, 1500.0f, 442.0f, 197.0f, 116.5f, 75.0f, 51.5f, 36.3f, 25.9f,
                                   18.6f, 13.0f, 9.1f, 6.2f, 4.08f, 2.59f, 1.6f, 1.0f } };
        return t.at (juce::jlimit (0.0f, 127.0f, byte), true);
    }

    // Sustain: measured level in dB below full scale.
    inline float sustainLevel (float byte)
    {
        // (re-measured as plateaus after a fast decay: nearly straight in dB at
        // the top, about 1.1 dB per 8 bytes, steeper towards the bottom)
        static const Table17 t { { -60.0f, -29.70f, -23.49f, -19.68f, -16.89f, -14.63f, -12.73f, -11.05f, -9.54f,
                                   -8.14f, -6.84f, -5.61f, -4.44f, -3.29f, -2.18f, -1.09f, 0.0f } };
        byte = juce::jlimit (0.0f, 127.0f, byte);
        if (byte < 8.0f)
            return (byte / 8.0f) * juce::Decibels::decibelsToGain (-29.70f);
        return juce::Decibels::decibelsToGain (t.at (byte));
    }

    // Punch Intensity (measured): at note on the voice's level is multiplied by
    // 1 + height * keyWeight * exp(-t / time). The time follows the key - 0.68 ms
    // at C1, halving every octave, so it always spans the same slice of a
    // cycle - and the height falls towards the top of the keyboard, so on high
    // notes there is next to nothing. Velocity, attack and sustain leave it be.
    inline float punchHeight (float byte)
    {
        static const float x[] { 0.0f, 8.0f, 16.0f, 32.0f, 48.0f, 64.0f, 80.0f, 96.0f, 112.0f, 127.0f };
        static const float y[] { 0.0f, 0.24f, 0.45f, 0.82f, 1.18f, 1.58f, 2.08f, 2.74f, 3.62f, 4.77f };
        return lookup (x, y, juce::jlimit (0.0f, 127.0f, byte));
    }

    inline float punchKeyWeight (float note)
    {
        static const float x[] { 12.0f, 24.0f, 36.0f, 48.0f, 60.0f, 72.0f, 84.0f, 96.0f };
        static const float y[] { 1.25f, 1.151f, 1.0f, 0.809f, 0.593f, 0.348f, 0.18f, 0.12f };
        return lookup (x, y, juce::jlimit (12.0f, 96.0f, note));
    }

    inline float punchSeconds (float note)
    {
        return 6.782e-4f * std::exp2 (-(note - 36.0f) / 12.0f);
    }

    // The filter envelope's attack (a straight ramp), measured - much slower
    // than the amp's in the middle: 0.72 s at 64, doubling every 16 bytes above.
    inline float modAttackMs (float byte)
    {
        // (the top re-measured: about 7 s at 115 and 17 s at 127)
        static const float x[] { 0.0f, 8.0f, 16.0f, 24.0f, 32.0f, 40.0f, 48.0f, 64.0f, 80.0f, 105.0f, 115.0f, 127.0f };
        static const float y[] { -2.0f, 3.2f, 5.2f, 6.1f, 7.3f, 7.8f, 8.5f, 9.5f, 10.5f, 12.04f, 12.81f, 14.05f };   // log2 ms
        return std::exp2 (lookup (x, y, juce::jlimit (0.0f, 127.0f, byte)));
    }

    // The inverse: an envelope level on the amp's curve back to the plain
    // 0..1 the envelope is as a modulator (Sustain byte / 127).
    inline float modLevelFromAmp (float level)
    {
        static const float db[] { -60.0f, -29.70f, -23.49f, -19.68f, -16.89f, -14.63f, -12.73f, -11.05f, -9.54f,
                                  -8.14f, -6.84f, -5.61f, -4.44f, -3.29f, -2.18f, -1.09f, 0.0f };
        const float g8 = 0.03273f;   // -29.70 dB
        if (level <= 0.0f) return 0.0f;
        if (level < g8) return level / g8 * (8.0f / 127.0f);
        const float d = 20.0f * std::log10 (level);
        for (int k = 2; k < 17; ++k)
            if (d <= db[k])
                return ((float) (k - 1) + (d - db[k - 1]) / (db[k] - db[k - 1])) * 8.0f / 127.0f;
        return 1.0f;
    }

    // Sustain Time (slope): 64 holds. Below, the level keeps falling; above,
    // it climbs back to full. dB per second, by distance from the centre.
    inline float slopeDbPerSecond (float byte)
    {
        byte = juce::jlimit (0.0f, 127.0f, byte);
        const float d = std::abs (byte - 64.0f);
        static const float fall[9] = { 0.0f, 0.5f, 1.6f, 4.2f, 8.5f, 17.7f, 47.0f, 150.0f, 1000.0f };
        static const float rise[9] = { 0.0f, 0.56f, 1.36f, 4.5f, 10.0f, 25.0f, 80.0f, 200.0f, 1000.0f };
        const auto* table = byte < 64.0f ? fall : rise;
        const int i = juce::jmin (7, (int) (d / 8.0f));
        const float f = juce::jmin (1.0f, (d - 8.0f * (float) i) / 8.0f);
        const float r = i == 0 ? table[1] * f
                               : std::exp (std::log (table[i]) + (std::log (table[i + 1]) - std::log (table[i])) * f);
        return byte < 64.0f ? -r : r;
    }

    //== filter ===============================================================
    // Cutoff: the 2-pole's corner (bilinear, prewarped) by byte. Modulation
    // can push the byte past either end; it extends exponentially.
    // The top half bends down: the resonance peak sits 0.7 semitone below a
    // straight exponential at byte 90, 1.2 at 100, 1.8 at 110 and 2.5 at 120
    // (read off the peak harmonic of a resonant saw, low and band pass alike).
    inline float cutoffHz (float byte)
    {
        static const Table17 t { { 25.5f, 39.2f, 60.3f, 92.8f, 142.9f, 220.1f, 338.9f, 520.6f, 797.0f,
                                   1226.0f, 1901.5f, 2928.0f, 4489.0f, 6908.0f, 10670.0f, 16483.0f, 21970.0f } };
        if (byte < 0.0f)
            return 25.5f * std::exp2 (byte / 12.86f);
        if (byte > 127.0f)
            return 21970.0f * std::exp2 ((byte - 127.0f) / 12.86f);
        return t.at (byte, true);
    }

    // The Analog modes keep the straight exponential top (their own map was
    // measured on it; see the engine's filter set-up).
    inline float cutoffHzStraight (float byte)
    {
        static const Table17 t { { 25.5f, 39.2f, 60.3f, 92.8f, 142.9f, 220.1f, 338.9f, 522.2f, 805.1f,
                                   1244.9f, 1948.3f, 3034.2f, 4755.4f, 7516.7f, 11948.5f, 19000.0f, 26000.0f } };
        if (byte < 0.0f)
            return 25.5f * std::exp2 (byte / 12.86f);
        if (byte > 127.0f)
            return 26000.0f * std::exp2 ((byte - 127.0f) / 12.86f);
        return t.at (byte, true);
    }

    // The filter's damping with resonance at zero changes a little across
    // the range (it is what the reference's response measures as).
    inline float baseQ (float byte)
    {
        static const Table17 t { { 0.641f, 0.640f, 0.640f, 0.640f, 0.639f, 0.639f, 0.638f, 0.635f, 0.629f,
                                   0.613f, 0.558f, 0.560f, 0.560f, 0.559f, 0.562f, 0.562f, 0.562f } };
        return t.at (juce::jlimit (0.0f, 127.0f, byte));
    }

    // Resonance: the Q it gives (at the centre cutoff), and the level the
    // pass band loses - the hardware resonance that "eats the bass".
    inline float resoQ (float byte)
    {
        static const Table17 t { { 0.629f, 0.706f, 0.806f, 1.033f, 1.428f, 1.834f, 2.545f, 3.265f, 4.529f,
                                   5.803f, 8.036f, 10.46f, 14.97f, 26.28f, 87.5f, 121.7f, 260.0f } };
        return t.at (juce::jlimit (0.0f, 127.0f, byte), true);
    }

    // Low and high pass: the level the pass band keeps, per filter (measured on
    // white noise through one filter at a time, fitted with a 2-pole response).
    inline float resoGain (float byte)
    {
        static const Table17 t { { 0.0f, -0.6f, -0.9f, -1.1f, -1.2f, -1.9f, -2.4f, -3.3f, -4.0f,
                                   -4.8f, -5.7f, -6.5f, -7.6f, -8.4f, -9.1f, -10.2f, -11.7f } };
        return juce::Decibels::decibelsToGain (t.at (juce::jlimit (0.0f, 127.0f, byte)));
    }

    // Band pass: the gain on the band output (whose own peak is the Q), measured
    // the same way: +3.1 dB with no resonance, falling about 0.06 dB per byte.
    inline float bandPassGain (float byte)
    {
        // a straight line in dB, plus the measured correction where the
        // resonance gets steep (the band pass's level grows with Q there; the
        // reference's does not) - fitted on a saw across cutoff 30..90
        byte = juce::jlimit (0.0f, 127.0f, byte);
        static const float x[] { 0, 32, 64, 80, 96, 104, 112, 120, 127 };
        static const float c[] { 0.0f, 0.0f, -1.2f, 0.8f, 0.0f, -3.5f, -6.8f, -6.8f, -7.5f };
        return juce::Decibels::decibelsToGain (3.1f - 0.058f * byte + lookup (x, c, byte));
    }

    // The filter-mode dependent output gain: low/high pass lose level with
    // resonance, band pass follows its own law, and the notch keeps full level.
    inline float modeGain (int mode, float resByte)
    {
        switch (mode)
        {
            case 2:  return bandPassGain (resByte);
            case 3:  return 1.0f;
            default: return resoGain (resByte);
        }
    }

    // Filter Env Amount -> cutoff bytes at full envelope.
    inline float envAmountBytes (float amount01)
    {
        static const Table17 t { { 0.0f, 4.0f, 9.8f, 13.5f, 17.2f, 25.0f, 32.7f, 40.6f, 48.6f,
                                   58.7f, 68.8f, 78.5f, 88.1f, 95.6f, 103.2f, 105.0f, 106.5f } };
        const float a = std::abs (amount01) * 127.0f;
        const float y = t.at (juce::jmin (127.0f, a));
        return amount01 < 0.0f ? -y : y;
    }

    //== LFOs ==================================================================
    // Free-running rate, Hz (measured on LFO 1 -> pitch).
    inline float lfoRateHz (float byte)
    {
        // (byte 0 all but stands still: no drift measurable over a second)
        static const Table17 t { { 0.001f, 0.048f, 0.08f, 0.13f, 0.221f, 0.34f, 0.525f, 0.76f, 1.09f,
                                   1.57f, 2.27f, 3.35f, 4.97f, 8.2f, 13.45f, 30.0f, 86.3f } };
        return t.at (juce::jlimit (0.0f, 127.0f, byte), true);
    }

    // LFO 1 -> oscillator pitch: cents at a given amount (0..1 of the
    // bipolar knob). Gentle to the middle, then steep (measured).
    inline float lfoPitchCents (float amount01)
    {
        // (98 -> 250 cents read off a square LFO; the rest as measured before)
        static const float x[8] = { 0.0f, 0.125f, 0.25f, 0.5f, 0.53125f, 0.5625f, 0.75f, 1.0f };
        static const float y[8] = { 0.0f, 14.0f, 65.0f, 236.0f, 250.0f, 300.0f, 870.0f, 3400.0f };
        const float a = juce::jlimit (0.0f, 1.0f, std::abs (amount01));
        int i = 0; while (i < 6 && a > x[i + 1]) ++i;
        const float f = (a - x[i]) / (x[i + 1] - x[i]);
        const float c = i == 0 ? y[1] * f * f : std::exp (std::log (y[i]) + (std::log (y[i + 1]) - std::log (y[i])) * f);
        return amount01 < 0.0f ? -c : c;
    }

    //== oscillators and mixer ================================================
    // Classic Shape: a plain linear crossfade wave -> saw -> pulse, in which
    // the wave and the pulse are louder than the saw (measured on the
    // harmonics: the sine's fundamental is 2.36x the saw's, a square's odd
    // harmonics 1.52x the saw's).
    constexpr float kMorphWaveGain = 1.50f;
    constexpr float kMorphPulseGain = 1.52f;

    // Pulse Width: the duty cycle runs 50 % -> 0 % over the knob (byte/128,
    // 127 = 1.0), so at full width the pulse vanishes (measured: silent).
    inline float pulseWidth (float knob01)
    {
        const float k = knob01 >= 0.9999f ? 1.0f : juce::jlimit (0.0f, 1.0f, knob01) * (127.0f / 128.0f);
        return 0.5f - 0.5f * k;
    }

    // LFO 1 > Filter Gain: gain = centre(|x|) + slope(|x|) * sign(x) * lfo,
    // never below zero (x = the bipolar amount). Measured with the LFO held
    // still at -1, -0.5, 0, 0.5 and 1.
    inline float lfoFilterGain (float x, float lfo)
    {
        const float a = std::abs (x);
        static const float ax[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float c[] { 1.0f, 0.64f, 0.48f, 0.316f, 0.157f };
        static const float d[] { 0.0f, 0.145f, 0.235f, 0.33f, 0.46f };
        const float g = lookup (ax, c, a) + lookup (ax, d, a) * (x < 0.0f ? -lfo : lfo);
        return juce::jmax (0.0f, g);
    }

    // FM index (peak deviation over the carrier frequency) against FM Amount.
    // Exact, read off the mean pitch of positive-triangle FM (the carrier's
    // mean frequency is f * (1 + I/2), so the fractional part of that ratio
    // shows as the offset of every spectral line; every 4 bytes, C3 and C5
    // alike): piecewise linear, 1/32 a byte to 32 (I = 1), 3/32 to 64 (4),
    // 1/8 to 96 (8), 1/4 to 124 (15), and 16 at full.
    inline float fmIndex (float byte)
    {
        static const float x[] { 0, 32, 64, 96, 124, 127 };
        static const float y[] { 0.0f, 1.0f, 4.0f, 8.0f, 15.0f, 16.0f };
        return lookup (x, y, byte);
    }

    // Osc Balance (measured on two oscillators a fifth apart): gains of
    // oscillator 1 and oscillator 2, every 16 bytes plus 127.
    inline float balanceGain1 (float byte)
    {
        static const float x[] { 0, 16, 32, 48, 64, 80, 96, 112, 127 };
        static const float y[] { 1.0f, 0.989f, 0.944f, 0.861f, 0.750f, 0.610f, 0.442f, 0.234f, 0.0f };
        return lookup (x, y, byte);
    }
    inline float balanceGain2 (float byte)
    {
        static const float x[] { 0, 16, 32, 48, 64, 80, 96, 112, 127 };
        static const float y[] { 0.0f, 0.243f, 0.452f, 0.631f, 0.776f, 0.881f, 0.966f, 1.0f, 1.0f };
        return lookup (x, y, byte);
    }

    // Sub oscillator volume: square law (measured, square and triangle alike).
    inline float subVolumeGain (float knob01) { return knob01 * knob01; }

    // Noise volume: close to exponential, about 0.2 dB a byte (dB against 127).
    inline float noiseVolumeGain (float byte)
    {
        static const float x[] { 0, 16, 32, 48, 64, 80, 96, 112, 127 };
        static const float y[] { 0.0f, 0.0385f, 0.0861f, 0.143f, 0.219f, 0.316f, 0.452f, 0.661f, 1.0f };
        return lookup (x, y, byte);
    }

    // Ring modulator volume: steeper than linear (dB against 127: -11.5 at 64).
    inline float ringVolumeGain (float byte)
    {
        static const float x[] { 0, 16, 32, 64, 96, 127 };
        static const float y[] { 0.0f, 0.06f, 0.102f, 0.266f, 0.519f, 1.0f };
        return lookup (x, y, byte);
    }

    // Noise Colour (fitted to the reference's noise spectra within 0.5 dB):
    // below the centre a one-pole low pass whose gain keeps the noise loud
    // (+9.3 dB at 0), above it a one-pole high pass. Hz and dB per colour byte.
    inline float noiseLowPassHz (float byte)
    {
        static const float x[] { 0, 16, 32, 48, 64 };
        static const float lnHz[] { std::log (269.0f), std::log (360.0f), std::log (1174.0f), std::log (4258.0f), std::log (200000.0f) };
        return std::exp (lookup (x, lnHz, byte));
    }
    inline float noiseLowPassDb (float byte)
    {
        static const float x[] { 0, 16, 32, 48, 64 };
        static const float y[] { 9.3f, 8.3f, 6.1f, 3.3f, 0.0f };
        return lookup (x, y, byte);
    }
    inline float noiseHighPassHz (float byte)
    {
        static const float x[] { 64, 80, 96, 112, 127 };
        static const float lnHz[] { std::log (4.0f), std::log (65.0f), std::log (399.0f), std::log (1396.0f), std::log (3797.0f) };
        return std::exp (lookup (x, lnHz, byte));
    }
    inline float noiseHighPassDb (float byte)
    {
        static const float x[] { 64, 96, 112, 127 };
        static const float y[] { 0.0f, 0.2f, 0.2f, -0.8f };
        return lookup (x, y, byte);
    }

    // Oscillator Keyfollow (measured, sine pitch at C2/C4/C6): centred on C4,
    // 64 does not track at all, 96 tracks normally, 0 runs backwards at double
    // speed and 127 tracks double - (byte - 64) / 32 with the top end at 2.
    inline float oscKeyTrack (float byte)
    {
        static const float x[] { 0, 64, 96, 120, 127 };
        static const float y[] { -2.0f, 0.0f, 1.0f, 1.75f, 2.0f };
        return lookup (x, y, juce::jlimit (0.0f, 127.0f, byte));
    }

    // Osc 2 fine detune, cents.
    inline float detuneCents (float byte)
    {
        static const Table17 t { { 0.0f, 0.5f, 1.2f, 2.4f, 4.0f, 5.6f, 8.0f, 10.4f, 12.5f,
                                   14.8f, 18.0f, 21.5f, 25.0f, 31.0f, 38.0f, 47.5f, 58.5f } };
        return t.at (juce::jlimit (0.0f, 127.0f, byte));
    }

    // Every new note is detuned a little, by the next entry of a fixed cycle
    // of 16 (an even spread of +-3.7 cents, in this order), on all its
    // oscillators alike. Read off 40 single notes in a row at C3 (repeatable
    // to 0.01 cent); higher notes get less of it, about 0.84x per octave
    // (C4 0.84, C5 0.73, C6 0.60).
    inline float voiceDetuneCents (juce::uint32 counter, int note)
    {
        static const float cycle[16] { -3.69f, -0.75f, 2.16f, -2.64f, 0.21f, 3.12f, -1.68f, 1.68f,
                                       -3.13f, -0.21f, 2.65f, -2.15f, 0.76f, 3.69f, -1.19f, 1.20f };
        const float scale = juce::jlimit (0.5f, 1.4f, std::pow (0.843f, (float) (note - 48) / 12.0f));
        return cycle[counter & 15u] * scale;
    }

    // Unison: cents of the outermost voice.
    inline float unisonCents (float byte)
    {
        static const Table17 t { { 0.0f, 0.3f, 0.6f, 1.4f, 2.3f, 3.3f, 4.2f, 5.2f, 6.2f,
                                   7.8f, 9.4f, 11.3f, 13.3f, 16.3f, 20.0f, 25.5f, 32.0f } };
        return t.at (juce::jlimit (0.0f, 127.0f, byte));
    }

    // Osc Volume: below the centre a fade, (byte/64)^1.3; above it, level
    // stays put and only the saturation drive rises.
    inline float oscVolumeGain (float byte)
    {
        byte = juce::jlimit (0.0f, 127.0f, byte);
        return byte >= 64.0f ? 1.0f : std::pow (byte / 64.0f, 1.3f);
    }

    // Patch Volume: much steeper than linear (dB relative to 127).
    inline float patchVolumeGain (float byte)
    {
        byte = juce::jlimit (0.0f, 127.0f, byte);
        if (byte <= 0.0f) return 0.0f;
        static const float x[4] = { 32.0f, 64.0f, 100.0f, 127.0f };
        static const float y[4] = { -30.5f, -16.8f, -6.6f, 0.0f };
        float db;
        if (byte < 32.0f) db = -30.5f + 2.55f * 20.0f * std::log10 (byte / 32.0f);
        else
        {
            int i = 0; while (i < 2 && byte > x[i + 1]) ++i;
            const float f = (std::log (byte) - std::log (x[i])) / (std::log (x[i + 1]) - std::log (x[i]));
            db = y[i] + (y[i + 1] - y[i]) * f;
        }
        return juce::Decibels::decibelsToGain (db);
    }

    // Amp velocity: positive amounts duck soft notes, negative ones duck
    // hard notes (full negative silences a full-velocity note).
    inline float velocityGain (float amountBipolar, float velocity01)
    {
        return amountBipolar >= 0.0f ? 1.0f - amountBipolar * (1.0f - velocity01)
                                     : 1.0f + amountBipolar * velocity01;
    }

    //== saturation ===========================================================
    // Light / Soft / Medium / Hard are one arctangent curve at increasing
    // drive; Digital is a hard clip, Shaper a sine fold. Drive rises with Osc
    // Volume above the centre. Output is level matched: in the reference the drive
    // changes the tone, not the loudness.
    inline float satDrive (int curve, float oscVolByte)
    {
        static const float g[7][3] = {
            { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.77f, 3.93f },     // Light
            { 1.4f, 2.15f, 3.12f },     // Soft
            { 1.9f, 3.29f, 6.56f },     // Medium
            { 4.77f, 11.78f, 21.5f },   // Hard
            { 1.87f, 14.3f, 60.0f },    // Digital
            { 3.35f, 8.56f, 22.7f } };  // Shaper
        const int c = juce::jlimit (0, 6, curve);
        const float b = juce::jlimit (0.0f, 127.0f, oscVolByte);
        float lg;
        if (b <= 64.0f)      lg = std::log (g[c][0]);
        else if (b <= 96.0f) lg = std::log (g[c][0]) + (std::log (g[c][1]) - std::log (g[c][0])) * (b - 64.0f) / 32.0f;
        else                 lg = std::log (g[c][1]) + (std::log (g[c][2]) - std::log (g[c][1])) * (b - 96.0f) / 31.0f;
        return std::exp (lg);
    }

    // What's left of the level difference after the RMS make-up (measured).
    inline float satTrim (int curve, float oscVolByte)
    {
        static const float t[7][3] = {
            { 0.0f, 0.0f, 0.0f },
            { 0.2f, 1.0f, 0.7f }, { -0.4f, 1.7f, 1.3f }, { 1.1f, 1.6f, 1.2f }, { 1.1f, 1.0f, 0.3f },
            { 0.9f, -1.4f, -1.2f }, { -3.4f, -4.4f, -5.9f } };
        const int c = juce::jlimit (0, 6, curve);
        const float b = juce::jlimit (64.0f, 127.0f, oscVolByte);
        const float db = b <= 96.0f ? t[c][0] + (t[c][1] - t[c][0]) * (b - 64.0f) / 32.0f
                                    : t[c][1] + (t[c][2] - t[c][1]) * (b - 96.0f) / 31.0f;
        return juce::Decibels::decibelsToGain (db);
    }

    inline float satShape (int curve, float x)
    {
        switch (curve)
        {
            case 1: case 2: case 3: case 4: return std::atan (x);
            case 5:                         return juce::jlimit (-1.0f, 1.0f, x);
            case 6:                         return std::sin (x);
            default:                        return x;
        }
    }

    // RMS of a full-scale saw through the curve at this drive, relative to
    // the clean saw: the make-up gain that keeps the level where it was.
    inline float satMakeup (int curve, float drive)
    {
        constexpr int n = 256;
        double sIn = 0.0, sOut = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = 2.0f * ((float) i + 0.5f) / (float) n - 1.0f;
            const float y = satShape (curve, x * drive);
            sIn += (double) x * x; sOut += (double) y * y;
        }
        return sOut > 1.0e-12 ? (float) std::sqrt (sIn / sOut) : 1.0f;
    }


    //-- Filter envelope -> Osc 2 pitch, in semitones at full envelope. Measured
    //   on a sine: tiny around the centre, then doubling every 8 bytes up to
    //   +64 semitones at 127 (and nearly mirror-image below the centre).
    inline float envPitchSemis (float amountBipolar)      // -1..+1
    {
        static const float x[] { 0.0f, 4.0f, 8.0f, 16.0f, 24.0f, 32.0f, 40.0f, 48.0f, 56.0f, 63.0f };
        static const float y[] { 0.0f, 0.05f, 0.18f, 0.645f, 1.67f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
        const float d = juce::jlimit (0.0f, 63.0f, std::abs (amountBipolar) * 64.0f);   // bipolar is (byte-64)/64
        static const float lx[] { 24.0f, 32.0f, 40.0f, 48.0f, 56.0f, 63.0f };
        static const float ly[] { 0.74f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };   // log2 of the above
        const float v = d >= 24.0f ? std::exp2 (lookup (lx, ly, d)) : lookup (x, y, d);
        return amountBipolar < 0.0f ? -juce::jmin (v, 46.5f) : v;
    }

    //-- LFO 3 -> pitch depth in semitones (peak), by amount 0..1.
    inline float lfo3PitchSemis (float amount01)
    {
        static const float x[] { 0.0f, 64.0f, 100.0f, 108.0f, 117.0f, 127.0f };
        static const float y[] { 0.0f, 1.14f, 2.8f, 3.46f, 5.2f, 8.0f };
        return lookup (x, y, juce::jlimit (0.0f, 1.0f, amount01) * 127.0f);
    }

    //-- Analog (ladder) filter resonance, measured on a low saw.
    //   Feedback reaches self-oscillation (4.0) a little above byte 100 at
    //   mid cutoffs, but fades as the cutoff rises: by byte 96 of cutoff a
    //   resonance of 96 leaves no peak at all.
    //-- Analog modes (the 1..4 pole ladder), measured on a saw at C3 against
    //   a grid of cutoff (30..120 every 10) and resonance (0..127): the poles'
    //   frequency, the feedback around them and the level that comes out, each
    //   fitted with this ladder (zero-delay feedback, tanh at the input).
    struct Grid2
    {
        static constexpr int nc = 10, nr = 11;
        static constexpr float cx[nc] { 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
        static constexpr float rx[nr] { 0, 16, 32, 48, 64, 80, 96, 104, 112, 120, 127 };
        float v[nc][nr];

        float at (float cut, float res) const
        {
            const auto locate = [] (const float* xs, int n, float x, int& i, float& f)
            {
                if (x <= xs[0]) { i = 0; f = 0.0f; return; }
                if (x >= xs[n - 1]) { i = n - 2; f = 1.0f; return; }
                i = 0;
                while (x > xs[i + 1]) ++i;
                f = (x - xs[i]) / (xs[i + 1] - xs[i]);
            };
            int ic, ir; float fc, fr;
            locate (cx, nc, cut, ic, fc);
            locate (rx, nr, res, ir, fr);
            const float a = v[ic][ir] + (v[ic][ir + 1] - v[ic][ir]) * fr;
            const float b = v[ic + 1][ir] + (v[ic + 1][ir + 1] - v[ic + 1][ir]) * fr;
            return a + (b - a) * fc;
        }
    };

    // feedback (4 = the linear ladder's self-oscillation point)
    inline float analogK (float cut, float res)
    {
        static const Grid2 g { {
            { 0.0f, 1.22f, 2.47f, 3.12f, 3.51f, 3.92f, 4.57f, 5.0f, 5.4f, 5.8f, 6.2f },   // 30
            { 0.0f, 1.60f, 2.69f, 3.60f, 4.30f, 4.62f, 4.69f, 5.0f, 5.4f, 5.8f, 6.2f },   // 40
            { 0.0f, 1.11f, 1.71f, 2.44f, 2.87f, 3.20f, 3.67f, 4.6f, 5.0f, 5.4f, 5.8f },   // 50
            { 0.0f, 0.79f, 1.29f, 1.80f, 2.26f, 2.70f, 3.05f, 4.6f, 5.0f, 5.4f, 5.8f },   // 60
            { 0.0f, 0.49f, 0.84f, 1.19f, 1.53f, 1.86f, 2.19f, 4.6f, 5.0f, 5.4f, 5.8f },   // 70
            { 0.0f, 0.25f, 0.45f, 0.65f, 0.85f, 1.03f, 1.21f, 3.6f, 4.6f, 5.0f, 5.4f },   // 80
            { 0.0f, 0.02f, 0.11f, 0.20f, 0.28f, 0.36f, 0.43f, 1.1f, 2.3f, 4.0f, 4.4f },   // 90
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.1f, 2.0f, 2.7f, 4.2f },         // 100
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.11f, 0.24f, 0.37f, 1.28f, 2.01f, 2.71f, 3.74f },  // 110
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.05f, 0.13f, 0.20f, 0.86f, 1.47f, 1.75f, 1.75f }   // 120
        } };
        return g.at (cut, res);
    }

    // the poles' frequency against the (digital) cutoff map: a little lower
    // while the resonance is moderate, right on it once the ladder sings
    inline float analogPoleRatio (float cut, float res)
    {
        static const float x[] { 30, 60, 70, 80, 90, 100, 110, 120 };
        static const float y[] { 0.90f, 0.90f, 0.885f, 0.84f, 0.86f, 0.865f, 0.80f, 0.74f };
        const float low = lookup (x, y, cut);
        const float t = juce::jlimit (0.0f, 1.0f, (res - 96.0f) / 8.0f);
        return low + (1.0f - low) * t;
    }

    // While the ladder sings, the reference's earlier taps come out louder than
    // a plain ladder's do (its poles stay below the note it sings): measured
    // on the 1, 2 and 3 pole modes against the 4 pole one, dB at full.
    inline float analogSingWeight (float cut, float res)
    {
        static const float cx[] { 40, 70, 100, 120 };
        static const float at112[] { 0.54f, 0.12f, 0.0f, 0.0f };
        static const float at127[] { 1.15f, 0.95f, 0.48f, 0.3f };
        const float w112 = lookup (cx, at112, cut), w127 = lookup (cx, at127, cut);
        return res <= 96.0f ? 0.0f
             : res <= 112.0f ? w112 * (res - 96.0f) / 16.0f
                             : w112 + (w127 - w112) * (res - 112.0f) / 15.0f;
    }

    inline float analogTapBoostDb (int poles, float cut, float res)
    {
        static const float full[4] { 6.5f, 4.9f, 2.4f, 0.0f };
        static const float cx[] { 40, 70, 100, 120 };
        static const float at112[] { 0.54f, 0.12f, 0.0f, 0.0f };
        static const float at127[] { 1.15f, 0.95f, 0.48f, 0.3f };
        const float w112 = lookup (cx, at112, cut), w127 = lookup (cx, at127, cut);
        const float w = res <= 96.0f ? 0.0f
                      : res <= 112.0f ? w112 * (res - 96.0f) / 16.0f
                                      : w112 + (w127 - w112) * (res - 112.0f) / 15.0f;
        return full[juce::jlimit (0, 3, poles - 1)] * w;
    }

    // Once the ladder sings, the note through it is pushed down (the reference's
    // ladder saturates on its own tone): dB taken off the input.
    inline float analogDuckDb (float cut, float res)
    {
        static const Grid2 g { {
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },   // 30
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 7.67f, 12.24f, 12.14f, 19.31f },   // 40
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 7.68f, 12.94f, 12.27f, 17.28f },   // 50
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 5.54f, 12.37f, 11.44f, 12.95f },   // 60
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 2.66f, 5.57f, 9.60f, 10.16f },   // 70
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 1.28f, 1.55f, 7.95f },   // 80
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.36f, 0.38f, 0.00f, 1.16f },   // 90
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.18f, 0.15f, 0.15f, 0.00f },   // 100
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.19f, 0.23f, 0.34f, 0.52f },   // 110
            { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.31f, 0.33f, 0.38f, 0.44f }   // 120
        } };
        return g.at (cut, res);
    }

    // the level that comes out, dB, on top of undoing the ladder's own (1 + k) loss
    inline float analogLevelDb (float cut, float res)
    {
        static const Grid2 g { {
            { 8.84f, -1.76f, -6.00f, -7.99f, -9.23f, -10.43f, -12.20f, -18.14f, -17.78f, -20.98f, -22.94f },   // 30
            { 3.71f, -6.89f, -9.47f, -10.84f, -12.53f, -16.01f, -16.67f, -17.05f, -17.70f, -20.84f, -23.13f },   // 40
            { 3.06f, -5.01f, -7.63f, -10.14f, -11.66f, -12.15f, -11.84f, -15.69f, -16.07f, -19.75f, -22.71f },   // 50
            { 1.41f, -4.13f, -6.51f, -8.39f, -9.91f, -11.33f, -12.65f, -15.76f, -14.99f, -18.70f, -22.63f },   // 60
            { -0.03f, -3.03f, -5.04f, -6.67f, -8.06f, -9.24f, -10.33f, -15.45f, -17.86f, -17.38f, -21.70f },   // 70
            { 0.46f, -1.84f, -3.33f, -4.61f, -5.72f, -6.70f, -7.59f, -14.57f, -17.29f, -20.16f, -18.76f },   // 80
            { 0.58f, -1.02f, -2.03f, -2.96f, -3.76f, -4.47f, -5.18f, -10.01f, -13.66f, -20.40f, -20.60f },   // 90
            { -0.63f, -1.53f, -2.33f, -3.06f, -3.71f, -4.36f, -4.95f, -10.14f, -13.68f, -16.26f, -20.63f },   // 100
            { -1.83f, -2.73f, -3.70f, -4.56f, -5.51f, -6.40f, -7.17f, -12.35f, -16.05f, -18.65f, -22.79f },   // 110
            { -2.28f, -3.18f, -3.91f, -4.54f, -5.23f, -5.92f, -6.53f, -10.83f, -14.14f, -16.55f, -20.44f }   // 120
        } };
        return g.at (cut, res);
    }

    //-- HyperSaw (counted from the partials around the fundamental in the reference):
    //   Density 0 is a single saw; 3 saws through the middle, 5 then 9 at the top.
    inline int hypersawCount (float densityByte)
    {
        return densityByte < 16.0f ? 1 : densityByte < 80.0f ? 3 : densityByte < 112.0f ? 5 : densityByte < 120.0f ? 7 : 9;
    }
    //   Outer detune in cents (+-): steep in Spread (about 7 % of full at the
    //   centre), and wider with more saws (137 cents at full Density/Spread).
    inline float hypersawSpreadCents (float spreadByte, float densityByte)
    {
        static const float x[] { 0.0f, 32.0f, 64.0f, 96.0f, 127.0f }, y[] { 0.37f, 0.37f, 0.47f, 0.66f, 1.0f };
        return 137.0f * std::pow (juce::jlimit (0.0f, 1.0f, spreadByte / 127.0f), 3.8f) * lookup (x, y, densityByte);
    }

    //-- Distortion curves 1..11 (the effect section's, not the voice saturation).
    //   Measured on a saw: the stage sits BEFORE Patch Volume, so how hard it
    //   drives depends on the voices, not on the volume knob. The input is
    //   normalised to one full voice (kDistRef); curves 1..6 are a waveshaper
    //   with a drive and a make-up per Intensity, fitted to the harmonics the
    //   reference produces at Intensity 0, 32, 64, 80 and 127.
    constexpr float kDistRef = 0.85f;    // the peak of one plain voice on the effect bus

    inline float distTable (const float (&v)[5], float intensity, bool logDomain)
    {
        static const float x[5] { 0.0f, 32.0f, 64.0f, 80.0f, 127.0f };
        if (! logDomain) return lookup (x, v, juce::jlimit (0.0f, 127.0f, intensity));
        float lv[5]; for (int k = 0; k < 5; ++k) lv[k] = std::log (v[k]);
        return std::exp (lookup (x, lv, juce::jlimit (0.0f, 127.0f, intensity)));
    }

    // drive (on the normalised input) and make-up gain for curves 1..6
    inline void distShaper (int curve, float intensity, float& drive, float& gain)
    {
        static const float d[6][5] = {
            { 1.13f, 1.54f, 2.25f, 2.42f, 3.20f },     // Light   (tanh)
            { 1.25f, 1.63f, 1.90f, 2.74f, 2.82f },     // Soft    (tanh)
            { 1.90f, 2.70f, 3.58f, 3.80f, 4.47f },     // Medium  (tanh)
            { 6.26f, 7.83f, 10.5f, 15.8f, 27.2f },     // Hard    (arctangent)
            { 1.78f, 5.22f, 15.8f, 59.6f, 64.9f },     // Digital (hard clip)
            { 3.11f, 5.07f, 6.35f, 10.65f, 23.0f } };  // Shaper  (sine fold)
        static const float g[6][5] = {
            { 0.6f, -0.3f, -0.25f, 0.07f, -2.13f },
            { -0.5f, -0.24f, -1.26f, -1.54f, -1.56f },
            { -1.0f, -1.74f, -1.2f, -1.0f, -1.96f },
            { -4.54f, -5.18f, -5.99f, -6.26f, -7.18f },
            { -1.36f, -3.34f, -5.12f, -5.69f, -5.19f },
            { -4.96f, -5.26f, -7.45f, -7.44f, -8.63f } };
        const int c = juce::jlimit (1, 6, curve) - 1;
        drive = distTable (d[c], intensity, true);
        gain = juce::Decibels::decibelsToGain (distTable (g[c], intensity, false));
    }

    inline float distShape (int curve, float x)
    {
        switch (curve)
        {
            case 1: case 2: case 3: return std::tanh (x);
            case 4:                 return std::atan (x);
            case 5:                 return juce::jlimit (-1.0f, 1.0f, x);
            default:                return std::sin (x);
        }
    }

    // Rectifier: folds the signal at a point that comes in from beyond the peak
    // (no change up to Intensity 80) to the centre at 127, where a saw becomes
    // a triangle. Returns the fold point; the make-up keeps a saw's level.
    inline float distFoldPoint (float intensity)
    {
        static const float x[] { 0.0f, 80.0f, 104.0f, 127.0f }, y[] { 1.05f, 1.05f, 0.55f, 0.0f };
        return lookup (x, y, juce::jlimit (0.0f, 127.0f, intensity));
    }
    inline float distFoldMakeup (float c)
    {
        // standard deviation of |x - c| for x uniform in -1..1, against the saw's 1/sqrt(3)
        c = juce::jlimit (0.0f, 1.05f, c);
        double m1 = 0.0, m2 = 0.0; constexpr int n = 64;
        for (int i = 0; i < n; ++i) { const double xx = -1.0 + 2.0 * (i + 0.5) / n, y = std::abs (xx - c); m1 += y; m2 += y * y; }
        m1 /= n; m2 /= n;
        const double sd = std::sqrt (juce::jmax (1.0e-9, m2 - m1 * m1));
        return (float) (0.57735 / sd) * (c > 0.95f ? 0.85f : 1.05f);
    }

    // The voice saturation's Rectifier folds the same way, with the fold point
    // coming in as Osc Volume rises (measured: none up to 80, 0.78 of the peak
    // at 96, 0.22 at 127).
    inline float satFoldPoint (float oscVolByte)
    {
        static const float x[] { 0.0f, 80.0f, 96.0f, 127.0f }, y[] { 1.05f, 1.05f, 0.78f, 0.22f };
        return lookup (x, y, juce::jlimit (0.0f, 127.0f, oscVolByte));
    }

    // Bit Reducer: quantisation steps per unit of the normalised input
    inline float distSteps (float intensity) { return 0.75f * std::exp2 ((127.0f - juce::jlimit (0.0f, 127.0f, intensity)) / 8.0f); }
    // Rate Reducer: samples held (1 = untouched)
    inline int distHold (float intensity) { return intensity <= 80.0f ? 1 : (int) std::exp2 ((intensity - 80.0f) / 8.0f); }

    // Low Pass / High Pass "curves": one-pole filters whose corner follows
    // Intensity (measured on noise; 0 leaves the signal alone).
    inline float distLowPassHz (float intensity)
    {
        static const float x[] { 0.0f, 8.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f, 80.0f, 96.0f, 112.0f, 127.0f };
        static const float y[] { 15.0f, 13.93f, 13.40f, 12.92f, 12.38f, 11.23f, 9.88f, 8.50f, 7.43f, 5.88f, 4.9f };   // log2 Hz
        return std::exp2 (lookup (x, y, juce::jlimit (0.0f, 127.0f, intensity)));
    }
    inline float distHighPassHz (float intensity)
    {
        static const float x[] { 0.0f, 8.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f, 80.0f, 96.0f, 112.0f };
        static const float y[] { 3.0f, 5.52f, 5.67f, 6.41f, 6.97f, 8.19f, 9.50f, 10.79f, 12.12f, 13.99f };   // log2 Hz
        return std::exp2 (lookup (x, y, juce::jlimit (0.0f, 112.0f, intensity)));
    }
    inline float distHighPassLossDb (float intensity) { return intensity <= 112.0f ? 0.0f : -13.0f * (intensity - 112.0f) / 15.0f; }

    //-- EQ, fitted with RBJ shelves/bells to the reference's response on noise.
    //   Both shelves have a Q of about 0.47; gain is +-16 dB as before.
    inline float eqShelfQ() { return 0.47f; }
    inline float eqLowHz (float byte)
    {
        static const float x[] { 0.0f, 32.0f, 64.0f, 127.0f }, y[] { 5.6f, 6.25f, 6.95f, 9.6f };   // log2 Hz
        return std::exp2 (lookup (x, y, byte));
    }
    inline float eqHighHz (float byte)      // 127 is past the top of the audio band: no effect
    {
        static const float x[] { 0.0f, 32.0f, 64.0f, 96.0f, 127.0f }, y[] { 9.03f, 10.1f, 11.16f, 12.2f, 15.0f };
        return std::exp2 (lookup (x, y, byte));
    }
    inline float eqMidHz (float byte)       { return juce::jmax (20.0f, 665.0f * std::exp2 ((byte - 64.0f) * 0.0806f)); }
    inline float eqQ (float byte)           { return 0.15f * std::pow (9.67f, byte / 127.0f); }

    //-- Analog Boost (Bass Intensity / Bass Tune): a broad low shelf whose corner
    //   rises with Tune, and towards the top of Tune a high-shelf cut as well -
    //   more of a tilt. Gains are for full intensity.
    struct AnalogBoost { float lowHz, lowDb, highDb; };
    inline AnalogBoost analogBoost (float intensityByte, float tuneByte)
    {
        static const float x[] { 0.0f, 32.0f, 64.0f, 96.0f, 127.0f };
        static const float lf[] { 6.5f, 8.2f, 9.0f, 9.35f, 9.65f };      // log2 Hz
        static const float lg[] { 5.0f, 6.5f, 7.5f, 5.0f, 2.5f };
        static const float hg[] { 0.0f, 0.0f, -2.0f, -4.5f, -7.5f };
        const float i = juce::jlimit (0.0f, 1.0f, intensityByte / 127.0f);
        const float sL = std::pow (i, 0.75f), sH = std::pow (i, 0.9f);
        return { std::exp2 (lookup (x, lf, tuneByte)), lookup (x, lg, tuneByte) * sL, lookup (x, hg, tuneByte) * sH };
    }
}

//==============================================================================
// One 2-pole state-variable filter (trapezoidal, i.e. the bilinear transform
// with prewarping - exactly the model the reference's responses fit to within a
// tenth of a dB), stereo.
//==============================================================================
struct Svf
{
    float ic1[2] {}, ic2[2] {};

    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    struct Coefs { float g { 0.1f }, k { 1.4f }, a1 { 1 }, a2 { 0 }, a3 { 0 }; };

    static Coefs make (float hz, float q, double sr)
    {
        Coefs c;
        c.g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (5.0f, (float) (sr * 0.49), hz) / (float) sr);
        c.k = 1.0f / juce::jmax (0.05f, q);
        c.a1 = 1.0f / (1.0f + c.g * (c.g + c.k));
        c.a2 = c.g * c.a1;
        c.a3 = c.g * c.a2;
        return c;
    }

    enum Mode { LP = 0, HP = 1, BP = 2, BS = 3 };

    inline float process (float v0, const Coefs& c, int mode, int ch)
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = c.a1 * ic1[ch] + c.a2 * v3;
        const float v2 = ic2[ch] + c.a2 * ic1[ch] + c.a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        // keep a self-oscillating filter from running away
        if (! (std::abs (ic1[ch]) < 50.0f)) { ic1[ch] = 0.0f; ic2[ch] = 0.0f; }
        switch (mode)
        {
            case HP: return v0 - c.k * v1 - v2;
            case BP: return v1;                      // peaks at Q; the engine applies the band-pass gain
            case BS: return v0 - c.k * v1;
            default: return v2;
        }
    }
};

// The Analog 1-4 pole modes.
struct Ladder
{
    // Four TPT one-poles with zero-delay feedback taken from the last stage,
    // as in a transistor ladder; the Analog 1..4 pole modes tap the output
    // after that many stages (the reference's 1-pole mode resonates and even
    // self-oscillates, so the feedback cannot come from the tap itself).
    float s[4][2] {};
    void reset() { std::memset (s, 0, sizeof (s)); }

    // sat1: how far the first stage's output is pushed into a soft square
    // (0 = clean; the reference's ladder does this while it self-oscillates,
    // which is where its 1 and 2 pole outputs get their odd harmonics)
    float sat1 { 0.0f };

    inline float process (float x, float G, float feedback, int tap, int ch)
    {
        float S = 0.0f;
        for (int p = 0; p < 4; ++p) S = G * S + (1.0f - G) * s[p][ch];
        const float G4 = G * G * G * G;
        const float yEst = (G4 * x + S) / (1.0f + feedback * G4);
        float u = std::tanh (x - feedback * yEst);
        float out = u;
        for (int p = 0; p < 4; ++p)
        {
            const float v = (u - s[p][ch]) * G;
            float y = v + s[p][ch];
            s[p][ch] = y + v;
            if (p == 0 && sat1 > 0.0f)
            {
                constexpr float peak = 0.74f, drive = 3.5f;
                static const float norm = peak / std::tanh (drive);
                y += (norm * std::tanh (drive * y / peak) - y) * sat1;
            }
            u = y;
            if (p + 1 == tap) out = y;
        }
        return out;
    }
};

//==============================================================================
// The Original engine's phaser (measured on white noise, held still): Mode
// 0..5 is 1..6 second-order all-pass sections at Frequency (Q 2.9), Spread
// fans them out (1.68 octaves apart at full, where their Q has dropped to
// 0.78, so the notches stay deep and wide), Feedback (bipolar: 0.93 of
// the knob into the loop gain upwards, 0.88 downwards) goes round the whole
// chain with one sample of delay (the right channel's sections sit 0.15
// octave above the left's), and Mix is an equal-power blend: an even mix
// at 64 (the deepest notches), all phase-shifted signal at 127. The sweep
// (traced by a full-feedback resonance) is a triangle that only rises from
// Frequency (the sections by up to 3.8 octaves at full Depth, which moves
// the resonance about 2.3), at 0.02 Hz (Rate 0) to
// about 18 Hz (127), the same on both channels.
//==============================================================================
struct MeasuredPhaser
{
    struct Settings { int sections { 4 }; float centreHz { 1000 }, spreadOct { 0 }, q { 2.9f }, feedback { 0 }, rateHz { 0.5f }, depthOct { 0 }; };

    float z1[2][6] {}, z2[2][6] {};
    float b0[2][6] {}, b1[2][6] {}, b2[2][6] {}, a1[2][6] {}, a2[2][6] {};
    float last[2] {}, lp[2] {};
    double phase { 0.0 };
    double sr { 44100.0 };
    int countdown { 0 };
    float lpCoef { 0.4f };

    void prepare (double sampleRate) { sr = sampleRate; lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 3700.0f / (float) sr); reset(); }
    void reset() { std::memset (z1, 0, sizeof (z1)); std::memset (z2, 0, sizeof (z2)); last[0] = last[1] = lp[0] = lp[1] = 0.0f; phase = 0.0; countdown = 0; }

    void update (const Settings& s)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            // the sweep only goes up from Frequency: a triangle from 0 to Depth's range
            const double lfo = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
            for (int i = 0; i < s.sections; ++i)
            {
                const float offset = s.sections > 1 ? s.spreadOct * ((float) i - 0.5f * (float) (s.sections - 1)) : 0.0f;
                // (the right channel's sections sit 0.147 octave above the left's - measured)
                const float hz = juce::jlimit (20.0f, (float) (sr * 0.45), s.centreHz * std::exp2 (offset + s.depthOct * (float) lfo + (ch == 1 ? 0.147f : 0.0f)));
                const float w = juce::MathConstants<float>::twoPi * hz / (float) sr;
                const float al = std::sin (w) / (2.0f * s.q), c = std::cos (w), a0 = 1.0f + al;
                b0[ch][i] = (1.0f - al) / a0; b1[ch][i] = -2.0f * c / a0; b2[ch][i] = 1.0f;
                a1[ch][i] = b1[ch][i]; a2[ch][i] = b0[ch][i];
            }
        }
    }

    void process (float& l, float& r, float mix01, const Settings& s)
    {
        phase += (double) s.rateHz / sr;
        if (phase >= 1.0) phase -= 1.0;
        if (--countdown <= 0) { update (s); countdown = 16; }

        const float th = juce::jlimit (0.0f, 1.0f, mix01) * juce::MathConstants<float>::halfPi;
        const float dry = std::cos (th), wet = std::sin (th);
        float* io[2] = { &l, &r };
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = *io[ch] + s.feedback * last[ch];
            for (int i = 0; i < s.sections; ++i)
            {
                const float y = b0[ch][i] * x + z1[ch][i];
                z1[ch][i] = b1[ch][i] * x - a1[ch][i] * y + z2[ch][i];
                z2[ch][i] = b2[ch][i] * x - a2[ch][i] * y;
                x = y;
            }
            last[ch] = x;
            *io[ch] = dry * *io[ch] + wet * x;
        }
    }
};

//==============================================================================
// The Original engine's chorus: one modulated delay per channel, the right
// channel's sweep the mirror image of the left's (which is where the width
// comes from), feedback around each line, and an equal-power dry/wet mix in
// which the wet signal sits a little lower than the dry (measured: wet alone
// is about 3.5 dB down).
//==============================================================================
struct StereoChorus
{
    std::vector<float> line[2];
    int writePos { 0 };
    double phase { 0.0 };
    float last[2] {};
    float shValue { 0.0f }, shPrev { 0.0f };
    juce::Random rng { 0x5eed };
    static constexpr float kDepthMs = 10.0f;     // sweep at full Depth, either way
    static constexpr float kWetGain = 0.8f;

    void prepare (double sr)
    {
        const int n = juce::jmax (8, (int) (sr * 0.08));
        for (auto& l : line) l.assign ((size_t) n, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0; phase = 0.0; last[0] = last[1] = 0.0f; shValue = shPrev = 0.0f;
    }

    // shape: 0 sine, 1 triangle, 2 saw, 3 square, 4 sample & hold, 5 sample & glide
    float lfo (int shape)
    {
        const double ph = phase;
        switch (shape)
        {
            case 0:  return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
            case 2:  return (float) (2.0 * ph - 1.0);
            case 3:  return ph < 0.5 ? 1.0f : -1.0f;
            case 4:  return shValue;
            case 5:  return shPrev + (shValue - shPrev) * (float) ph;
            default: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
        }
    }

    void process (float& l, float& r, double sr, float rateHz, float baseMs, float depth01,
                  float feedback, float mix01, int shape)
    {
        const int size = (int) line[0].size();
        if (size < 8) return;
        phase += rateHz / sr;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            shPrev = shValue; shValue = rng.nextFloat() * 2.0f - 1.0f;
        }
        const float mod = lfo (shape);
        const float in[2] = { l, r };
        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            const float sweep = c == 0 ? mod : -mod;
            // the sweep is absolute, but never more than twice the base delay (no width at no delay)
            const float ms = juce::jmax (0.05f, baseMs + juce::jmin (kDepthMs * depth01, 2.0f * baseMs) * sweep);
            const double d = juce::jlimit (1.0, (double) size - 2.0, ms * 0.001 * sr);
            double rp = (double) writePos - d;
            if (rp < 0.0) rp += size;
            const int i0 = (int) rp, i1 = (i0 + 1) % size;
            const float fr = (float) (rp - i0);
            wet[c] = line[c][(size_t) i0] + (line[c][(size_t) i1] - line[c][(size_t) i0]) * fr;
            line[c][(size_t) writePos] = in[c] + feedback * wet[c];
        }
        writePos = (writePos + 1) % size;
        const float a = mix01 * juce::MathConstants<float>::halfPi;
        const float gd = std::cos (a), gw = std::sin (a) * kWetGain;
        l = in[0] * gd + wet[0] * gw;
        r = in[1] * gd + wet[1] * gw;
    }
};

//==============================================================================
// The Original engine's delay, measured with clicks through every mode. Each
// side is one feedback loop of a single period with its own first echo:
//   Simple          both sides: first echo T, period T
//   Ping Pong a:b   left T, right (b/a)T, period T
//   Pattern a+b     unit u = T/2: left a*u, right b*u, both repeating every a*u
// Colour is a one-pole filter on the way out and again inside the loop, so
// every repeat is a little darker (or thinner) than the one before; the loop
// gain is 0.88 x Feedback. A slow sine moves the two sides' read points in
// quadrature (Delay Modulation).
//==============================================================================
struct PatternDelay
{
    static constexpr double kMaxSeconds = 6.0;

    struct Settings
    {
        float firstMs[2] { 250.0f, 250.0f }, periodMs { 250.0f };
        float feedback { 0.0f };                 // loop gain
        float modHz { 0.3f }, modMs { 0.0f };    // modulation rate and depth
        float colourCoef { 0.0f }; int colourMode { 0 };   // 0 off, 1 low pass, 2 high pass
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : buf)
            b.assign ((size_t) (sampleRate * kMaxSeconds) + 8, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        pos = 0; phase = 0.0;
        for (auto& z : outState) z = 0.0f;
        for (auto& z : loopState) z = 0.0f;
    }

    // One-pole coefficient for a colour: low pass (plain one-pole) or high
    // pass (matched one-pole, flat to the top).
    static float colourCoefFor (int mode, float hz, double sr)
    {
        hz = juce::jlimit (5.0f, (float) (sr * 0.45), hz);
        if (mode == 1) return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sr);
        const float g = std::tan (juce::MathConstants<float>::pi * hz / (float) sr);
        return g / (1.0f + g);
    }

    void process (float inL, float inR, float& outL, float& outR, const Settings& s)
    {
        const int size = (int) buf[0].size();
        if (size < 16) { outL = outR = 0.0f; return; }

        phase += (double) s.modHz / fs;
        if (phase >= 1.0) phase -= 1.0;
        const double modS = (double) s.modMs * 0.001 * fs;
        const double sn = modS * std::sin (juce::MathConstants<double>::twoPi * phase);
        const double m[2] { sn, -sn };   // the sides in anti-phase (measured)
        const double maxD = (double) size - 4.0;

        const float in[2] { inL, inR };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const double dFirst = juce::jlimit (1.0, maxD, (double) s.firstMs[c] * 0.001 * fs + m[c]);
            const double dLoop  = juce::jlimit (1.0, maxD, (double) s.periodMs * 0.001 * fs + m[c]);
            const float tapFirst = read (c, dFirst, size), tapLoop = read (c, dLoop, size);
            out[c] = colour (tapFirst, outState[c], s);
            buf[(size_t) c][(size_t) pos] = in[c] + s.feedback * colour (tapLoop, loopState[c], s);
        }
        pos = (pos + 1) % size;
        outL = out[0]; outR = out[1];
    }

private:
    float read (int c, double delay, int size) const
    {
        double p = (double) pos - delay;
        while (p < 0.0) p += size;
        // 4-point Hermite: linear interpolation would dull every repeat
        const int i1 = (int) p, i0 = (i1 - 1 + size) % size, i2 = (i1 + 1) % size, i3 = (i1 + 2) % size;
        const float f = (float) (p - (double) i1);
        const auto& b = buf[(size_t) c];
        const float y0 = b[(size_t) i0], y1 = b[(size_t) i1], y2 = b[(size_t) i2], y3 = b[(size_t) i3];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    static float colour (float x, float& z, const Settings& s)
    {
        if (s.colourMode == 1) { z += (x - z) * s.colourCoef; return z; }
        if (s.colourMode == 2)
        {
            const float v = (x - z) * s.colourCoef, lp = v + z;
            z = lp + v;
            return x - lp;
        }
        return x;
    }

    double fs { 44100.0 }, phase { 0.0 };
    std::vector<float> buf[2];
    int pos { 0 };
    float outState[2] {}, loopState[2] {};
};

namespace curves
{
    // Delay Color (measured, per echo): a one-pole low pass below the centre
    // (340 Hz at 0, 1.7 kHz at 32), a one-pole high pass above it (255 Hz at
    // 96, 4 kHz at 127). Returns Hz; mode 1 = low pass, 2 = high pass, 0 = off.
    inline float delayColourHz (float byte, int& mode)
    {
        if (byte < 63.5f)
        {
            mode = 1;
            static const float x[] { 0, 32, 48, 64 };
            static const float lnHz[] { std::log (340.0f), std::log (1700.0f), std::log (6000.0f), std::log (40000.0f) };
            return std::exp (lookup (x, lnHz, byte));
        }
        if (byte > 64.5f)
        {
            mode = 2;
            static const float x[] { 64, 80, 96, 127 };
            static const float lnHz[] { std::log (5.0f), std::log (60.0f), std::log (255.0f), std::log (4000.0f) };
            return std::exp (lookup (x, lnHz, byte));
        }
        mode = 0;
        return 0.0f;
    }

    // Delay Time without a clock (measured echo times).
    inline float delayTimeMs (float byte)
    {
        static const float x[] { 0, 20, 64, 110, 127 };
        static const float y[] { 1.0f, 111.0f, 343.0f, 592.0f, 686.0f };
        return lookup (x, y, byte);
    }
}

//==============================================================================
// The Original engine's reverb: an eight-line feedback delay network whose
// decay is set directly as a time (the reference's Time byte runs from a short
// slap to a frozen tail), with input diffusion, gentle damping, a predelay and
// the reference's colour control (low pass below the centre, high pass above).
//==============================================================================
struct FdnReverb
{
    static constexpr int kLines = 8;
    std::vector<float> line[kLines];
    int pos[kLines] {}, len[kLines] {};
    float damp[kLines] {};
    std::vector<float> pre[2]; int prePos { 0 };
    std::vector<float> ap[4]; int apPos[4] {};
    float colourState[2] {};
    double sr { 44100.0 };
    float gain[kLines] {}; float lastRt { -1.0f }; float dampCoef { 0.3f };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static const int base[kLines] { 1327, 1453, 1601, 1733, 1867, 1999, 2143, 2269 };
        for (int i = 0; i < kLines; ++i)
        {
            len[i] = juce::jmax (8, (int) (base[i] * sr / 44100.0));
            line[i].assign ((size_t) len[i], 0.0f);
        }
        static const int apl[4] { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i) ap[i].assign ((size_t) juce::jmax (4, (int) (apl[i] * sr / 44100.0)), 0.0f);
        for (auto& p : pre) p.assign ((size_t) (sr * 0.4) + 2, 0.0f);
        dampCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 16000.0f / (float) sr);
        reset();
    }
    void reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& a : ap) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& p : pre) std::fill (p.begin(), p.end(), 0.0f);
        for (int i = 0; i < kLines; ++i) { pos[i] = 0; damp[i] = 0.0f; }
        for (int i = 0; i < 4; ++i) apPos[i] = 0;
        prePos = 0; colourState[0] = colourState[1] = 0.0f; lastRt = -1.0f;
    }

    // rt60 in seconds (very large = frozen); colourG one-pole coefficient, colourHp true for high pass
    void process (float inL, float inR, float& outL, float& outR, float rt60, float predelayMs,
                  float colourG, int colourMode)
    {
        if (rt60 != lastRt)
        {
            lastRt = rt60;
            for (int i = 0; i < kLines; ++i)
                gain[i] = rt60 > 200.0f ? 1.0f : std::pow (10.0f, -3.0f * (float) len[i] / ((float) sr * juce::jmax (0.05f, rt60)));
        }
        // predelay
        const int psz = (int) pre[0].size();
        const int pd = juce::jlimit (0, psz - 1, (int) (predelayMs * 0.001f * (float) sr));
        pre[0][(size_t) prePos] = inL; pre[1][(size_t) prePos] = inR;
        int rp = prePos - pd; if (rp < 0) rp += psz;
        float x = 0.5f * (pre[0][(size_t) rp] + pre[1][(size_t) rp]);
        float xs = 0.5f * (pre[0][(size_t) rp] - pre[1][(size_t) rp]);
        prePos = (prePos + 1) % psz;
        // colour on the way in
        if (colourMode != 0)
        {
            colourState[0] += (x - colourState[0]) * colourG;
            colourState[1] += (xs - colourState[1]) * colourG;
            x = colourMode < 0 ? colourState[0] : x - colourState[0];
            xs = colourMode < 0 ? colourState[1] : xs - colourState[1];
        }
        // input diffusion
        for (int i = 0; i < 4; ++i)
        {
            auto& a = ap[i]; const int n = (int) a.size();
            const float d = a[(size_t) apPos[i]];
            const float v = x + 0.6f * d;
            a[(size_t) apPos[i]] = v;
            x = d - 0.6f * v;
            apPos[i] = (apPos[i] + 1) % n;
        }
        // the network
        float o[kLines]; float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            o[i] = line[(size_t) i][(size_t) pos[i]];
            damp[i] += (o[i] - damp[i]) * dampCoef;
            o[i] = damp[i];
            sum += o[i];
        }
        const float h = sum * (2.0f / kLines);
        for (int i = 0; i < kLines; ++i)
        {
            static const float sgn[kLines] { 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f };
            const float inj = sgn[i] * ((i & 1) ? x + xs : x - xs);
            line[(size_t) i][(size_t) pos[i]] = (o[i] - h) * gain[i] + inj * 0.35f;
            pos[i] = (pos[i] + 1) % len[i];
        }
        outL = o[0] - o[2] + o[4] - o[6] + 0.5f * (o[1] - o[5]);
        outR = o[1] - o[3] + o[5] - o[7] + 0.5f * (o[2] - o[6]);
    }
};

namespace curves
{
    // Reverb (measured through a short note): decay time by the Time byte
    // (0.7 s at 32, 2.35 s at 64, 6.5 s at 96, frozen at 127), send law,
    // predelay and colour.
    inline float reverbRt60 (float byte)
    {
        static const float x[] { 0.0f, 16.0f, 32.0f, 48.0f, 64.0f, 80.0f, 96.0f, 112.0f, 120.0f, 127.0f };
        static const float y[] { -2.9f, -1.6f, -0.51f, 0.4f, 1.23f, 2.0f, 2.70f, 3.9f, 5.0f, 9.0f };   // log2 s
        return std::exp2 (lookup (x, y, juce::jlimit (0.0f, 127.0f, byte)));
    }
    // the tail's level for a given decay, relative to the send (dB), so short and frozen tails sit as measured
    inline float reverbLevelDb (float byte)
    {
        static const float x[] { 0.0f, 32.0f, 64.0f, 96.0f, 127.0f };
        static const float y[] { 8.0f, 8.0f, 0.0f, -0.4f, -9.9f };
        return lookup (x, y, juce::jlimit (0.0f, 127.0f, byte));
    }
    inline float reverbSendGain (float send01) { return std::pow (juce::jlimit (0.0f, 1.0f, send01), 1.33f); }
    inline float reverbPredelayMs (float byte) { return juce::jmin (64.0f, juce::jmax (0.0f, byte)) * 3.0f; }
    // colour: below the centre a low pass closing to ~250 Hz, above it a high pass opening to ~5 kHz
    inline float reverbColourHz (float byte)
    {
        const float d = (byte - 64.0f) / 64.0f;
        return d < 0.0f ? 16000.0f * std::exp2 (6.0f * d) : 20.0f * std::exp2 (8.0f * d);
    }
}

} // namespace original
} // namespace aquavibrio
