#pragma once
//==============================================================================
//  AquaVibrioParameters.h  -  GENERATED, do not edit by hand.
//
//  The full public patch-parameter set of the Access Virus, lifted from
//  gearmulator's parameterDescriptions_TI.json and flattened into a plain
//  table an APVTS can be built from directly. Hardware globals (MIDI channel,
//  LCD contrast, bank select) are deliberately absent: Aqua Vibrio is a
//  plugin, not a front panel.
//
//  Every entry keeps its hardware address (page + byte index), which is what
//  makes reading an original .syx bank a straight table lookup rather than a
//  reverse-engineering exercise.
//
//  Ranges stay in the hardware's 0..127 integer domain. Turning those into
//  musical units is each module's business, not this table's.
//==============================================================================

#include <JuceHeader.h>
#include "AquaVibrioDisplay.h"

namespace aquavibrio
{

    enum class ValueScale
    {
        Continuous,     // plain 0..max, shown as a number
        Bipolar,        // centred at 64, shown as -64..+63
        Percent,        // shown as 0..100 %
        BipolarPercent, // shown as -100..+100 %
        Enum            // one of the named choice lists below
    };

    struct ParamInfo
    {
        // Stable identifier, e.g. "Osc1Semitone". Kept to 31 characters or fewer:
        // that is the longest parameter ID the AAX format accepts, and JUCE
        // asserts on anything longer. Four names are abbreviated for this reason
        // (Osc2HsawFiltEnvSyncFreq and the three SoftKnob config values).
        const char* id;
        const char* name;        // hardware name, e.g. "Osc1 Semitone"
        const char* displayName; // long form for the UI
        const char* region;      // GUI region id, e.g. "oscA" - "" if unplaced
        int          page;        // hardware sysex page: 112 = A, 113 = B, 114 = C, 115 = D
        int          index;       // byte offset within that page
        int          minValue;
        int          maxValue;
        int          defaultValue;
        ValueScale   scale;
        const char* choiceList;  // key into choicesFor(), "" when not an enum
    };

    //==============================================================================
    // Named choice lists
    //==============================================================================
    inline const juce::StringArray& choices_180Degree180Degree()
    {
        static const juce::StringArray a{ "-180^", "-177^", "-174^", "-172^", "-169^", "-166^", "-163^", "-160^", "-158^", "-155^", "-152^", "-149^", "-146^", "-143^", "-141^", "-138^", "-135^", "-132^", "-129^", "-127^", "-124^", "-121^", "-118^", "-115^", "-113^", "-110^", "-107^", "-104^", "-101^", "-98^", "-96^", "-93^", "-90^", "-87^", "-84^", "-82^", "-79^", "-76^", "-73^", "-70^", "-68^", "-65^", "-62^", "-59^", "-56^", "-53^", "-51^", "-48^", "-45^", "-42^", "-39^", "-37^", "-34^", "-31^", "-28^", "-25^", "-23^", "-20^", "-17^", "-14^", "-11^", "-8^", "-6^", "-3^", "+0^", "+3^", "+6^", "+8^", "+11^", "+14^", "+17^", "+20^", "+23^", "+25^", "+28^", "+31^", "+34^", "+37^", "+39^", "+42^", "+45^", "+48^", "+51^", "+53^", "+56^", "+59^", "+62^", "+65^", "+68^", "+70^", "+73^", "+76^", "+79^", "+82^", "+84^", "+87^", "+90^", "+93^", "+96^", "+98^", "+101^", "+104^", "+107^", "+110^", "+113^", "+115^", "+118^", "+121^", "+124^", "+127^", "+129^", "+132^", "+135^", "+138^", "+141^", "+143^", "+146^", "+149^", "+152^", "+155^", "+158^", "+160^", "+163^", "+166^", "+169^", "+172^", "+174^", "+180^" };
        return a;
    }

    inline const juce::StringArray& choices_016Onoff()
    {
        static const juce::StringArray a{ "Off", "On", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
        return a;
    }

    inline const juce::StringArray& choices_Namedistortionmodes()
    {
        static const juce::StringArray a{ "Off", "Light", "Soft", "Medium", "Hard", "Digital", "Wave Shaper", "Rectifier", "Bit Reducer Old", "Rate Reducer Old", "Low Pass", "High Pass", "Wide", "Soft Bounce", "Hard Bounce", "Sine Fold ", "Triangle Fold", "Sawtooth Fold", "Rate Reducer", "Bit Reducer", "Mint Overdrive", "Curry Overdrive", "Saffron Overdrive", "Onion Overdrive", "Pepper Overdrive", "Chili Overdrive" };
        return a;
    }

    inline const juce::StringArray& choices_Onoff()
    {
        static const juce::StringArray a{ "Off", "On" };
        return a;
    }

    inline const juce::StringArray& choices_Arpmodes()
    {
        static const juce::StringArray a{ "Off", "Up", "Down", "Up&Down", "As Played", "Random", "Chord", "Arp>Matrix" };
        return a;
    }

    inline const juce::StringArray& choices_Category()
    {
        static const juce::StringArray a{ "--", "Lead", "Bass", "Pad", "Decay", "Pluck", "Acid", "Classic", "Arpeggiator", "Effects", "Drums", "Percussion", "Input", "Vocoder", "Favourite 1", "Favourite 2", "Favourite 3", "Organ", "Piano", "String", "FM", "Digital", "Atomizer" };
        return a;
    }

    inline const juce::StringArray& choices_Characters()
    {
        static const juce::StringArray a{ "Analog Boost", "Vintage 1", "Vintage 2", "Vintage 3", "Pad Opener", "Lead Enhancer", "Bass Enhancer", "Stereo Widener", "Speaker Cabinet" };
        return a;
    }

    inline const juce::StringArray& choices_Chorustype()
    {
        static const juce::StringArray a{ "Off", "Classic", "Vintage", "Hyper Chorus", "Air Chorus", "Vibrato", "Rotary Speaker" };
        return a;
    }

    inline const juce::StringArray& choices_Controlsmoothmode()
    {
        static const juce::StringArray a{ "Off", "On", "Auto", "Note", "Quantise 1/64", "Quantise 1/32", "Quantise 1/16", "Quantise 1/8", "Quantise 1/4", "Quantise 1/2", "Quantise 3/64", "Quantise 3/32", "Quantise 3/16", "Quantise 3/8", "Quantise 1/24", "Quantise 1/12", "Quantise 1/6", "Quantise 1/3", "Quantise 2/3", "Quantise 3/4", "Quantise 1/1" };
        return a;
    }

    inline const juce::StringArray& choices_Delaylfoshape()
    {
        static const juce::StringArray a{ "Sine", "Triangle", "Saw", "Square", "S&H", "S&G" };
        return a;
    }

    inline const juce::StringArray& choices_Delaymode()
    {
        static const juce::StringArray a{ "Off", "Simple Delay", "Ping Pong 2:1", "Ping Pong 4:3", "Ping Pong 4:1", "Ping Pong 8:7", "Pattern 1+1", "Pattern 2+1", "Pattern 3+1", "Pattern 4+1", "Pattern 5+1", "Pattern 2+3", "Pattern 2+5", "Pattern 3+2", "Pattern 3+3", "Pattern 3+4", "Pattern 3+5", "Pattern 4+3", "Pattern 4+5", "Pattern 5+2", "Pattern 5+3", "Pattern 5+4", "Pattern 5+5" };
        return a;
    }

    inline const juce::StringArray& choices_Delayratio()
    {
        static const juce::StringArray a{ "1/4", "2/4", "3/4", "4/4", "4/3", "4/2", "4/1" };
        return a;
    }

    inline const juce::StringArray& choices_Delaytype()
    {
        static const juce::StringArray a{ "Classic", "Tape Clocked", "Tape Free", "Tape Doppler" };
        return a;
    }

    inline const juce::StringArray& choices_Filter1Mode()
    {
        static const juce::StringArray a{ "Low Pass", "High Pass", "Band Pass", "Band Stop", "Analog 1 Pole", "Analog 2 Pole", "Analog 3 Pole", "Analog 4 Pole" };
        return a;
    }

    inline const juce::StringArray& choices_Filter2Mode()
    {
        static const juce::StringArray a{ "Low Pass", "High Pass", "Band Pass", "Band Stop" };
        return a;
    }

    inline const juce::StringArray& choices_Filterbanktype()
    {
        static const juce::StringArray a{ "Off", "Ring Modulator", "Frequency Shifter", "Vowel Filter", "Comb Filter", "1 Pole XFade", "2 Pole XFade", "4 Pole XFade", "6 Pole XFade", "LP VariSlope", "HP VariSlope", "BP VariSlope" };
        return a;
    }

    inline const juce::StringArray& choices_Filterkeytrackbase()
    {
        static const juce::StringArray a{ "C-1", "C#-1", "D-1", "D#-1", "E-1", "F-1", "F#-1", "G-1", "G#-1", "A-1", "A#-1", "B-1", "C0", "C#0", "D0", "D#0", "E0", "F0", "F#0", "G0", "G#0", "A0", "A#0", "B0", "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2", "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3", "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4", "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5", "C6", "C#6", "D6", "D#6", "E6", "F6", "F#6", "G6", "G#6", "A6", "A#6", "B6", "C7", "C#7", "D7", "D#7", "E7", "F7", "F#7", "G7", "G#7", "A7", "A#7", "B7", "C8", "C#8", "D8", "D#8", "E8", "F8", "F#8", "G8", "G#8", "A8", "A#8", "B8", "C9", "C#9", "D9", "D#9", "E9", "F9", "F#9", "G9" };
        return a;
    }

    inline const juce::StringArray& choices_Filterrouting()
    {
        static const juce::StringArray a{ "Serial 4", "Serial 6", "Parallel 4", "Split Mode" };
        return a;
    }

    inline const juce::StringArray& choices_Filterselect()
    {
        static const juce::StringArray a{ "Filter 1", "Filter 2", "Filter 1+2" };
        return a;
    }

    inline const juce::StringArray& choices_Inputfollowermode()
    {
        static const juce::StringArray a{ "Off", "In L", "In L+R", "In R" };
        return a;
    }

    inline const juce::StringArray& choices_Keymode()
    {
        static const juce::StringArray a{ "Poly", "Mono 1", "Mono 2", "Mono 3", "Mono 4", "Hold" };
        return a;
    }

    inline const juce::StringArray& choices_Lfodest()
    {
        static const juce::StringArray a{ "Osc 1 Pitch", "Osc 1+2 Pitch", "Osc 2 Pitch", "Osc 1 Pulse Width", "Osc 1+2 Pulse Width", "Osc 2 Pulse Width", "Sync Phase" };
        return a;
    }

    inline const juce::StringArray& choices_Lfomode()
    {
        static const juce::StringArray a{ "Poly", "Mono" };
        return a;
    }

    inline const juce::StringArray& choices_Lfoshape()
    {
        static const juce::StringArray a{ "Sine", "Triangle", "Saw", "Square", "S&H", "S&G", "Wave 3", "Wave 4", "Wave 5", "Wave 6", "Wave 7", "Wave 8", "Wave 9", "Wave 10", "Wave 11", "Wave 12", "Wave 13", "Wave 14", "Wave 15", "Wave 16", "Wave 17", "Wave 18", "Wave 19", "Wave 20", "Wave 21", "Wave 22", "Wave 23", "Wave 24", "Wave 25", "Wave 26", "Wave 27", "Wave 28", "Wave 29", "Wave 30", "Wave 31", "Wave 32", "Wave 33", "Wave 34", "Wave 35", "Wave 36", "Wave 37", "Wave 38", "Wave 39", "Wave 40", "Wave 41", "Wave 42", "Wave 43", "Wave 44", "Wave 45", "Wave 46", "Wave 47", "Wave 48", "Wave 49", "Wave 50", "Wave 51", "Wave 52", "Wave 53", "Wave 54", "Wave 55", "Wave 56", "Wave 57", "Wave 58", "Wave 59", "Wave 60", "Wave 61", "Wave 62", "Wave 63", "Wave 64" };
        return a;
    }

    inline const juce::StringArray& choices_Linexp()
    {
        static const juce::StringArray a{ "Linear", "Exponential" };
        return a;
    }

    inline const juce::StringArray& choices_Modmatrixdest()
    {
        static const juce::StringArray a{ "Off", "Patch Volume", "Osc 1 Interpolation", "Panorama", "Transpose", "Portamento", "Osc 1 Shape/Index", "Osc 1 Pulse Width", "Osc 1 Wave Select", "Osc 1 Pitch", "Slot 6 Amount 3", "Osc 2 Shape/Index", "Osc 2 Pulse Width", "Osc 2 Wave Select", "Osc 2 Pitch", "Osc 2 Detune", "Osc 2 FM Amount", "FiltEnv > Osc 2 Pitch", "FiltEnv>FM/Sync", "Osc 2 Interpolation", "Osc Balance", "Sub Osc Volume", "Osc Volume", "Noise Volume", "Filter 1 Cutoff", "Filter 2 Cutoff", "Filter 1 Resonance", "Filter 2 Resonance", "Filter 1 Env Amount", "Filter 2 Env Amount", "Slot 5 Amount 2", "Slot 5 Amount 3", "Filter Balance", "Filter Env Attack", "Filter Env Decay", "Filter Env Sustain", "Filter Env Slope", "Filter Env Release", "Amp Env Attack", "Amp Env Decay", "Amp Env Sustain", "Amp Env Slope", "Amp Env Release", "LFO 1 Rate", "LFO 1 Contour", "LFO 1>Osc 1 Pitch", "LFO 1>Osc 2 Pitch", "LFO 1>Pulse Width", "LFO 1>Resonance", "LFO 1>Filter Gain", "LFO 2 Rate", "LFO 2 Contour", "LFO 2>Shape", "LFO 2>FM Amount", "LFO 2>Cutoff 1", "LFO 2>Cutoff 2", "LFO 2>Panorama", "LFO 3 Rate", "LFO 3 Assign Amt", "Unison Detune", "Pan Spread", "Unison LFO Phase", "Chorus Mix", "Chorus Mod Rate", "Chorus Mod Depth", "Chorus Delay", "Chorus Feedback", "Delay Send", "Delay Time", "Delay Feedback", "Delay Mod Rate", "Delay Mod Depth", "Reverb Send", "Osc 1 Wavetable Index", "Osc 2 Wavetable Index", "Slot 6 Amount 2", "Slot 4 Amount 2", "Slot 4 Amount 3", "Filterbank Reso", "Filterbank Poles", "Slot 2 Amount 3", "Filterbank Slope", "Slot 1 Amount 1", "Slot 2 Amount 1", "Slot 2 Amount 2", "Slot 3 Amount 1", "Slot 3 Amount 2", "Slot 3 Amount 3", "88", "Punch Intensity", "Ring Modulator", "Noise Color", "Delay Coloration", "Slot 1 Amount 2", "Slot 1 Amount 3", "Distortion Intensity", "Filterbank Freq", "Osc 3 Volume", "Osc 3 Pitch", "Osc 3 Detune", "LFO 1 Assign Amt", "LFO 2 Assign Amt", "Phaser Mix", "Phaser Mod Rate", "Phaser Mod Depth", "Phaser Frequency", "Phaser Feedback", "107", "Reverb Time", "Reverb Damping", "Reverb Color", "Reverb PreDelay", "112", "Surround Balance", "Arp Note Length", "Arp Swing Factor", "Arp Pattern", "EQ Mid Gain", "EQ Mid Frequency", "119", "Slot 4 Amount 1", "Slot 5 Amount 1", "Slot 6 Amount 1", "Osc 1 F-Shift", "Osc 2 F-Shift", "Osc 1 F-Spread", "Osc 2 F-Spread", "Distortion Mix" };
        return a;
    }

    inline const juce::StringArray& choices_Modmatrixsource()
    {
        static const juce::StringArray a{ "Off", "Pitch Bend", "Chan Pressure", "Mod Wheel", "Breath", "Controller 3", "Foot Pedal", "Data Entry", "Balance", "Controller 9", "Expression", "Controller 12", "Controller 13", "Controller 14", "Controller 15", "Controller 16", "Hold Pedal", "Portamento Sw", "Sost Pedal", "Amp Envelope", "Filter Envelope", "LFO 1 bipolar", "LFO 2 bipolar", "LFO 3 bipolar", "Velocity On", "Velocity Off", "Key Follow", "Random", "Arp Input", "LFO 1 unipolar", "LFO 2 unipolar", "LFO 3 unipolar", "1% constant", "10% constant", "AnaKey1 Fine", "AnaKey2 Fine", "AnaKey1Coarse", "AnaKey2Coarse", "Envelope 3", "Envelope 4" };
        return a;
    }

    inline const juce::StringArray& choices_Namecombfilter()
    {
        static const juce::StringArray a{ "C0", "C#0", "D0", "D#0", "E0", "F0", "F#0", "G0", "G#0", "A0", "A#0", "B0", "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2", "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3", "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4", "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5", "C6", "C#6", "D6", "D#6", "E6", "F6", "F#6", "G6", "G#6", "A6", "A#6", "B6", "C7", "C#7", "D7", "D#7", "E7", "F7", "F#7", "G7", "G#7", "A7", "A#7", "B7", "C8" };
        return a;
    }

    inline const juce::StringArray& choices_Nameinputmode()
    {
        static const juce::StringArray a{ "Off", "Dynamic", "Static" };
        return a;
    }

    inline const juce::StringArray& choices_Nameinputselectleftright()
    {
        static const juce::StringArray a{ "Left", "L + R", "Right" };
        return a;
    }

    inline const juce::StringArray& choices_Nameoscillator12Mode()
    {
        static const juce::StringArray a{ "Classic", "HyperSaw", "Wavetable", "Wave PWM", "Grain Simple", "Grain Complex", "Formant Simple", "Formant Complex" };
        return a;
    }

    inline const juce::StringArray& choices_Negpos()
    {
        static const juce::StringArray a{ "Negative", "Positive" };
        return a;
    }

    inline const juce::StringArray& choices_Numarpclockdividers()
    {
        static const juce::StringArray a{ "", "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "3/128", "3/64", "3/32", "3/16", "1/48", "1/24", "1/12", "1/6", "1/3", "3/8", "1/2" };
        return a;
    }

    inline const juce::StringArray& choices_Numdelayclockdividers()
    {
        static const juce::StringArray a{ "Off", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "3/64", "3/32", "3/16", "3/8", "1/24", "1/12", "1/6", "1/3", "2/3", "3/4" };
        return a;
    }

    inline const juce::StringArray& choices_Numlfoclockdividers()
    {
        static const juce::StringArray a{ "Off", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "3/64", "3/32", "3/16", "3/8", "1/24", "1/12", "1/6", "1/3", "2/3", "3/4", "1/1", "2/1", "4/1", "8/1", "16/1" };
        return a;
    }

    inline const juce::StringArray& choices_Osc3Mode()
    {
        static const juce::StringArray a{ "Off", "Slave", "Saw", "Pulse", "Sine", "Triangle", "Wave 3", "Wave 4", "Wave 5", "Wave 6", "Wave 7", "Wave 8", "Wave 9", "Wave 10", "Wave 11", "Wave 12", "Wave 13", "Wave 14", "Wave 15", "Wave 16", "Wave 17", "Wave 18", "Wave 19", "Wave 20", "Wave 21", "Wave 22", "Wave 23", "Wave 24", "Wave 25", "Wave 26", "Wave 27", "Wave 28", "Wave 29", "Wave 30", "Wave 31", "Wave 32", "Wave 33", "Wave 34", "Wave 35", "Wave 36", "Wave 37", "Wave 38", "Wave 39", "Wave 40", "Wave 41", "Wave 42", "Wave 43", "Wave 44", "Wave 45", "Wave 46", "Wave 47", "Wave 48", "Wave 49", "Wave 50", "Wave 51", "Wave 52", "Wave 53", "Wave 54", "Wave 55", "Wave 56", "Wave 57", "Wave 58", "Wave 59", "Wave 60", "Wave 61", "Wave 62", "Wave 63", "Wave 64" };
        return a;
    }

    inline const juce::StringArray& choices_Oscfmmode()
    {
        static const juce::StringArray a{ "Pos Triangle", "Triangle", "Wave", "Noise", "In L", "In L+R", "In R" };
        return a;
    }

    inline const juce::StringArray& choices_Oscfmmodewavetable()
    {
        static const juce::StringArray a{ "FreqMod", "PhaseMod" };
        return a;
    }

    inline const juce::StringArray& choices_Oscwave()
    {
        static const juce::StringArray a{ "Sine", "Triangle", "Wave 3", "Wave 4", "Wave 5", "Wave 6", "Wave 7", "Wave 8", "Wave 9", "Wave 10", "Wave 11", "Wave 12", "Wave 13", "Wave 14", "Wave 15", "Wave 16", "Wave 17", "Wave 18", "Wave 19", "Wave 20", "Wave 21", "Wave 22", "Wave 23", "Wave 24", "Wave 25", "Wave 26", "Wave 27", "Wave 28", "Wave 29", "Wave 30", "Wave 31", "Wave 32", "Wave 33", "Wave 34", "Wave 35", "Wave 36", "Wave 37", "Wave 38", "Wave 39", "Wave 40", "Wave 41", "Wave 42", "Wave 43", "Wave 44", "Wave 45", "Wave 46", "Wave 47", "Wave 48", "Wave 49", "Wave 50", "Wave 51", "Wave 52", "Wave 53", "Wave 54", "Wave 55", "Wave 56", "Wave 57", "Wave 58", "Wave 59", "Wave 60", "Wave 61", "Wave 62", "Wave 63", "Wave 64" };
        return a;
    }

    inline const juce::StringArray& choices_Phasermode()
    {
        static const juce::StringArray a{ "1 Stage", "2 Stages", "3 Stages", "4 Stages", "5 Stages", "6 Stages" };
        return a;
    }

    inline const juce::StringArray& choices_Reverbmode()
    {
        static const juce::StringArray a{ "Off", "Reverb", "Feedback 1", "Feedback 2" };
        return a;
    }

    inline const juce::StringArray& choices_Reverbtype()
    {
        static const juce::StringArray a{ "Ambience", "Small Room", "Large Room", "Hall" };
        return a;
    }

    inline const juce::StringArray& choices_Satcurve()
    {
        static const juce::StringArray a{ "Off", "Light", "Soft", "Middle", "Hard", "Digital", "Wave Shaper", "Rectifier", "Bit Reducer", "Rate Reducer", "Rate+Follow", "Low Pass", "Low+Follow", "High Pass", "High+Follow" };
        return a;
    }

    inline const juce::StringArray& choices_Send()
    {
        static const juce::StringArray a{ "Off", "-46.2dB", "-40.2dB", "-36.6dB", "-34.1dB", "-32.2dB", "-30.6dB", "-29.3dB", "-28.1dB", "-27.1dB", "-26.2dB", "-25.4dB", "-24.6dB", "-23.9dB", "-23.3dB", "-22.7dB", "-22.1dB", "-21.6dB", "-21.1dB", "-20.6dB", "-20.6dB", "-19.7dB", "-19.3dB", "-18.9dB", "-18.6dB", "-18.2dB", "-17.9dB", "-17.6dB", "-17.2dB", "-16.9dB", "-16.6dB", "-16.4dB", "-16.1dB", "-15.8dB", "-15.6dB", "-15.3dB", "-15.0dB", "-14.75dB", "-14.5dB", "-14.25dB", "-14.0dB", "-13.75dB", "-13.5dB", "-13.25dB", "-13.0dB", "-12.75dB", "-12.5dB", "-12.25dB", "-12.0dB", "-11.75dB", "-11.5dB", "-11.25dB", "-11.0dB", "-10.75dB", "-10.5dB", "-10.25dB", "-10.0dB", "-9.75dB", "-9.5dB", "-9.25dB", "-9.0dB", "-8.75dB", "-8.5dB", "-8.25dB", "-8.0dB", "-7.75dB", "-7.5dB", "-7.25dB", "-7.0dB", "-6.75dB", "-6.5dB", "-6.25dB", "-6.0dB", "-5.75dB", "-5.5dB", "-5.25dB", "-5.0dB", "-4.75dB", "-4.5dB", "-4.25dB", "-4.0dB", "-3.75dB", "-3.5dB", "-3.25dB", "-3.0dB", "-2.75dB", "-2.5dB", "-2.25dB", "-2.0dB", "-1.75dB", "-1.5dB", "-1.25dB", "-1.0dB", "-0.75dB", "-0.5dB", "-0.25dB", "0/0dB", "0/-0.3dB", "0/-0.6dB", "0/-0.9dB", "0/-1.2dB", "0/-1.5dB", "0/-1.8dB", "0/-2.1dB", "0/-2.5dB", "0/-2.9dB", "0/-3.3dB", "0/-3.7dB", "0/-4.1dB", "0/-4.5dB", "0/-5.0dB", "0/-5.5dB", "0/-6.0dB", "0/-6.6dB", "0/-7.2dB", "0/-7.8dB", "0/-8.5dB", "0/-9.3dB", "0/-10.1dB", "0/-11.0dB", "0/-12.0dB", "0/-13.2dB", "0/-14.5dB", "0/-16.1dB", "0/-18.1dB", "0/-20.6dB", "0/-24.0dB", "Effect" };
        return a;
    }

    inline const juce::StringArray& choices_Softknobdest()
    {
        static const juce::StringArray a{ "Off", "Modulation Wheel", "Breath", "Control 03", "Foot Pedal", "Data Entry", "Balance", "Control 09", "Expression", "Control 12", "Control 13", "Control 14", "Control 15", "Control 16", "Patch Volume", "Channel Volume", "Panorama", "Transpose", "Portamento", "Unison Detune", "Unison Spread", "Unison LFO Phase", "Chorus Mix", "Chorus Rate", "Chorus Depth", "Chorus Delay", "Chorus Feedback", "Effect Send (Delay)", "DelayTime", "Delay Feedback", "Delay Rate", "Delay Depth", "Osc 1 Wave Select", "Osc 1 Pulse Width", "Osc 1 Pitch", "Osc 1 Key Follow", "Osc 2 Wave Select", "Osc 2 Pulse Width", "FiltEnv > Osc 2 Pitch", "FiltEnv > FM Amount", "Osc 2 Key Follow", "Noise Volume", "Filter 1 Resonance", "Filter 2 Resonance", "Filter 1 Env Amount", "Filter 2 Env Amount", "Filter 1 Key Follow", "Filter 2 Key Follow", "LFO 1 Contour", "LFO 1 > Osc 1", "LFO 1 > Osc 2", "LFO 1 > Pulse Width", "LFO 1 > Resonance", "LFO 1 > Filter Gain", "LFO 2 Contour", "LFO 2 > Shape", "LFO 2 > Fm Amount", "LFO 2 > Cutoff 1", "LFO 2 > Cutoff 2", "LFO 2 > Panorama", "LFO 3 Rate", "LFO 3 > Assign Amt", "Bend Up", "Bend Down", "Aftertouch", "Velo > FM Amount", "Velo > Filt 1 Env Amt", "Velo > Filt 2 Env Amt", "Velo > Resonance 1", "Velo > Resonance 2", "Velo > Volume", "Velo > Panorama", "Assign 1 Amount 1", "Assign 2 Amount 1", "Assign 2 Amount 2", "Assign 3 Amount 1", "Assign 3 Amount 2", "Assign 3 Amount 3", "ClockTempo", "InputThru", "Osc Initial Phase", "Punch Intensity", "Ring Modulator", "Noise Color", "Delay Coloration", "Analog Boost Int", "Analog Boost Tune", "Distortion Intensity", "Filterbank Frequency", "Osc 3 Volume", "Osc 3 Pitch", "Osc 3 Detune", "LFO 1 > Assign Amt", "LFO 2 > Assign Amt", "Phaser Mix", "Phaser Rate", "Phaser Depth", "Phaser Frequency", "Phaser Feedback", "Phaser Spread", "Reverb Decay", "Reverb Damping", "Reverb Coloration", "Reverb Feedback", "Surround Balance", "Arp Mode", "Arp Pattern", "Arp Resolution", "Arp Note Length", "Arp Swing", "Arp Octaves", "Arp Hold", "EQ Mid Gain", "EQ Mid Frequency", "EQ Mid Q-Factor", "Assign 4 Amount 1", "Assign 5 Amount 1", "Assign 6 Amount 1", "Effect Send (Revb)", "Osc 1 Local Detune", "Osc 2 Local Detune", "Osc 1 F-Shift", "Osc 2 F-Shift", "Osc 1 F-Spread", "Osc 2 F-Spread", "Osc 1 Interpolation", "Osc 2 Interpolation", "Filter Bank Mix" };
        return a;
    }

    inline const juce::StringArray& choices_Softknobname()
    {
        static const juce::StringArray a{ "~Para", "+3rds", "+4ths", "+5ths", "+7ths", "+Octave", "Access", "ArpMode", "ArpOct", "Attack", "Balance", "Chorus", "Cutoff", "Decay", "Delay", "Depth", "Destroy", "Detune", "Disolve", "Distort", "Dive", "Effects", "Elevate", "Energy", "EqHigh", "EqLow", "EqMid", "Fast", "Fear", "Filter", "FM", "Glide", "Hold", "Hype", "Infect", "Length", "Mix", "Morph", "Mutate", "Noise", "Open", "Orbit", "Pan", "Phaser", "Phatter", "Pitch", "Pulsate", "Push", "PWM", "Rate", "Release", "Reso", "Reverb", "Scream", "Shape", "Sharpen", "Slow", "Soften", "Speed", "SubOsc", "Sustain", "Sweep", "Swing", "Tempo", "Thinner", "Tone", "Tremolo", "Vibrato", "WahWah", "Warmth", "Warp", "vf", "Bite", "Flanger", "RingMod", "Punch", "Fuzz", "Modulate", "Party!", "Interpolation", "F-Shift", "F-Spread", "Bush", "Muscle", "Sack", "Vowel", "Comb", "Speaker" };
        return a;
    }

    inline const juce::StringArray& choices_Suboscshape()
    {
        static const juce::StringArray a{ "Square", "Triangle" };
        return a;
    }

    inline const juce::StringArray& choices_Tapedelayclock()
    {
        static const juce::StringArray a{ "1/32", "1/16", "2/16", "3/16", "4/16", "5/16" };
        return a;
    }

    inline const juce::StringArray& choices_Unisonmode()
    {
        static const juce::StringArray a{ "Off", "Twin", "3", "4", "5", "6", "7", "8" };
        return a;
    }

    inline const juce::StringArray& choices_Vocodermode()
    {
        static const juce::StringArray a{ "Off", "Oscillator", "Osc Hold", "Noise", "In L", "In L+R", "In R" };
        return a;
    }

    inline const juce::StringArray& choices_Wavetablesnames()
    {
        static const juce::StringArray a{ "Sine", "HarmncSweep", "Glass Sweep", "Draw Bars", "Clusters", "Insine Out", "Landing", "Liquid Metal", "Opposition", "Overtunes 1", "Overtunes 2", "Scale Trix", "Sine Rider", "Sqr Series", "Upsine Down", "Thumbs Up", "Waterphone", "E-Chime", "Tinkabell", "Bellfizz", "Bellentine", "Robot Wars", "Alternator", "Finger Bass", "Fizzybar", "Flutes", "HP Love", "Majestix", "Hotch Potch", "Resynater", "Smooth Rough", "Sawsalito", "Bells 1", "Bells 2", "SportReport", "Metal Guru", "Bat Cave", "Acetate", "Buzzbizz", "Buzzpartout", "Vanish", "Overbones", "Pulsechecker", "Stratosfear", "Sooty Sweep", "Throaty", "Didgitalis", "Evil", "Chords", "FM Grit", "Bellsarnie", "Octavius", "Eat Pulse", "Sinzin", "Sine System", "Clip Sweep", "Roughage", "Waving", "Pling Saw", "E-Peas", "Bump Sweep", "Filter Sqr", "Fourmant", "Formantera", "Sundial 1", "Sundial 2", "Sundial 3", "Clipdial 1", "Clipdial 2", "Voxonix", "Solenoid", "KlingKlang", "Violator", "Potassium", "Pile Up", "Tincanali", "Sniper", "Squeezy", "Decomposer", "Morfants", "Pingvox", "Adenoids", "Nasal", "Partialism", "TableDance", "Cascade", "Prismism", "Friction", "Robotix", "Whizzfizz", "Spangly", "Fluxbin", "Fiboglide", "Fibonice", "Fibonasty", "Penetrator", "Blinder", "Element 5", "Bad Signs", "Domina7rix" };
        return a;
    }

    inline const juce::StringArray& choicesFor(const juce::String& listName)
    {
        static const juce::StringArray empty;
        if (listName == "-180Degree+180Degree") return choices_180Degree180Degree();
        if (listName == "0-16OnOff") return choices_016Onoff();
        if (listName == "NameDistortionModes") return choices_Namedistortionmodes();
        if (listName == "OnOff") return choices_Onoff();
        if (listName == "arpModes") return choices_Arpmodes();
        if (listName == "category") return choices_Category();
        if (listName == "characters") return choices_Characters();
        if (listName == "chorusType") return choices_Chorustype();
        if (listName == "controlSmoothMode") return choices_Controlsmoothmode();
        if (listName == "delayLfoShape") return choices_Delaylfoshape();
        if (listName == "delayMode") return choices_Delaymode();
        if (listName == "delayRatio") return choices_Delayratio();
        if (listName == "delayType") return choices_Delaytype();
        if (listName == "filter1Mode") return choices_Filter1Mode();
        if (listName == "filter2Mode") return choices_Filter2Mode();
        if (listName == "filterBankType") return choices_Filterbanktype();
        if (listName == "filterKeytrackBase") return choices_Filterkeytrackbase();
        if (listName == "filterRouting") return choices_Filterrouting();
        if (listName == "filterSelect") return choices_Filterselect();
        if (listName == "inputFollowerMode") return choices_Inputfollowermode();
        if (listName == "keyMode") return choices_Keymode();
        if (listName == "lfoDest") return choices_Lfodest();
        if (listName == "lfoMode") return choices_Lfomode();
        if (listName == "lfoShape") return choices_Lfoshape();
        if (listName == "linExp") return choices_Linexp();
        if (listName == "modmatrixDest") return choices_Modmatrixdest();
        if (listName == "modmatrixSource") return choices_Modmatrixsource();
        if (listName == "nameCombFilter") return choices_Namecombfilter();
        if (listName == "nameInputMode") return choices_Nameinputmode();
        if (listName == "nameInputSelectLeftRight") return choices_Nameinputselectleftright();
        if (listName == "nameOscillator12Mode") return choices_Nameoscillator12Mode();
        if (listName == "negPos") return choices_Negpos();
        if (listName == "numArpClockdividers") return choices_Numarpclockdividers();
        if (listName == "numDelayClockdividers") return choices_Numdelayclockdividers();
        if (listName == "numLFOClockdividers") return choices_Numlfoclockdividers();
        if (listName == "osc3Mode") return choices_Osc3Mode();
        if (listName == "oscFMMode") return choices_Oscfmmode();
        if (listName == "oscFMModeWaveTable") return choices_Oscfmmodewavetable();
        if (listName == "oscWave") return choices_Oscwave();
        if (listName == "phaserMode") return choices_Phasermode();
        if (listName == "reverbMode") return choices_Reverbmode();
        if (listName == "reverbType") return choices_Reverbtype();
        if (listName == "satCurve") return choices_Satcurve();
        if (listName == "send") return choices_Send();
        if (listName == "softknobDest") return choices_Softknobdest();
        if (listName == "softknobName") return choices_Softknobname();
        if (listName == "suboscShape") return choices_Suboscshape();
        if (listName == "tapeDelayClock") return choices_Tapedelayclock();
        if (listName == "unisonMode") return choices_Unisonmode();
        if (listName == "vocoderMode") return choices_Vocodermode();
        if (listName == "wavetablesNames") return choices_Wavetablesnames();
        return empty;
    }

    //==============================================================================
    // The parameter table (434 parameters)
    //==============================================================================
    inline const std::vector<ParamInfo>& allParameters()
    {
        static const std::vector<ParamInfo> table
        {
            // --- oscA ---
            { "Osc1HypersawDensity", "Osc1 HyperSaw/Density", "Oscillator 1 Density (HyperSaw / Wavetable stack)", "oscA", 112, 17, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc1Shape", "Osc1 Shape", "Oscillator 1 Waveform Shape", "oscA", 112, 17, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Osc1WavetableWavetableindex", "Osc1 Wavetable/WaveTableIndex", "Oscillator 1 Wavetable Index", "oscA", 112, 17, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc1HypersawDetunespread", "Osc1 HyperSaw/DetuneSpread", "Oscillator 1 Local Detune", "oscA", 112, 18, 0, 127, 64, ValueScale::Continuous, "" },
            { "Osc1Pulsewidth", "Osc1 Pulsewidth", "Oscillator 1 Pulsewidth", "oscA", 112, 18, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc1WaveSelect", "Osc1 Wave Select", "Oscillator 1 Wave Select", "oscA", 112, 19, 0, 63, 0, ValueScale::Enum, "oscWave" },
            { "Osc1WavetableWavetableselect", "Osc1 Wavetable/WaveTableSelect", "Oscillator 1 Wavetable", "oscA", 112, 19, 0, 99, 0, ValueScale::Enum, "wavetablesNames" },
            { "Osc1Semitone", "Osc1 Semitone", "Oscillator 1 Semitone", "oscA", 112, 20, 16, 112, 64, ValueScale::Bipolar, "" },
            { "Osc1Keyfollow", "Osc1 Keyfollow", "Oscillator 1 Keyfollow", "oscA", 112, 21, 0, 127, 96, ValueScale::Bipolar, "" },
            { "Osc1ShapeVelocity", "Osc1 Shape Velocity", "Velocity > Osc1 Waveform Shape", "oscA", 113, 47, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc1Mode", "Osc1 Mode", "Oscillator 1 Model", "oscA", 110, 30, 0, 7, 0, ValueScale::Enum, "nameOscillator12Mode" },
            { "Osc1WavetableSync", "Osc1 Wavetable/Sync", "Oscillator 1 Formant Spread", "oscA", 110, 37, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc1WavetableFormantshift", "Osc1 Wavetable/FormantShift", "Oscillator 1 Formant Shift", "oscA", 110, 42, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Osc1WavetableInternalDetune", "Osc1 Wavetable/Internal Detune", "Oscillator 1 Local Detune", "oscA", 110, 43, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc1WavetableInterpolation", "Osc1 Wavetable/Interpolation", "Oscillator 1 Interpolation", "oscA", 110, 44, 0, 127, 0, ValueScale::Continuous, "" },
            // --- oscB ---
            { "Osc2HypersawDensity", "Osc2 HyperSaw/Density", "Oscillator 2 Density (HyperSaw / Wavetable stack)", "oscB", 112, 22, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc2Shape", "Osc2 Shape", "Oscillator 2 Shape", "oscB", 112, 22, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Osc2WavetableWavetableindex", "Osc2 Wavetable/WaveTableIndex", "Oscillator 2 Wavetable Index", "oscB", 112, 22, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc2HypersawDetunespread", "Osc2 HyperSaw/DetuneSpread", "Oscillator 2 Local Detune", "oscB", 112, 23, 0, 127, 10, ValueScale::Continuous, "" },
            { "Osc2Pulsewidth", "Osc2 Pulsewidth", "Oscillator 2 Pulsewidth", "oscB", 112, 23, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc2WaveSelect", "Osc2 Wave Select", "Oscillator 2 Wave Select", "oscB", 112, 24, 0, 63, 0, ValueScale::Enum, "oscWave" },
            { "Osc2WavetableWavetableselect", "Osc2 Wavetable/WaveTableSelect", "Oscillator 2 Wavetable", "oscB", 112, 24, 0, 99, 0, ValueScale::Enum, "wavetablesNames" },
            { "Osc2Semitone", "Osc2 Semitone", "Oscillator 2 Semitone", "oscB", 112, 25, 16, 112, 64, ValueScale::Bipolar, "" },
            { "Osc2Detune", "Osc2 Detune", "Oscillator 2 Fine Detune", "oscB", 112, 26, 0, 127, 32, ValueScale::Continuous, "" },
            { "Osc2HypersawCrossoscsyncfreq", "Osc2 HyperSaw/CrossOscSyncFreq", "Oscillator 2 Sync Frequency", "oscB", 112, 27, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc2FmAmount", "Osc2 FM Amount", "Oscillator 2 FM Amount", "oscB", 112, 27, 0, 127, 0, ValueScale::Percent, "" },
            { "Osc2WavetableFmAmount", "Osc2 Wavetable/FM Amount", "Oscillator 2 Wavetable/FM Amount", "oscB", 112, 27, 0, 127, 0, ValueScale::Percent, "" },
            { "Osc2Sync", "Osc2 Sync", "Oscillator 2 Hard Sync to Osc 1", "oscB", 112, 28, 0, 1, 0, ValueScale::Continuous, "" },
            { "Osc2FiltEnvAmt", "Osc2 Filt Env Amt", "Filter Envelope > Pitch", "oscB", 112, 29, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2HypersawFilterenvPitch", "Osc2 HyperSaw/FilterEnv > Pitch", "Filter Envelope > Pitch", "oscB", 112, 29, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2WavetableFilterenvPitch", "Osc2 Wavetable/FilterEnv > Pitch", "Filter Envelope > Pitch", "oscB", 112, 29, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "FmFiltEnvAmt", "FM Filt Env Amt", "Filter Envelope > FM", "oscB", 112, 30, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2WavetableFilterenvFm", "Osc2 Wavetable/FilterEnv > FM", "Filter Envelope > FM", "oscB", 112, 30, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2HsawFiltEnvSyncFreq", "Osc2 HyperSaw/FilterEnv > SyncFrequency", "Filter Envelope > Sync Frequency", "oscB", 112, 30, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2Keyfollow", "Osc2 Keyfollow", "Oscillator 2 Keyfollow", "oscB", 112, 31, 0, 127, 96, ValueScale::Bipolar, "" },
            { "OscFmMode", "Osc FM Mode", "Oscillator FM Mode", "oscB", 113, 34, 0, 6, 0, ValueScale::Enum, "oscFMMode" },
            { "Osc2WavetableFmModeWavetable", "Osc2 Wavetable/FM Mode WaveTable", "Wavetable Oscillator FM Mode", "oscB", 113, 34, 0, 1, 0, ValueScale::Enum, "oscFMModeWaveTable" },
            { "Osc2ShapeVelocity", "Osc2 Shape Velocity", "Velocity > Osc2 Waveform Shape", "oscB", 113, 48, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "FmAmountVelocity", "Fm Amount Velocity", "Velocity > FM Amount", "oscB", 113, 50, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2Mode", "Osc2 Mode", "Oscillator 2 Model", "oscB", 110, 35, 0, 7, 0, ValueScale::Enum, "nameOscillator12Mode" },
            { "Osc2WavetableSync", "Osc2 Wavetable/Sync", "Oscillator 2 Formant Spread", "oscB", 110, 57, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc2WavetableFormantshift", "Osc2 Wavetable/FormantShift", "Oscillator 2 Formant Shift", "oscB", 110, 62, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Osc2WavetableInternalDetune", "Osc2 Wavetable/Internal Detune", "Oscillator 2 Local Detune", "oscB", 110, 63, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc2WavetableInterpolation", "Osc2 Wavetable/Interpolation", "Oscillator 2 Interpolation", "oscB", 110, 64, 0, 127, 0, ValueScale::Continuous, "" },
            // --- oscC ---
            { "Osc3Mode", "Osc3 Mode", "Oscillator 3 Model", "oscC", 113, 41, 0, 67, 0, ValueScale::Enum, "osc3Mode" },
            { "Osc3Volume", "Osc3 Volume", "Oscillator 3 Volume", "oscC", 113, 42, 0, 127, 64, ValueScale::Continuous, "" },
            { "Osc3Semitone", "Osc3 Semitone", "Oscillator 3 Semitone", "oscC", 113, 43, 16, 112, 64, ValueScale::Bipolar, "" },
            { "Osc3Detune", "Osc3 Detune", "Oscillator 3 Fine Detune", "oscC", 113, 44, 0, 127, 32, ValueScale::Continuous, "" },
            { "Osc3Backupmode", "Osc 3/BackupMode", "Oscillator 3/BackupMode", "oscC", 110, 124, 1, 66, 1, ValueScale::Enum, "osc3Mode" },
            // --- oscCommon ---
            { "OscBalance", "Osc Balance", "Oscillator Balance", "oscCommon", 112, 33, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "SuboscillatorVolume", "Suboscillator Volume", "Sub Oscillator Volume", "oscCommon", 112, 34, 0, 127, 0, ValueScale::Continuous, "" },
            { "SuboscillatorShape", "Suboscillator Shape", "Sub Oscillator Waveform Shape", "oscCommon", 112, 35, 0, 1, 0, ValueScale::Enum, "suboscShape" },
            { "OscMainvolume", "Osc Mainvolume", "Oscillator Section Volume", "oscCommon", 112, 36, 0, 127, 64, ValueScale::Bipolar, "" },
            { "RingmodulatorVolume", "Ringmodulator Volume", "Ring Modulator Volume", "oscCommon", 112, 50, 0, 127, 0, ValueScale::Continuous, "" },
            { "OscInitPhase", "Osc Init Phase", "Oscillator Section Initial Phase", "oscCommon", 113, 35, 0, 127, 0, ValueScale::Continuous, "" },
            { "PulsewidthVelocity", "PulseWidth Velocity", "Velocity > Pulsewidth", "oscCommon", 113, 49, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "OscillatorsSelect", "Oscillators/Select", "Oscillators/Select", "oscCommon", 113, 127, 0, 2, 0, ValueScale::Continuous, "" },
            { "OscillatorsBackupkeymode", "Oscillators/BackupKeyMode", "Oscillators/BackupKeyMode", "oscCommon", 110, 122, 1, 4, 1, ValueScale::Enum, "keyMode" },
            // --- noise ---
            { "NoiseVolume", "Noise Volume", "Noise Oscillator Volume", "oscCommon", 112, 37, 0, 127, 0, ValueScale::Continuous, "" },
            { "NoiseColor", "Noise Color", "Noise Color", "oscCommon", 112, 39, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- filterA ---
            { "Cutoff", "Cutoff", "Filter 1 Cutoff", "filterA", 112, 40, 0, 127, 127, ValueScale::Continuous, "" },
            { "Filter1Resonance", "Filter1 Resonance", "Filter 1 Resonance", "filterA", 112, 42, 0, 127, 0, ValueScale::Continuous, "" },
            { "Filter1EnvAmt", "Filter1 Env Amt", "Filter 1 Envelope Amount", "filterA", 112, 44, 0, 127, 0, ValueScale::Percent, "" },
            { "Filter1Keyfollow", "Filter1 Keyfollow", "Filter 1 Keyfollow", "filterA", 112, 46, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Filter1Mode", "Filter1 Mode", "Filter 1 Mode", "filterA", 112, 51, 0, 7, 0, ValueScale::Enum, "filter1Mode" },
            { "Filter1EnvPolarity", "Filter1 Env Polarity", "Filter 1 Envelope Polarity", "filterA", 113, 30, 0, 1, 1, ValueScale::Enum, "negPos" },
            { "Flt1EnvamtVelocity", "Flt1 EnvAmt Velocity", "Velocity > Filter 1 Envelope Amount", "filterA", 113, 54, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Resonance1Velocity", "Resonance1 Velocity", "Velocity > Filter 1 Resonance", "filterA", 113, 56, 0, 127, 64, ValueScale::BipolarPercent, "" },
            // --- filterB ---
            { "Cutoff2", "Cutoff2", "Filter 2 Cutoff", "filterB", 112, 41, 0, 127, 64, ValueScale::Continuous, "" },
            { "OffsetForFilterlink", "Offset for FilterLink", "Filter 2 Offset (For Filter Link)", "filterB", 112, 41, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Filter2Resonance", "Filter2 Resonance", "Filter 2 Resonance", "filterB", 112, 43, 0, 127, 0, ValueScale::Continuous, "" },
            { "Filter2EnvAmt", "Filter2 Env Amt", "Filter 2 Envelope Amount", "filterB", 112, 45, 0, 127, 0, ValueScale::Percent, "" },
            { "Filter2Keyfollow", "Filter2 Keyfollow", "Filter 2 Keyfollow", "filterB", 112, 47, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Filter2Mode", "Filter2 Mode", "Filter 2 Mode", "filterB", 112, 52, 0, 3, 0, ValueScale::Enum, "filter2Mode" },
            { "Filter2EnvPolarity", "Filter2 Env Polarity", "Filter 2 Envelope Polarity", "filterB", 113, 31, 0, 1, 1, ValueScale::Enum, "negPos" },
            { "Filter2CutoffLink", "Filter2 Cutoff Link", "Filter Cutoff Link", "filterB", 113, 32, 0, 1, 1, ValueScale::Continuous, "" },
            { "Flt2EnvamtVelocity", "Flt2 EnvAmt Velocity", "Velocity > Filter 2 Envelope Amount", "filterB", 113, 55, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Resonance2Velocity", "Resonance2 Velocity", "Velocity > Filter 2 Resonance", "filterB", 113, 57, 0, 127, 64, ValueScale::BipolarPercent, "" },
            // --- filterCommon ---
            { "FilterBalance", "Filter Balance", "Filter Balance", "filterCommon", 112, 48, 0, 127, 64, ValueScale::Bipolar, "" },
            { "SaturationCurve", "Saturation Curve", "Voice Saturation Type", "filterCommon", 112, 49, 0, 14, 0, ValueScale::Enum, "satCurve" },
            { "FilterRouting", "Filter Routing", "Filter Routing", "filterCommon", 112, 53, 0, 3, 0, ValueScale::Enum, "filterRouting" },
            { "FilterKeytrackBase", "Filter Keytrack Base", "Filter Keyfollow Base", "filterCommon", 113, 33, 0, 127, 60, ValueScale::Enum, "filterKeytrackBase" },
            // --- envFilter ---
            { "FilterEnvAttack", "Filter Env Attack", "Filter Envelope Attack", "envFilter", 112, 54, 0, 127, 0, ValueScale::Continuous, "" },
            { "FilterEnvDecay", "Filter Env Decay", "Filter Envelope/Decay", "envFilter", 112, 55, 0, 127, 46, ValueScale::Continuous, "" },
            { "FilterEnvSustain", "Filter Env Sustain", "Filter Envelope/Sustain", "envFilter", 112, 56, 0, 127, 0, ValueScale::Percent, "" },
            { "FilterEnvSustainTime", "Filter Env Sustain Time", "Filter Envelope/Sustain Slope", "envFilter", 112, 57, 0, 127, 64, ValueScale::Bipolar, "" },
            { "FilterEnvRelease", "Filter Env Release", "Filter Envelope/Release", "envFilter", 112, 58, 0, 127, 127, ValueScale::Continuous, "" },
            // --- envAmp ---
            { "AmpEnvAttack", "Amp Env Attack", "Amplifier Envelope/Attack", "envAmp", 112, 59, 0, 127, 0, ValueScale::Continuous, "" },
            { "AmpEnvDecay", "Amp Env Decay", "Amplifier Envelope/Decay", "envAmp", 112, 60, 0, 127, 127, ValueScale::Continuous, "" },
            { "AmpEnvSustain", "Amp Env Sustain", "Amplifier Envelope/Sustain", "envAmp", 112, 61, 0, 127, 127, ValueScale::Percent, "" },
            { "AmpEnvSustainTime", "Amp Env Sustain Time", "Amplifier Envelope/Sustain Slope", "envAmp", 112, 62, 0, 127, 64, ValueScale::Bipolar, "" },
            { "AmpEnvRelease", "Amp Env Release", "Amplifier Envelope/Release", "envAmp", 112, 63, 0, 127, 4, ValueScale::Continuous, "" },
            // --- env3 ---
            { "Envelope3Attack", "Envelope 3/Attack", "Envelope 3/Attack", "env3", 110, 80, 0, 127, 20, ValueScale::Continuous, "" },
            { "Envelope3Decay", "Envelope 3/Decay", "Envelope 3/Decay", "env3", 110, 81, 0, 127, 70, ValueScale::Continuous, "" },
            { "Envelope3Sustain", "Envelope 3/Sustain", "Envelope 3/Sustain", "env3", 110, 82, 0, 127, 64, ValueScale::Percent, "" },
            { "Envelope3SustainTime", "Envelope 3/Sustain Time", "Envelope 3/Sustain Slope", "env3", 110, 83, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Envelope3Release", "Envelope 3/Release", "Envelope 3/Release", "env3", 110, 84, 0, 127, 70, ValueScale::Continuous, "" },
            // --- env4 ---
            { "Envelope4Attack", "Envelope 4/Attack", "Envelope 4/Attack", "env4", 110, 85, 0, 127, 20, ValueScale::Continuous, "" },
            { "Envelope4Decay", "Envelope 4/Decay", "Envelope 4/Decay", "env4", 110, 86, 0, 127, 70, ValueScale::Continuous, "" },
            { "Envelope4Sustain", "Envelope 4/Sustain", "Envelope 4/Sustain", "env4", 110, 87, 0, 127, 64, ValueScale::Percent, "" },
            { "Envelope4SustainTime", "Envelope 4/Sustain Time", "Envelope 4/Sustain Slope", "env4", 110, 88, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Envelope4Release", "Envelope 4/Release", "Envelope 4/Release", "env4", 110, 89, 0, 127, 70, ValueScale::Continuous, "" },
            // --- lfoA ---
            { "Lfo1Rate", "Lfo1 Rate", "LFO 1/Rate", "lfoA", 112, 67, 0, 127, 48, ValueScale::Continuous, "" },
            { "Lfo1Shape", "Lfo1 Shape", "LFO 1/Waveform Shape", "lfoA", 112, 68, 0, 67, 1, ValueScale::Enum, "lfoShape" },
            { "Lfo1EnvMode", "Lfo1 Env Mode", "LFO 1 Envelope Mode", "lfoA", 112, 69, 0, 1, 0, ValueScale::Continuous, "" },
            { "Lfo1Mode", "Lfo1 Mode", "LFO 1 Mode", "lfoA", 112, 70, 0, 1, 0, ValueScale::Enum, "lfoMode" },
            { "Lfo1Symmetry", "Lfo1 Symmetry", "LFO 1/Waveform Contour", "lfoA", 112, 71, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Lfo1Keyfollow", "Lfo1 Keyfollow", "LFO 1 Keyfollow", "lfoA", 112, 72, 0, 127, 0, ValueScale::Percent, "" },
            { "Lfo1Keytrigger", "Lfo1 Keytrigger", "LFO 1 Trigger Phase", "lfoA", 112, 73, 0, 127, 0, ValueScale::Continuous, "" },
            { "Osc1Lfo1Amount", "Osc1 Lfo1 Amount", "LFO 1 > Osc 1", "lfoA", 112, 74, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Osc2Lfo1Amount", "Osc2 Lfo1 Amount", "LFO 1 > Osc 2", "lfoA", 112, 75, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "PwLfo1Amount", "PW Lfo1 Amount", "LFO 1 > Pulsewidth", "lfoA", 112, 76, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "ResoLfo1Amount", "Reso Lfo1 Amount", "LFO 1 > Filter Resonance 1+2", "lfoA", 112, 77, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "FiltgainLfo1Amount", "FiltGain Lfo1 Amount", "LFO 1 > Filter Envelope Gain", "lfoA", 112, 78, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Lfo1Clock", "Lfo1 Clock", "LFO 1/Clock", "lfoA", 113, 18, 0, 21, 0, ValueScale::Enum, "numLFOClockdividers" },
            { "Lfo1AssignDest", "LFO1 Assign Dest", "LFO 1 User Destination", "lfoA", 113, 79, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Lfo1AssignAmount", "LFO1 Assign Amount", "LFO 1 User Destination Amount", "lfoA", 113, 80, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Lfo1Backupshape", "LFO 1/BackupShape", "LFO 1/BackupShape", "lfoA", 110, 110, 0, 67, 0, ValueScale::Enum, "lfoShape" },
            // --- lfoB ---
            { "Lfo2Rate", "Lfo2 Rate", "LFO 2/Rate", "lfoB", 112, 79, 0, 127, 48, ValueScale::Continuous, "" },
            { "Lfo2Shape", "Lfo2 Shape", "LFO 2/Waveform Shape", "lfoB", 112, 80, 0, 67, 1, ValueScale::Enum, "lfoShape" },
            { "Lfo2EnvMode", "Lfo2 Env Mode", "LFO 2 Envelope Mode", "lfoB", 112, 81, 0, 1, 0, ValueScale::Continuous, "" },
            { "Lfo2Mode", "Lfo2 Mode", "LFO 2 Mode", "lfoB", 112, 82, 0, 1, 0, ValueScale::Enum, "lfoMode" },
            { "Lfo2Symmetry", "Lfo2 Symmetry", "LFO 2/Waveform Contour", "lfoB", 112, 83, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Lfo2Keyfollow", "Lfo2 Keyfollow", "LFO 2 Keyfollow", "lfoB", 112, 84, 0, 127, 0, ValueScale::Percent, "" },
            { "Lfo2Keytrigger", "Lfo2 Keytrigger", "LFO 2 Trigger Phase", "lfoB", 112, 85, 0, 127, 0, ValueScale::Continuous, "" },
            { "ShapeLfo2Amount", "Shape Lfo2 Amount", "LFO 2 > Osc Shape 1+2", "lfoB", 112, 86, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "FmLfo2Amount", "FM Lfo2 Amount", "LFO 2 > Osc FM Amount", "lfoB", 112, 87, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Cutoff1Lfo2Amount", "Cutoff1 Lfo2 Amount", "LFO 2 > Cutoff 1", "lfoB", 112, 88, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Cutoff2Lfo2Amount", "Cutoff2 Lfo2 Amount", "LFO 2 > Cutoff 2", "lfoB", 112, 89, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "PanLfo2Amount", "Pan Lfo2 Amount", "LFO 2 > Panorama", "lfoB", 112, 90, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Lfo2Clock", "Lfo2 Clock", "LFO 2/Clock", "lfoB", 113, 19, 0, 21, 0, ValueScale::Enum, "numLFOClockdividers" },
            { "Lfo2AssignDest", "LFO2 Assign Dest", "LFO 2 User Destination", "lfoB", 113, 81, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Lfo2AssignAmount", "LFO2 Assign Amount", "LFO 2 User Destination Amount", "lfoB", 113, 82, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "Lfo2Backupshape", "LFO 2/BackupShape", "LFO 2/BackupShape", "lfoB", 110, 111, 0, 67, 0, ValueScale::Enum, "lfoShape" },
            // --- lfoC ---
            { "Lfo3Rate", "Lfo3 Rate", "LFO 3/Rate", "lfoC", 113, 7, 0, 127, 92, ValueScale::Continuous, "" },
            { "Lfo3Shape", "Lfo3 Shape", "LFO 3/Waveform Shape", "lfoC", 113, 8, 0, 67, 1, ValueScale::Enum, "lfoShape" },
            { "Lfo3Mode", "Lfo3 Mode", "LFO 3 Mode", "lfoC", 113, 9, 0, 1, 0, ValueScale::Enum, "lfoMode" },
            { "Lfo3Keyfollow", "Lfo3 Keyfollow", "LFO 3 Keyfollow", "lfoC", 113, 10, 0, 127, 0, ValueScale::Percent, "" },
            { "Lfo3Destination", "Lfo3 Destination", "LFO 3 User Destination", "lfoC", 113, 11, 0, 6, 1, ValueScale::Enum, "lfoDest" },
            { "OscLfo3Amount", "Osc Lfo3 Amount", "LFO 3 User Destination Amount", "lfoC", 113, 12, 0, 127, 0, ValueScale::Percent, "" },
            { "Lfo3FadeInTime", "Lfo3 Fade-In Time", "LFO 3/Fade In Time", "lfoC", 113, 13, 0, 127, 0, ValueScale::Continuous, "" },
            { "Lfo3Clock", "Lfo3 Clock", "LFO 3/Clock", "lfoC", 113, 21, 0, 21, 0, ValueScale::Enum, "numLFOClockdividers" },
            { "Lfo3Backupshape", "LFO 3/BackupShape", "LFO 3/BackupShape", "lfoC", 110, 112, 0, 67, 0, ValueScale::Enum, "lfoShape" },
            // --- amp ---
            { "PatchVolume", "Patch Volume", "Patch Volume", "amp", 112, 91, 0, 127, 100, ValueScale::Continuous, "" },
            { "SecondOutputBalance", "Second Output Balance", "Surround Channel Balance", "amp", 113, 58, 0, 127, 0, ValueScale::Bipolar, "" },
            { "AmpVelocity", "Amp Velocity", "Velocity > Volume", "amp", 113, 60, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "PanoramaVelocity", "Panorama Velocity", "Velocity > Panorama", "amp", 113, 61, 0, 127, 64, ValueScale::BipolarPercent, "" },
            // --- common ---
            { "Transpose", "Transpose", "Patch Transposition", "common", 112, 93, 0, 127, 64, ValueScale::Bipolar, "" },
            { "KeyMode", "Key Mode", "Oscillator Section Keyboard Mode", "common", 112, 94, 0, 5, 0, ValueScale::Enum, "keyMode" },
            { "ControlSmoothMode", "Control Smooth Mode", "Parameter Smooth Mode", "common", 113, 25, 0, 20, 1, ValueScale::Enum, "controlSmoothMode" },
            { "BenderRangeUp", "Bender Range Up", "Bender Up Range", "common", 113, 26, 0, 127, 66, ValueScale::Bipolar, "" },
            { "BenderRangeDown", "Bender Range Down", "Bender Down Range", "common", 113, 27, 0, 127, 62, ValueScale::Bipolar, "" },
            { "BenderScale", "Bender Scale", "Bender Scale", "common", 113, 28, 0, 1, 1, ValueScale::Enum, "linExp" },
            // --- unison ---
            { "UnisonMode", "Unison Mode", "Unison Mode", "unison", 111, 120, 0, 7, 0, ValueScale::Enum, "unisonMode" },
            { "UnisonDetune", "Unison Detune", "Unison Detune", "unison", 111, 121, 0, 127, 48, ValueScale::Continuous, "" },
            { "UnisonPanSpread", "Unison Pan Spread", "Unison Panorama Spread", "unison", 111, 122, 0, 127, 127, ValueScale::Percent, "" },
            { "UnisonLfoPhase", "Unison Lfo Phase", "Unison LFO Phase Offset", "unison", 111, 123, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- chorus ---
            { "ChorusType", "Chorus/Type", "Chorus/Type", "chorus", 112, 103, 0, 6, 1, ValueScale::Enum, "chorusType" },
            { "ChorusMix2", "Chorus/Mix2", "Chorus/Mix 2", "chorus", 112, 104, 0, 127, 0, ValueScale::Continuous, "" },
            { "ChorusMix", "Chorus Mix", "Chorus/Mix", "chorus", 112, 105, 0, 127, 0, ValueScale::Continuous, "" },
            { "ChorusRate", "Chorus Rate", "Chorus/LFO Rate", "chorus", 112, 106, 0, 127, 64, ValueScale::Continuous, "" },
            { "ChorusSpeed", "Chorus/Speed", "Chorus/Speed", "chorus", 112, 106, 0, 127, 0, ValueScale::Continuous, "" },
            { "ChorusDistance", "Chorus/Distance", "Chorus/Distance", "chorus", 112, 107, 0, 127, 0, ValueScale::Continuous, "" },
            { "ChorusDepth", "Chorus Depth", "Chorus/LFO Depth", "chorus", 112, 107, 0, 127, 16, ValueScale::Percent, "" },
            { "ChorusAmount", "Chorus/Amount", "Chorus/Amount", "chorus", 112, 108, 0, 127, 64, ValueScale::Continuous, "" },
            { "ChorusDelay", "Chorus Delay", "Chorus/Delay", "chorus", 112, 108, 0, 127, 64, ValueScale::Continuous, "" },
            { "ChorusMicAngle", "Chorus/Mic Angle", "Chorus/Mic Angle", "chorus", 112, 108, 0, 127, 0, ValueScale::Continuous, "" },
            { "ChorusFeedback", "Chorus Feedback", "Chorus/Feedback", "chorus", 112, 109, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "ChorusLowhighBal", "Chorus/LowHigh Bal", "Chorus/LowHigh Balance", "chorus", 112, 109, 0, 127, 0, ValueScale::BipolarPercent, "" },
            { "ChorusLfoShape", "Chorus Lfo Shape", "Chorus/LFO Shape", "chorus", 112, 110, 0, 5, 1, ValueScale::Enum, "lfoShape" },
            { "ChorusXOver", "Chorus/X Over", "Chorus/X Over", "chorus", 112, 111, 0, 127, 64, ValueScale::Continuous, "" },
            // --- delay ---
            { "DelayMode", "Delay Mode", "Delay Mode", "delay", 112, 112, 0, 22, 1, ValueScale::Enum, "delayMode" },
            { "DelaySend", "Delay Send", "Delay Send", "delay", 112, 113, 0, 127, 0, ValueScale::Continuous, "" },
            { "DelayTapeDelayTime", "Delay/Tape Delay Time", "Delay Tape Delay Time (ms)", "delay", 112, 114, 0, 127, 64, ValueScale::Continuous, "" },
            { "DelayTime", "Delay Time", "Delay Time (ms)", "delay", 112, 114, 0, 127, 64, ValueScale::Continuous, "" },
            { "DelayFeedback", "Delay Feedback", "Delay Feedback", "delay", 112, 115, 0, 127, 0, ValueScale::Percent, "" },
            { "DelayTapeDelayFeedback", "Delay/Tape Delay Feedback", "Delay Tape Delay Feedback", "delay", 112, 115, 0, 127, 0, ValueScale::Continuous, "" },
            { "DlyRateRevDecay", "Dly Rate / Rev Decay", "Delay LFO Rate", "delay", 112, 116, 0, 127, 16, ValueScale::Continuous, "" },
            { "DlyDepth", "Dly Depth ", "Delay LFO Depth", "delay", 112, 117, 0, 127, 12, ValueScale::Percent, "" },
            { "DelayTapeDelayModulation", "Delay/Tape Delay Modulation", "Delay Tape Delay Modulation", "delay", 112, 117, 0, 127, 12, ValueScale::Percent, "" },
            { "DelayLfoShape", "Delay Lfo Shape", "Delay LFO Shape", "delay", 112, 118, 0, 5, 1, ValueScale::Enum, "delayLfoShape" },
            { "DelayColor", "Delay Color", "Delay Color", "delay", 112, 119, 0, 127, 64, ValueScale::Bipolar, "" },
            { "DelayTapeDelayCenterFrequency", "Delay/Tape Delay Center Frequency", "Delay Tape Delay Frequency", "delay", 112, 119, 0, 127, 64, ValueScale::Continuous, "" },
            { "DelayClock", "Delay Clock", "Delay Clock", "delay", 113, 20, 0, 16, 0, ValueScale::Enum, "numDelayClockdividers" },
            { "DelayType", "Delay Type", "Delay Type", "delay", 110, 10, 0, 3, 0, ValueScale::Enum, "delayType" },
            { "DelayTapeDelayRatio", "Delay Tape Delay Ratio", "Delay Tape Delay Ratio", "delay", 110, 12, 0, 6, 3, ValueScale::Enum, "delayRatio" },
            { "DelayTapeDelayLeftClock", "Delay Tape Delay Left Clock", "Delay Tape Delay Clock Left", "delay", 110, 13, 0, 5, 4, ValueScale::Enum, "tapeDelayClock" },
            { "DelayTapeDelayRightClock", "Delay Tape Delay Right Clock", "Delay Tape Delay Clock Right", "delay", 110, 14, 0, 5, 2, ValueScale::Enum, "tapeDelayClock" },
            { "DelayTapeDelayBandwidth", "Delay Tape Delay Bandwidth", "Delay Tape Delay Bandwidth", "delay", 110, 17, 0, 127, 1, ValueScale::Continuous, "" },
            // --- reverb ---
            { "ReverbMode", "Reverb Mode", "Reverb/Mode", "reverb", 110, 1, 0, 3, 1, ValueScale::Enum, "reverbMode" },
            { "ReverbSend", "Reverb Send", "Reverb/Send", "reverb", 110, 2, 0, 127, 0, ValueScale::Continuous, "" },
            { "ReverbType", "Reverb Type", "Reverb/Type", "reverb", 110, 3, 0, 3, 0, ValueScale::Enum, "reverbType" },
            { "ReverbTime", "Reverb Time", "Reverb/Time", "reverb", 110, 4, 0, 127, 64, ValueScale::Continuous, "" },
            { "ReverbDamping", "Reverb Damping", "Reverb/Damping", "reverb", 110, 5, 0, 127, 10, ValueScale::Percent, "" },
            { "ReverbColor", "Reverb Color", "Reverb/Color", "reverb", 110, 6, 0, 127, 64, ValueScale::Bipolar, "" },
            { "ReverbPredelay", "Reverb Predelay", "Reverb/Predelay", "reverb", 110, 7, 0, 92, 20, ValueScale::Continuous, "" },
            { "ReverbClock", "Reverb Clock", "Reverb/Clock", "reverb", 110, 8, 0, 16, 0, ValueScale::Enum, "numDelayClockdividers" },
            { "ReverbFeedback", "Reverb Feedback", "Reverb/Feedback", "reverb", 110, 9, 0, 127, 64, ValueScale::Continuous, "" },
            // --- arp ---
            { "ArpPatternSelct", "Arp Pattern Selct", "Arpeggiator/Pattern", "arp", 113, 2, 0, 63, 1, ValueScale::Continuous, "" },
            { "ArpOctaveRange", "Arp Octave Range", "Arpeggiator Range In Octaves", "arp", 113, 3, 0, 3, 0, ValueScale::Continuous, "" },
            { "ArpHoldEnable", "Arp Hold Enable", "Arpeggiator Hold Mode", "arp", 113, 4, 0, 1, 0, ValueScale::Enum, "OnOff" },
            { "ArpNoteLength", "Arp Note Length", "Arpeggiator Note Length", "arp", 113, 5, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "ArpSwing", "Arp Swing", "Arpeggiator Swing Factor", "arp", 113, 6, 0, 127, 0, ValueScale::Continuous, "" },
            { "ArpMode", "Arp Mode", "Arpeggiator Mode", "arp", 113, 15, 0, 7, 0, ValueScale::Enum, "arpModes" },
            { "ArpClock", "Arp Clock", "Arpeggiator Clock", "arp", 113, 17, 0, 17, 4, ValueScale::Enum, "numArpClockdividers" },
            { "ArpeggiatorBackupmode", "Arpeggiator/BackupMode", "Arpeggiator/BackupMode", "arp", 110, 123, 1, 7, 1, ValueScale::Continuous, "" },
            { "ArpeggiatorUserpatternlength", "Arpeggiator/UserPatternLength", "Arpeggiator Pattern Length", "arp", 110, 127, 0, 31, 31, ValueScale::Continuous, "" },
            { "Step1Length", "Step 1 Length", "Step 1 Length", "arp", 111, 0, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step1Velocity", "Step 1 Velocity", "Step 1 Velocity", "arp", 111, 1, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step1Bitfield", "Step 1 Bitfield", "Step 1 Bitfield", "arp", 111, 2, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step2Length", "Step 2 Length", "Step 2 Length", "arp", 111, 3, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step2Velocity", "Step 2 Velocity", "Step 2 Velocity", "arp", 111, 4, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step2Bitfield", "Step 2 Bitfield", "Step 2 Bitfield", "arp", 111, 5, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step3Length", "Step 3 Length", "Step 3 Length", "arp", 111, 6, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step3Velocity", "Step 3 Velocity", "Step 3 Velocity", "arp", 111, 7, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step3Bitfield", "Step 3 Bitfield", "Step 3 Bitfield", "arp", 111, 8, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step4Length", "Step 4 Length", "Step 4 Length", "arp", 111, 9, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step4Velocity", "Step 4 Velocity", "Step 4 Velocity", "arp", 111, 10, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step4Bitfield", "Step 4 Bitfield", "Step 4 Bitfield", "arp", 111, 11, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step5Length", "Step 5 Length", "Step 5 Length", "arp", 111, 12, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step5Velocity", "Step 5 Velocity", "Step 5 Velocity", "arp", 111, 13, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step5Bitfield", "Step 5 Bitfield", "Step 5 Bitfield", "arp", 111, 14, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step6Length", "Step 6 Length", "Step 6 Length", "arp", 111, 15, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step6Velocity", "Step 6 Velocity", "Step 6 Velocity", "arp", 111, 16, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step6Bitfield", "Step 6 Bitfield", "Step 6 Bitfield", "arp", 111, 17, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step7Length", "Step 7 Length", "Step 7 Length", "arp", 111, 18, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step7Velocity", "Step 7 Velocity", "Step 7 Velocity", "arp", 111, 19, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step7Bitfield", "Step 7 Bitfield", "Step 7 Bitfield", "arp", 111, 20, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step8Length", "Step 8 Length", "Step 8 Length", "arp", 111, 21, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step8Velocity", "Step 8 Velocity", "Step 8 Velocity", "arp", 111, 22, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step8Bitfield", "Step 8 Bitfield", "Step 8 Bitfield", "arp", 111, 23, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step9Length", "Step 9 Length", "Step 9 Length", "arp", 111, 24, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step9Velocity", "Step 9 Velocity", "Step 9 Velocity", "arp", 111, 25, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step9Bitfield", "Step 9 Bitfield", "Step 9 Bitfield", "arp", 111, 26, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step10Length", "Step 10 Length", "Step 10 Length", "arp", 111, 27, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step10Velocity", "Step 10 Velocity", "Step 10 Velocity", "arp", 111, 28, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step10Bitfield", "Step 10 Bitfield", "Step 10 Bitfield", "arp", 111, 29, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step11Length", "Step 11 Length", "Step 11 Length", "arp", 111, 30, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step11Velocity", "Step 11 Velocity", "Step 11 Velocity", "arp", 111, 31, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step11Bitfield", "Step 11 Bitfield", "Step 11 Bitfield", "arp", 111, 32, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step12Length", "Step 12 Length", "Step 12 Length", "arp", 111, 33, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step12Velocity", "Step 12 Velocity", "Step 12 Velocity", "arp", 111, 34, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step12Bitfield", "Step 12 Bitfield", "Step 12 Bitfield", "arp", 111, 35, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step13Length", "Step 13 Length", "Step 13 Length", "arp", 111, 36, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step13Velocity", "Step 13 Velocity", "Step 13 Velocity", "arp", 111, 37, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step13Bitfield", "Step 13 Bitfield", "Step 13 Bitfield", "arp", 111, 38, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step14Length", "Step 14 Length", "Step 14 Length", "arp", 111, 39, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step14Velocity", "Step 14 Velocity", "Step 14 Velocity", "arp", 111, 40, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step14Bitfield", "Step 14 Bitfield", "Step 14 Bitfield", "arp", 111, 41, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step15Length", "Step 15 Length", "Step 15 Length", "arp", 111, 42, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step15Velocity", "Step 15 Velocity", "Step 15 Velocity", "arp", 111, 43, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step15Bitfield", "Step 15 Bitfield", "Step 15 Bitfield", "arp", 111, 44, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step16Length", "Step 16 Length", "Step 16 Length", "arp", 111, 45, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step16Velocity", "Step 16 Velocity", "Step 16 Velocity", "arp", 111, 46, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step16Bitfield", "Step 16 Bitfield", "Step 16 Bitfield", "arp", 111, 47, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step17Length", "Step 17 Length", "Step 17 Length", "arp", 111, 48, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step17Velocity", "Step 17 Velocity", "Step 17 Velocity", "arp", 111, 49, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step17Bitfield", "Step 17 Bitfield", "Step 17 Bitfield", "arp", 111, 50, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step18Length", "Step 18 Length", "Step 18 Length", "arp", 111, 51, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step18Velocity", "Step 18 Velocity", "Step 18 Velocity", "arp", 111, 52, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step18Bitfield", "Step 18 Bitfield", "Step 18 Bitfield", "arp", 111, 53, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step19Length", "Step 19 Length", "Step 19 Length", "arp", 111, 54, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step19Velocity", "Step 19 Velocity", "Step 19 Velocity", "arp", 111, 55, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step19Bitfield", "Step 19 Bitfield", "Step 19 Bitfield", "arp", 111, 56, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step20Length", "Step 20 Length", "Step 20 Length", "arp", 111, 57, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step20Velocity", "Step 20 Velocity", "Step 20 Velocity", "arp", 111, 58, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step20Bitfield", "Step 20 Bitfield", "Step 20 Bitfield", "arp", 111, 59, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step21Length", "Step 21 Length", "Step 21 Length", "arp", 111, 60, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step21Velocity", "Step 21 Velocity", "Step 21 Velocity", "arp", 111, 61, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step21Bitfield", "Step 21 Bitfield", "Step 21 Bitfield", "arp", 111, 62, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step22Length", "Step 22 Length", "Step 22 Length", "arp", 111, 63, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step22Velocity", "Step 22 Velocity", "Step 22 Velocity", "arp", 111, 64, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step22Bitfield", "Step 22 Bitfield", "Step 22 Bitfield", "arp", 111, 65, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step23Length", "Step 23 Length", "Step 23 Length", "arp", 111, 66, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step23Velocity", "Step 23 Velocity", "Step 23 Velocity", "arp", 111, 67, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step23Bitfield", "Step 23 Bitfield", "Step 23 Bitfield", "arp", 111, 68, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step24Length", "Step 24 Length", "Step 24 Length", "arp", 111, 69, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step24Velocity", "Step 24 Velocity", "Step 24 Velocity", "arp", 111, 70, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step24Bitfield", "Step 24 Bitfield", "Step 24 Bitfield", "arp", 111, 71, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step25Length", "Step 25 Length", "Step 25 Length", "arp", 111, 72, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step25Velocity", "Step 25 Velocity", "Step 25 Velocity", "arp", 111, 73, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step25Bitfield", "Step 25 Bitfield", "Step 25 Bitfield", "arp", 111, 74, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step26Length", "Step 26 Length", "Step 26 Length", "arp", 111, 75, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step26Velocity", "Step 26 Velocity", "Step 26 Velocity", "arp", 111, 76, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step26Bitfield", "Step 26 Bitfield", "Step 26 Bitfield", "arp", 111, 77, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step27Length", "Step 27 Length", "Step 27 Length", "arp", 111, 78, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step27Velocity", "Step 27 Velocity", "Step 27 Velocity", "arp", 111, 79, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step27Bitfield", "Step 27 Bitfield", "Step 27 Bitfield", "arp", 111, 80, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step28Length", "Step 28 Length", "Step 28 Length", "arp", 111, 81, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step28Velocity", "Step 28 Velocity", "Step 28 Velocity", "arp", 111, 82, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step28Bitfield", "Step 28 Bitfield", "Step 28 Bitfield", "arp", 111, 83, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step29Length", "Step 29 Length", "Step 29 Length", "arp", 111, 84, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step29Velocity", "Step 29 Velocity", "Step 29 Velocity", "arp", 111, 85, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step29Bitfield", "Step 29 Bitfield", "Step 29 Bitfield", "arp", 111, 86, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step30Length", "Step 30 Length", "Step 30 Length", "arp", 111, 87, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step30Velocity", "Step 30 Velocity", "Step 30 Velocity", "arp", 111, 88, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step30Bitfield", "Step 30 Bitfield", "Step 30 Bitfield", "arp", 111, 89, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step31Length", "Step 31 Length", "Step 31 Length", "arp", 111, 90, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step31Velocity", "Step 31 Velocity", "Step 31 Velocity", "arp", 111, 91, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step31Bitfield", "Step 31 Bitfield", "Step 31 Bitfield", "arp", 111, 92, 0, 1, 1, ValueScale::Continuous, "" },
            { "Step32Length", "Step 32 Length", "Step 32 Length", "arp", 111, 93, 0, 127, 64, ValueScale::Continuous, "" },
            { "Step32Velocity", "Step 32 Velocity", "Step 32 Velocity", "arp", 111, 94, 0, 127, 100, ValueScale::Continuous, "" },
            { "Step32Bitfield", "Step 32 Bitfield", "Step 32 Bitfield", "arp", 111, 95, 0, 1, 1, ValueScale::Continuous, "" },
            // --- punch ---
            { "PunchIntensity", "Punch Intensity", "Oscillator Punch Intensity", "amp", 113, 36, 0, 127, 0, ValueScale::Percent, "" },
            // --- input ---
            { "InputRingmodulator", "Input Ringmodulator", "Ring Modulator Mix", "input", 113, 99, 0, 127, 0, ValueScale::Continuous, "" },
            { "InputMode", "Input Mode", "Input Mode", "input", 111, 124, 0, 2, 0, ValueScale::Enum, "nameInputMode" },
            { "InputSelect", "Input Select", "Input Select", "input", 111, 125, 0, 2, 1, ValueScale::Enum, "nameInputSelectLeftRight" },
            // --- assignA ---
            { "Assign1Source", "Assign1 Source", "Mod Matrix Slot 1/Source", "assignA", 113, 64, 0, 39, 0, ValueScale::Enum, "modmatrixSource" },
            { "Assign1Destination", "Assign1 Destination", "Mod Matrix Slot 1/Dest 1", "assignA", 113, 65, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign1Amount", "Assign1 Amount", "Mod Matrix Slot 1/Amount 1", "assignA", 113, 66, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign1Destination2", "Assign 1 Destination 2", "Mod Matrix Slot 1/Dest 2", "assignA", 110, 90, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign1Amount2", "Assign 1 Amount 2", "Mod Matrix Slot 1/Amount 2", "assignA", 110, 91, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign1Destination3", "Assign 1 Destination 3", "Mod Matrix Slot 1/Dest 3", "assignA", 110, 92, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign1Amount3", "Assign 1 Amount 3", "Mod Matrix Slot 1/Amount 3", "assignA", 110, 93, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- assignB ---
            { "Assign2Source", "Assign2 Source", "Mod Matrix Slot 2/Source", "assignB", 113, 67, 0, 39, 0, ValueScale::Enum, "modmatrixSource" },
            { "Assign2Destination1", "Assign2 Destination1", "Mod Matrix Slot 2/Dest 1", "assignB", 113, 68, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign2Amount1", "Assign2 Amount1", "Mod Matrix Slot 2/Amount 1", "assignB", 113, 69, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign2Destination2", "Assign2 Destination2", "Mod Matrix Slot 2/Dest 2", "assignB", 113, 70, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign2Amount2", "Assign2 Amount2", "Mod Matrix Slot 2/Amount 2", "assignB", 113, 71, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign2Destination3", "Assign 2 Destination 3", "Mod Matrix Slot 2/Dest 3", "assignB", 110, 94, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign2Amount3", "Assign 2 Amount 3", "Mod Matrix Slot 2/Amount 3", "assignB", 110, 95, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- assignC ---
            { "Assign3Source", "Assign3 Source", "Mod Matrix Slot 3/Source", "assignC", 113, 72, 0, 39, 0, ValueScale::Enum, "modmatrixSource" },
            { "Assign3Destination1", "Assign3 Destination1", "Mod Matrix Slot 3/Dest 1", "assignC", 113, 73, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign3Amount1", "Assign3 Amount1", "Mod Matrix Slot 3/Amount 1", "assignC", 113, 74, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign3Destination2", "Assign3 Destination2", "Mod Matrix Slot 3/Dest 2", "assignC", 113, 75, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign3Amount2", "Assign3 Amount2", "Mod Matrix Slot 3/Amount 2", "assignC", 113, 76, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign3Destination3", "Assign3 Destination3", "Mod Matrix Slot 3/Dest 3", "assignC", 113, 77, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign3Amount3", "Assign3 Amount3", "Mod Matrix Slot 3/Amount 3", "assignC", 113, 78, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- assignD ---
            { "Assign4Source", "Assign 4 Source", "Mod Matrix Slot 4/Source", "assignD", 113, 103, 0, 39, 0, ValueScale::Enum, "modmatrixSource" },
            { "Assign4Destination", "Assign 4 Destination", "Mod Matrix Slot 4/Dest 1", "assignD", 113, 104, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign4Amount", "Assign 4 Amount", "Assign Slot 4/Amount 1", "assignD", 113, 105, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign4Destination2", "Assign 4 Destination 2", "Mod Matrix Slot 4/Dest 2", "assignD", 110, 96, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign4Amount2", "Assign 4 Amount 2", "Mod Matrix Assign Slot 4/Amount 2", "assignD", 110, 97, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign4Destination3", "Assign 4 Destination 3", "Mod Matrix Slot 4/Dest 3", "assignD", 110, 98, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign4Amount3", "Assign 4 Amount 3", "Mod Matrix Slot 4/Amount 3", "assignD", 110, 99, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- assignE ---
            { "Assign5Source", "Assign 5 Source", "Mod Matrix Slot 5/Source", "assignE", 113, 106, 0, 39, 0, ValueScale::Enum, "modmatrixSource" },
            { "Assign5Destination", "Assign 5 Destination", "Mod Matrix Slot 5/Dest 1", "assignE", 113, 107, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign5Amount", "Assign 5 Amount", "Mod Matrix Slot 5/Amount 1", "assignE", 113, 108, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign5Destination2", "Assign 5 Destination 2", "Mod Matrix Slot 5/Dest 2", "assignE", 110, 100, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign5Amount2", "Assign 5 Amount 2", "Mod Matrix Slot 5/Amount 2", "assignE", 110, 101, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign5Destination3", "Assign 5 Destination 3", "Mod Matrix Slot 5/Dest 3", "assignE", 110, 102, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign5Amount3", "Assign 5 Amount 3", "Mod Matrix Slot 5/Amount 3", "assignE", 110, 103, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- assignF ---
            { "Assign6Source", "Assign 6 Source", "Mod Matrix Slot 6/Source", "assignF", 113, 109, 0, 39, 0, ValueScale::Enum, "modmatrixSource" },
            { "Assign6Destination", "Assign 6 Destination", "Mod Matrix Slot 6/Dest 1", "assignF", 113, 110, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign6Amount", "Assign 6 Amount", "Mod Matrix Slot 6/Amount 1", "assignF", 113, 111, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign6Destination2", "Assign 6 Destination 2", "Mod Matrix Slot 6/Dest 2", "assignF", 110, 104, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign6Amount2", "Assign 6 Amount 2", "Mod Matrix Slot 6/Amount 2", "assignF", 110, 105, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Assign6Destination3", "Assign 6 Destination 3", "Mod Matrix Slot 6/Dest 3", "assignF", 110, 106, 0, 127, 0, ValueScale::Enum, "modmatrixDest" },
            { "Assign6Amount3", "Assign 6 Amount 3", "Mod Matrix Slot 6/Amount 3", "assignF", 110, 107, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- phaser ---
            { "PhaserMode", "Phaser Mode", "Phaser/Stages", "phaser", 113, 84, 0, 5, 3, ValueScale::Enum, "phaserMode" },
            { "PhaserMix", "Phaser Mix", "Phaser/Mix", "phaser", 113, 85, 0, 127, 0, ValueScale::Continuous, "" },
            { "PhaserRate", "Phaser Rate", "Phaser/LFO Rate", "phaser", 113, 86, 0, 127, 36, ValueScale::Continuous, "" },
            { "PhaserDepth", "Phaser Depth", "Phaser/Depth", "phaser", 113, 87, 0, 127, 112, ValueScale::Continuous, "" },
            { "PhaserFrequency", "Phaser Frequency", "Phaser/Frequency", "phaser", 113, 88, 0, 127, 64, ValueScale::Continuous, "" },
            { "PhaserFeedback", "Phaser Feedback", "Phaser/Feedback", "phaser", 113, 89, 0, 127, 64, ValueScale::Bipolar, "" },
            { "PhaserSpread", "Phaser Spread", "Phaser/Spread", "phaser", 113, 90, 0, 127, 127, ValueScale::Continuous, "" },
            // --- eq ---
            { "LoweqFrequency", "LowEQ Frequency", "EQ/Low Frequency (Hz)", "eq", 113, 45, 0, 127, 5, ValueScale::Bipolar, "" },
            { "HigheqFrequency", "HighEQ Frequency", "EQ/High Frequency (kHz)", "eq", 113, 46, 0, 127, 0, ValueScale::Bipolar, "" },
            { "MideqGain", "MidEQ Gain", "EQ/Mid Gain (dB)", "eq", 113, 92, 0, 127, 64, ValueScale::Bipolar, "" },
            { "MideqFrequency", "MidEQ Frequency", "EQ/Mid Frequency (Hz)", "eq", 113, 93, 0, 127, 84, ValueScale::Bipolar, "" },
            { "MideqQFactor", "MidEQ Q-Factor", "EQ/Mid Q-Factor", "eq", 113, 94, 0, 127, 32, ValueScale::Bipolar, "" },
            { "LoweqGain", "LowEQ Gain", "EQ/Low Gain (dB)", "eq", 113, 95, 0, 127, 64, ValueScale::Bipolar, "" },
            { "HigheqGain", "HighEQ Gain", "EQ/High Gain (dB)", "eq", 113, 96, 0, 127, 64, ValueScale::Bipolar, "" },
            // --- bassBoost ---
            { "BassIntensity", "Bass Intensity", "Character Intensity", "character", 113, 97, 0, 127, 0, ValueScale::Percent, "" },
            { "BassTune", "Bass Tune", "Character Tune", "character", 113, 98, 0, 127, 32, ValueScale::Continuous, "" },
            // --- distortion ---
            { "DistortionCurve", "Distortion Curve", "Distortion Type", "distortion", 113, 100, 0, 25, 0, ValueScale::Enum, "NameDistortionModes" },
            { "DistortionIntensity", "Distortion Intensity", "Distortion Intensity", "distortion", 113, 101, 0, 127, 0, ValueScale::Percent, "" },
            { "PatchDistortionTrebleBooster", "Patch Distortion/Treble Booster", "Distortion Treble Booster", "distortion", 110, 70, 0, 127, 0, ValueScale::Percent, "" },
            { "PatchDistortionHighCut", "Patch Distortion/High Cut", "Distortion High Cut", "distortion", 110, 71, 0, 127, 127, ValueScale::Percent, "" },
            { "PatchDistortionMix", "Patch Distortion/Mix", "Distortion Mix", "distortion", 110, 72, 0, 127, 127, ValueScale::Percent, "" },
            { "PatchDistortionQuality", "Patch Distortion/Quality", "Distortion Quality", "distortion", 110, 73, 0, 127, 127, ValueScale::Percent, "" },
            { "PatchDistortionTone127", "Patch Distortion/Tone127", "Distortion Tone W", "distortion", 110, 74, 0, 127, 64, ValueScale::BipolarPercent, "" },
            { "PatchDistortionTone64", "Patch Distortion/Tone64", "Distortion Tone N", "distortion", 110, 74, 0, 64, 64, ValueScale::BipolarPercent, "" },
            // --- vocoder ---
            { "VocoderCarrierCenterFrequency", "Vocoder/Carrier Center Frequency", "Vocoder Carrier Center Frequency", "vocoder", 112, 40, 0, 127, 64, ValueScale::Bipolar, "" },
            { "VocoderModulatorCenterFrequency", "Vocoder/Modulator Center Frequency", "Vocoder Modulator Center Frequency", "vocoder", 112, 41, 0, 127, 64, ValueScale::Bipolar, "" },
            { "VocoderModulatorFrequencyOffset", "Vocoder/Modulator Frequency Offset", "Vocoder Modulator Frequency Offset", "vocoder", 112, 41, 0, 127, 64, ValueScale::Bipolar, "" },
            { "VocoderCarrierQFactor", "Vocoder/Carrier Q Factor", "Vocoder Carrier Q-Factor", "vocoder", 112, 42, 0, 127, 64, ValueScale::Continuous, "" },
            { "VocoderModulatorQFactor", "Vocoder/Modulator Q Factor", "Vocoder Modulator Q-Factor", "vocoder", 112, 43, 0, 127, 64, ValueScale::Continuous, "" },
            { "CarrierFrequencySpread", "Carrier Frequency Spread", "Vocoder Carrier Frequency Spread", "vocoder", 112, 46, 0, 127, 0, ValueScale::Percent, "" },
            { "VocoderModulatorFrequencySpread", "Vocoder/Modulator Frequency Spread", "VocoderModulator Frequency Spread", "vocoder", 112, 47, 0, 127, 127, ValueScale::Percent, "" },
            { "VocoderBalance", "Vocoder/Balance", "Vocoder Balance (Dry-Wet)", "vocoder", 112, 48, 0, 127, 64, ValueScale::Bipolar, "" },
            { "VocoderAttack", "Vocoder/Attack", "Vocoder Envelope Attack", "vocoder", 112, 54, 0, 127, 0, ValueScale::Continuous, "" },
            { "VocoderRelease", "Vocoder/Release", "Vocoder Envelope Release", "vocoder", 112, 55, 0, 127, 46, ValueScale::Continuous, "" },
            { "VocoderSpectralBalance", "Vocoder/Spectral Balance", "Vocoder Spectral Balance", "vocoder", 112, 57, 0, 127, 64, ValueScale::Bipolar, "" },
            { "VocoderBands", "Vocoder/Bands", "Vocoder Amount Of Synthesis Bands", "vocoder", 112, 58, 0, 31, 31, ValueScale::Continuous, "" },
            { "VocoderLink", "Vocoder Link", "Filter Cutoff Link", "vocoder", 113, 32, 0, 1, 1, ValueScale::Continuous, "" },
            { "VocoderMode", "Vocoder Mode", "Vocoder Mode", "vocoder", 113, 39, 0, 6, 0, ValueScale::Enum, "vocoderMode" },
            { "FilterSelect", "Filter Select", "Filter Select", "vocoder", 113, 122, 0, 2, 2, ValueScale::Enum, "filterSelect" },
            // --- inputFollower ---
            { "InputFollowerAttack", "Input Follower/Attack", "Input Follower Envelope Attack", "input", 112, 54, 0, 127, 64, ValueScale::Continuous, "" },
            { "InputFollowerLevel", "Input Follower/Level", "Input Follower/Level", "input", 112, 56, 0, 127, 0, ValueScale::Continuous, "" },
            { "InputFollowerRelease", "Input Follower/Release", "Input Follower Envelope Release", "input", 112, 58, 0, 127, 64, ValueScale::Continuous, "" },
            { "InputFollowerMode", "Input Follower Mode", "Input Follower/Select", "input", 113, 38, 0, 3, 0, ValueScale::Enum, "inputFollowerMode" },
            // --- filterBank ---
            { "FilterBankType", "Filter Bank/Type", "Filter Bank Type", "filterBank", 110, 19, 0, 11, 0, ValueScale::Enum, "filterBankType" },
            { "FilterBankMix", "Filter Bank/Mix", "Filter Bank Mix", "filterBank", 110, 20, 0, 127, 127, ValueScale::Percent, "" },
            { "FilterBankCombFrequency", "Filter Bank/Comb Frequency", "Filter Bank Frequency", "filterBank", 110, 21, 0, 96, 48, ValueScale::Enum, "nameCombFilter" },
            { "FilterBankFilterFrequency", "Filter Bank/Filter Frequency", "Filter Bank Frequency", "filterBank", 110, 21, 0, 127, 64, ValueScale::Continuous, "" },
            { "FilterBankFrequency", "Filter Bank/Frequency", "Filter Bank Frequency", "filterBank", 110, 21, 0, 127, 64, ValueScale::Bipolar, "" },
            { "FilterBankVowelFrequency", "Filter Bank/Vowel Frequency", "Filter Bank Frequency", "filterBank", 110, 21, 0, 127, 0, ValueScale::Percent, "" },
            { "FilterBankStereoPhase", "Filter Bank/Stereo Phase", "Filter Bank Stereo Phase", "filterBank", 110, 22, 0, 127, 64, ValueScale::Enum, "-180Degree+180Degree" },
            { "FilterBankFilterType", "Filter Bank/Filter Type", "Filter Bank Filter Type", "filterBank", 110, 23, 0, 127, 0, ValueScale::Continuous, "" },
            { "FilterBankPoles", "Filter Bank/Poles", "Filter Bank Filter Poles", "filterBank", 110, 23, 0, 127, 0, ValueScale::Continuous, "" },
            { "FilterBankShapeL", "Filter Bank/Shape L", "Frequency Shifter Left Shape", "filterBank", 110, 23, 0, 127, 127, ValueScale::BipolarPercent, "" },
            { "FilterBankShapeR", "Filter Bank/Shape R", "Frequency Shifter Right Shape", "filterBank", 110, 24, 0, 127, 127, ValueScale::BipolarPercent, "" },
            { "FilterBankSlope", "Filter Bank/Slope", "Filter Bank Filter Slope", "filterBank", 110, 24, 0, 127, 0, ValueScale::Continuous, "" },
            { "FilterBankResonance", "Filter Bank/Resonance", "Filter Bank Resonance", "filterBank", 110, 25, 0, 127, 64, ValueScale::Percent, "" },
            { "CharacterType", "Character Type", "Character Type", "character", 110, 26, 0, 8, 0, ValueScale::Enum, "characters" },
            // --- atomizer ---
            // A plain 0..127 knob, not an enum: the engine (Engine::updateParameters,
            // via scaling::unit) and the factory presets above both already treat
            // this as a full hardware byte. Capped at 16 with an "On/Off" choice
            // list, as this used to be, the highest reachable value was 16/127 -
            // and the lowest non-off setting, 1/127, fell under the effect's own
            // 0.01 activation threshold, so it never did anything.
            { "Atomizer", "Granulator Mix", "Granulator Mix", "granulator", 111, 126, 0, 127, 0, ValueScale::Percent, "" },
            // The granulator: a rolling buffer of whatever just played, resprayed
            // as a cloud of windowed grains. (The Atomizer byte keeps its id and
            // patch slot so older sessions still load - it is now the wet mix.)
            { "GranulatorGrains", "Granulator Grains", "Granulator Grain Count", "granulator", 115, 20, 0, 127, 40, ValueScale::Continuous, "" },
            { "GranulatorSize", "Granulator Size", "Granulator Grain Size", "granulator", 115, 21, 0, 127, 48, ValueScale::Continuous, "" },
            { "GranulatorPosition", "Granulator Position", "Granulator Buffer Position", "granulator", 115, 22, 0, 127, 0, ValueScale::Percent, "" },
            { "GranulatorSpray", "Granulator Spray", "Granulator Position Spray", "granulator", 115, 23, 0, 127, 12, ValueScale::Percent, "" },
            { "GranulatorPitch", "Granulator Pitch", "Granulator Grain Pitch", "granulator", 115, 24, 0, 127, 64, ValueScale::Bipolar, "" },
            { "GranulatorPitchDisp", "Granulator Pitch Disp", "Granulator Pitch Dispersion", "granulator", 115, 25, 0, 127, 0, ValueScale::Percent, "" },
            { "GranulatorStereo", "Granulator Stereo", "Granulator Stereo Spread", "granulator", 115, 26, 0, 127, 38, ValueScale::Percent, "" },
            { "GranulatorFreeze", "Granulator Freeze", "Granulator Freeze Buffer", "granulator", 115, 27, 0, 1, 0, ValueScale::Enum, "OnOff" },
            // --- lfos ---
            { "LfosSelect", "LFOs/Select", "LFOs/Select", "lfos", 110, 116, 0, 16, 0, ValueScale::Continuous, "" },
            // --- fx ---
            { "FxUpperSelect", "FX Upper/Select", "FX Upper/Select", "fx", 110, 117, 0, 16, 0, ValueScale::Continuous, "" },
            { "FxLowerSelect", "FX Lower/Select", "FX Lower/Select", "fx", 110, 118, 0, 16, 0, ValueScale::Continuous, "" },
            // --- modmatrix knob, now living in the Matrix Slot section since the
            // standalone Matrix module (which only ever held this one knob) is
            // gone - see AquaVibrioPanelLayout.h ---
            { "AssignsSlotSelect", "Assigns/Slot Select", "Assigns/Slot Select", "assignA", 110, 115, 0, 17, 0, ValueScale::Continuous, "" },
            // --- unplaced ---
            { "PortamentoTime", "Portamento Time", "Portamento Time", "common", 112, 5, 0, 127, 0, ValueScale::Continuous, "" },
            { "ChannelVolume", "Channel Volume", "Channel Volume", "", 112, 7, 0, 127, 0, ValueScale::Continuous, "" },
            { "Balance", "Balance", "Balance", "", 112, 8, 0, 127, 0, ValueScale::Continuous, "" },
            { "Panorama", "Panorama", "Panorama", "amp", 112, 10, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Cutoff2Offset", "Cutoff2 Offset", "Filter 2 Offset", "", 112, 41, 0, 127, 64, ValueScale::Bipolar, "" },
            { "Deprecated112103", "deprecated_112_103", "", "", 112, 103, 0, 6, 0, ValueScale::Enum, "chorusType" },
            { "ClockTempo", "Clock Tempo", "Tempo (when host has no clock)", "common", 113, 16, 0, 127, 57, ValueScale::Continuous, "" },
            { "SoftKnob1Shortname", "Soft Knob1 ShortName", "Soft Knob 1 Name", "", 113, 51, 0, 87, 0, ValueScale::Enum, "softknobName" },
            { "SoftKnob2Shortname", "Soft Knob2 ShortName", "Soft Knob 2 Name", "", 113, 52, 0, 87, 0, ValueScale::Enum, "softknobName" },
            { "SoftKnob3Shortname", "Soft Knob3 ShortName", "Soft Knob 3 Name", "", 113, 53, 0, 87, 0, ValueScale::Enum, "softknobName" },
            { "SoftKnob1ShortnameMain", "Soft Knob1 ShortName Main", "Soft Knob 1 Name", "", 113, 51, 0, 87, 0, ValueScale::Enum, "softknobName" },
            { "SoftKnob2ShortnameMain", "Soft Knob2 ShortName Main", "Soft Knob 2 Name", "", 113, 52, 0, 87, 0, ValueScale::Enum, "softknobName" },
            { "SoftKnob3ShortnameMain", "Soft Knob3 ShortName Main", "Soft Knob 3 Name", "", 113, 53, 0, 87, 0, ValueScale::Enum, "softknobName" },
            { "SoftKnob1Single", "Soft Knob-1 Single", "Soft Knob 1 Destination", "", 113, 62, 0, 127, 0, ValueScale::Enum, "softknobDest" },
            { "SoftKnob2Single", "Soft Knob-2 Single", "Soft Knob 2 Destination", "", 113, 63, 0, 127, 0, ValueScale::Enum, "softknobDest" },
            { "Category1", "Category1", "Patch Category 1", "", 113, 123, 0, 22, 0, ValueScale::Enum, "category" },
            { "Category2", "Category2", "Patch Category 2", "", 113, 124, 0, 22, 0, ValueScale::Enum, "category" },
            { "SoftKnobsDestination3", "Soft Knobs/Destination 3", "Soft Knob 3 Destination", "", 110, 28, 0, 127, 0, ValueScale::Enum, "softknobDest" },
            { "PartDetune", "Part Detune", "Part Detune", "", 114, 38, 0, 127, 64, ValueScale::Bipolar, "" },
            { "PartVolume", "Part Volume", "Part Volume", "", 114, 39, 0, 127, 64, ValueScale::Bipolar, "" },
            { "SoftKnob0ConfigValue", "Soft Knob Configuration/Value Softknob0", "Soft Knob Configuration/Value Softknob0", "", 115, 10, 0, 127, 0, ValueScale::Continuous, "" },
            { "SoftKnob1ConfigValue", "Soft Knob Configuration/Value Softknob1", "Soft Knob Configuration/Value Softknob1", "", 115, 11, 0, 127, 0, ValueScale::Continuous, "" },
            { "SoftKnob2ConfigValue", "Soft Knob Configuration/Value Softknob2", "Soft Knob Configuration/Value Softknob2", "", 115, 12, 0, 127, 0, ValueScale::Continuous, "" },
        };
        return table;
    }

    //==============================================================================
    // APVTS layout. Enum parameters become AudioParameterChoice, everything else
    // an AudioParameterInt over the hardware range, so a patch maps straight back
    // and forth without a lossy float round trip.
    //==============================================================================
    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (const auto& p : allParameters())
        {
            const juce::ParameterID pid{ p.id, 1 };

            if (p.scale == ValueScale::Enum)
            {
                auto ch = choicesFor(p.choiceList);
                const int wanted = p.maxValue - p.minValue + 1;
                while (ch.size() > wanted)  ch.remove(ch.size() - 1);
                while (ch.size() < wanted)  ch.add(juce::String(ch.size() + p.minValue));

                layout.add(std::make_unique<juce::AudioParameterChoice>(
                    pid, p.displayName, ch,
                    juce::jlimit(0, ch.size() - 1, p.defaultValue - p.minValue)));
            }
            else
            {
                // Values read in the engine's own units where the knob has them
                // (see AquaVibrioDisplay.h); otherwise by scale. A plain 0..127
                // amount reads as a percentage - "50 %" means more than "64".
                const juce::String id(p.id);
                const auto scale = p.scale;
                const int minV = p.minValue, maxV = p.maxValue;

                const auto toText = [id, scale, maxV](int v) -> juce::String
                    {
                        auto musical = display::musicalValue(id, v);
                        if (musical.isNotEmpty())
                            return musical;

                        switch (scale)
                        {
                        case ValueScale::Bipolar:        return display::signedValue((float)(v - 64) * 100.0f / 64.0f, " %");
                        case ValueScale::Percent:        return juce::String(juce::roundToInt(v * 100.0 / 127.0)) + " %";
                        case ValueScale::BipolarPercent: return juce::String(juce::roundToInt((v - 64) * 100.0 / 64.0)) + " %";
                        default:                         return maxV == 127 ? juce::String(juce::roundToInt(v * 100.0 / 127.0)) + " %"
                            : juce::String(v);
                        }
                    };

                layout.add(std::make_unique<juce::AudioParameterInt>(
                    pid, p.displayName, p.minValue, p.maxValue, p.defaultValue,
                    juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction([toText](int v, int) { return toText(v); })
                    .withValueFromStringFunction([toText, minV, maxV](const juce::String& t)
                        { return display::parse(t, minV, maxV, toText); })));
            }
        }

        return layout;
    }

    // Every parameter belonging to one GUI region, in table order.
    // Parameters that stay in the APVTS (so old sessions and host automation
    // still load) but are kept OFF the panel. Each is either a second reading of
    // a knob that is already shown - the hardware reuses one byte per oscillator
    // model, which put three "Filter Envelope > Pitch" knobs side by side - a
    // hardware page selector with no sound of its own, or a control for a variant
    // this engine folds into another knob. One knob, one job.
    inline bool isHiddenFromPanel(const juce::String& id)
    {
        static const juce::StringArray hidden{
            // duplicate readings of oscillator knobs
            "Osc1WavetableInternalDetune", "Osc2WavetableInternalDetune",
            "Osc2HypersawFilterenvPitch", "Osc2WavetableFilterenvPitch",
            "Osc2WavetableFilterenvFm", "Osc2WavetableFmAmount", "Osc2WavetableFmModeWavetable",
            // hardware page selectors and backup bytes
            "Osc3Backupmode", "OscillatorsSelect", "OscillatorsBackupkeymode",
            "Lfo1Backupshape", "Lfo2Backupshape", "Lfo3Backupshape", "ArpeggiatorBackupmode",
            "LfosSelect", "FxUpperSelect", "FxLowerSelect", "AssignsSlotSelect", "FilterSelect",
            "ControlSmoothMode",
            // chorus variants folded into Type / Rate / Depth
            "ChorusMix2", "ChorusSpeed", "ChorusDistance", "ChorusAmount", "ChorusMicAngle",
            "ChorusLowhighBal", "ChorusXOver", "ChorusFeedback",
            // tape delay duplicates: the tape types use the main delay knobs
            "DelayTapeDelayTime", "DelayTapeDelayFeedback", "DelayTapeDelayCenterFrequency",
            "DelayTapeDelayBandwidth", "DelayTapeDelayRatio", "DelayTapeDelayLeftClock",
            "DelayTapeDelayRightClock", "DelayLfoShape", "ReverbClock",
            // distortion duplicates
            "PatchDistortionQuality", "PatchDistortionTone64",
            // one Filter Bank Frequency knob serves every bank type
            "FilterBankCombFrequency", "FilterBankFilterFrequency", "FilterBankVowelFrequency",
            "FilterBankShapeL", "FilterBankShapeR", "FilterBankFilterType"
        };
        return hidden.contains(id);
    }

    inline std::vector<const ParamInfo*> parametersInRegion(const juce::String& regionId)
    {
        std::vector<const ParamInfo*> v;
        for (const auto& p : allParameters())
            if (regionId == p.region && !isHiddenFromPanel(p.id))
                v.push_back(&p);
        return v;
    }

    // Lookup by hardware address. Several parameters can share one address - the
    // three oscillator models are three readings of the same byte - so this hands
    // back all of them and a patch load writes the raw byte to each.
    inline std::vector<const ParamInfo*> parametersAt(int page, int index)
    {
        std::vector<const ParamInfo*> v;
        for (const auto& p : allParameters())
            if (p.page == page && p.index == index)
                v.push_back(&p);
        return v;
    }

} // namespace aquavibrio