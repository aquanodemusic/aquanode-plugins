#pragma once
//==============================================================================
//  AquaVibrioDisplay.h
//
//  What a knob SAYS. Each value is printed in the units the engine actually
//  uses (Hz, ms, semitones, cents, bpm, dB), computed with the same curves
//  from AquaVibrioScaling.h - a cutoff reads "740 Hz" rather than "64".
//  Typing a value back in ("2 s", "440 Hz", "+7") finds the nearest setting.
//==============================================================================

#include "AquaVibrioScaling.h"

namespace aquavibrio
{
namespace display
{
    inline juce::String hz (float f)
    {
        if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz";
        if (f >= 100.0f)  return juce::String (juce::roundToInt (f)) + " Hz";
        if (f >= 10.0f)   return juce::String (f, 1) + " Hz";
        return juce::String (f, 2) + " Hz";
    }

    inline juce::String ms (float t)
    {
        if (t >= 1000.0f) return juce::String (t / 1000.0f, t >= 10000.0f ? 1 : 2) + " s";
        if (t >= 100.0f)  return juce::String (juce::roundToInt (t)) + " ms";
        if (t >= 10.0f)   return juce::String (t, 1) + " ms";
        return juce::String (t, 2) + " ms";
    }

    inline juce::String signedValue (float x, const char* unitText, int decimals = 0)
    {
        const auto s = decimals == 0 ? juce::String (juce::roundToInt (x)) : juce::String (x, decimals);
        return (x > 0.0f ? "+" : "") + s + unitText;
    }

    inline juce::String percent (float x01) { return juce::String (juce::roundToInt (x01 * 100.0f)) + " %"; }

    // Musical value for a parameter, or an empty string for "use the default
    // presentation of its scale".
    inline juce::String musicalValue (const juce::String& id, int v)
    {
        using namespace scaling;
        const float f = (float) v;

        if (id == "Cutoff" || id == "Cutoff2")                      return hz (cutoffHz (f / 127.0f));

        if (id.endsWith ("SustainTime"))
        {
            if (v == 64) return "Hold";
            return (v < 64 ? "Fall " : "Rise ") + percent (std::abs (bipolar (f)));
        }

        if (id.endsWith ("Attack") && (id.startsWith ("FilterEnv") || id.startsWith ("AmpEnv") || id.startsWith ("Envelope")))
            return ms (envMs (f));
        if (id.endsWith ("Decay") && (id.startsWith ("FilterEnv") || id.startsWith ("AmpEnv") || id.startsWith ("Envelope")))
            return ms (envMs (f));
        if (id.endsWith ("Release") && (id.startsWith ("FilterEnv") || id.startsWith ("AmpEnv") || id.startsWith ("Envelope")))
            return ms (envMs (f));

        if (id == "Lfo1Rate" || id == "Lfo2Rate" || id == "Lfo3Rate") return hz (lfoRateHz (f));
        if (id == "Lfo3FadeInTime")                                  return v == 0 ? juce::String ("Off") : ms (lfo3FadeSec (f) * 1000.0f);
        if (id == "Lfo1Keytrigger" || id == "Lfo2Keytrigger")        return v == 0 ? juce::String ("Free") : juce::String (juce::roundToInt ((f - 1.0f) / 126.0f * 360.0f)) + juce::String::fromUTF8 ("\xc2\xb0");

        if (id == "Osc1Semitone" || id == "Osc2Semitone" || id == "Osc3Semitone" || id == "Transpose")
            return signedValue (semitones (f), " st");
        if (id == "Osc2Detune" || id == "Osc3Detune")                return signedValue (fineDetuneCents (f), " ct", 1);
        if (id == "UnisonDetune")                                    return juce::String (unisonCents (f), 1) + " ct";
        if (id == "Osc1HypersawDetunespread" || id == "Osc2HypersawDetunespread")
            return juce::String (localDetuneCents (f), 1) + " ct";
        if (id == "Osc1Keyfollow" || id == "Osc2Keyfollow")          return juce::String (juce::roundToInt (f / 96.0f * 100.0f)) + " %";
        if (id == "Osc1Pulsewidth" || id == "Osc2Pulsewidth")        return juce::String (50.0f - 48.0f * unit (f), 1) + " %";
        if (id == "OscInitPhase")                                    return v == 0 ? juce::String ("Free") : juce::String (juce::roundToInt (unit (f) * 360.0f)) + juce::String::fromUTF8 ("\xc2\xb0");

        // Osc Volume: below the centre it fades the oscillators out, above it
        // drives the saturation (or adds up to half again with saturation off).
        if (id == "OscMainvolume")
        {
            if (v < 64) return v == 0 ? juce::String ("-inf dB") : signedValue (20.0f * std::log10 (f / 64.0f), " dB", 1);
            return v == 64 ? juce::String ("0 dB") : "Drive " + percent (bipolar (f));
        }

        if (id == "PortamentoTime")                                  return v == 0 ? juce::String ("Off") : ms (portamentoMs (f));
        if (id == "ClockTempo")                                      return juce::String (tempoBpm (f), 1) + " bpm";
        if (id == "BenderRangeUp")                                   return signedValue (f - 64.0f, " st");
        if (id == "BenderRangeDown")                                 return signedValue (-(64.0f - f), " st");
        if (id == "FilterKeytrackBase")                              return juce::MidiMessage::getMidiNoteName (v, true, true, 4);

        if (id == "ArpNoteLength")                                   return percent (arpGate (f));
        if (id == "ArpSwing")                                        return percent (arpSwing (f));
        if (id == "ArpeggiatorUserpatternlength")                    return juce::String (v + 1) + " steps";
        if (id == "ArpPatternSelct")                                 return v == 0 ? juce::String ("User") : (v == 1 ? juce::String ("All steps") : "Groove " + juce::String (v - 1));

        if (id == "GranulatorSize")                                  return ms (5.0f * std::pow (100.0f, unit (f)));
        if (id == "GranulatorPitch")                                 return signedValue (bipolar (f) * 24.0f, " st", 1);
        if (id == "GranulatorGrains")                                return juce::String (1 + juce::roundToInt (23.0f * unit (f))) + " grains";

        if (id == "DelayTime")                                       return ms (delayMs (f));
        if (id == "DlyRateRevDecay")                                 return hz (delayModHz (f));
        if (id == "ReverbPredelay")                                  return ms (reverbPredelayMs (f));
        if (id == "ChorusRate")                                      return hz (chorusRateHz (f));
        if (id == "ChorusDelay")                                     return ms (chorusDelayMs (f));
        if (id == "PhaserRate")                                      return hz (phaserRateHz (f));
        if (id == "PhaserFrequency")                                 return hz (phaserCentreHz (f));

        if (id == "LoweqFrequency")                                  return hz (eqLowHz (f));
        if (id == "MideqFrequency")                                  return hz (eqMidHz (f));
        if (id == "HigheqFrequency")                                 return hz (eqHighHz (f));
        if (id == "LoweqGain" || id == "MideqGain" || id == "HigheqGain") return signedValue (eqGainDb (f), " dB", 1);
        if (id == "MideqQFactor")                                    return "Q " + juce::String (eqQ (f), 2);

        if (id == "VocoderCarrierCenterFrequency" || id == "VocoderModulatorCenterFrequency")
            return hz (vocoderCentreHz (f));
        if (id == "VocoderBands")                                    return v < 11 ? juce::String ("8 bands") : (v < 22 ? juce::String ("16 bands") : juce::String ("32 bands"));

        return {};
    }

    // A number in comparable base units: kHz -> Hz, s -> ms. Words ("Off")
    // have no number and come back as NaN.
    inline double canonical (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        if (t.isEmpty() || ! (juce::CharacterFunctions::isDigit (t[0]) || t[0] == '-' || t[0] == '+' || t[0] == '.'))
            return std::numeric_limits<double>::quiet_NaN();

        double x = t.getDoubleValue();
        if (t.contains ("k"))                                     x *= 1000.0;   // "1.5k", "12 kHz"
        else if (t.endsWith ("s") && ! t.endsWith ("ms"))         x *= 1000.0;   // seconds -> ms
        return x;
    }

    // Typed text -> the setting whose display is closest to it.
    inline int parse (const juce::String& text, int minValue, int maxValue,
                      const std::function<juce::String (int)>& toText)
    {
        for (int v = minValue; v <= maxValue; ++v)          // exact word or text match first
            if (toText (v).equalsIgnoreCase (text.trim()))
                return v;

        const double wanted = canonical (text);
        if (std::isnan (wanted))
            return juce::jlimit (minValue, maxValue, text.getIntValue());

        int best = minValue;
        double bestDist = std::numeric_limits<double>::max();
        for (int v = minValue; v <= maxValue; ++v)
        {
            const double here = canonical (toText (v));
            if (! std::isnan (here) && std::abs (here - wanted) < bestDist)
            {
                bestDist = std::abs (here - wanted);
                best = v;
            }
        }
        return best;
    }
}
}
