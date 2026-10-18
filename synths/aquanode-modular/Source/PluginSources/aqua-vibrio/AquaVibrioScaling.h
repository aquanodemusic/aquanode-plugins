#pragma once
//==============================================================================
//  AquaVibrioScaling.h
//
//  Every knob's response curve, in ONE place: parameter byte (0..127) in,
//  musical units out. The engine uses these to make sound and the panel uses
//  the same functions to print values, so what a knob says is always exactly
//  what it does.
//==============================================================================

#include <JuceHeader.h>

namespace aquavibrio
{
namespace scaling
{
    inline float unit (float v)          { return juce::jlimit (0.0f, 1.0f, v / 127.0f); }
    inline float bipolar (float v)       { return juce::jlimit (-1.0f, 1.0f, (v - 64.0f) / 64.0f); }
    inline float sq (float x)            { return x * x; }

    // Cutoff: 26 Hz doubling every 12.9 bytes (about 20 Hz .. 20 kHz). Takes
    // the knob as 0..1 because the engine adds modulation in those units.
    inline float cutoffHz (float knob01)
    {
        const float byte = juce::jlimit (0.0f, 1.0f, knob01) * 127.0f;
        return juce::jlimit (20.0f, 20000.0f, 25.9f * std::pow (2.0f, byte / 12.9f));
    }

    // Envelope stage time: 0.22 ms doubling every 7.65 bytes (0.2 ms .. 20 s).
    inline float envMs (float byte)      { return 0.22f * std::pow (2.0f, juce::jlimit (0.0f, 127.0f, byte) / 7.65f); }

    inline float lfoRateHz (float byte)  { return 0.1f * std::pow (1000.0f, unit (byte)); }        // 0.1 .. 100 Hz
    inline float lfo3FadeSec (float byte){ const float f = unit (byte); return f > 0.0f ? 0.02f + 8.0f * f * f : 0.0f; }

    inline float semitones (float v)     { return v - 64.0f; }
    inline float fineDetuneCents (float byte) { return 100.0f * sq (unit (byte)); }              // 0 .. 100 cents
    inline float unisonCents (float byte)     { return 50.0f * sq (unit (byte)); }               // 0 .. 50 cents
    inline float localDetuneCents (float byte){ return 60.0f * sq (unit (byte)); }               // 0 .. 60 cents
    inline float portamentoMs (float byte)    { return std::pow (2.0f, byte / 11.0f); }          // 0 = off, else 1 ms .. 3 s
    inline float tempoBpm (float byte)        { return 63.0f + byte * (177.0f / 127.0f); }       // 63 .. 240 bpm

    inline float arpGate (float byte)         { return 0.05f + 0.95f * unit (byte); }            // fraction of a step
    inline float arpSwing (float byte)        { return 0.33f * unit (byte); }

    inline float delayMs (float byte)         { return 1.0f + 1499.0f * sq (unit (byte)); }      // 1 ms .. 1.5 s
    inline float delayModHz (float byte)      { return 8.0f * sq (unit (byte)); }
    inline float reverbPredelayMs (float byte){ return juce::jlimit (0.0f, 200.0f, byte / 92.0f * 200.0f); }

    inline float chorusRateHz (float byte)    { return 0.1f * std::pow (100.0f, unit (byte)); }   // the chorus module's own 0.1 .. 10 Hz
    inline float chorusDelayMs (float byte)   { return 1.0f + 19.0f * unit (byte); }
    inline float phaserRateHz (float byte)    { return 0.05f * std::pow (200.0f, unit (byte)); }
    inline float phaserCentreHz (float byte)  { return 100.0f * std::pow (100.0f, unit (byte)); }

    inline float eqLowHz (float byte)         { return 32.0f * std::pow (32.0f, unit (byte)); }
    inline float eqMidHz (float byte)         { return 20.0f * std::pow (1000.0f, unit (byte)); }
    inline float eqHighHz (float byte)        { return 1000.0f * std::pow (20.0f, unit (byte)); }
    inline float eqQ (float byte)             { return 0.3f * std::pow (33.0f, unit (byte)); }
    inline float eqGainDb (float byte)        { return bipolar (byte) * 16.0f; }

    inline float vocoderCentreHz (float byte) { return 100.0f * std::pow (50.0f, unit (byte)); }
}
}
