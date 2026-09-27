/*
    FxChain.h  -  global effects placed after the DX7 engine.

    The DSP here is ported from the Aquanode Modular effect modules
    (ChorusModule, DelayModule, PhaserModule, AquatonReverbModule). Those are
    written against Aquanode's SynthModule base class, which brings in the
    module descriptor / socket / modulation machinery this plugin has no use
    for, so each algorithm has been lifted out into a plain class with explicit
    setters instead. The signal maths is unchanged; what differs is:

      - stereo in-place block processing rather than per-sample StereoFrame I/O
      - parameters set once per block instead of read per sample from a
        modulatable param array
      - Dry/Wet and the enable state are ramped with SmoothedValue, because on
        a phone these get moved by finger and would otherwise zipper
      - the delay is free-running in milliseconds, not tempo-synced. Aquanode
        syncs to the host transport; standalone on Android there is no
        transport to sync to, so a synced control would sit at the 120 BPM
        fallback forever.

    Chain order is fixed: Chorus -> Delay -> Phaser -> Reverb.

    One section, Harmonizer ("Chord"), is not part of that audio chain at
    all: it works on MIDI *ahead of* the DX7 engine, turning each note into
    up to 6 extra transposed notes that the engine renders as if they had
    really been played. It is still exposed and toggled the same way as the
    other four (FxChain::processMidi(), called once per block before the
    engine sees the buffer).

    Everything is inline so this stays a header-only addition to the build.

    GPLv3.
*/
#pragma once
#include <JuceHeader.h>

#include <vector>
#include <cmath>
#include <array>

namespace vdx7fx {

    // ============================================================================
    //  Base: shared enable ramp.
    //
    //  A hard bypass switch clicks, so `enable` drives a smoothed gain that
    //  crossfades between the processed and the untouched signal. Once that gain
    //  has fully settled at zero the effect stops processing altogether, which is
    //  what keeps four idle effects off the CPU budget on a phone.
    // ============================================================================
    class FxBase
    {
    public:
        virtual ~FxBase() = default;

        virtual void prepare(double sr, int maxBlock)
        {
            sampleRate = sr > 0.0 ? sr : 48000.0;
            juce::ignoreUnused(maxBlock);
            enableGain.reset(sampleRate, 0.02);
            enableGain.setCurrentAndTargetValue(enabled ? 1.0f : 0.0f);
            dryWet.reset(sampleRate, 0.05);
            reset();
        }

        virtual void reset() = 0;

        void setEnabled(bool shouldBeEnabled)
        {
            enabled = shouldBeEnabled;
            enableGain.setTargetValue(enabled ? 1.0f : 0.0f);
        }

        void setDryWet(float percent) { dryWet.setTargetValue(juce::jlimit(0.0f, 1.0f, percent * 0.01f)); }

        // Returns false when the effect is fully bypassed and settled, so the
        // caller can skip it entirely.
        bool isActive() const { return enabled || enableGain.isSmoothing(); }

        void process(float* left, float* right, int numSamples)
        {
            if (!isActive())
            {
                // The fade-out has finished and we are about to stop processing.
                // Clear the buffers once, so switching the effect back on later
                // starts from silence instead of firing off a months-old tail.
                if (!cleared)
                {
                    reset();
                    cleared = true;
                }
                return;
            }

            cleared = false;
            processInternal(left, right, numSamples);
        }

    protected:
        virtual void processInternal(float* left, float* right, int numSamples) = 0;

        // Mixes one processed sample back over the dry one, honouring both the
        // per-effect Dry/Wet and the enable crossfade.
        void mix(float dryL, float dryR, float wetL, float wetR,
            float& outL, float& outR)
        {
            const float w = dryWet.getNextValue() * enableGain.getNextValue();
            outL = dryL + (wetL - dryL) * w;
            outR = dryR + (wetR - dryR) * w;
        }

        double sampleRate{ 48000.0 };
        bool   enabled{ false };
        bool   cleared{ true };
        juce::SmoothedValue<float> enableGain{ 0.0f };
        juce::SmoothedValue<float> dryWet{ 0.5f };
    };

    // ============================================================================
    //  Chorus  -  modulated delay line per channel; the LFO phase offset between
    //  the channels is the stereo spread.  (Aquanode ChorusModule)
    // ============================================================================
    class Chorus : public FxBase
    {
    public:
        void prepare(double sr, int maxBlock) override
        {
            const int size = juce::jmax(4, (int)(sr * 0.06));   // 60 ms > 20 ms max base + sweep
            for (int c = 0; c < 2; ++c)
                line[c].assign((size_t)size, 0.0f);
            FxBase::prepare(sr, maxBlock);
        }

        void reset() override
        {
            for (int c = 0; c < 2; ++c)
                std::fill(line[c].begin(), line[c].end(), 0.0f);
            writePos = 0;
            lfoPhase = 0.0;
        }

        void setRate(float hz) { rate = hz; }
        void setDepth(float percent) { depth = percent * 0.01f; }
        void setBaseDelay(float ms) { baseMs = ms; }
        void setSpread(float percent) { spread = percent * 0.01f; }

    private:
        void processInternal(float* left, float* right, int numSamples) override
        {
            const int size = (int)line[0].size();
            if (size < 4)
                return;

            for (int n = 0; n < numSamples; ++n)
            {
                const float dryL = left[n], dryR = right[n];
                float wet[2]{ dryL, dryR };

                lfoPhase += rate / sampleRate;
                lfoPhase -= std::floor(lfoPhase);

                for (int c = 0; c < 2; ++c)
                {
                    const double ph = lfoPhase + (c == 1 ? spread * 0.5 : 0.0);   // up to 180 deg
                    const float lfo = (float)std::sin(ph * juce::MathConstants<double>::twoPi);

                    // sweep around the base delay; never below 0.5 ms
                    const float delayMs = juce::jmax(0.5f, baseMs * (1.0f + 0.9f * depth * lfo));
                    const double delaySamples =
                        juce::jlimit(1.0, (double)size - 2.0, delayMs * 0.001 * sampleRate);

                    double readPos = (double)writePos - delaySamples;
                    while (readPos < 0.0) readPos += size;
                    const int i0 = (int)readPos;
                    const int i1 = (i0 + 1) % size;
                    const float frac = (float)(readPos - i0);
                    wet[c] = line[c][(size_t)i0]
                        + (line[c][(size_t)i1] - line[c][(size_t)i0]) * frac;

                    line[c][(size_t)writePos] = (c == 0 ? dryL : dryR);
                }

                writePos = (writePos + 1) % size;
                mix(dryL, dryR, wet[0], wet[1], left[n], right[n]);
            }
        }

        std::vector<float> line[2];
        int    writePos{ 0 };
        double lfoPhase{ 0.0 };
        float  rate{ 0.5f }, depth{ 0.5f }, baseMs{ 7.0f }, spread{ 0.5f };
    };

    // ============================================================================
    //  Delay  -  stereo, independent times per channel, one-pole high-pass in the
    //  feedback loop.  (Aquanode DelayModule, retimed to milliseconds)
    // ============================================================================
    class Delay : public FxBase
    {
    public:
        static constexpr float maxTimeMs = 2000.0f;

        void prepare(double sr, int maxBlock) override
        {
            const int size = juce::jmax(4, (int)(sr * (maxTimeMs * 0.001 + 0.1)));
            for (int c = 0; c < 2; ++c)
            {
                line[c].assign((size_t)size, 0.0f);
                smoothedDelay[c] = sr * 0.25;
            }
            FxBase::prepare(sr, maxBlock);
        }

        void reset() override
        {
            for (int c = 0; c < 2; ++c)
            {
                std::fill(line[c].begin(), line[c].end(), 0.0f);
                hpState[c] = 0.0f;
            }
            writePos = 0;
        }

        void setTimeL(float ms) { timeMs[0] = ms; }
        void setTimeR(float ms) { timeMs[1] = ms; }
        void setFeedback(float fb) { feedback = fb; }
        void setHighPass(float hz) { hpCut = hz; }

    private:
        void processInternal(float* left, float* right, int numSamples) override
        {
            const int size = (int)line[0].size();
            if (size < 2)
                return;

            const float hpCoeff =
                std::exp(-juce::MathConstants<float>::twoPi * hpCut / (float)sampleRate);

            for (int n = 0; n < numSamples; ++n)
            {
                const float dry[2]{ left[n], right[n] };
                float wet[2]{ dry[0], dry[1] };

                for (int c = 0; c < 2; ++c)
                {
                    const double target = juce::jlimit(1.0, (double)size - 2.0,
                        timeMs[c] * 0.001 * sampleRate);
                    smoothedDelay[c] += 0.0005 * (target - smoothedDelay[c]);

                    double readPos = (double)writePos - smoothedDelay[c];
                    while (readPos < 0.0) readPos += size;
                    const int i0 = (int)readPos;
                    const int i1 = (i0 + 1) % size;
                    const float frac = (float)(readPos - i0);
                    const float delayed = line[c][(size_t)i0]
                        + (line[c][(size_t)i1] - line[c][(size_t)i0]) * frac;

                    // one-pole high-pass in the feedback loop
                    hpState[c] = hpCoeff * hpState[c] + (1.0f - hpCoeff) * delayed;
                    const float hpOut = delayed - hpState[c];

                    line[c][(size_t)writePos] =
                        juce::jlimit(-4.0f, 4.0f, dry[c] + hpOut * feedback);
                    wet[c] = delayed;
                }

                writePos = (writePos + 1) % size;
                mix(dry[0], dry[1], wet[0], wet[1], left[n], right[n]);
            }
        }

        std::vector<float> line[2];
        int    writePos{ 0 };
        double smoothedDelay[2]{ 0.0, 0.0 };
        float  hpState[2]{};
        float  timeMs[2]{ 250.0f, 375.0f };
        float  feedback{ 0.3f }, hpCut{ 100.0f };
    };

    // ============================================================================
    //  Phaser  -  2..12 first-order allpass stages per channel swept by an LFO
    //  around a centre frequency, with feedback.  (Aquanode PhaserModule)
    // ============================================================================
    class Phaser : public FxBase
    {
    public:
        static constexpr int maxStages = 12;

        void reset() override
        {
            for (int c = 0; c < 2; ++c)
            {
                lastOut[c] = 0.0f;
                for (auto& s : apState[c]) s = 0.0f;
            }
            lfoPhase = 0.0;
        }

        void setRate(float hz) { rate = hz; }
        void setDepth(float percent) { depth = percent * 0.01f; }
        void setCentre(float hz) { centre = hz; }
        void setFeedback(float percent) { feedback = percent * 0.01f * 0.9f; }
        void setStages(int choiceIndex)
        {
            static const int choices[] = { 2, 4, 6, 8, 10, 12 };
            stages = choices[juce::jlimit(0, 5, choiceIndex)];
        }

    private:
        void processInternal(float* left, float* right, int numSamples) override
        {
            for (int n = 0; n < numSamples; ++n)
            {
                const float dry[2]{ left[n], right[n] };
                float wet[2]{ dry[0], dry[1] };

                lfoPhase += rate / sampleRate;
                lfoPhase -= std::floor(lfoPhase);
                const float lfo = (float)std::sin(lfoPhase * juce::MathConstants<double>::twoPi);

                // sweep +-2 octaves around centre at full depth
                const float sweepHz = juce::jlimit(30.0f, (float)(sampleRate * 0.45),
                    centre * std::pow(2.0f, lfo * depth * 2.0f));

                const float wt = std::tan(juce::MathConstants<float>::pi * sweepHz / (float)sampleRate);
                const float a = (wt - 1.0f) / (wt + 1.0f);

                for (int c = 0; c < 2; ++c)
                {
                    float x = dry[c] + lastOut[c] * feedback;

                    for (int s = 0; s < stages; ++s)
                    {
                        const float y = a * x + apState[c][s];
                        apState[c][s] = x - a * y;
                        x = y;
                    }

                    lastOut[c] = x;
                    wet[c] = (dry[c] + x) * 0.5f;
                }

                mix(dry[0], dry[1], wet[0], wet[1], left[n], right[n]);
            }
        }

        float  apState[2][maxStages]{};
        float  lastOut[2]{};
        double lfoPhase{ 0.0 };
        float  rate{ 0.5f }, depth{ 0.5f }, centre{ 800.0f }, feedback{ 0.18f };
        int    stages{ 4 };
    };

    // ============================================================================
    //  Reverb  -  8-line FDN with input diffusion allpasses, per-line lowpass
    //  damping and a Householder feedback matrix. Freeze holds the tank.
    //  (Aquanode AquatonReverbModule)
    // ============================================================================
    class Reverb : public FxBase
    {
    public:
        static constexpr int numLines = 8;
        static constexpr int numDiffusers = 4;

        void prepare(double sr, int maxBlock) override
        {
            // largest delay at max size (8x) + modulation headroom
            maxLine = (int)(sr * 0.0797 * 8.0) + 512;
            for (int l = 0; l < numLines; ++l)
                lines[l].assign((size_t)maxLine, 0.0f);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < numDiffusers; ++i)
                    diffusers[c][i].assign((int)(kDiffMs[i] * 0.001 * sr) + 1);

            FxBase::prepare(sr, maxBlock);
        }

        void reset() override
        {
            for (int l = 0; l < numLines; ++l)
            {
                std::fill(lines[l].begin(), lines[l].end(), 0.0f);
                writePos[l] = 0;
                lpState[l] = 0.0f;
                lfoPhase[l] = (double)l / numLines;   // spread LFO phases across lines
            }
            for (int c = 0; c < 2; ++c)
                for (auto& ap : diffusers[c])
                    ap.clear();
        }

        void setSize(float s) { size = s; }
        void setFeedback(float fb) { feedbackAmt = fb; }
        void setDamping(float percent) { damping = percent * 0.01f; }
        void setModRate(float hz) { modRate = hz; }
        void setModDepth(float d) { modDepth = d; }
        void setFreeze(bool f) { frozen = f; }

    private:
        struct Allpass
        {
            std::vector<float> buf;
            int pos{ 0 };
            void assign(int n) { buf.assign((size_t)juce::jmax(4, n), 0.0f); pos = 0; }
            void clear() { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; }
            float process(float in, float coeff)
            {
                const float delayed = buf[(size_t)pos];
                const float y = -coeff * in + delayed;
                buf[(size_t)pos] = in + coeff * y;
                if (++pos >= (int)buf.size()) pos = 0;
                return y;
            }
        };

        float readLine(int l, double delaySamples) const
        {
            double readPos = (double)writePos[l] - delaySamples;
            while (readPos < 0.0) readPos += maxLine;
            const int i0 = (int)readPos % maxLine;
            const int i1 = (i0 + 1) % maxLine;
            const float frac = (float)(readPos - std::floor(readPos));
            return lines[l][(size_t)i0] * (1.0f - frac) + lines[l][(size_t)i1] * frac;
        }

        void processInternal(float* left, float* right, int numSamples) override
        {
            if (maxLine < 512)
                return;

            // freeze: unity feedback, no new input, no damping loss
            const float fb = frozen ? 1.0f : juce::jlimit(0.0f, 0.98f, feedbackAmt * 0.8f);
            const float lpCoeff = frozen ? 1.0f : juce::jlimit(0.05f, 1.0f, 1.0f - damping * 0.9f);

            for (int n = 0; n < numSamples; ++n)
            {
                const float dryL = left[n], dryR = right[n];

                // ---- input diffusion (allpass chain per channel) ----------------
                float difL = dryL, difR = dryR;
                if (!frozen)
                {
                    for (int i = 0; i < numDiffusers; ++i)
                    {
                        difL = diffusers[0][i].process(difL, 0.62f);
                        difR = diffusers[1][i].process(difR, 0.62f);
                    }
                }
                else
                {
                    difL = difR = 0.0f;
                }

                // ---- read all modulated lines -----------------------------------
                float lineOut[numLines];
                float sum = 0.0f;
                for (int l = 0; l < numLines; ++l)
                {
                    lfoPhase[l] += modRate / sampleRate;
                    lfoPhase[l] -= std::floor(lfoPhase[l]);
                    const double lfo = std::sin(lfoPhase[l] * juce::MathConstants<double>::twoPi);

                    const double dly = juce::jlimit(16.0, (double)maxLine - 4.0,
                        kLineMs[l] * 0.001 * sampleRate * size + lfo * modDepth);

                    lineOut[l] = readLine(l, dly);
                    sum += lineOut[l];
                }

                // ---- Householder feedback matrix: y_i = x_i - (2/N) * sum -------
                const float h = 2.0f / (float)numLines;
                for (int l = 0; l < numLines; ++l)
                {
                    lpState[l] += lpCoeff * ((lineOut[l] - h * sum) - lpState[l]);
                    const float mixed = lpState[l] * fb;

                    // inject the diffused input into alternating lines
                    const float inject = (l & 1) ? difR : difL;
                    lines[l][(size_t)writePos[l]] = juce::jlimit(-4.0f, 4.0f, mixed + inject * 0.5f);
                    writePos[l] = (writePos[l] + 1) % maxLine;
                }

                // ---- stereo tap: odd lines left, even lines right ---------------
                float wetL = 0.0f, wetR = 0.0f;
                for (int l = 0; l < numLines; ++l)
                {
                    if (l & 1) wetL += lineOut[l];
                    else       wetR += lineOut[l];
                }
                wetL *= 0.4f;
                wetR *= 0.4f;

                mix(dryL, dryR, wetL, wetR, left[n], right[n]);
            }
        }

        // mutually-prime base delays (ms) for the 8 FDN lines, scaled by Size
        static constexpr double kLineMs[numLines] =
        { 29.7, 37.1, 41.1, 43.7, 53.3, 61.9, 71.3, 79.7 };
        static constexpr double kDiffMs[numDiffusers] = { 4.7, 3.6, 12.7, 9.3 };

        std::vector<float> lines[numLines];
        int     writePos[numLines]{};
        float   lpState[numLines]{};
        double  lfoPhase[numLines]{};
        Allpass diffusers[2][numDiffusers];
        int     maxLine{ 0 };
        float   size{ 1.0f }, feedbackAmt{ 0.7f }, damping{ 0.4f };
        float   modRate{ 0.25f }, modDepth{ 4.0f };
        bool    frozen{ false };
    };

    // ============================================================================
    //  Harmonizer  -  MIDI-level "chord" section: up to 6 extra notes per note
    //  played, each an independently-set number of semitones away (-36..+36,
    //  0 = off). Unlike Chorus/Delay/Phaser/Reverb above, this works on MIDI
    //  ahead of the DX7 engine rather than on audio after it - the extra notes
    //  are voices the engine itself renders, so they get the current patch's own
    //  envelopes, velocity response, and everything else exactly like the note
    //  that was actually played.
    // ============================================================================
    class Harmonizer
    {
    public:
        static constexpr int kNumSlots = 6;

        // Turning the section off releases every extra note it is currently
        // holding, but leaves the original notes untouched; turning it back on
        // while notes are still held immediately adds the extra notes for
        // whichever slots are non-zero. That mirrors the other FX on/off
        // switches, which affect the sound the instant they're clicked rather
        // than only notes played afterwards.
        void setEnabled(bool shouldBeEnabled, juce::MidiBuffer& out, int samplePos = 0)
        {
            if (shouldBeEnabled == enabled)
                return;
            enabled = shouldBeEnabled;

            for (auto& h : held)
                for (int slot = 0; slot < kNumSlots; ++slot)
                    applySlot(h, slot, out, samplePos);
        }

        // Called once per block with the knob positions currently read from the
        // APVTS. A slot whose value changed re-strikes (or releases) its extra
        // note on every note currently held - so moving a knob away from 0 while
        // a note is held plays the new interval right away, "as soon as you move
        // it", and moving it back to 0 lets go of just that one added note.
        void updateOffsets(const int(&newOffsets)[kNumSlots], juce::MidiBuffer& out, int samplePos = 0)
        {
            for (int slot = 0; slot < kNumSlots; ++slot)
            {
                const int nv = juce::jlimit(-36, 36, newOffsets[slot]);
                if (nv == offsets[(size_t)slot])
                    continue;
                offsets[(size_t)slot] = nv;
                if (enabled)
                    for (auto& h : held)
                        applySlot(h, slot, out, samplePos);
            }
        }

        // Feed every note-on / note-off pulled from the incoming buffer through
        // here. The caller still forwards the original message itself - this
        // only ever appends the extra ones for the currently-active slots.
        void processMessage(const juce::MidiMessage& msg, juce::MidiBuffer& out, int samplePos)
        {
            if (msg.isNoteOn())
            {
                HeldNote h;
                h.channel = msg.getChannel();
                h.note = msg.getNoteNumber();
                h.velocity = msg.getVelocity();
                h.extra.fill(-1);

                if (enabled)
                    for (int slot = 0; slot < kNumSlots; ++slot)
                        applySlot(h, slot, out, samplePos);

                held.push_back(h);
            }
            else if (msg.isNoteOff())
            {
                const int channel = msg.getChannel();
                const int note = msg.getNoteNumber();
                for (size_t i = 0; i < held.size(); )
                {
                    if (held[i].channel == channel && held[i].note == note)
                    {
                        for (int slot = 0; slot < kNumSlots; ++slot)
                            if (held[i].extra[(size_t)slot] >= 0)
                                out.addEvent(juce::MidiMessage::noteOff(channel, held[i].extra[(size_t)slot]),
                                    samplePos);
                        held.erase(held.begin() + (long)i);
                    }
                    else ++i;
                }
            }
        }

        // Clears all bookkeeping without emitting note-offs - used alongside a
        // full engine/voice reload, where anything currently held is about to go
        // silent anyway.
        void reset() { held.clear(); }

    private:
        struct HeldNote
        {
            int channel{ 1 }, note{ 0 }, velocity{ 0 };
            std::array<int, kNumSlots> extra{ -1, -1, -1, -1, -1, -1 };
        };

        // Brings one slot of one held note in line with the current enabled
        // state + offset: releases the extra note it was playing (if any) and,
        // if the slot is now active, starts a new one at the right pitch. Used
        // both when a note first goes down (all slots start at "off") and when
        // a knob or the enable switch changes under an already-held note.
        void applySlot(HeldNote& h, int slot, juce::MidiBuffer& out, int samplePos)
        {
            auto& e = h.extra[(size_t)slot];
            if (e >= 0)
            {
                out.addEvent(juce::MidiMessage::noteOff(h.channel, e), samplePos);
                e = -1;
            }
            if (enabled && offsets[(size_t)slot] != 0)
            {
                const int n = juce::jlimit(0, 127, h.note + offsets[(size_t)slot]);
                out.addEvent(juce::MidiMessage::noteOn(h.channel, n, (uint8_t)h.velocity), samplePos);
                e = n;
            }
        }

        bool enabled{ false };
        std::array<int, kNumSlots> offsets{ 0, 0, 0, 0, 0, 0 };
        std::vector<HeldNote> held;
    };

    // ============================================================================
    //  SustainHold  -  MIDI-level sustain toggle for the Chord section. While
    //  active, every note-off is intercepted before it reaches anything else -
    //  the DX7 engine itself and the Harmonizer above both simply never see it,
    //  so neither the originally played note nor any chord-added note lets go.
    //  Switching the toggle back off releases everything that was being held
    //  back, all at once. A note that gets re-struck while its earlier release
    //  is being held back (the same key pressed again) just cancels the pending
    //  release instead of stacking a second one.
    //
    //  This sits ahead of the Harmonizer in processMidi() on purpose: filtering
    //  out the note-off first means the Harmonizer's own `held` bookkeeping
    //  never learns the note went away either, so its extra notes stay alive
    //  and still respond correctly to slot/offset changes while sustained.
    // ============================================================================
    class SustainHold
    {
    public:
        // Mirrors Harmonizer::setEnabled - only acts on an actual change, and an
        // off-transition is what flushes every note-off that had been held back.
        void setEnabled(bool shouldBeEnabled, juce::MidiBuffer& out, int samplePos = 0)
        {
            if (shouldBeEnabled == enabled)
                return;
            enabled = shouldBeEnabled;

            if (!enabled)
            {
                for (auto& h : held)
                    out.addEvent(juce::MidiMessage::noteOff(h.channel, h.note), samplePos);
                held.clear();
            }
        }

        // Called for every note-on/note-off pulled from the incoming buffer.
        // Returns true if the message should keep going (on to the Harmonizer
        // and the engine), false if this call consumed it (a note-off held back
        // by an active sustain).
        bool filterMessage(const juce::MidiMessage& msg)
        {
            const int channel = msg.getChannel();
            const int note = msg.getNoteNumber();

            if (msg.isNoteOff())
            {
                if (!enabled)
                    return true;
                held.push_back({ channel, note });
                return false;
            }

            if (msg.isNoteOn() && enabled)
            {
                // Re-pressing a key whose release is currently being held back:
                // drop the pending release rather than let it fire once this new
                // press eventually ends, which would cut the new press short.
                held.erase(std::remove_if(held.begin(), held.end(),
                    [&](const Held& h) { return h.channel == channel && h.note == note; }),
                    held.end());
            }

            return true;
        }

        // Clears bookkeeping without emitting note-offs - used alongside a full
        // engine/voice reload, where anything held is about to go silent anyway.
        void reset() { held.clear(); }

    private:
        struct Held { int channel, note; };
        bool enabled{ false };
        std::vector<Held> held;
    };

    // ============================================================================
    //  FxChain  -  the four effects in series, after the DX7 engine.
    //
    //  Parameter IDs are deliberately not in the "p<vcedOffset>" namespace the
    //  VCED parameters use, so the processor's autoParams_ table (built from
    //  Voice::paramTable()) never sees them and no FX value is ever mistaken for
    //  a byte to send to the emulator.
    // ============================================================================
    struct FxParam
    {
        const char* id;
        const char* name;
    };

    namespace ids {
        // Chorus
        static constexpr const char* chorusOn = "fxChorusOn";
        static constexpr const char* chorusRate = "fxChorusRate";
        static constexpr const char* chorusDepth = "fxChorusDepth";
        static constexpr const char* chorusDelay = "fxChorusDelay";
        static constexpr const char* chorusSpread = "fxChorusSpread";
        static constexpr const char* chorusMix = "fxChorusMix";
        // Delay
        static constexpr const char* delayOn = "fxDelayOn";
        static constexpr const char* delayTimeL = "fxDelayTimeL";
        static constexpr const char* delayTimeR = "fxDelayTimeR";
        static constexpr const char* delayFb = "fxDelayFeedback";
        static constexpr const char* delayHp = "fxDelayHighPass";
        static constexpr const char* delayMix = "fxDelayMix";
        // Phaser
        static constexpr const char* phaserOn = "fxPhaserOn";
        static constexpr const char* phaserRate = "fxPhaserRate";
        static constexpr const char* phaserDepth = "fxPhaserDepth";
        static constexpr const char* phaserCentre = "fxPhaserCentre";
        static constexpr const char* phaserFb = "fxPhaserFeedback";
        static constexpr const char* phaserStages = "fxPhaserStages";
        static constexpr const char* phaserMix = "fxPhaserMix";
        // Reverb
        static constexpr const char* reverbOn = "fxReverbOn";
        static constexpr const char* reverbSize = "fxReverbSize";
        static constexpr const char* reverbFb = "fxReverbFeedback";
        static constexpr const char* reverbDamp = "fxReverbDamping";
        static constexpr const char* reverbRate = "fxReverbModRate";
        static constexpr const char* reverbDepth = "fxReverbModDepth";
        static constexpr const char* reverbFreeze = "fxReverbFreeze";
        static constexpr const char* reverbMix = "fxReverbMix";
        // Chord (per-note MIDI harmonizer, see Harmonizer above)
        static constexpr const char* chordOn = "fxChordOn";
        static constexpr const char* chordNote1 = "fxChordNote1";
        static constexpr const char* chordNote2 = "fxChordNote2";
        static constexpr const char* chordNote3 = "fxChordNote3";
        static constexpr const char* chordNote4 = "fxChordNote4";
        static constexpr const char* chordNote5 = "fxChordNote5";
        static constexpr const char* chordNote6 = "fxChordNote6";
        static constexpr const char* chordSustain = "fxChordSustain";
    }

    class FxChain
    {
    public:
        // Adds every FX parameter to the plugin's APVTS layout. Called from
        // VDX7AudioProcessor::createLayout().
        static void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
        {
            using namespace ids;

            auto addFloat = [&layout](const char* id, const char* name,
                juce::NormalisableRange<float> range, float def,
                const char* suffix)
                {
                    layout.add(std::make_unique<juce::AudioParameterFloat>(
                        juce::ParameterID{ id, 1 }, name, range, def,
                        juce::AudioParameterFloatAttributes().withLabel(suffix)));
                };
            auto addBool = [&layout](const char* id, const char* name, bool def)
                {
                    layout.add(std::make_unique<juce::AudioParameterBool>(
                        juce::ParameterID{ id, 1 }, name, def));
                };

            const juce::NormalisableRange<float> percent(0.0f, 100.0f, 0.1f);

            // ---- Chorus ---------------------------------------------------------
            addBool(chorusOn, "Chorus On", false);
            addFloat(chorusRate, "Chorus Rate", logRange(0.1f, 10.0f), 0.5f, "Hz");
            addFloat(chorusDepth, "Chorus Depth", percent, 50.0f, "%");
            addFloat(chorusDelay, "Chorus Delay", { 1.0f, 20.0f, 0.1f }, 7.0f, "ms");
            addFloat(chorusSpread, "Chorus Spread", percent, 50.0f, "%");
            addFloat(chorusMix, "Chorus Mix", percent, 50.0f, "%");

            // ---- Delay ----------------------------------------------------------
            addBool(delayOn, "Delay On", false);
            addFloat(delayTimeL, "Delay Time L", logRange(20.0f, Delay::maxTimeMs), 250.0f, "ms");
            addFloat(delayTimeR, "Delay Time R", logRange(20.0f, Delay::maxTimeMs), 375.0f, "ms");
            addFloat(delayFb, "Delay Feedback", { 0.0f, 1.2f, 0.001f }, 0.3f, "");
            addFloat(delayHp, "Delay High-Pass", logRange(20.0f, 2000.0f), 100.0f, "Hz");
            addFloat(delayMix, "Delay Mix", percent, 35.0f, "%");

            // ---- Phaser ---------------------------------------------------------
            addBool(phaserOn, "Phaser On", false);
            addFloat(phaserRate, "Phaser Rate", logRange(0.05f, 10.0f), 0.5f, "Hz");
            addFloat(phaserDepth, "Phaser Depth", percent, 50.0f, "%");
            addFloat(phaserCentre, "Phaser Center", logRange(100.0f, 10000.0f), 800.0f, "Hz");
            addFloat(phaserFb, "Phaser Feedback", percent, 20.0f, "%");
            addFloat(phaserMix, "Phaser Mix", percent, 50.0f, "%");
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{ phaserStages, 1 }, "Phaser Stages",
                juce::StringArray{ "2", "4", "6", "8", "10", "12" }, 1));

            // ---- Reverb ---------------------------------------------------------
            addBool(reverbOn, "Reverb On", false);
            addFloat(reverbSize, "Reverb Size", logRange(0.1f, 8.0f), 1.0f, "");
            addFloat(reverbFb, "Reverb Feedback", { 0.0f, 1.2f, 0.001f }, 0.7f, "");
            addFloat(reverbDamp, "Reverb Damping", percent, 40.0f, "%");
            addFloat(reverbRate, "Reverb Mod Rate", logRange(0.01f, 8.0f), 0.25f, "Hz");
            addFloat(reverbDepth, "Reverb Mod Depth", { 0.0f, 50.0f, 0.1f }, 4.0f, "");
            addBool(reverbFreeze, "Reverb Freeze", false);
            addFloat(reverbMix, "Reverb Mix", percent, 35.0f, "%");

            // ---- Chord (per-note MIDI harmonizer) --------------------------------
            addBool(chordOn, "Chord On", false);
            auto addSemitones = [&layout](const char* id, const char* name, int defaultSemitones)
                {
                    layout.add(std::make_unique<juce::AudioParameterInt>(
                        juce::ParameterID{ id, 1 }, name, -36, 36, defaultSemitones,
                        juce::AudioParameterIntAttributes().withLabel("st")));
                };
            addSemitones(chordNote1, "Chord Note 1", 0);
            addSemitones(chordNote2, "Chord Note 2", 3);
            addSemitones(chordNote3, "Chord Note 3", 5);
            addSemitones(chordNote4, "Chord Note 4", 8);
            addSemitones(chordNote5, "Chord Note 5", 10);
            addSemitones(chordNote6, "Chord Note 6", 12);
            addBool(chordSustain, "Chord Sustain", false);
        }

        // Frequency-ish controls want a knob that spends its travel where the ear
        // does, not linearly across 20..2000.
        static juce::NormalisableRange<float> logRange(float lo, float hi)
        {
            juce::NormalisableRange<float> r(lo, hi);
            r.setSkewForCentre(std::sqrt(lo * hi));
            return r;
        }

        void prepare(double sr, int maxBlock)
        {
            chorus.prepare(sr, maxBlock);
            delay.prepare(sr, maxBlock);
            phaser.prepare(sr, maxBlock);
            reverb.prepare(sr, maxBlock);
            chord.reset(); sustain.reset();
        }

        void reset()
        {
            chorus.reset(); delay.reset(); phaser.reset(); reverb.reset();
            chord.reset(); sustain.reset();
        }

        // Caches the raw atomic pointers once, so the audio thread never touches
        // the APVTS parameter objects or does string lookups per block.
        void bindParameters(juce::AudioProcessorValueTreeState& apvts)
        {
            using namespace ids;
            auto get = [&apvts](const char* id) { return apvts.getRawParameterValue(id); };

            pChorusOn = get(chorusOn); pChorusRate = get(chorusRate);
            pChorusDepth = get(chorusDepth); pChorusDelay = get(chorusDelay);
            pChorusSpread = get(chorusSpread); pChorusMix = get(chorusMix);

            pDelayOn = get(delayOn); pDelayTimeL = get(delayTimeL);
            pDelayTimeR = get(delayTimeR); pDelayFb = get(delayFb);
            pDelayHp = get(delayHp); pDelayMix = get(delayMix);

            pPhaserOn = get(phaserOn); pPhaserRate = get(phaserRate);
            pPhaserDepth = get(phaserDepth); pPhaserCentre = get(phaserCentre);
            pPhaserFb = get(phaserFb); pPhaserStages = get(phaserStages);
            pPhaserMix = get(phaserMix);

            pReverbOn = get(reverbOn); pReverbSize = get(reverbSize);
            pReverbFb = get(reverbFb); pReverbDamp = get(reverbDamp);
            pReverbRate = get(reverbRate); pReverbDepth = get(reverbDepth);
            pReverbFreeze = get(reverbFreeze); pReverbMix = get(reverbMix);

            pChordOn = get(chordOn);
            pChordNote[0] = get(chordNote1); pChordNote[1] = get(chordNote2);
            pChordNote[2] = get(chordNote3); pChordNote[3] = get(chordNote4);
            pChordNote[4] = get(chordNote5); pChordNote[5] = get(chordNote6);
            pChordSustain = get(chordSustain);
        }

        // Called once per audio block, ahead of forwarding the block's incoming
        // MIDI to the engine. Expands each note-on/note-off already in `midi`
        // into up to 6 extra transposed notes and appends them to the same
        // buffer in place, so the caller can keep forwarding the whole buffer to
        // the engine exactly as it did before the chord section existed.
        void processMidi(juce::MidiBuffer& midi)
        {
            if (pChordOn == nullptr)
                return;

            auto v = [](const std::atomic<float>* p) { return p != nullptr ? p->load(std::memory_order_relaxed) : 0.0f; };

            // ---- Sustain, ahead of everything else -------------------------------
            // Filters the incoming buffer first: any note-off held back here never
            // reaches the Harmonizer below or the engine, so both the real note and
            // any chord-added notes riding on it stay sounding untouched.
            juce::MidiBuffer filtered;
            sustain.setEnabled(v(pChordSustain) >= 0.5f, filtered, 0);
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if ((msg.isNoteOn() || msg.isNoteOff()) && !sustain.filterMessage(msg))
                    continue; // note-off held back by sustain - not forwarded
                filtered.addEvent(msg, meta.samplePosition);
            }
            midi.swapWith(filtered);

            int offsets[Harmonizer::kNumSlots];
            for (int i = 0; i < Harmonizer::kNumSlots; ++i)
                offsets[i] = (int)v(pChordNote[(size_t)i]);

            juce::MidiBuffer extra;
            chord.setEnabled(v(pChordOn) >= 0.5f, extra);
            chord.updateOffsets(offsets, extra);

            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() || msg.isNoteOff())
                    chord.processMessage(msg, extra, meta.samplePosition);
            }

            for (const auto meta : extra)
                midi.addEvent(meta.getMessage(), meta.samplePosition);
        }

        // Audio thread. `left`/`right` are the DX7 engine's output, processed in
        // place. Safe to call before bindParameters() has run - it no-ops.
        void process(float* left, float* right, int numSamples)
        {
            if (pChorusOn == nullptr)
                return;

            auto v = [](const std::atomic<float>* p) { return p != nullptr ? p->load(std::memory_order_relaxed) : 0.0f; };
            auto on = [&v](const std::atomic<float>* p) { return v(p) >= 0.5f; };

            chorus.setEnabled(on(pChorusOn));
            chorus.setRate(v(pChorusRate));
            chorus.setDepth(v(pChorusDepth));
            chorus.setBaseDelay(v(pChorusDelay));
            chorus.setSpread(v(pChorusSpread));
            chorus.setDryWet(v(pChorusMix));

            delay.setEnabled(on(pDelayOn));
            delay.setTimeL(v(pDelayTimeL));
            delay.setTimeR(v(pDelayTimeR));
            delay.setFeedback(v(pDelayFb));
            delay.setHighPass(v(pDelayHp));
            delay.setDryWet(v(pDelayMix));

            phaser.setEnabled(on(pPhaserOn));
            phaser.setRate(v(pPhaserRate));
            phaser.setDepth(v(pPhaserDepth));
            phaser.setCentre(v(pPhaserCentre));
            phaser.setFeedback(v(pPhaserFb));
            phaser.setStages((int)v(pPhaserStages));
            phaser.setDryWet(v(pPhaserMix));

            reverb.setEnabled(on(pReverbOn));
            reverb.setSize(v(pReverbSize));
            reverb.setFeedback(v(pReverbFb));
            reverb.setDamping(v(pReverbDamp));
            reverb.setModRate(v(pReverbRate));
            reverb.setModDepth(v(pReverbDepth));
            reverb.setFreeze(on(pReverbFreeze));
            reverb.setDryWet(v(pReverbMix));

            chorus.process(left, right, numSamples);
            delay.process(left, right, numSamples);
            phaser.process(left, right, numSamples);
            reverb.process(left, right, numSamples);
        }

        // True if anything is audible, so the caller can keep the tail alive.
        bool isAnyActive() const
        {
            return chorus.isActive() || delay.isActive()
                || phaser.isActive() || reverb.isActive();
        }

    private:
        Chorus     chorus;
        Delay      delay;
        Phaser     phaser;
        Reverb     reverb;
        Harmonizer chord;
        SustainHold sustain;

        std::atomic<float>* pChorusOn{ nullptr }, * pChorusRate{ nullptr }, * pChorusDepth{ nullptr },
            * pChorusDelay{ nullptr }, * pChorusSpread{ nullptr }, * pChorusMix{ nullptr };
        std::atomic<float>* pDelayOn{ nullptr }, * pDelayTimeL{ nullptr }, * pDelayTimeR{ nullptr },
            * pDelayFb{ nullptr }, * pDelayHp{ nullptr }, * pDelayMix{ nullptr };
        std::atomic<float>* pPhaserOn{ nullptr }, * pPhaserRate{ nullptr }, * pPhaserDepth{ nullptr },
            * pPhaserCentre{ nullptr }, * pPhaserFb{ nullptr }, * pPhaserStages{ nullptr },
            * pPhaserMix{ nullptr };
        std::atomic<float>* pReverbOn{ nullptr }, * pReverbSize{ nullptr }, * pReverbFb{ nullptr },
            * pReverbDamp{ nullptr }, * pReverbRate{ nullptr }, * pReverbDepth{ nullptr },
            * pReverbFreeze{ nullptr }, * pReverbMix{ nullptr };
        std::atomic<float>* pChordOn{ nullptr };
        std::array<std::atomic<float>*, Harmonizer::kNumSlots> pChordNote{ nullptr, nullptr, nullptr,
                                                                            nullptr, nullptr, nullptr };
        std::atomic<float>* pChordSustain{ nullptr };
    };

} // namespace vdx7fx