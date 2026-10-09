#pragma once
//==============================================================================
//  AquaVibrioExtras.h
//
//  The two pieces of the Virus with no equivalent anywhere in the Aquanode
//  module set, written here rather than bent out of something that does not
//  fit. Everything else in this synth is somebody else's module doing what it
//  already did.
//
//  1. FrequencyShifter - single-sideband shifting. Not pitch shifting: every
//     partial moves by the same number of Hz rather than the same ratio, so
//     harmonic relationships break and the sound goes metallic and clangy.
//     That is the point of it.
//
//  2. Atomizer - the Virus's slice-and-repeat effect. It captures short
//     slices of the output in time with the clock and replays them, so a
//     held chord turns into a stutter. One knob on the hardware, which sets
//     how short the slices get.
//==============================================================================

#include <JuceHeader.h>

namespace aquavibrio
{

//==============================================================================
// A Hilbert transformer, built as two cascades of first-order allpass
// sections. Feed a signal in and the two outputs are 90 degrees apart across
// the audio band, which is all a single-sideband shifter needs. The
// coefficients are the classic pair from Olli Niemitalo's write-up - each
// cascade is four one-pole allpasses whose combined phase responses differ by
// a quarter cycle from roughly 20 Hz to 20 kHz.
//==============================================================================
class HilbertPair
{
public:
    void reset()
    {
        for (auto& s : stateA) s.reset();
        for (auto& s : stateB) s.reset();
        delayed = 0.0f;
    }

    // outputs: 'inPhase' and 'quadrature', 90 degrees apart
    void process (float x, float& inPhase, float& quadrature)
    {
        float a = x;
        for (int i = 0; i < 4; ++i)
            a = stateA[(size_t) i].process (a, coeffsA[i]);

        float b = delayed;   // the B cascade runs one sample behind
        for (int i = 0; i < 4; ++i)
            b = stateB[(size_t) i].process (b, coeffsB[i]);

        delayed = x;

        inPhase = b;
        quadrature = a;
    }

private:
    // Each section is a SECOND-order allpass: y[n] = a^2 * (x[n] + y[n-2]) - x[n-2].
    // The two-sample memory is not incidental - these coefficients are
    // derived for that form, and a one-sample version does not give a
    // quarter-cycle difference at all.
    struct Allpass
    {
        float x1 { 0.0f }, x2 { 0.0f }, y1 { 0.0f }, y2 { 0.0f };

        void reset() { x1 = x2 = y1 = y2 = 0.0f; }

        float process (float x, float a)
        {
            const float y = a * a * (x + y2) - x2;
            x2 = x1;  x1 = x;
            y2 = y1;  y1 = y;
            return y;
        }
    };

    static constexpr float coeffsA[4] { 0.6923877f, 0.9360654f, 0.9882295f, 0.9987488f };
    static constexpr float coeffsB[4] { 0.4021921f, 0.8561711f, 0.9722910f, 0.9952885f };

    std::array<Allpass, 4> stateA {}, stateB {};
    float delayed { 0.0f };
};

//==============================================================================
class FrequencyShifter
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset()
    {
        left.reset();
        right.reset();
        phase = 0.0;
    }

    // shiftHz may be negative, which moves partials down instead of up
    void setShift (float shiftHz) { shift = shiftHz; }
    void setMix (float newMix)    { mix = juce::jlimit (0.0f, 1.0f, newMix); }

    void process (float& l, float& r)
    {
        if (mix <= 0.0f)
            return;

        phase += (double) shift / sampleRate;
        phase -= std::floor (phase);

        const float angle = (float) (phase * juce::MathConstants<double>::twoPi);
        const float cosine = std::cos (angle);
        const float sine = std::sin (angle);

        float li, lq, ri, rq;
        left.process (l, li, lq);
        right.process (r, ri, rq);

        // The upper sideband is i*cos - q*sin; the lower is i*cos + q*sin.
        // Negative shift frequencies fall out of the maths on their own.
        const float wetL = li * cosine - lq * sine;
        const float wetR = ri * cosine - rq * sine;

        l += (wetL - l) * mix;
        r += (wetR - r) * mix;
    }

private:
    HilbertPair left, right;
    double sampleRate { 44100.0 }, phase { 0.0 };
    float shift { 0.0f }, mix { 0.0f };
};

//==============================================================================
// Atomizer. Records continuously into a ring buffer; when it is switched on,
// it stops advancing the read position past the slice and loops it instead.
// Slice length follows the tempo so the stutter stays in time, and the knob
// chooses how short the slice gets - the Virus's single control.
//==============================================================================
class Atomizer
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        buffer.setSize (2, (int) (newSampleRate * 2.0) + 4);
        reset();
    }

    void reset()
    {
        buffer.clear();
        writePos = 0;
        sliceStart = 0;
        sliceCounter = 0;
        active = false;
    }

    void setTempo (double bpm) { tempoBpm = bpm > 1.0 ? bpm : 120.0; }

    // amount 0..1, straight from the hardware byte
    void setAmount (float newAmount)
    {
        const bool wasActive = active;
        amount = juce::jlimit (0.0f, 1.0f, newAmount);
        active = amount > 0.01f;

        if (active && ! wasActive)
        {
            // Start on what was just played rather than whatever is stale.
            sliceStart = writePos;
            sliceCounter = 0;
        }
    }

    void process (float& l, float& r)
    {
        const int size = buffer.getNumSamples();

        if (size <= 0)
            return;

        buffer.setSample (0, writePos, l);
        buffer.setSample (1, writePos, r);

        if (! active)
        {
            writePos = (writePos + 1) % size;
            return;
        }

        // Slice length: a beat at the bottom of the knob down to a 32nd at
        // the top. Short slices are where it turns into a pitched buzz.
        const double beatSamples = 60.0 / tempoBpm * sampleRate;
        const double fraction = std::pow (0.5, std::floor ((double) amount * 5.0));
        const int sliceSamples = juce::jmax (32, (int) (beatSamples * fraction));

        // The slice start is captured once, when the slice begins, and the
        // read head then walks forward through it. Deriving the start from
        // the counter each sample - as this did - cancels out and leaves the
        // tap frozen on one sample, which is silence with a DC step.
        const int tap = (sliceStart + sliceCounter) % size;

        l = buffer.getSample (0, tap);
        r = buffer.getSample (1, tap);

        if (++sliceCounter >= sliceSamples)
        {
            sliceCounter = 0;
            sliceStart = ((writePos - sliceSamples) % size + size) % size;   // the slice just recorded
        }

        writePos = (writePos + 1) % size;
    }

private:
    juce::AudioBuffer<float> buffer;
    double sampleRate { 44100.0 }, tempoBpm { 120.0 };
    int writePos { 0 }, sliceStart { 0 }, sliceCounter { 0 };
    float amount { 0.0f };
    bool active { false };
};

} // namespace aquavibrio
