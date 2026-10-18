#pragma once

#include "ModuleCore.h"
#include <array>
#include <cmath>

//==============================================================================
// Polyphony Sum - counts how many voices are actually sounding and pulls the
// level down to match, so a chord does not arrive N times louder than a single
// note.
//
// Why this is a module rather than something the engine does silently: a patch
// that runs hot is sometimes exactly what you want, and clamping every patch by
// default would take that away. Put this in front of Audio Out when you want
// predictable level, leave it out when you don't.
//
// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------
// This is a Flexible module, which matters: voiceNoteOn / voiceReset are only
// delivered to modules the engine has marked per-voice, so a Global module
// cannot see voice lifecycle at all. Fed by any per-voice source (an
// Oscillator, an ADSR, anything upstream that plays notes) this runs in the
// per-voice lane and gets the callbacks.
//
// If nothing per-voice feeds it, it lands in the global lane, never learns a
// count, and passes audio through at unity. That is the honest fallback: it
// cannot know what it was not told, and silently guessing would be worse.
//
// The count is decremented on voiceReset, NOT on voiceNoteOff. A voice that has
// been released is still audible while its envelope tail rings out, and
// counting it as gone the instant the key lifts would jump the gain up mid-tail
// and pump audibly. voiceReset is the point where the engine has genuinely
// finished with the voice.
//
// Voices are tracked as a flag per slot rather than as a bare counter, so a
// retrigger of an already-active voice cannot double-count it.
//
// ---------------------------------------------------------------------------
// The law
// ---------------------------------------------------------------------------
// gain = N ^ -amount, where N is the live voice count.
//
//   amount 0.0   no compensation at all - bypass, the old behaviour
//   amount 0.5   1/sqrt(N), correct for UNCORRELATED voices. Different pitches
//                sum as power rather than as amplitude, so this is the right
//                default for ordinary chords.
//   amount 1.0   1/N, correct for CORRELATED voices. Unison stacks, or several
//                voices on the same pitch and phase, sum as amplitude.
//
// Real chords sit between the two, which is why this is a knob and not a
// constant. Note that a full 0.5 makes a six-note chord peak at exactly the
// level of one note, which is numerically right but often musically wrong -
// chords usually want to be a little louder than single notes. Somewhere around
// 0.35-0.45 tends to feel better than the theoretically correct value.
//
// ---------------------------------------------------------------------------
// Smoothing
// ---------------------------------------------------------------------------
// The voice count is an integer, so the gain it implies is a staircase, and
// stepping it directly clicks on every note. Smooth is a one-pole time constant
// on the gain. It is advanced exactly once per sample even though
// processVoiceSample is called once per voice per sample - see newSampleStarted().
//==============================================================================
class PolySumModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pAmount = 0, pSmooth };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double newSampleRate) override
    {
        aquanode::SynthModule::prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        voiceActive_.fill (false);
        liveVoices_ = 0;
        smoothedGain_ = 1.0f;
        lastVoiceSeen_ = -1;
    }

    //=== voice lifecycle ======================================================
    void voiceNoteOn (int voice, int, bool) override
    {
        if (! inRange (voice)) return;
        if (! voiceActive_[(size_t) voice])
        {
            voiceActive_[(size_t) voice] = true;
            ++liveVoices_;
        }
    }

    // Deliberately NOT voiceNoteOff: a released voice is still ringing.
    void voiceReset (int voice) override
    {
        if (! inRange (voice)) return;
        if (voiceActive_[(size_t) voice])
        {
            voiceActive_[(size_t) voice] = false;
            if (liveVoices_ > 0) --liveVoices_;
        }
    }

    //=== audio ================================================================
    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        if (newSampleStarted (voice))
            advanceGain();

        outputs[0][0] = inputs[0][0] * smoothedGain_;
        outputs[0][1] = inputs[0][1] * smoothedGain_;
        outputs[1][0] = voicesModOut();
        outputs[1][1] = outputs[1][0];
    }

    // Global lane: no per-voice source upstream, so no count was ever
    // delivered. Pass through untouched rather than pretend.
    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override
    {
        advanceGainToward (1.0f);
        outputs[0][0] = inputs[0][0];
        outputs[0][1] = inputs[0][1];
        outputs[1][0] = 0.0f;
        outputs[1][1] = 0.0f;
    }

    // Shown next to the module title so the count is visible while playing.
    juce::String titleSuffix() const override
    {
        return liveVoices_ > 0 ? (" [" + juce::String (liveVoices_) + "]") : juce::String();
    }

private:
    static bool inRange (int v) { return v >= 0 && v < aquanode::kMaxVoices; }

    // processVoiceSample runs once per active voice per sample, but the gain
    // ramp must advance once per SAMPLE. The engine walks its active-voice list
    // in ascending order, so the first call of each new sample is the one whose
    // voice index is not greater than the previous call's. That detects a
    // wrap without assuming which voices happen to be active.
    bool newSampleStarted (int voice)
    {
        const bool wrapped = (voice <= lastVoiceSeen_);
        lastVoiceSeen_ = voice;
        return wrapped;
    }

    float targetGain() const
    {
        const int n = juce::jmax (1, liveVoices_);
        if (n <= 1) return 1.0f;
        const float amount = juce::jlimit (0.0f, 1.0f, param (pAmount));
        if (amount <= 0.0f) return 1.0f;
        return std::pow ((float) n, -amount);
    }

    void advanceGain() { advanceGainToward (targetGain()); }

    void advanceGainToward (float target)
    {
        const float ms = juce::jmax (0.1f, param (pSmooth));
        const float coeff = 1.0f - std::exp (-1.0f / (float) (juce::jmax (1.0, sampleRate) * ms * 0.001));
        smoothedGain_ += (target - smoothedGain_) * coeff;
    }

    float voicesModOut() const
    {
        return (float) liveVoices_ / (float) aquanode::kMaxVoices;
    }

    std::array<bool, aquanode::kMaxVoices> voiceActive_ {};
    int   liveVoices_ { 0 };
    float smoothedGain_ { 1.0f };
    int   lastVoiceSeen_ { -1 };
};
