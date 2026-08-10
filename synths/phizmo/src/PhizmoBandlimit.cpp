/*
    PhizmoBandlimit.cpp
    -------------------
    An optional second oscillator engine for Phizmo.

    The existing engine reads the raw frames with bilinear interpolation and no
    band-limiting. On a bright table that aliases above roughly C4 - which is
    arguably correct, since the hardware's sample playback chip does linear
    interpolation and nothing else, and the GRIT control leans into exactly that
    character. This file does NOT replace it. It adds a switchable alternative
    for when you want the same table to stay clean at the top of the keyboard.

    How it works: each frame is analysed once into a mip pyramid. Mip 0 keeps
    1024 harmonics, mip 1 keeps 512, and so on. At playback the mip whose top
    harmonic still fits under Nyquist for the note being played is selected.
    Cost is about 2 MB per slot for a 64-frame table, built once on load.

    Nothing else changes: same frames, same evolution curves, same visualiser.
*/

#include "PluginProcessor.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // These mirror PhizmoAudioProcessor::MipSet, which is private, so they are
    // repeated here rather than referenced. Keep the two in step.
    constexpr int kMipCount   = 10;
    constexpr int kBaseLength = 2048;
    constexpr int kBaseHarms  = 1024;

    int mipLength (int m) { return juce::jmax (64, kBaseLength >> m); }
    int mipHarms  (int m) { return juce::jmax (1,  kBaseHarms  >> m); }
}

//==============================================================================
void PhizmoAudioProcessor::buildMips (int slot)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) return;

    // Mark unavailable first: any voice that starts while the rebuild is in
    // flight takes the vintage path instead of touching half-built levels.
    mips[slot].ready.store(false);

    // Snapshot the frames under the wavetable lock, then do the heavy lifting
    // outside it so a long analysis never stalls the audio thread.
    std::vector<std::vector<float>> frames;
    int cycleSamples = 0;
    {
        juce::ScopedLock sl (wt[slot].lock);
        if (! wt[slot].loaded || wt[slot].frames.empty()) { clearMips (slot); return; }
        frames = wt[slot].frames;
        cycleSamples = wt[slot].cycleSamples;
    }

    const int numFrames = (int) frames.size();
    std::vector<std::array<std::vector<float>, kMipCount>> built ((size_t) numFrames);

    juce::dsp::FFT fft (11);   // 2048
    std::vector<float> work ((size_t) (2 * kBaseLength));
    std::vector<float> spec ((size_t) (2 * kBaseLength));

    for (int f = 0; f < numFrames; ++f)
    {
        // Resample the cycle to 2048 so one FFT size covers every cycle length
        // the user might have dialled in.
        std::fill (work.begin(), work.end(), 0.0f);
        const auto& src = frames[(size_t) f];
        for (int i = 0; i < kBaseLength; ++i)
        {
            const double p = (double) i * cycleSamples / (double) kBaseLength;
            const int i0 = (int) p % cycleSamples;
            const int i1 = (i0 + 1) % cycleSamples;
            const float fr = (float) (p - std::floor (p));
            work[(size_t) i] = src[(size_t) i0] * (1.0f - fr) + src[(size_t) i1] * fr;
        }

        fft.performRealOnlyForwardTransform (work.data());

        for (int m = 0; m < kMipCount; ++m)
        {
            const int H = juce::jmin (mipHarms (m), kBaseLength / 2 - 1);
            const int len = mipLength (m);

            std::fill (spec.begin(), spec.end(), 0.0f);
            for (int h = 1; h <= H; ++h)
            {
                const float re = work[(size_t) (2 * h)];
                const float im = work[(size_t) (2 * h) + 1];
                spec[(size_t) (2 * h)]     = re;
                spec[(size_t) (2 * h) + 1] = im;
                const int mirror = kBaseLength - h;
                spec[(size_t) (2 * mirror)]     =  re;
                spec[(size_t) (2 * mirror) + 1] = -im;
            }

            fft.performRealOnlyInverseTransform (spec.data());

            const int stride = kBaseLength / len;
            auto& level = built[(size_t) f][(size_t) m];
            level.resize ((size_t) len + 1);
            for (int i = 0; i < len; ++i)
                level[(size_t) i] = spec[(size_t) (i * stride)];
            level[(size_t) len] = level[0];   // guard point for interpolation
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (mips[slot].swapLock);
        mips[slot].levels = std::move (built);
        mips[slot].numFrames = numFrames;
    }
    mips[slot].ready.store (true);
}

void PhizmoAudioProcessor::clearMips (int slot)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) return;
    mips[slot].ready.store (false);
    const juce::SpinLock::ScopedLockType sl (mips[slot].swapLock);
    mips[slot].levels.clear();
    mips[slot].numFrames = 0;
}

//==============================================================================
bool PhizmoAudioProcessor::sampleFrameMip (int slot, float fi, double ph, double freqHz,
                                           float& out) const
{
    const auto& ms = mips[slot];
    if (! ms.ready.load()) return false;

    // Try, never block. If a rebuild owns the lock this instant, the caller
    // falls back to the vintage read for this one sample.
    const juce::SpinLock::ScopedTryLockType sl (ms.swapLock);
    if (! sl.isLocked() || ms.numFrames <= 0) return false;

    // Pick the coarsest mip whose top harmonic still fits under Nyquist.
    const double allowed = (currentSampleRate * 0.5) / juce::jmax (1.0, freqHz);
    int m = kMipCount - 1;
    for (int i = 0; i < kMipCount; ++i)
        if ((double) mipHarms (i) <= allowed) { m = i; break; }

    const int i0 = juce::jlimit (0, ms.numFrames - 1, (int) fi);
    const int i1 = juce::jlimit (0, ms.numFrames - 1, i0 + 1);
    const float blend = fi - (float) i0;

    const auto& a = ms.levels[(size_t) i0][(size_t) m];
    const auto& b = ms.levels[(size_t) i1][(size_t) m];
    if (a.empty() || b.empty()) return false;

    const int len = (int) a.size() - 1;
    const double pos = juce::jlimit (0.0, 0.999999, ph) * len;
    const int s0 = (int) pos;
    const float fr = (float) (pos - s0);

    const float va = a[(size_t) s0] + fr * (a[(size_t) (s0 + 1)] - a[(size_t) s0]);
    const float vb = b[(size_t) s0] + fr * (b[(size_t) (s0 + 1)] - b[(size_t) s0]);
    out = va + blend * (vb - va);
    return true;
}
