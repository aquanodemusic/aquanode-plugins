#pragma once
//==============================================================================
//  AquaVibrioLabels.h
//
//  What each control is called on the panel. The parameter table's names
//  come from the hardware's own list and repeat what the card already says
//  ("Lfo1 Rate" inside the LFO 1 card, "Asgn 3 Amt 2" inside Matrix Slot 3),
//  or do not say it at all ("Lfo1 Amt" twice, for two different
//  oscillators). These are short, consistent within each card, and never
//  ambiguous within one; the full name stays in the tooltip.
//==============================================================================

#include <JuceHeader.h>
#include <unordered_map>

namespace aquavibrio
{

inline juce::String panelLabel (const juce::String& id)
{
    static const std::unordered_map<std::string, const char*> labels = []
    {
        std::unordered_map<std::string, const char*> m;
        const auto both = [&m] (const char* suffix, const char* label)
        {
            m[std::string ("Osc1") + suffix] = label;
            m[std::string ("Osc2") + suffix] = label;
        };

        // oscillators 1 and 2
        both ("Mode", "Model");
        both ("WaveSelect", "Wave");
        both ("WavetableWavetableselect", "Wavetable");
        both ("Shape", "Shape");
        both ("Pulsewidth", "Pulse Width");
        both ("Semitone", "Semitone");
        both ("Keyfollow", "Key Follow");
        both ("ShapeVelocity", "Shape Vel");
        both ("HypersawDensity", "Density");
        both ("HypersawDetunespread", "Spread");
        both ("WavetableWavetableindex", "Index");
        both ("WavetableSync", "Formant Spr.");
        both ("WavetableFormantshift", "Formant");
        both ("WavetableInterpolation", "Interpolate");

        m["Osc2Detune"] = "Detune";
        m["Osc2Sync"] = "Sync";
        m["Osc2FmAmount"] = "FM Amount";
        m["Osc2HypersawCrossoscsyncfreq"] = "Sync Freq";
        m["Osc2FiltEnvAmt"] = "Env > Pitch";
        m["FmFiltEnvAmt"] = "Env > FM";
        m["Osc2HsawFiltEnvSyncFreq"] = "Env > Sync";
        m["OscFmMode"] = "FM Mode";
        m["FmAmountVelocity"] = "FM Vel";

        // oscillator 3 and the mixer
        m["Osc3Mode"] = "Mode";             m["Osc3Volume"] = "Volume";
        m["Osc3Semitone"] = "Semitone";     m["Osc3Detune"] = "Detune";
        m["OscBalance"] = "Balance";        m["OscMainvolume"] = "Osc Volume";
        m["SuboscillatorVolume"] = "Sub";   m["SuboscillatorShape"] = "Sub Shape";
        m["RingmodulatorVolume"] = "Ring Mod";
        m["NoiseVolume"] = "Noise";         m["NoiseColor"] = "Noise Color";
        m["OscInitPhase"] = "Init Phase";   m["PulsewidthVelocity"] = "PW Vel";

        // unison
        m["UnisonMode"] = "Voices";         m["UnisonDetune"] = "Detune";
        m["UnisonPanSpread"] = "Pan Spread"; m["UnisonLfoPhase"] = "LFO Phase";

        // filters
        m["Cutoff"] = "Cutoff";             m["Cutoff2"] = "Cutoff";
        m["Filter1Resonance"] = "Resonance"; m["Filter2Resonance"] = "Resonance";
        m["Filter1EnvAmt"] = "Env Amount";  m["Filter2EnvAmt"] = "Env Amount";
        m["Filter1Keyfollow"] = "Key Follow"; m["Filter2Keyfollow"] = "Key Follow";
        m["Filter1Mode"] = "Mode";          m["Filter2Mode"] = "Mode";
        m["Filter1EnvPolarity"] = "Env Polarity"; m["Filter2EnvPolarity"] = "Env Polarity";
        m["Flt1EnvamtVelocity"] = "Env Vel"; m["Flt2EnvamtVelocity"] = "Env Vel";
        m["Resonance1Velocity"] = "Reso Vel"; m["Resonance2Velocity"] = "Reso Vel";
        m["Filter2CutoffLink"] = "Link";    m["OffsetForFilterlink"] = "Link Offset";
        m["FilterBalance"] = "Balance";     m["SaturationCurve"] = "Saturation";
        m["FilterRouting"] = "Routing";     m["FilterKeytrackBase"] = "Key Base";

        // envelopes
        for (auto* e : { "FilterEnv", "AmpEnv", "Envelope3", "Envelope4" })
        {
            const std::string p (e);
            m[p + "Attack"] = "Attack";     m[p + "Decay"] = "Decay";
            m[p + "Sustain"] = "Sustain";   m[p + "SustainTime"] = "Slope";
            m[p + "Release"] = "Release";
        }

        // LFOs
        for (auto* l : { "Lfo1", "Lfo2", "Lfo3" })
        {
            const std::string p (l);
            m[p + "Rate"] = "Rate";         m[p + "Shape"] = "Wave";
            m[p + "EnvMode"] = "Env Mode";  m[p + "Mode"] = "Mode";
            m[p + "Symmetry"] = "Contour";  m[p + "Keyfollow"] = "Key Follow";
            m[p + "Keytrigger"] = "Key Trigger"; m[p + "Clock"] = "Clock";
            m[p + "AssignDest"] = "Assign To"; m[p + "AssignAmount"] = "Assign Amt";
        }
        m["Osc1Lfo1Amount"] = "Osc 1 Pitch"; m["Osc2Lfo1Amount"] = "Osc 2 Pitch";
        m["PwLfo1Amount"] = "Pulse Width";  m["ResoLfo1Amount"] = "Resonance";
        m["FiltgainLfo1Amount"] = "Filter Gain";
        m["ShapeLfo2Amount"] = "Osc Shape"; m["FmLfo2Amount"] = "FM Amount";
        m["Cutoff1Lfo2Amount"] = "Cutoff 1"; m["Cutoff2Lfo2Amount"] = "Cutoff 2";
        m["PanLfo2Amount"] = "Panorama";
        m["Lfo3Destination"] = "Target";    m["OscLfo3Amount"] = "Amount";
        m["Lfo3FadeInTime"] = "Fade In";

        // amp and common
        m["PatchVolume"] = "Volume";        m["SecondOutputBalance"] = "Out 2";
        m["AmpVelocity"] = "Velocity";      m["PanoramaVelocity"] = "Pan Vel";
        m["PunchIntensity"] = "Punch";      m["Panorama"] = "Pan";
        m["Transpose"] = "Transpose";       m["KeyMode"] = "Key Mode";
        m["BenderRangeUp"] = "Bend Up";     m["BenderRangeDown"] = "Bend Down";
        m["BenderScale"] = "Bend Curve";    m["PortamentoTime"] = "Glide";
        m["ClockTempo"] = "Tempo";

        // matrix: every slot reads the same
        for (int s = 1; s <= 6; ++s)
        {
            const std::string p ("Assign" + std::to_string (s));
            m[p + "Source"] = "Source";
            m[p + "Destination"] = "Dest 1";   m[p + "Destination1"] = "Dest 1";
            m[p + "Amount"] = "Amount 1";      m[p + "Amount1"] = "Amount 1";
            m[p + "Destination2"] = "Dest 2";  m[p + "Amount2"] = "Amount 2";
            m[p + "Destination3"] = "Dest 3";  m[p + "Amount3"] = "Amount 3";
        }

        // arpeggiator
        m["ArpMode"] = "Mode";              m["ArpPatternSelct"] = "Pattern";
        m["ArpOctaveRange"] = "Octaves";    m["ArpHoldEnable"] = "Hold";
        m["ArpNoteLength"] = "Length";      m["ArpSwing"] = "Swing";
        m["ArpClock"] = "Clock";            m["ArpeggiatorUserpatternlength"] = "Steps";

        // effects
        m["InputMode"] = "Mode";            m["InputSelect"] = "Channels";
        m["InputRingmodulator"] = "Ring Mod"; m["InputFollowerMode"] = "Follower";
        m["InputFollowerAttack"] = "Attack"; m["InputFollowerLevel"] = "Level";
        m["InputFollowerRelease"] = "Release";

        m["DistortionCurve"] = "Type";      m["DistortionIntensity"] = "Drive";
        m["PatchDistortionTrebleBooster"] = "Treble Boost";
        m["PatchDistortionHighCut"] = "High Cut"; m["PatchDistortionMix"] = "Mix";
        m["PatchDistortionTone127"] = "Tone";

        m["FilterBankType"] = "Type";       m["FilterBankMix"] = "Mix";
        m["FilterBankFrequency"] = "Frequency"; m["FilterBankResonance"] = "Resonance";
        m["FilterBankStereoPhase"] = "Stereo"; m["FilterBankPoles"] = "Poles";
        m["FilterBankSlope"] = "Slope";

        m["VocoderMode"] = "Mode";          m["VocoderBalance"] = "Balance";
        m["VocoderBands"] = "Bands";        m["VocoderLink"] = "Link";
        m["VocoderAttack"] = "Attack";      m["VocoderRelease"] = "Release";
        m["VocoderSpectralBalance"] = "Brightness";
        m["VocoderCarrierCenterFrequency"] = "Carrier Freq";
        m["VocoderModulatorCenterFrequency"] = "Mod Freq";
        m["VocoderModulatorFrequencyOffset"] = "Mod Offset";
        m["VocoderCarrierQFactor"] = "Carrier Q";  m["VocoderModulatorQFactor"] = "Mod Q";
        m["CarrierFrequencySpread"] = "Carrier Spread";
        m["VocoderModulatorFrequencySpread"] = "Mod Spread";

        m["CharacterType"] = "Type";        m["BassIntensity"] = "Intensity";
        m["BassTune"] = "Tune";

        m["LoweqGain"] = "Low";             m["LoweqFrequency"] = "Low Freq";
        m["MideqGain"] = "Mid";             m["MideqFrequency"] = "Mid Freq";
        m["MideqQFactor"] = "Mid Q";
        m["HigheqGain"] = "High";           m["HigheqFrequency"] = "High Freq";

        m["PhaserMode"] = "Stages";         m["PhaserMix"] = "Mix";
        m["PhaserRate"] = "Rate";           m["PhaserDepth"] = "Depth";
        m["PhaserFrequency"] = "Frequency"; m["PhaserFeedback"] = "Feedback";
        m["PhaserSpread"] = "Spread";

        m["ChorusType"] = "Type";           m["ChorusMix"] = "Mix";
        m["ChorusRate"] = "Rate";           m["ChorusDepth"] = "Depth";
        m["ChorusDelay"] = "Delay";         m["ChorusLfoShape"] = "LFO Wave";

        m["DelayMode"] = "Mode";            m["DelayType"] = "Type";
        m["DelaySend"] = "Send";            m["DelayTime"] = "Time";
        m["DelayFeedback"] = "Feedback";    m["DelayClock"] = "Clock";
        m["DlyRateRevDecay"] = "Mod Rate";  m["DlyDepth"] = "Mod Depth";
        m["DelayTapeDelayModulation"] = "Wow";
        m["DelayColor"] = "Color";

        m["ReverbMode"] = "Mode";           m["ReverbType"] = "Type";
        m["ReverbSend"] = "Send";           m["ReverbTime"] = "Time";
        m["ReverbDamping"] = "Damping";     m["ReverbColor"] = "Color";
        m["ReverbPredelay"] = "Predelay";   m["ReverbFeedback"] = "Feedback";

        m["Atomizer"] = "Mix";              m["GranulatorGrains"] = "Grains";
        m["GranulatorSize"] = "Size";       m["GranulatorPosition"] = "Position";
        m["GranulatorSpray"] = "Spray";     m["GranulatorPitch"] = "Pitch";
        m["GranulatorPitchDisp"] = "Pitch Spread";
        m["GranulatorStereo"] = "Stereo";   m["GranulatorFreeze"] = "Freeze";
        return m;
    }();

    const auto it = labels.find (id.toStdString());
    return it != labels.end() ? juce::String (it->second) : juce::String();
}

} // namespace aquavibrio
