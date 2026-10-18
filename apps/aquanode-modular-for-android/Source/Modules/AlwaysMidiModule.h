#pragma once

#include "ModuleCore.h"

// Always Midi - holds one note down forever, so a generator sounds without
// anyone touching a keyboard. Patch its Midi Out into a generator's Add Midi
// In and that generator drones on the chosen note from the moment the plugin
// loads.
//
// The note is re-triggered every Renew seconds rather than simply held, which
// is the whole point: envelopes need a fresh note-on to run again, and the
// one-shot voices (Pluck, Kick, Modal Drum, Snare) fall silent without one.
// Renew is therefore the drone's pulse. Fade is the gap between the release
// and the next attack - small enough to sound continuous, long enough that the
// voice's own gate ramp has time to close, so retriggers never click.
//
// Like the Arp (and unlike Midi Add) this REPLACES what the player is holding
// for anything listening to it: patch it in and that generator answers to this
// module alone.
class AlwaysMidiModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pNote = 0, pRenew, pFade };

    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }

    void reset() override
    {
        currentNote = -1;
        timer = 0.0;
        releasing = false;
    }

    void midiDriverAdvance (double sr, int& onNote, int& offNote) override
    {
        onNote = -1;
        offNote = -1;

        const int note = juce::jlimit (0, 127, (int) param (pNote));

        // first tick after a reset: start the drone straight away
        if (currentNote < 0)
        {
            onNote = note;
            currentNote = note;
            timer = 0.0;
            releasing = false;
            return;
        }

        timer += 1.0 / sr;

        const double renew = juce::jmax (0.02, (double) param (pRenew));
        const double fade = juce::jlimit (0.0, renew * 0.5, (double) param (pFade) * 0.001);

        if (! releasing && timer >= renew - fade)
        {
            // let the voice release, so the retrigger has something to fade from
            offNote = currentNote;
            releasing = true;
        }
        else if (releasing && timer >= renew)
        {
            // re-attack, picking up any change to the Note knob
            onNote = note;
            currentNote = note;
            timer = 0.0;
            releasing = false;
        }
    }

private:
    int currentNote { -1 };
    double timer { 0.0 };
    bool releasing { false };
};
