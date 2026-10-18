#pragma once
#include <JuceHeader.h>
#include <algorithm>

namespace aquanova {

//==============================================================================
struct StereoSample { float l = 0.0f, r = 0.0f; };

//==============================================================================
/** Leaky integrator DC blocker: y[n] = x[n] - x[n-1] + leak * y[n-1].

    The differencer kills the offset, the leaky integrator in the feedback path
    puts the low end back. The very first sample of any new DC content passes
    through unchanged - the filter only pulls it back out over the corner's
    time constant - so a fast corner is what makes that pull-back audible as
    a "clack" on a note's onset, while a slow one spreads the same pull-back
    over long enough that the ear never registers it as a transient. The
    corner is a runtime knob (the DC Cancel Speed control) rather than fixed:
    1 Hz settles over roughly half a second and is inaudible; 5 Hz settles in
    well under a tenth of a second and can click on percussive, DC-heavy
    material. Either end still fully cancels a steady offset - from an
    asymmetric wave, a self-oscillating filter, hard distortion, or anything
    circulating in a feedback line - it is only the speed, not whether it
    works, that changes.
*/
class DcBlocker
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = juce::jmax (8000.0, sr);
        setCornerHz (1.0f);
        reset();
    }

    /** Recomputes the corner. Cheap enough to call once per block - see
        SynthEngine::process(), which drives it from the DC Cancel Speed knob. */
    void setCornerHz (float hz) noexcept
    {
        leak = (float) std::exp (-juce::MathConstants<double>::twoPi * (double) hz / sampleRate);
    }

    void reset() noexcept { x1l = y1l = x1r = y1r = 0.0f; }

    inline void process (float* left, float* right, int num) noexcept
    {
        for (int i = 0; i < num; ++i)
        {
            const float inL = left[i];
            y1l = inL - x1l + leak * y1l;
            x1l = inL;
            left[i] = y1l;

            const float inR = right[i];
            y1r = inR - x1r + leak * y1r;
            x1r = inR;
            right[i] = y1r;
        }
    }

private:
    double sampleRate = 48000.0;
    float leak = 0.999f;
    float x1l = 0.0f, y1l = 0.0f, x1r = 0.0f, y1r = 0.0f;
};

//==============================================================================
/** A transparent safety limiter for the master bus.

    Nothing upstream keeps the sum of several voices, or a self-oscillating
    resonant filter, under 0 dBFS - a chord, or resonance turned up, can push
    the buffer past +-1 by design. Below a -1 dB knee this passes the signal
    completely unchanged; above it, peaks are eased toward +-1 with a smooth
    tanh curve instead of flat-topping, so pushing a patch hard rounds off
    gracefully instead of hard-clipping and refusing to get any louder.
*/
class OutputLimiter
{
public:
    inline void process (float* left, float* right, int num) const noexcept
    {
        for (int i = 0; i < num; ++i)
        {
            left[i]  = shape (left[i]);
            right[i] = shape (right[i]);
        }
    }

private:
    static inline float shape (float x) noexcept
    {
        const float ax = std::abs (x);
        if (ax <= kKnee)
            return x;

        const float over = (ax - kKnee) / (1.0f - kKnee);
        return std::copysign (kKnee + (1.0f - kKnee) * std::tanh (over), x);
    }

    static constexpr float kKnee = 0.891f;   // -1 dBFS
};

//==============================================================================
/** Simple shelving bass/treble pair, matching the SN2's two-knob EQ. */
class EqSection
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; reset(); }
    void reset() noexcept { lowL = lowR = highL = highR = 0.0f; }

    inline StereoSample process (StereoSample in, float bass, float treble) noexcept
    {
        const float aLow  = coeff (200.0f);
        const float aHigh = coeff (3000.0f);

        lowL  += aLow  * (in.l - lowL);
        lowR  += aLow  * (in.r - lowR);
        highL += aHigh * (in.l - highL);
        highR += aHigh * (in.r - highR);

        const float bGain = bass   * 1.5f;
        const float tGain = treble * 1.5f;

        return { in.l + lowL * bGain + (in.l - highL) * tGain,
                 in.r + lowR * bGain + (in.r - highR) * tGain };
    }

private:
    inline float coeff (float hz) const noexcept
    {
        return 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                * juce::jlimit (0.0005f, 0.49f, hz / (float) sampleRate));
    }

    double sampleRate = 44100.0;
    float lowL = 0, lowR = 0, highL = 0, highR = 0;
};

//==============================================================================
/** Distortion with output level, gain compensation and a morphing curve. */
class Distortion
{
public:
    inline StereoSample process (StereoSample in, float level, float output,
                                 float gainComp, float curve) const noexcept
    {
        if (level <= 0.0005f)
            return in;

        const float drive = 1.0f + level * 60.0f;
        const float comp = 1.0f / (1.0f + level * 8.0f * (0.5f + gainComp * 0.5f));
        const float hard = juce::jlimit (0.0f, 1.0f, curve * 0.5f + 0.5f);

        auto shape = [drive, hard] (float x)
        {
            const float d = x * drive;
            const float soft = std::tanh (d);
            const float clip = juce::jlimit (-1.0f, 1.0f, d * 0.7f);
            return soft * (1.0f - hard) + clip * hard;
        };

        return { shape (in.l) * comp * output, shape (in.r) * comp * output };
    }
};

//==============================================================================
/** Quad Chorus / Chorus-Flanger / Phaser in one block, as the hardware has it. */
class ChorusSection
{
public:
    enum Type { QuadChorus = 0, ChorusFlanger, Phaser };

    void prepare (double sr, int maxBlock) noexcept
    {
        juce::ignoreUnused (maxBlock);
        sampleRate = sr;
        const int len = (int) (sr * 0.06) + 4;     // 60 ms is plenty
        bufL.assign ((size_t) len, 0.0f);
        bufR.assign ((size_t) len, 0.0f);
        writePos = 0;
        phase = 0.0f;
        for (auto& s : apL) s = 0.0f;
        for (auto& s : apR) s = 0.0f;
        fbL = fbR = 0.0f;
    }

    /** Silences the tail without reallocating - safe to call from the audio thread. */
    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        for (auto& s : apL) s = 0.0f;
        for (auto& s : apR) s = 0.0f;
        fbL = fbR = 0.0f;
        writePos = 0;
    }

    inline StereoSample process (StereoSample in, float send, float speed, float depth,
                                 float feedback, float delayMs, float width, int type) noexcept
    {
        if (send <= 0.0005f || bufL.empty())
            return { 0.0f, 0.0f };

        const float rate = 0.02f * std::pow (500.0f, speed);
        phase += rate / (float) sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;

        const float lfoL = std::sin (juce::MathConstants<float>::twoPi * phase);
        const float lfoR = std::sin (juce::MathConstants<float>::twoPi
                                     * (phase + 0.25f * width));

        StereoSample wet;

        if (type == Phaser)
        {
            wet = phaser (in, lfoL, lfoR, depth, feedback);
        }
        else
        {
            const float baseMs = type == QuadChorus ? (4.0f + delayMs * 20.0f)
                                                    : (0.4f + delayMs * 6.0f);
            const float swing = type == QuadChorus ? depth * 6.0f : depth * 3.0f;

            wet.l = readDelay (bufL, (baseMs + lfoL * swing) * 0.001f * (float) sampleRate);
            wet.r = readDelay (bufR, (baseMs + lfoR * swing) * 0.001f * (float) sampleRate);

            if (type == QuadChorus)
            {
                // Two more taps, spread in phase - the thick four-voice wash.
                wet.l += 0.7f * readDelay (bufL, (baseMs + lfoR * swing) * 0.001f * (float) sampleRate);
                wet.r += 0.7f * readDelay (bufR, (baseMs - lfoL * swing) * 0.001f * (float) sampleRate);
                wet.l *= 0.6f; wet.r *= 0.6f;
            }

            const float fb = juce::jlimit (0.0f, 0.95f, feedback);
            bufL[(size_t) writePos] = in.l + wet.l * fb;
            bufR[(size_t) writePos] = in.r + wet.r * fb;

            if (++writePos >= (int) bufL.size()) writePos = 0;
        }

        return { wet.l * send, wet.r * send };
    }

private:
    inline StereoSample phaser (StereoSample in, float lfoL, float lfoR,
                                float depth, float feedback) noexcept
    {
        const float fL = 300.0f * std::pow (30.0f, 0.5f + 0.5f * lfoL * depth);
        const float fR = 300.0f * std::pow (30.0f, 0.5f + 0.5f * lfoR * depth);

        auto run = [this] (float x, float* state, float freq, float& fb, float fbAmt)
        {
            const float g = (std::tan (juce::MathConstants<float>::pi
                                       * juce::jlimit (20.0f, (float) sampleRate * 0.45f, freq)
                                       / (float) sampleRate) - 1.0f)
                          / (std::tan (juce::MathConstants<float>::pi
                                       * juce::jlimit (20.0f, (float) sampleRate * 0.45f, freq)
                                       / (float) sampleRate) + 1.0f);

            float v = x + fb * fbAmt;

            for (int i = 0; i < 6; ++i)
            {
                const float y = g * v + state[i];
                state[i] = v - g * y;
                v = y;
            }

            fb = v;
            return 0.5f * (x + v);
        };

        const float fbAmt = juce::jlimit (0.0f, 0.9f, feedback);
        return { run (in.l, apL, fL, fbL, fbAmt), run (in.r, apR, fR, fbR, fbAmt) };
    }

    inline float readDelay (const std::vector<float>& buf, float samples) const noexcept
    {
        const int len = (int) buf.size();
        float pos = (float) writePos - juce::jlimit (1.0f, (float) len - 2.0f, samples);
        while (pos < 0.0f) pos += (float) len;

        // A float cannot hold len - 0.002 exactly once len is large: it rounds
        // up to len itself, and the read then runs one past the end. Wrap after
        // the addition and clamp the integer part rather than trusting it.
        while (pos >= (float) len) pos -= (float) len;

        const int i0 = juce::jlimit (0, len - 1, (int) pos);
        const int i1 = (i0 + 1) % len;
        const float f = juce::jlimit (0.0f, 1.0f, pos - (float) i0);

        return buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
    }

    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    float phase = 0.0f, fbL = 0.0f, fbR = 0.0f;
    float apL[6] { }, apR[6] { };
};

//==============================================================================
/** Comb filter with its own LFO and stereo spread - the SN2's "Special" effect. */
class CombSection
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        const int len = (int) (sr * 0.05) + 4;
        bufL.assign ((size_t) len, 0.0f);
        bufR.assign ((size_t) len, 0.0f);
        writePos = 0;
        phase = 0.0f;
    }

    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        writePos = 0;
    }

    inline StereoSample process (StereoSample in, float freq, float boost,
                                 float speed, float depth, float spread) noexcept
    {
        if (boost <= 0.0005f || bufL.empty())
            return in;

        phase += (0.02f * std::pow (200.0f, speed)) / (float) sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;

        const float mod = std::sin (juce::MathConstants<float>::twoPi * phase) * depth;
        const float hz = 40.0f * std::pow (60.0f, juce::jlimit (0.0f, 1.0f, freq + mod * 0.3f));

        const float dL = (float) sampleRate / juce::jmax (20.0f, hz * (1.0f - spread * 0.3f));
        const float dR = (float) sampleRate / juce::jmax (20.0f, hz * (1.0f + spread * 0.3f));

        const float wl = read (bufL, dL);
        const float wr = read (bufR, dR);
        const float fb = juce::jlimit (0.0f, 0.97f, boost);

        bufL[(size_t) writePos] = in.l + wl * fb;
        bufR[(size_t) writePos] = in.r + wr * fb;
        if (++writePos >= (int) bufL.size()) writePos = 0;

        return { in.l + wl * boost, in.r + wr * boost };
    }

private:
    inline float read (const std::vector<float>& buf, float samples) const noexcept
    {
        const int len = (int) buf.size();
        float pos = (float) writePos - juce::jlimit (1.0f, (float) len - 2.0f, samples);
        while (pos < 0.0f) pos += (float) len;

        // See the note in ChorusSection::readDelay - the wrap can land exactly
        // on len once float precision runs out at these buffer sizes.
        while (pos >= (float) len) pos -= (float) len;

        const int i0 = juce::jlimit (0, len - 1, (int) pos);
        const int i1 = (i0 + 1) % len;
        const float f = juce::jlimit (0.0f, 1.0f, pos - (float) i0);
        return buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
    }

    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    float phase = 0.0f;
};

//==============================================================================
/** Stereo delay with HF damping, width and an independent left/right ratio. */
class DelaySection
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        const int len = (int) (sr * 2.6) + 4;      // a shade over 2.5 s
        bufL.assign ((size_t) len, 0.0f);
        bufR.assign ((size_t) len, 0.0f);
        writePos = 0;
        dampL = dampR = 0.0f;
    }

    /** Ratio index follows the hardware list: 1:1, 1:0.75, 0.75:1 ... Off:1 */
    static void ratioFor (int index, float& left, float& right) noexcept
    {
        static const float table[13][2] =
        {
            { 1.0f, 1.0f },    { 1.0f, 0.75f }, { 0.75f, 1.0f }, { 1.0f, 0.66f },
            { 0.66f, 1.0f },   { 1.0f, 0.5f },  { 0.5f, 1.0f },  { 1.0f, 0.33f },
            { 0.33f, 1.0f },   { 1.0f, 0.25f }, { 0.25f, 1.0f }, { 1.0f, 0.0f },
            { 0.0f, 1.0f }
        };

        const int i = juce::jlimit (0, 12, index);
        left  = table[i][0];
        right = table[i][1];
    }

    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        dampL = dampR = 0.0f;
        writePos = 0;
    }

    inline StereoSample process (StereoSample in, float send, float timeSeconds,
                                 float feedback, float hfDamp, float width,
                                 int ratioIndex) noexcept
    {
        if (bufL.empty())
            return { 0.0f, 0.0f };

        float rl, rr;
        ratioFor (ratioIndex, rl, rr);

        const float maxSamps = (float) bufL.size() - 4.0f;
        const float baseSamps = juce::jlimit (1.0f, maxSamps, timeSeconds * (float) sampleRate);

        float wl = rl > 0.0f ? read (bufL, baseSamps * rl) : 0.0f;
        float wr = rr > 0.0f ? read (bufR, baseSamps * rr) : 0.0f;

        // HF damping in the feedback path, so repeats get darker rather than
        // the whole output being filtered.
        const float a = juce::jlimit (0.02f, 1.0f, 1.0f - hfDamp * 0.98f);
        dampL += a * (wl - dampL);
        dampR += a * (wr - dampR);

        const float fb = juce::jlimit (0.0f, 0.98f, feedback);
        bufL[(size_t) writePos] = in.l + dampR * fb * width + dampL * fb * (1.0f - width);
        bufR[(size_t) writePos] = in.r + dampL * fb * width + dampR * fb * (1.0f - width);

        if (++writePos >= (int) bufL.size()) writePos = 0;

        return { dampL * send, dampR * send };
    }

private:
    inline float read (const std::vector<float>& buf, float samples) const noexcept
    {
        const int len = (int) buf.size();
        float pos = (float) writePos - juce::jlimit (1.0f, (float) len - 2.0f, samples);
        while (pos < 0.0f) pos += (float) len;

        // See the note in ChorusSection::readDelay - the wrap can land exactly
        // on len once float precision runs out at these buffer sizes.
        while (pos >= (float) len) pos -= (float) len;

        const int i0 = juce::jlimit (0, len - 1, (int) pos);
        const int i1 = (i0 + 1) % len;
        const float f = juce::jlimit (0.0f, 1.0f, pos - (float) i0);
        return buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
    }

    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    float dampL = 0.0f, dampR = 0.0f;
};

//==============================================================================
/** Reverb covering the SN2's sixteen types.

    Four gated programs plus twelve rooms/plates. The gated ones are built from
    the same early reflection tap set with an amplitude shape imposed on them,
    which is how the original does it too.
*/
class ReverbSection
{
public:
    enum { NumTypes = 16 };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;

        static const float combMs[8] = { 25.3f, 27.9f, 31.1f, 34.1f, 37.3f, 40.1f, 43.7f, 46.1f };
        static const float apMs[4]   = { 5.1f, 7.3f, 10.1f, 12.7f };

        for (int i = 0; i < 8; ++i)
        {
            combs[i].assign ((size_t) (combMs[i] * 0.001f * sr) + 2, 0.0f);
            combPos[i] = 0;
            combState[i] = 0.0f;
        }

        for (int i = 0; i < 4; ++i)
        {
            aps[i].assign ((size_t) (apMs[i] * 0.001f * sr) + 2, 0.0f);
            apPos[i] = 0;
        }

        erBuf.assign ((size_t) (0.12 * sr) + 2, 0.0f);
        erPos = 0;
        gateCount = 0;
    }

    void reset() noexcept
    {
        for (auto& c : combs) std::fill (c.begin(), c.end(), 0.0f);
        for (auto& a : aps)   std::fill (a.begin(), a.end(), 0.0f);
        std::fill (erBuf.begin(), erBuf.end(), 0.0f);
        for (auto& s : combState) s = 0.0f;
        for (auto& p : combPos) p = 0;
        for (auto& p : apPos) p = 0;
        erPos = 0;
        gateCount = 0;
    }

    inline StereoSample process (StereoSample in, float send, float decay,
                                 float hfDamp, float earlyRef, int type) noexcept
    {
        if (send <= 0.0005f || combs[0].empty())
            return { 0.0f, 0.0f };

        const float mono = 0.5f * (in.l + in.r);
        const bool gated = type < 4;

        // --- early reflections
        erBuf[(size_t) erPos] = mono;
        if (++erPos >= (int) erBuf.size()) erPos = 0;

        static const float erTapMs[6] = { 7.0f, 13.0f, 21.0f, 29.0f, 41.0f, 53.0f };
        float er = 0.0f;
        for (int i = 0; i < 6; ++i)
            er += erTap (erTapMs[i]) * (1.0f - (float) i * 0.13f);
        er *= 0.22f;

        if (gated)
        {
            // Gated programs: shape the reflection burst and stop dead.
            const float lenSamps = (0.08f + decay * 0.35f) * (float) sampleRate;
            const float t = juce::jlimit (0.0f, 1.0f, (float) gateCount / juce::jmax (1.0f, lenSamps));

            float shape = 1.0f;
            switch (type)
            {
                case 0: shape = t;                     break;   // Gated Reverse
                case 1: shape = 0.3f + t * 0.7f;       break;   // Gated Rising
                case 2: shape = 1.0f - t * 0.35f;      break;   // Gated Gentle
                default: shape = 1.0f - t;             break;   // Gated Falling
            }

            if (std::abs (mono) > 0.02f && t > 0.4f)
                gateCount = 0;
            else
                ++gateCount;

            const float g = t >= 1.0f ? 0.0f : shape;
            return { er * g * send, er * g * send * 0.92f };
        }

        // --- tail: the twelve room and plate programs differ in size and decay
        const float sizeScale = 0.6f + 0.05f * (float) (type - 4);
        const float feedback = juce::jlimit (0.0f, 0.96f,
                                             0.7f + decay * 0.26f * sizeScale);
        const float damp = juce::jlimit (0.02f, 1.0f, 1.0f - hfDamp * 0.9f);

        float tail = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const int len = (int) combs[i].size();
            float v = combs[i][(size_t) combPos[i]];
            combState[i] += damp * (v - combState[i]);
            combs[i][(size_t) combPos[i]] = mono + combState[i] * feedback;
            if (++combPos[i] >= len) combPos[i] = 0;
            tail += v * (i % 2 == 0 ? 1.0f : -1.0f);
        }
        tail *= 0.125f;

        for (int i = 0; i < 4; ++i)
        {
            const int len = (int) aps[i].size();
            const float d = aps[i][(size_t) apPos[i]];
            const float v = tail + d * 0.5f;
            aps[i][(size_t) apPos[i]] = v;
            if (++apPos[i] >= len) apPos[i] = 0;
            tail = d - v * 0.5f;
        }

        const float wet = tail + er * earlyRef;
        return { wet * send, (tail * 0.94f - er * earlyRef * 0.3f) * send };
    }

private:
    inline float erTap (float ms) const noexcept
    {
        const int len = (int) erBuf.size();
        int pos = erPos - (int) (ms * 0.001f * (float) sampleRate);
        while (pos < 0) pos += len;
        return erBuf[(size_t) pos];
    }

    double sampleRate = 44100.0;
    std::vector<float> combs[8], aps[4], erBuf;
    int combPos[8] { }, apPos[4] { }, erPos = 0, gateCount = 0;
    float combState[8] { };
};

//==============================================================================
/** Panner: static placement plus auto-pan, tremolo and hard L-R / R-L modes. */
class PanSection
{
public:
    enum Type { AutoPan = 0, Tremolo, LeftRight, RightLeft };

    void prepare (double sr) noexcept { sampleRate = sr; phase = 0.0f; }
    void reset() noexcept             { phase = 0.0f; }

    inline StereoSample process (StereoSample in, float pan, int type,
                                 float speed, float depth) noexcept
    {
        float position = juce::jlimit (-1.0f, 1.0f, pan);
        float amp = 1.0f;

        if (depth > 0.0005f)
        {
            phase += (0.02f * std::pow (400.0f, speed)) / (float) sampleRate;
            if (phase >= 1.0f) phase -= 1.0f;

            const float lfo = std::sin (juce::MathConstants<float>::twoPi * phase);

            switch (type)
            {
                case Tremolo:   amp = 1.0f - depth * 0.5f * (1.0f - lfo); break;
                case LeftRight: position = juce::jlimit (-1.0f, 1.0f, position + depth); break;
                case RightLeft: position = juce::jlimit (-1.0f, 1.0f, position - depth); break;
                case AutoPan:
                default:        position = juce::jlimit (-1.0f, 1.0f, position + lfo * depth); break;
            }
        }

        // Constant-power pan.
        const float a = (position + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        return { in.l * std::cos (a) * amp, in.r * std::sin (a) * amp };
    }

private:
    double sampleRate = 44100.0;
    float phase = 0.0f;
};

} // namespace aquanova
