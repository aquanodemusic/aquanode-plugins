#pragma once
#include <JuceHeader.h>

namespace aquanova {

//==============================================================================
/** Supernova II envelope.

    Beyond the usual ADSR it has the three things that make the SN2's envelopes
    move the way they do:

      Sustain Time - how long the sustain stage lasts before it gives up and
                     releases on its own (127 = hold forever)
      Sustain Rate - sustain drifts up or down while held, instead of sitting flat
      AD Repeat    - the attack/decay pair loops a set number of times, which is
                     where the machine's looping, rhythmic pads come from
*/
class Envelope
{
public:
    enum Stage { Idle = 0, Delay, Attack, Decay, Sustain, Release };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        stage = Idle;
        level = 0.0f;
        stageSamples = 0;
        repeatsLeft = 0;
        sustainSamples = 0;
    }

    struct Settings
    {
        int   delay = 0, attack = 0, decay = 0, sustain = 127, release = 0;
        int   sustainTime = 127;        // 127 = infinite
        float sustainRate = 0.0f;       // -1..+1
        int   adRepeat = 0;             // 0 = off, 127 = infinite
        float velocityAmount = 0.0f;    // 0..1
        float keyTrack = 0.0f;          // 0..1, shortens with pitch
    };

    void noteOn (const Settings& s, float velocity, int midiNote, bool retrigger) noexcept
    {
        cfg = s;
        velScale = 1.0f - cfg.velocityAmount * (1.0f - velocity);
        keyScale = 1.0f / (1.0f + cfg.keyTrack * ((float) (midiNote - 60) / 60.0f));
        keyScale = juce::jlimit (0.15f, 6.0f, keyScale);

        repeatsLeft = cfg.adRepeat == 0 ? 0 : (cfg.adRepeat >= 127 ? -1 : cfg.adRepeat);

        if (retrigger)
            level = 0.0f;

        sustainSamples = 0;
        stage = cfg.delay > 0 ? Delay : Attack;
        stageSamples = 0;
    }

    void noteOff() noexcept
    {
        if (stage != Idle)
            stage = Release;
    }

    bool isActive() const noexcept   { return stage != Idle; }
    float getLevel() const noexcept  { return level * velScale; }

    /** Advance one sample and return the current level, 0..1. */
    inline float process() noexcept
    {
        switch (stage)
        {
            case Idle:
                return 0.0f;

            case Delay:
                if (++stageSamples >= timeToSamples (cfg.delay))
                {
                    stage = Attack;
                    stageSamples = 0;
                }
                break;

            case Attack:
            {
                const float rate = rateFor (cfg.attack);
                level += rate * (1.05f - level);       // slight curve, like the hardware
                if (level >= 0.999f || cfg.attack == 0)
                {
                    level = 1.0f;
                    stage = Decay;
                }
                break;
            }

            case Decay:
            {
                const float target = (float) cfg.sustain / 127.0f;
                const float rate = rateFor (cfg.decay);
                level += rate * (target - level - 0.001f);

                if (level <= target + 0.001f)
                {
                    level = target;

                    if (repeatsLeft != 0)
                    {
                        if (repeatsLeft > 0) --repeatsLeft;
                        level = 0.0f;
                        stage = Attack;
                    }
                    else
                    {
                        stage = Sustain;
                        sustainSamples = 0;
                    }
                }
                break;
            }

            case Sustain:
            {
                if (std::abs (cfg.sustainRate) > 0.001f)
                {
                    // Sustain Rate is slow by design - seconds, not milliseconds.
                    level += cfg.sustainRate * 0.00002f;
                    level = juce::jlimit (0.0f, 1.0f, level);
                }

                if (cfg.sustainTime < 127)
                {
                    if (++sustainSamples >= timeToSamples (cfg.sustainTime))
                        stage = Release;
                }
                break;
            }

            case Release:
            {
                const float rate = rateFor (cfg.release);
                level -= rate * (level + 0.001f);

                if (level <= 0.0005f || cfg.release == 0)
                {
                    level = 0.0f;
                    stage = Idle;
                }
                break;
            }
        }

        return level * velScale;
    }

private:
    /** 0..127 maps to roughly 1 ms .. 20 s, exponentially. */
    inline float timeSeconds (int v) const noexcept
    {
        const float t = (float) juce::jlimit (0, 127, v) / 127.0f;
        return 0.001f * std::pow (20000.0f, t) * keyScale;
    }

    inline int timeToSamples (int v) const noexcept
    {
        return (int) (timeSeconds (v) * (float) sampleRate);
    }

    inline float rateFor (int v) const noexcept
    {
        const float s = timeSeconds (v);
        return juce::jlimit (0.0f, 1.0f, 1.0f / juce::jmax (1.0f, s * (float) sampleRate * 0.4f));
    }

    double sampleRate = 44100.0;
    Settings cfg;
    Stage stage = Idle;
    float level = 0.0f, velScale = 1.0f, keyScale = 1.0f;
    int stageSamples = 0, sustainSamples = 0, repeatsLeft = 0;
};

} // namespace aquanova
