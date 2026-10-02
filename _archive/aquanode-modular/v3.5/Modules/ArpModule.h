#pragma once

#include "ModuleCore.h"

// Arp - holds down whatever chord you play and walks through it in time.
// Patch its Midi Out into a generator's Add Midi In and that generator hears
// ONLY the arpeggio, not the chord you are holding - which is the difference
// between this and Midi Add. Midi Add layers notes on top of yours; the Arp
// takes them over. Generators with nothing patched keep playing the chord as
// normal, so one Arp'd Oscillator over one plain pad is a two-cable trick.
//
// Runs from the host tempo. Octaves repeats the chord upward; Gate sets how
// much of each step actually sounds (short = staccato, ~100% = legato lines
// that feed nicely into Glide with Voices = 1); Swing delays every second step.
class ArpModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDivision = 0, pMode, pOctaves, pGate, pSwing };
    enum Mode { mUp = 0, mDown, mUpDown, mRandom, mAsPlayed };

    // this source REPLACES the played notes for anything listening to it
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }

    void reset() override
    {
        numHeld = 0;
        currentNote = -1;
        stepIndex = 0;
        phase = 0.0;
        goingUp = true;
        gateClosed = true;
    }

    //=== called by the engine as keys go down and up (audio thread) ==========
    void midiDriverHeldNoteOn (int note) override
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note)
                return;                    // already down
        if (numHeld < 128)
            held[numHeld++] = note;        // keep the order they were played in
    }

    void midiDriverHeldNoteOff (int note) override
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                    held[j] = held[j + 1];
                --numHeld;
                return;
            }
    }

    void midiDriverAllHeldOff() override { numHeld = 0; }

    bool isIdle() const { return numHeld == 0 && currentNote < 0; }

    // One sample of arpeggiator time. Reports at most one note-off and one
    // note-on; both can land on the same sample when a step boundary arrives
    // with the previous step still sounding.
    void midiDriverAdvance (double sr, int& onNote, int& offNote) override
    {
        onNote = -1;
        offNote = -1;

        if (numHeld == 0)
        {
            if (currentNote >= 0)          // chord released: let the last note go
            {
                offNote = currentNote;
                currentNote = -1;
            }
            phase = 0.0;
            stepIndex = 0;
            goingUp = true;
            return;
        }

        const double bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;
        double stepSeconds = (60.0 / bpm) * aquanode::seqDivisionBeats((int)param(pDivision));

        // swing: every second step starts late, so lengthen it and shorten the next
        const double swing = param (pSwing) * 0.01 * 0.5;
        if (swing > 0.0)
            stepSeconds *= (stepCounter % 2 == 0) ? (1.0 + swing) : (1.0 - swing);

        phase += 1.0 / (juce::jmax (0.0005, stepSeconds) * sr);

        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            if (currentNote >= 0 && ! gateClosed)
                offNote = currentNote;

            onNote = pickNext();
            currentNote = onNote;
            gateClosed = false;
            ++stepCounter;
        }
        else if (currentNote >= 0 && ! gateClosed && phase >= param (pGate) * 0.01)
        {
            offNote = currentNote;
            gateClosed = true;
        }
    }

private:
    // the chord, expanded across the octave range, in the current direction
    int buildSequence (int* out) const
    {
        int sorted[128];
        for (int i = 0; i < numHeld; ++i)
            sorted[i] = held[i];
        std::sort (sorted, sorted + numHeld);

        const bool asPlayed = (int) param (pMode) == mAsPlayed;
        const int* src = asPlayed ? held : sorted;
        const int octaves = juce::jlimit (1, 4, (int) param (pOctaves));

        int n = 0;
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < numHeld; ++i)
            {
                const int note = src[i] + o * 12;
                if (note <= 127)
                    out[n++] = note;
            }
        return n;
    }

    int pickNext()
    {
        int seq[512];
        const int n = buildSequence (seq);
        if (n <= 0)
            return -1;

        switch ((int) param (pMode))
        {
            case mDown:
                stepIndex = (stepIndex - 1 + n) % n;
                break;

            case mUpDown:
                if (n == 1) { stepIndex = 0; break; }
                if (goingUp)
                {
                    if (++stepIndex >= n - 1) { stepIndex = n - 1; goingUp = false; }
                }
                else
                {
                    if (--stepIndex <= 0) { stepIndex = 0; goingUp = true; }
                }
                break;

            case mRandom:
                stepIndex = random.nextInt (n);
                break;

            default:   // Up / As Played
                stepIndex = (stepIndex + 1) % n;
                break;
        }

        return seq[juce::jlimit (0, n - 1, stepIndex)];
    }

    int held[128] {};
    int numHeld { 0 };
    int currentNote { -1 };
    int stepIndex { 0 };
    juce::uint64 stepCounter { 0 };
    double phase { 0.0 };
    bool goingUp { true };
    bool gateClosed { true };
    juce::Random random;
};
