/*
    PluginProcessor.h  -  83ChorusVerb

    A standalone effects plugin extracted from VirtualDX7's global FX
    section. Only the four audio effects are kept - Chorus, Delay, Phaser,
    Reverb - wired in that fixed order, one after another:

        IN  ->  Chorus  ->  Delay  ->  Phaser  ->  Reverb  ->  OUT

    (VirtualDX7 also had a fifth "Chord" section, a MIDI note-harmonizer
    that isn't an audio effect at all - it isn't part of this plugin.)

    The DSP below is the same algorithms as VirtualDX7's FxChain.h
    (themselves ported from the Aquanode Modular effect modules), lifted
    out unchanged. Everything unrelated to these four effects - the DX7
    engine, patch/voice model, MIDI handling, ROM loading - has been
    stripped away, so this plugin can be dropped into any DAW as a plain
    stereo insert effect.

    GPLv3.
*/
#pragma once
#include <JuceHeader.h>

#include <atomic>
#include <vector>
#include <cmath>
#include <array>

namespace cv83 {

    // ========================================================================
    //  FxBase  -  shared enable ramp + dry/wet mix.
    //
    //  A hard bypass switch clicks, so `enable` drives a smoothed gain that
    //  crossfades between the processed and the untouched signal. Once that
    //  gain has fully settled at zero the effect stops processing altogether,
    //  which keeps an idle effect off the CPU budget.
    // ========================================================================
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

    // ========================================================================
    //  Chorus  -  modulated delay line per channel; the LFO phase offset
    //  between the channels is the stereo spread.
    // ========================================================================
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

    // ========================================================================
    //  Delay  -  stereo, independent times per channel, one-pole high-pass in
    //  the feedback loop.
    // ========================================================================
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

    // ========================================================================
    //  Phaser  -  2..12 first-order allpass stages per channel swept by an
    //  LFO around a centre frequency, with feedback.
    // ========================================================================
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

    // ========================================================================
    //  Reverb  -  8-line FDN with input diffusion allpasses, per-line lowpass
    //  damping and a Householder feedback matrix. Freeze holds the tank.
    // ========================================================================
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

    // ========================================================================
    //  Parameter IDs
    // ========================================================================
    namespace ids {
        // Chorus
        static constexpr const char* chorusOn = "chorusOn";
        static constexpr const char* chorusRate = "chorusRate";
        static constexpr const char* chorusDepth = "chorusDepth";
        static constexpr const char* chorusDelay = "chorusDelay";
        static constexpr const char* chorusSpread = "chorusSpread";
        static constexpr const char* chorusMix = "chorusMix";
        // Delay
        static constexpr const char* delayOn = "delayOn";
        static constexpr const char* delayTimeL = "delayTimeL";
        static constexpr const char* delayTimeR = "delayTimeR";
        static constexpr const char* delayFb = "delayFeedback";
        static constexpr const char* delayHp = "delayHighPass";
        static constexpr const char* delayMix = "delayMix";
        // Phaser
        static constexpr const char* phaserOn = "phaserOn";
        static constexpr const char* phaserRate = "phaserRate";
        static constexpr const char* phaserDepth = "phaserDepth";
        static constexpr const char* phaserCentre = "phaserCentre";
        static constexpr const char* phaserFb = "phaserFeedback";
        static constexpr const char* phaserStages = "phaserStages";
        static constexpr const char* phaserMix = "phaserMix";
        // Reverb
        static constexpr const char* reverbOn = "reverbOn";
        static constexpr const char* reverbSize = "reverbSize";
        static constexpr const char* reverbFb = "reverbFeedback";
        static constexpr const char* reverbDamp = "reverbDamping";
        static constexpr const char* reverbRate = "reverbModRate";
        static constexpr const char* reverbDepth = "reverbModDepth";
        static constexpr const char* reverbFreeze = "reverbFreeze";
        static constexpr const char* reverbMix = "reverbMix";
    }

    // ========================================================================
    //  FxChain  -  the four effects in series, in the fixed order
    //  Chorus -> Delay -> Phaser -> Reverb.
    // ========================================================================
    class FxChain
    {
    public:
        // Adds every FX parameter to the plugin's APVTS layout. Called from
        // ChorusVerbAudioProcessor::createLayout().
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
        }

        void prepare(double sr, int maxBlock)
        {
            chorus.prepare(sr, maxBlock);
            delay.prepare(sr, maxBlock);
            phaser.prepare(sr, maxBlock);
            reverb.prepare(sr, maxBlock);
        }

        void reset()
        {
            chorus.reset(); delay.reset(); phaser.reset(); reverb.reset();
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
        }

        // Audio thread. `left`/`right` are the plugin's own audio input,
        // processed in place, in the fixed order Chorus -> Delay -> Phaser ->
        // Reverb. Safe to call before bindParameters() has run - it no-ops.
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

        // True if anything is audible, so the caller could keep a tail alive.
        bool isAnyActive() const
        {
            return chorus.isActive() || delay.isActive()
                || phaser.isActive() || reverb.isActive();
        }

    private:
        // Frequency-ish controls want a knob that spends its travel where the
        // ear does, not linearly across the whole range.
        static juce::NormalisableRange<float> logRange(float lo, float hi)
        {
            juce::NormalisableRange<float> r(lo, hi);
            r.setSkewForCentre(std::sqrt(lo * hi));
            return r;
        }

        Chorus chorus;
        Delay  delay;
        Phaser phaser;
        Reverb reverb;

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
    };

} // namespace cv83

// ============================================================================
//  juce::AudioProcessor  -  plain stereo insert effect.
// ============================================================================
class ChorusVerbAudioProcessor : public juce::AudioProcessor
{
public:
    ChorusVerbAudioProcessor();
    ~ChorusVerbAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "83ChorusVerb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // The FDN reverb rings on well after the input stops.
    double getTailLengthSeconds() const override { return 8.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int sizeInBytes) override;

    // Every knob in the UI is a SliderParameterAttachment (or Button/ComboBox
    // attachment) bound straight to one of these, so hosts can read, write and
    // automate all of them.
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    cv83::FxChain fx_;
    std::vector<float> scratchR_;   // scratch right channel, used only on a mono bus

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusVerbAudioProcessor)
};
