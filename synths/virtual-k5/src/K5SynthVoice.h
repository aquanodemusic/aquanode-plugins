#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

/*
    K5SynthVoice.h — additive synthesis engine for VirtualK5.

    Signal path, following the original Kawai K5:

      DFG   pitch: base note, detune, pitch envelope, LFO vibrato
      DHG   63 harmonics per source. Each harmonic has a static level and is
            assigned to one of 4 envelope busses, so a handful of envelopes
            animate the whole spectrum. A harmonic MODE mask (ALL/ODD/EVEN/
            OCTAVE/FIFTH) selects which partials sound at all.
      DDF   dynamic filter. On the real machine this is not a filter across
            the summed signal — it scales individual harmonics. We do the
            same, which is both more faithful and cheaper than running a
            biquad per voice.
      DDA   amplitude envelope, plus LFO tremolo.
      DFT   an 11-band spectral shape applied across the harmonic series.

    Everything above the raw oscillator sum runs at a control rate of
    kControlBlock samples, with per-sample linear interpolation of harmonic
    amplitudes. The original updated its harmonic levels in discrete steps
    too, so this is in keeping rather than a shortcut.

    No heap allocation happens anywhere below the constructor: every envelope
    shape is a fixed-size array, so this is safe to drive from the audio
    thread.
*/

//==============================================================================
namespace K5Constants
{
    static constexpr int numHarmonics   = 63;
    static constexpr int numEnvGroups   = 4;
    static constexpr int maxEnvStages   = 8;
    static constexpr int controlBlock   = 32;
    static constexpr int sineTableBits  = 11;
    static constexpr int sineTableSize  = 1 << sineTableBits;
    static constexpr int dftTableSize   = 1024;

    static constexpr float dftLog2Lo = 4.32f;    // ~20 Hz
    static constexpr float dftLog2Hi = 14.29f;   // ~20 kHz
}

//==============================================================================
/** Shared sine table. Built once, read-only afterwards. */
class SineTable
{
public:
    static const SineTable& get()
    {
        static SineTable instance;
        return instance;
    }

    /** phase must be in [0, 1). */
    inline float lookup (float phase) const noexcept
    {
        const float scaled = phase * (float) K5Constants::sineTableSize;
        const int   index  = (int) scaled;
        const float frac   = scaled - (float) index;
        const int   i0     = index & (K5Constants::sineTableSize - 1);

        return table[(size_t) i0] + frac * (table[(size_t) i0 + 1] - table[(size_t) i0]);
    }

private:
    SineTable()
    {
        for (int i = 0; i <= K5Constants::sineTableSize; ++i)
            table[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi
                                                    * (double) i / (double) K5Constants::sineTableSize);
    }

    std::array<float, (size_t) K5Constants::sineTableSize + 1> table;
};

/** log2(n) for harmonic numbers 1..63, so the inner loop never calls log2(). */
class HarmonicLogTable
{
public:
    static const HarmonicLogTable& get()
    {
        static HarmonicLogTable instance;
        return instance;
    }

    inline float log2Of (int harmonicIndex) const noexcept
    {
        return values[(size_t) harmonicIndex];
    }

private:
    HarmonicLogTable()
    {
        for (int h = 0; h < K5Constants::numHarmonics; ++h)
            values[(size_t) h] = std::log2 ((float) (h + 1));
    }

    std::array<float, (size_t) K5Constants::numHarmonics> values;
};

//==============================================================================
/** One segment of a multi-stage envelope: "reach targetLevel in timeSeconds". */
struct EnvStage
{
    float timeSeconds  = 0.1f;
    float targetLevel  = 0.0f;
};

/** A fixed-capacity list of stages. Deliberately not a std::vector — these get
    copied around on the audio thread. */
struct EnvShape
{
    std::array<EnvStage, (size_t) K5Constants::maxEnvStages> stages {};
    int numStages = 0;

    void clear() noexcept { numStages = 0; }

    void add (float timeSeconds, float targetLevel) noexcept
    {
        if (numStages < K5Constants::maxEnvStages)
            stages[(size_t) numStages++] = { timeSeconds, targetLevel };
    }
};

/** Maps a familiar ADSR onto the hardware's rate/level stage model.

    The K5's DDA is a 7-stage and its DDF/DFG envelopes are 6-stage rate/level
    generators. Exposing all of that on the front panel would mean ~50 extra
    knobs per source, so we expose the ADSR subset: attack rises to full,
    decay falls to the sustain level and holds there, release falls to zero.
    The engine underneath is the full stage list, so a future editor can drive
    it directly without changing anything here. */
inline void buildADSRStages (float attack, float decay, float sustain, float release,
                             EnvShape& attackOut, EnvShape& releaseOut) noexcept
{
    attackOut.clear();
    attackOut.add (juce::jmax (0.0005f, attack), 1.0f);
    attackOut.add (juce::jmax (0.0005f, decay), juce::jlimit (0.0f, 1.0f, sustain));

    releaseOut.clear();
    releaseOut.add (juce::jmax (0.0005f, release), 0.0f);
}

//==============================================================================
/** Multi-stage rate/level envelope, advanced a control block at a time.

    Each stage moves exponentially toward its target and is considered done
    when its time has elapsed, rather than when it gets "close enough". That
    keeps stage timing exact even when two consecutive stages share a level. */
class MultiStageEnvelope
{
public:
    void setSampleRate (double rate) noexcept
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
    }

    void setShapes (const EnvShape& attackShape, const EnvShape& releaseShape) noexcept
    {
        attackStages  = attackShape;
        releaseStages = releaseShape;
    }

    void noteOn() noexcept
    {
        active      = true;
        inRelease   = false;
        stageIndex  = 0;
        beginStage();
    }

    void noteOff() noexcept
    {
        if (! active)
            return;

        inRelease  = true;
        stageIndex = 0;
        beginStage();
    }

    void reset() noexcept
    {
        active     = false;
        inRelease  = false;
        stageIndex = 0;
        level      = 0.0f;
    }

    bool isActive() const noexcept { return active; }
    float getLevel() const noexcept { return level; }

    float advance (int numSamples) noexcept
    {
        if (! active)
            return 0.0f;

        int remaining = numSamples;

        while (remaining > 0)
        {
            const auto& shape = inRelease ? releaseStages : attackStages;

            if (stageIndex >= shape.numStages)
            {
                // Attack list exhausted: hold (this is the sustain level).
                // Release list exhausted: the note is finished.
                if (inRelease)
                {
                    level  = 0.0f;
                    active = false;
                }
                return level;
            }

            const int step = juce::jmin (remaining, samplesLeftInStage);
            const float coeff = blockCoefficient (step);

            level += (stageTarget - level) * coeff;

            samplesLeftInStage -= step;
            remaining          -= step;

            if (samplesLeftInStage <= 0)
            {
                level = stageTarget;
                ++stageIndex;
                beginStage();
            }
        }

        return level;
    }

private:
    void beginStage() noexcept
    {
        const auto& shape = inRelease ? releaseStages : attackStages;

        if (stageIndex >= shape.numStages)
        {
            samplesLeftInStage = 0;
            return;
        }

        const auto& stage = shape.stages[(size_t) stageIndex];
        stageTarget = stage.targetLevel;
        samplesLeftInStage = juce::jmax (1, (int) (stage.timeSeconds * (float) sampleRate));
        stageSamples = samplesLeftInStage;
    }

    /** Coefficient that covers ~99% of the remaining distance across a full
        stage, giving an exponential curve that still lands on time. */
    float blockCoefficient (int numSamples) const noexcept
    {
        const double tau = juce::jmax (1.0, (double) stageSamples / 4.6);
        return (float) (1.0 - std::exp (-(double) numSamples / tau));
    }

    double  sampleRate = 44100.0;
    EnvShape attackStages, releaseStages;

    bool  active = false, inRelease = false;
    int   stageIndex = 0, samplesLeftInStage = 0, stageSamples = 1;
    float level = 0.0f, stageTarget = 0.0f;
};

//==============================================================================
/** The DHG: 63 harmonic levels, 4 envelope busses, and a mode mask. */
class HarmonicGenerator
{
public:
    static constexpr int numHarmonics = K5Constants::numHarmonics;
    static constexpr int numEnvGroups = K5Constants::numEnvGroups;

    enum class HarmonicMode { all = 0, odd, even, octave, fifth };

    void setSampleRate (double rate) noexcept
    {
        for (auto& env : envelopes)
            env.setSampleRate (rate);
    }

    void setHarmonic (int index, float level, int group) noexcept
    {
        if (juce::isPositiveAndBelow (index, numHarmonics))
        {
            levels[(size_t) index] = juce::jlimit (0.0f, 1.0f, level);
            groups[(size_t) index] = juce::jlimit (0, numEnvGroups - 1, group);
        }
    }

    void setEnvelopeStages (int group, const EnvShape& attack, const EnvShape& release) noexcept
    {
        if (juce::isPositiveAndBelow (group, numEnvGroups))
            envelopes[(size_t) group].setShapes (attack, release);
    }

    void setMode (HarmonicMode newMode) noexcept { mode = newMode; }

    void noteOn() noexcept  { for (auto& e : envelopes) e.noteOn(); }
    void noteOff() noexcept { for (auto& e : envelopes) e.noteOff(); }
    void reset() noexcept   { for (auto& e : envelopes) e.reset(); }

    /** Advances the four busses and writes the 63 static-times-envelope
        amplitudes into out[]. Mode masking is applied here. */
    void advance (int numSamples, float* out) noexcept
    {
        std::array<float, (size_t) numEnvGroups> busLevel {};

        for (int g = 0; g < numEnvGroups; ++g)
            busLevel[(size_t) g] = envelopes[(size_t) g].advance (numSamples);

        for (int h = 0; h < numHarmonics; ++h)
            out[h] = passesMode (h + 1) ? levels[(size_t) h] * busLevel[(size_t) groups[(size_t) h]]
                                        : 0.0f;
    }

private:
    bool passesMode (int harmonicNumber) const noexcept
    {
        switch (mode)
        {
            case HarmonicMode::all:  return true;
            case HarmonicMode::odd:  return (harmonicNumber % 2) == 1;
            case HarmonicMode::even: return harmonicNumber == 1 || (harmonicNumber % 2) == 0;

            // Octave: the fundamental and its doublings — 1, 2, 4, 8, 16, 32.
            case HarmonicMode::octave:
                return (harmonicNumber & (harmonicNumber - 1)) == 0;

            // Fifth: octaves plus their perfect fifths — 1, 2, 3, 4, 6, 8, 12...
            case HarmonicMode::fifth:
            {
                int n = harmonicNumber;
                while ((n % 3) == 0) n /= 3;
                return (n & (n - 1)) == 0;
            }
        }

        return true;
    }

    std::array<float, (size_t) numHarmonics> levels { };
    std::array<int,   (size_t) numHarmonics> groups { };
    std::array<MultiStageEnvelope, (size_t) numEnvGroups> envelopes;
    HarmonicMode mode = HarmonicMode::all;
};

//==============================================================================
/** The DDF. Scales harmonics rather than filtering the summed signal, which
    is how the hardware does it. */
class DynamicFilter
{
public:
    void setSampleRate (double rate) noexcept { envelope.setSampleRate (rate); }

    void setEnvelopeStages (const EnvShape& attack, const EnvShape& release) noexcept
    {
        envelope.setShapes (attack, release);
    }

    void setParams (float cutoffHz, float resonanceAmount,
                    float envelopeAmount, bool use24dB) noexcept
    {
        cutoff     = juce::jlimit (20.0f, 20000.0f, cutoffHz);
        resonance  = juce::jlimit (0.0f, 0.99f, resonanceAmount);
        envAmount  = juce::jlimit (-1.0f, 1.0f, envelopeAmount);
        steepSlope = use24dB;
    }

    void noteOn() noexcept  { envelope.noteOn(); }
    void noteOff() noexcept { envelope.noteOff(); }
    void reset() noexcept   { envelope.reset(); }

    /** Advances the envelope and returns the effective cutoff in Hz.
        modOctaves lets the LFO push the cutoff around. */
    float advance (int numSamples, float modOctaves) noexcept
    {
        const float env = envelope.advance (numSamples);
        const float octaves = envAmount * env * 6.0f + modOctaves;

        return juce::jlimit (20.0f, 20000.0f, cutoff * std::exp2 (octaves));
    }

    /** Gain for a partial at frequency f, given an effective cutoff. */
    inline float gainFor (float frequency, float effectiveCutoff) const noexcept
    {
        const float r  = frequency / effectiveCutoff;
        const float r2 = r * r;
        const float r4 = r2 * r2;

        const float rolloff = steepSlope ? 1.0f / std::sqrt (1.0f + r4 * r4)   // 24 dB/oct
                                         : 1.0f / std::sqrt (1.0f + r4);       // 12 dB/oct

        if (resonance <= 0.0f)
            return rolloff;

        // A bump centred on the cutoff. (r - 1/r) is zero at r == 1 and grows
        // symmetrically either side in log-frequency, so this is a cheap
        // stand-in for a resonant peak with no transcendentals.
        const float d = r - 1.0f / juce::jmax (1.0e-6f, r);
        const float peak = (resonance * 3.0f) / (1.0f + 8.0f * d * d);

        return rolloff * (1.0f + peak);
    }

private:
    MultiStageEnvelope envelope;
    float cutoff = 4000.0f, resonance = 0.1f, envAmount = 0.0f;
    bool  steepSlope = false;
};

//==============================================================================
/** The DFT: an 11-band spectral shape applied across the harmonic series.

    Rebuilt into a log-frequency lookup table whenever a band changes, so the
    per-partial cost in the render loop is one table read. */
class DigitalFormantFilter
{
public:
    static constexpr int numBands = 11;

    DigitalFormantFilter() { rebuild(); }

    void setBand (int index, float frequency, float gain, float q) noexcept
    {
        if (! juce::isPositiveAndBelow (index, numBands))
            return;

        auto& band = bands[(size_t) index];
        const float newFreq = juce::jlimit (20.0f, 20000.0f, frequency);
        const float newGain = juce::jlimit (0.0f, 1.0f, gain);
        const float newQ    = juce::jlimit (0.0f, 0.99f, q);

        if (band.frequency != newFreq || band.gain != newGain || band.q != newQ)
        {
            band = { newFreq, newGain, newQ };
            dirty = true;
        }
    }

    /** Call from the parameter-update path, never from the render loop. */
    void updateIfNeeded() noexcept
    {
        if (dirty)
        {
            rebuild();
            dirty = false;
        }
    }

    /** log2Frequency is log2(f in Hz). */
    inline float gainAt (float log2Frequency) const noexcept
    {
        const float norm = (log2Frequency - K5Constants::dftLog2Lo)
                            / (K5Constants::dftLog2Hi - K5Constants::dftLog2Lo);
        const int index = juce::jlimit (0, K5Constants::dftTableSize - 1,
                                        (int) (norm * (float) K5Constants::dftTableSize));
        return table[(size_t) index];
    }

private:
    struct Band { float frequency = 1000.0f, gain = 0.0f, q = 0.6f; };

    void rebuild() noexcept
    {
        for (int i = 0; i < K5Constants::dftTableSize; ++i)
        {
            const float log2f = K5Constants::dftLog2Lo
                                 + (K5Constants::dftLog2Hi - K5Constants::dftLog2Lo)
                                    * ((float) i / (float) K5Constants::dftTableSize);

            // Bands are additive on top of a flat unity response, so a patch
            // with every gain at zero is transparent rather than silent.
            float total = 1.0f;

            for (const auto& band : bands)
            {
                if (band.gain <= 0.0f)
                    continue;

                const float width = 0.35f + (1.0f - band.q) * 1.6f;   // in octaves
                const float d = (log2f - std::log2 (band.frequency)) / width;
                total += band.gain * 4.0f * std::exp (-d * d);
            }

            table[(size_t) i] = total;
        }
    }

    std::array<Band, (size_t) numBands> bands;
    std::array<float, (size_t) K5Constants::dftTableSize> table {};
    bool dirty = false;
};

//==============================================================================
class LFO
{
public:
    enum class Shape { triangle = 0, saw, square, random };

    void setSampleRate (double rate) noexcept { sampleRate = rate > 0.0 ? rate : 44100.0; }
    void setShape (Shape newShape) noexcept   { shape = newShape; }
    void setRateHz (float hz) noexcept        { rateHz = juce::jlimit (0.01f, 50.0f, hz); }
    void setDelaySeconds (float seconds) noexcept { delaySeconds = juce::jmax (0.0f, seconds); }

    void noteOn() noexcept
    {
        phase = 0.0f;
        elapsed = 0.0f;
    }

    /** Advances by numSamples and returns a value in [-1, 1], already scaled
        by the delay ramp. */
    float advance (int numSamples) noexcept
    {
        const float dt = (float) numSamples / (float) sampleRate;
        elapsed += dt;

        const float previousPhase = phase;
        phase += rateHz * dt;

        while (phase >= 1.0f)
            phase -= 1.0f;

        float value = 0.0f;

        switch (shape)
        {
            case Shape::triangle: value = 4.0f * std::abs (phase - 0.5f) - 1.0f; break;
            case Shape::saw:      value = 2.0f * phase - 1.0f;                   break;
            case Shape::square:   value = phase < 0.5f ? 1.0f : -1.0f;           break;

            case Shape::random:
                if (phase < previousPhase)                 // wrapped: new value
                    randomValue = random.nextFloat() * 2.0f - 1.0f;
                value = randomValue;
                break;
        }

        const float ramp = delaySeconds <= 0.0f ? 1.0f
                                                : juce::jlimit (0.0f, 1.0f, elapsed / delaySeconds);
        return value * ramp;
    }

private:
    double sampleRate = 44100.0;
    Shape  shape = Shape::triangle;
    float  rateHz = 4.0f, delaySeconds = 0.0f;
    float  phase = 0.0f, elapsed = 0.0f, randomValue = 0.0f;
    juce::Random random;
};

//==============================================================================
/** One of the two sources (S1 / S2) that make up a K5 "single". */
class K5Source
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate = rate;
        dhg.setSampleRate (rate);
        ddf.setSampleRate (rate);
        ampEnvelope.setSampleRate (rate);
        pitchEnvelope.setSampleRate (rate);
    }

    void setLevel (float newLevel) noexcept { level = juce::jlimit (0.0f, 1.0f, newLevel); }
    void setDetuneCents (float cents) noexcept { detuneRatio = std::exp2 (cents / 1200.0f); }

    void setPitchEnvelope (const EnvShape& attack, const EnvShape& release, float depthSemitones) noexcept
    {
        pitchEnvelope.setShapes (attack, release);
        pitchDepth = depthSemitones;
    }

    void setAmpEnvelope (const EnvShape& attack, const EnvShape& release) noexcept
    {
        ampEnvelope.setShapes (attack, release);
    }

    DynamicFilter&     getDDF() noexcept { return ddf; }
    HarmonicGenerator& getDHG() noexcept { return dhg; }

    void noteOn (float frequency, float velocityIn) noexcept
    {
        baseFrequency = frequency;
        velocity = velocityIn;

        phases.fill (0.0f);
        amplitudes.fill (0.0f);

        dhg.noteOn();
        ddf.noteOn();
        ampEnvelope.noteOn();
        pitchEnvelope.noteOn();
    }

    void noteOff() noexcept
    {
        dhg.noteOff();
        ddf.noteOff();
        ampEnvelope.noteOff();
        pitchEnvelope.noteOff();
    }

    void reset() noexcept
    {
        dhg.reset();
        ddf.reset();
        ampEnvelope.reset();
        pitchEnvelope.reset();
        phases.fill (0.0f);
        amplitudes.fill (0.0f);
    }

    bool isActive() const noexcept { return ampEnvelope.isActive(); }

    /** Renders one control block, adding into out[]. */
    void renderControlBlock (float* out, int numSamples,
                             const DigitalFormantFilter& dft,
                             float vibratoRatio, float tremoloGain, float filterModOctaves) noexcept
    {
        const float ampEnv = ampEnvelope.advance (numSamples);
        const float pitchEnv = pitchEnvelope.advance (numSamples);
        const float cutoff = ddf.advance (numSamples, filterModOctaves);

        std::array<float, (size_t) K5Constants::numHarmonics> harmonicLevels {};
        dhg.advance (numSamples, harmonicLevels.data());

        if (! ampEnvelope.isActive() && ampEnv <= 0.0f)
            return;

        const float pitchRatio = std::exp2 (pitchEnv * pitchDepth / 12.0f);
        const float f0 = baseFrequency * detuneRatio * vibratoRatio * pitchRatio;
        const float log2f0 = std::log2 (juce::jmax (1.0f, f0));
        const float nyquist = 0.45f * (float) sampleRate;

        const auto& harmonicLog = HarmonicLogTable::get();
        const auto& sine = SineTable::get();

        // Normalising by the summed harmonic level keeps a 63-partial patch
        // roughly as loud as a 3-partial one, so the master knob isn't doing
        // all the work.
        float sum = 0.0f;
        for (int h = 0; h < K5Constants::numHarmonics; ++h)
            sum += harmonicLevels[(size_t) h];

        const float normalise = 1.0f / juce::jmax (0.25f, sum);
        const float voiceGain = level * ampEnv * tremoloGain * velocity * normalise;
        const float inverseSampleRate = 1.0f / (float) sampleRate;

        for (int h = 0; h < K5Constants::numHarmonics; ++h)
        {
            const float frequency = f0 * (float) (h + 1);

            float target = 0.0f;

            if (frequency < nyquist && harmonicLevels[(size_t) h] > 0.0f)
            {
                const float log2f = log2f0 + harmonicLog.log2Of (h);
                target = harmonicLevels[(size_t) h]
                            * ddf.gainFor (frequency, cutoff)
                            * dft.gainAt (log2f)
                            * voiceGain;
            }

            float amplitude = amplitudes[(size_t) h];
            const float step = (target - amplitude) / (float) numSamples;

            if (amplitude <= 1.0e-7f && target <= 1.0e-7f)
            {
                // Silent partial: keep its phase coherent but skip the work.
                amplitudes[(size_t) h] = target;
                phases[(size_t) h] = std::fmod (phases[(size_t) h]
                                        + frequency * inverseSampleRate * (float) numSamples, 1.0f);
                continue;
            }

            float phase = phases[(size_t) h];
            const float increment = frequency * inverseSampleRate;

            for (int i = 0; i < numSamples; ++i)
            {
                out[i] += amplitude * sine.lookup (phase);

                phase += increment;
                if (phase >= 1.0f)
                    phase -= 1.0f;

                amplitude += step;
            }

            phases[(size_t) h] = phase;
            amplitudes[(size_t) h] = target;
        }
    }

private:
    double sampleRate = 44100.0;

    HarmonicGenerator  dhg;
    DynamicFilter      ddf;
    MultiStageEnvelope ampEnvelope, pitchEnvelope;

    std::array<float, (size_t) K5Constants::numHarmonics> phases {};
    std::array<float, (size_t) K5Constants::numHarmonics> amplitudes {};

    float level = 0.8f, detuneRatio = 1.0f, pitchDepth = 0.0f;
    float baseFrequency = 440.0f, velocity = 1.0f;
};

//==============================================================================
/** A complete voice: two sources, a shared LFO and the shared DFT. */
class K5Voice
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate = rate;
        s1.prepare (rate);
        s2.prepare (rate);
        lfo.setSampleRate (rate);
    }

    K5Source& getS1() noexcept { return s1; }
    K5Source& getS2() noexcept { return s2; }
    LFO&      getLFO() noexcept { return lfo; }

    /*  The DFT is a global, patch-wide spectral shape, so all voices share a
        single instance owned by the processor. Giving each voice its own copy
        would mean rebuilding 16 lookup tables every time one formant knob
        moved. */
    void setSharedDFT (DigitalFormantFilter* shared) noexcept { dft = shared; }

    void setModRouting (float vibratoSemitones, float tremoloDepth, float filterOctaves) noexcept
    {
        vibratoAmount = vibratoSemitones;
        tremoloAmount = juce::jlimit (0.0f, 1.0f, tremoloDepth);
        filterAmount  = filterOctaves;
    }

    void noteOn (float frequency, float velocity) noexcept
    {
        lfo.noteOn();
        s1.noteOn (frequency, velocity);
        s2.noteOn (frequency, velocity);
    }

    void noteOff() noexcept
    {
        s1.noteOff();
        s2.noteOff();
    }

    void reset() noexcept
    {
        s1.reset();
        s2.reset();
    }

    bool isActive() const noexcept { return s1.isActive() || s2.isActive(); }

    /** Renders numSamples of mono output, adding into out[]. */
    void render (float* out, int numSamples) noexcept
    {
        if (dft == nullptr)
            return;

        int offset = 0;

        while (offset < numSamples)
        {
            const int block = juce::jmin (K5Constants::controlBlock, numSamples - offset);

            const float lfoValue = lfo.advance (block);
            const float vibratoRatio = std::exp2 (lfoValue * vibratoAmount / 12.0f);
            const float tremoloGain  = 1.0f - tremoloAmount * 0.5f * (1.0f - lfoValue);
            const float filterMod    = lfoValue * filterAmount;

            s1.renderControlBlock (out + offset, block, *dft, vibratoRatio, tremoloGain, filterMod);
            s2.renderControlBlock (out + offset, block, *dft, vibratoRatio, tremoloGain, filterMod);

            offset += block;
        }
    }

private:
    double sampleRate = 44100.0;

    K5Source s1, s2;
    LFO      lfo;
    DigitalFormantFilter* dft = nullptr;

    float vibratoAmount = 0.0f, tremoloAmount = 0.0f, filterAmount = 0.0f;
};

//==============================================================================
class K5Sound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
/** Thin juce::Synthesiser adapter around K5Voice. */
class K5SynthVoice : public juce::SynthesiserVoice
{
public:
    K5Voice voice;

    void prepare (double rate)
    {
        voice.prepare (rate);
        scratch.setSize (1, 4096, false, false, true);
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<K5Sound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int currentPitchWheelPosition) override
    {
        pitchBendSemitones = (float) (currentPitchWheelPosition - 8192) / 8192.0f * 2.0f;

        const float frequency = (float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber)
                                    * std::exp2 (pitchBendSemitones / 12.0f);

        voice.noteOn (frequency, juce::jmax (0.05f, velocity));
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            voice.noteOff();
        }
        else
        {
            voice.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        pitchBendSemitones = (float) (newValue - 8192) / 8192.0f * 2.0f;
    }

    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! voice.isActive())
        {
            if (isVoiceActive())
                clearCurrentNote();

            return;
        }

        if (scratch.getNumSamples() < numSamples)
            scratch.setSize (1, numSamples, false, false, true);

        scratch.clear (0, 0, numSamples);
        voice.render (scratch.getWritePointer (0), numSamples);

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addFrom (channel, startSample, scratch, 0, 0, numSamples);

        if (! voice.isActive())
            clearCurrentNote();
    }

private:
    juce::AudioBuffer<float> scratch;
    float pitchBendSemitones = 0.0f;
};
