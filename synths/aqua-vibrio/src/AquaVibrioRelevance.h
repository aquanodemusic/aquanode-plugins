#pragma once
//==============================================================================
//  AquaVibrioRelevance.h
//
//  Which knobs matter right now. Several controls only act in certain modes
//  (HyperSaw density does nothing to a Classic oscillator, the tape wobble
//  does nothing to the Classic delay, and so on). The panel dims a control
//  whose effect is switched off and says why in its tooltip, so turning a
//  knob and hearing nothing is never a mystery.
//
//  Rules only look at MODE switches, never at levels: a knob stays lit even
//  when its level is at zero, because turning that level up is the obvious
//  next move and the user must be able to find it.
//==============================================================================

#include <JuceHeader.h>

namespace aquavibrio
{

// get (id) returns the parameter's current raw value (the 0..127 byte or the
// choice index). Returns an empty string when the control is active, or the
// reason it is not.
inline juce::String inactiveReason (const juce::String& id, const std::function<int (const char*)>& get)
{
    const auto oscRule = [&] (int n, const char* modeId) -> juce::String
    {
        const int mode = get (modeId);
        const juce::String p ("Osc" + juce::String (n));

        if (id == p + "HypersawDensity" || id == p + "HypersawDetunespread")
            return mode == 0 ? "Needs the HyperSaw or a Wavetable model" : juce::String();

        if (id == p + "WaveSelect")
            return mode == 1 ? "HyperSaw is always a saw"
                 : mode >= 2 ? "Classic model only - Wavetable models use the Wavetable knobs" : juce::String();

        if (id == p + "Shape" || id == p + "Pulsewidth" || id == p + "ShapeVelocity")
            return mode == 1 ? "HyperSaw is always a saw" : juce::String();

        if (id == p + "WavetableWavetableselect" || id == p + "WavetableWavetableindex"
            || id == p + "WavetableSync" || id == p + "WavetableFormantshift"
            || id == p + "WavetableInterpolation")
            return mode < 2 ? "Needs a Wavetable model" : juce::String();

        return {};
    };

    if (id.startsWith ("Osc1"))
        if (auto r = oscRule (1, "Osc1Mode"); r.isNotEmpty()) return r;
    if (id.startsWith ("Osc2"))
        if (auto r = oscRule (2, "Osc2Mode"); r.isNotEmpty()) return r;

    if (id == "Osc3Volume" || id == "Osc3Detune")
        return get ("Osc3Mode") == 0 ? "Oscillator 3 is off" : juce::String();
    if (id == "Osc3Semitone")
        return get ("Osc3Mode") == 0 ? "Oscillator 3 is off"
             : get ("Osc3Mode") == 1 ? "In Slave mode oscillator 3 follows oscillator 2's pitch" : juce::String();

    // (Pan Spread and LFO Phase stay lit: HyperSaw stacks spread too.)
    if (id == "UnisonDetune")
        return get ("UnisonMode") == 0 ? "Unison is off" : juce::String();

    // Filter 2 follows filter 1 while linked
    if (id == "Cutoff2" || id == "Filter2Keyfollow" || id == "Cutoff2Lfo2Amount")
        return get ("Filter2CutoffLink") > 0 ? "Filter 2 is linked to filter 1 - use the Link Offset" : juce::String();
    if (id == "OffsetForFilterlink")
        return get ("Filter2CutoffLink") == 0 ? "Only while filter 2 is linked" : juce::String();

    // LFOs: a clock setting replaces the free rate
    for (int n = 1; n <= 3; ++n)
    {
        const juce::String l ("Lfo" + juce::String (n));
        if (id == l + "Rate")
            return get ((l + "Clock").toRawUTF8()) > 0 ? "The LFO is synced to the clock" : juce::String();
    }
    if (id == "Lfo1AssignAmount") return get ("Lfo1AssignDest") == 0 ? "Pick a destination first" : juce::String();
    if (id == "Lfo2AssignAmount") return get ("Lfo2AssignDest") == 0 ? "Pick a destination first" : juce::String();

    // Arpeggiator
    if (id.startsWith ("Arp") && id != "ArpMode" && get ("ArpMode") == 0)
        return "The arpeggiator is off";
    if (id == "ArpeggiatorUserpatternlength")
        return get ("ArpPatternSelct") != 0 ? "Only for the User pattern (Pattern 0)" : juce::String();

    // Effects
    if (id.startsWith ("Chorus") && id != "ChorusType" && get ("ChorusType") == 0)
        return "Chorus type is Off";

    if ((id.startsWith ("Delay") || id.startsWith ("Dly")) && id != "DelayMode" && get ("DelayMode") == 0)
        return "Delay mode is Off";
    if (id == "DelayTapeDelayModulation")
        return get ("DelayType") == 0 ? "Tape delay types only" : juce::String();
    if (id == "DelayTime")
        return get ("DelayClock") > 0 ? "The delay is synced to the clock" : juce::String();

    if (id.startsWith ("Reverb") && id != "ReverbMode" && get ("ReverbMode") == 0)
        return "Reverb mode is Off";
    if (id == "ReverbFeedback")
        return get ("ReverbMode") < 2 ? "Feedback reverb modes only" : juce::String();

    if ((id == "DistortionIntensity" || id.startsWith ("PatchDistortion")) && get ("DistortionCurve") == 0)
        return "Distortion type is Off";

    if (id.startsWith ("FilterBank") && id != "FilterBankType")
    {
        const int t = get ("FilterBankType");
        if (t == 0)                                        return "Filter bank type is Off";
        if (id == "FilterBankPoles" && t < 9)              return "VariSlope types only";
        if (id == "FilterBankSlope" && (t < 5 || t > 8))   return "XFade types only (Low Pass to High Pass)";
        if (id == "FilterBankResonance" && t <= 2)         return "Not used by the ring modulator or frequency shifter";
        if (id == "FilterBankStereoPhase" && (t == 2 || t == 4)) return "Not used by this type";
    }

    if (id.startsWith ("Vocoder") || id == "CarrierFrequencySpread")
    {
        if (id != "VocoderMode" && get ("VocoderMode") == 0)     return "Vocoder mode is Off";
        if (id == "VocoderModulatorCenterFrequency" && get ("VocoderLink") > 0)
            return "Linked: the modulator follows the carrier (use the Offset)";
        if (id == "VocoderModulatorFrequencyOffset" && get ("VocoderLink") == 0)
            return "Only while the modulator is linked to the carrier";
    }

    if ((id == "InputFollowerAttack" || id == "InputFollowerRelease" || id == "InputFollowerLevel")
        && get ("InputFollowerMode") == 0)
        return "Input follower is off";

    return {};
}

}
