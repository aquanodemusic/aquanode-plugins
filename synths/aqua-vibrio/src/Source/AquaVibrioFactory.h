#pragma once
//==============================================================================
//  AquaVibrioFactory.h
//
//  Factory presets written for THIS engine, not imported from hardware.
//
//  A Virus patch is a set of bytes whose meaning depends on the hardware's own
//  response curves. Ours are close but not identical, so an imported patch
//  lands somewhere near where it was meant to and often sounds wrong. These
//  presets are built the other way round: chosen by ear-target in musical
//  units, then converted into bytes through the curves this engine actually
//  uses - envelope times through 0.22 x 2^(v/7.65) ms, cutoff through
//  25.9 x 2^(v/12.9) Hz, LFO rates through 0.1 x 1000^(v/127) Hz.
//
//  Anything a preset does not name keeps its table default, so each one is a
//  short list of what makes it that sound rather than 424 opaque numbers.
//==============================================================================

#include <JuceHeader.h>
#include "AquaVibrioParameters.h"

namespace aquavibrio
{

struct FactoryPreset
{
    const char* category;
    const char* name;
    std::vector<std::pair<const char*, int>> values;
};

//==============================================================================
// Byte values for musical quantities, using this engine's measured curves.
// env: 17 = 1 ms, 42 = 10 ms, 54 = 30 ms, 65 = 80 ms, 75 = 200 ms,
//      85 = 500 ms, 93 = 1 s, 101 = 2 s, 111 = 5 s
// cut: 21 = 80 Hz, 33 = 150, 46 = 300, 58 = 600, 71 = 1.2 k, 85 = 2.5 k,
//      98 = 5 k, 109 = 9 k, 120 = 16 k
// lfo: 20 = 0.3 Hz, 42 = 1, 55 = 2, 68 = 4, 75 = 6, 85 = 10, 97 = 20
//==============================================================================
inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets
    {
        //== Pads ==============================================================
        { "Pads", "Warm Analogue Pad", {
            {"Osc1Shape",64},{"Osc2Shape",64},{"OscBalance",64},{"Osc2Detune",70},
            {"UnisonMode",1},{"UnisonDetune",28},{"UnisonPanSpread",90},
            {"Cutoff",64},{"Filter1Resonance",18},{"Filter1EnvAmt",34},
            {"FilterEnvAttack",70},{"FilterEnvDecay",95},{"FilterEnvSustain",70},
            {"AmpEnvAttack",72},{"AmpEnvDecay",100},{"AmpEnvSustain",110},{"AmpEnvRelease",95},
            {"ChorusMix",70},{"ChorusRate",30},{"ChorusDepth",80},
            {"ReverbSend",60},{"ReverbTime",90},{"PatchVolume",100} } },

        { "Pads", "Glass Bloom Pad", {
            {"Osc1Mode",2},{"Osc1WavetableWavetableselect",40},{"Osc1Shape",30},
            {"Osc2Shape",20},{"Osc2Semitone",76},{"OscBalance",70},
            {"Cutoff",88},{"Filter1Resonance",30},{"Filter1EnvAmt",20},
            {"AmpEnvAttack",88},{"AmpEnvDecay",105},{"AmpEnvSustain",100},{"AmpEnvRelease",100},
            {"Lfo1Rate",30},{"Lfo1Shape",0},{"Osc1Lfo1Amount",68},
            {"ReverbSend",85},{"ReverbTime",105},{"DelaySend",40},{"DelayTime",80} } },

        { "Pads", "Dark Choir Pad", {
            {"Osc1Shape",50},{"Osc2Shape",50},{"Osc2Detune",58},{"OscBalance",64},
            {"FilterBankType",3},{"FilterBankMix",70},{"FilterBankVowelFrequency",50},
            {"Cutoff",58},{"Filter1Resonance",25},
            {"AmpEnvAttack",80},{"AmpEnvSustain",115},{"AmpEnvRelease",100},
            {"ReverbSend",75},{"ReverbTime",100},{"PatchVolume",105} } },

        { "Pads", "Hyper Saw Pad", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",100},{"Osc1HypersawDetunespread",70},
            {"Osc2Mode",1},{"Osc2HypersawDensity",90},{"Osc2HypersawDetunespread",60},
            {"OscBalance",64},{"UnisonPanSpread",110},
            {"Cutoff",78},{"Filter1EnvAmt",25},
            {"AmpEnvAttack",66},{"AmpEnvSustain",120},{"AmpEnvRelease",92},
            {"ChorusMix",40},{"ReverbSend",55},{"DelaySend",35} } },

        //== Basses ============================================================
        { "Basses", "Deep Sub", {
            {"Osc1Shape",0},{"Osc1WaveSelect",0},{"OscBalance",0},
            {"SuboscillatorVolume",110},{"SuboscillatorShape",0},
            {"Cutoff",40},{"Filter1Resonance",10},{"Filter1EnvAmt",20},
            {"FilterEnvDecay",70},{"FilterEnvSustain",30},
            {"AmpEnvAttack",5},{"AmpEnvDecay",100},{"AmpEnvSustain",110},{"AmpEnvRelease",45},
            {"KeyMode",1},{"PatchVolume",110} } },

        { "Basses", "Reese Bass", {
            {"Osc1Shape",64},{"Osc2Shape",64},{"Osc2Detune",78},{"OscBalance",64},
            {"Cutoff",52},{"Filter1Resonance",30},{"Filter1EnvAmt",30},
            {"FilterEnvAttack",5},{"FilterEnvDecay",75},{"FilterEnvSustain",50},
            {"AmpEnvAttack",5},{"AmpEnvSustain",120},{"AmpEnvRelease",50},
            {"KeyMode",0},{"PatchVolume",108} } },

        { "Basses", "Acid Squelch", {
            {"Osc1Shape",64},{"OscBalance",0},
            {"Cutoff",38},{"Filter1Resonance",105},{"Filter1EnvAmt",75},
            {"Filter1Mode",0},{"SaturationCurve",3},
            {"FilterEnvAttack",0},{"FilterEnvDecay",62},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",90},{"AmpEnvSustain",100},{"AmpEnvRelease",40},
            {"PortamentoTime",30},{"KeyMode",1},{"DelaySend",30} } },

        { "Basses", "FM Growl Bass", {
            {"Osc1Shape",40},{"Osc2Shape",40},{"Osc2Semitone",76},
            {"Osc2FmAmount",85},{"OscFmMode",0},{"OscBalance",20},
            {"Cutoff",56},{"Filter1Resonance",35},{"Filter1EnvAmt",45},
            {"FilterEnvDecay",66},{"FilterEnvSustain",20},
            {"AmpEnvAttack",0},{"AmpEnvSustain",115},{"AmpEnvRelease",45},
            {"PatchDistortionMix",50},{"DistortionCurve",2},{"DistortionIntensity",40} } },

        { "Basses", "Pluck Bass", {
            {"Osc1Shape",64},{"SuboscillatorVolume",70},{"OscBalance",0},
            {"Cutoff",46},{"Filter1Resonance",40},{"Filter1EnvAmt",60},
            {"FilterEnvAttack",0},{"FilterEnvDecay",54},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",70},{"AmpEnvSustain",40},{"AmpEnvRelease",42},
            {"KeyMode",0} } },

        //== Psytrance =========================================================
        { "Psytrance", "Psy Rolling Bass", {
            {"Osc1Shape",64},{"OscBalance",0},{"SuboscillatorVolume",40},
            {"Cutoff",46},{"Filter1Resonance",20},{"Filter1EnvAmt",40},{"Filter1Keyfollow",100},
            {"FilterEnvAttack",0},{"FilterEnvDecay",54},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",70},{"AmpEnvSustain",0},{"AmpEnvRelease",30},
            {"KeyMode",1},{"PortamentoTime",0},{"PatchVolume",127},
            {"SaturationCurve",2},{"OscMainvolume",96} } },

        { "Psytrance", "Screaming Psy Lead", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",80},{"Osc1HypersawDetunespread",45},
            {"Osc2Shape",64},{"Osc2Semitone",76},{"OscBalance",50},
            {"FilterBankType",3},{"FilterBankMix",100},{"FilterBankVowelFrequency",70},
            {"FilterBankPoles",3},{"FilterBankSlope",110},{"FilterBankResonance",70},
            {"FilterBankMix",80},{"Cutoff",92},{"Filter1Resonance",40},{"PatchVolume",78},
            {"AmpEnvAttack",10},{"AmpEnvSustain",120},{"AmpEnvRelease",48},
            {"Lfo1Rate",68},{"Lfo1Shape",1},{"Lfo1AssignDest",1},{"Lfo1AssignAmount",78},
            {"DelaySend",55},{"DelayTime",70},{"ReverbSend",40} } },

        { "Psytrance", "Morphing Formant Lead", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",70},{"Osc1HypersawDetunespread",40},
            {"FilterBankType",3},{"FilterBankMix",85},{"FilterBankPoles",3},
            {"FilterBankSlope",127},{"FilterBankResonance",72},{"PatchVolume",78},
            {"Lfo2Rate",48},{"Lfo2Shape",1},{"Cutoff2Lfo2Amount",95},
            {"Cutoff",88},{"Filter1Resonance",50},
            {"AmpEnvAttack",8},{"AmpEnvSustain",120},{"AmpEnvRelease",50},
            {"DelaySend",50},{"ReverbSend",45} } },

        { "Psytrance", "Acid Zap", {
            {"Osc1Shape",100},{"Osc1Pulsewidth",40},{"OscBalance",0},
            {"Cutoff",40},{"Filter1Resonance",115},{"Filter1EnvAmt",85},
            {"FilterEnvAttack",0},{"FilterEnvDecay",46},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",60},{"AmpEnvSustain",20},{"AmpEnvRelease",35},
            {"DistortionCurve",21},{"DistortionIntensity",55},{"PatchDistortionMix",70},
            {"DelaySend",45},{"DelayTime",55} } },

        { "Psytrance", "Gated Psy Stab", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",90},{"Osc1HypersawDetunespread",55},
            {"Cutoff",80},{"Filter1Resonance",35},{"Filter1EnvAmt",50},
            {"FilterEnvAttack",0},{"FilterEnvDecay",58},{"FilterEnvSustain",10},
            {"AmpEnvAttack",0},{"AmpEnvDecay",52},{"AmpEnvSustain",0},{"AmpEnvRelease",32},
            {"Atomizer",40},{"ReverbSend",50},{"DelaySend",40} } },

        { "Psytrance", "Alien Talk", {
            {"Osc1Shape",64},{"Osc2Shape",30},{"Osc2Semitone",71},{"OscBalance",64},
            {"RingmodulatorVolume",90},
            {"FilterBankType",3},{"FilterBankMix",90},{"FilterBankPoles",2},
            {"Lfo1Rate",60},{"Lfo1Shape",4},{"Lfo1AssignDest",1},{"Lfo1AssignAmount",95},
            {"Cutoff",78},{"AmpEnvAttack",10},{"AmpEnvSustain",115},{"AmpEnvRelease",55},
            {"DelaySend",50},{"ReverbSend",45},{"PatchVolume",64} } },

        { "Psytrance", "Hoover Stab", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",110},{"Osc1HypersawDetunespread",80},
            {"Osc2Shape",64},{"Osc2Semitone",57},{"OscBalance",50},
            {"Cutoff",74},{"Filter1Resonance",40},{"Filter1EnvAmt",45},
            {"FilterEnvDecay",70},{"FilterEnvSustain",30},
            {"AmpEnvAttack",8},{"AmpEnvSustain",115},{"AmpEnvRelease",55},
            {"Lfo1Rate",55},{"Lfo1Shape",2},{"Osc1Lfo1Amount",72},
            {"ChorusMix",50},{"DelaySend",45} } },

        //== Leads =============================================================
        { "Leads", "Classic Saw Lead", {
            {"Osc1Shape",64},{"Osc2Shape",64},{"Osc2Detune",70},{"OscBalance",50},
            {"Cutoff",84},{"Filter1Resonance",25},{"Filter1EnvAmt",30},
            {"FilterEnvDecay",70},{"FilterEnvSustain",60},
            {"AmpEnvAttack",12},{"AmpEnvSustain",120},{"AmpEnvRelease",48},
            {"KeyMode",1},{"PortamentoTime",22},
            {"Lfo1Rate",62},{"Osc1Lfo1Amount",70},{"DelaySend",35},{"ReverbSend",30} } },

        { "Leads", "Square Whistle Lead", {
            {"Osc1Shape",127},{"Osc1Pulsewidth",0},{"OscBalance",0},
            {"Cutoff",96},{"Filter1Resonance",20},
            {"AmpEnvAttack",20},{"AmpEnvSustain",120},{"AmpEnvRelease",45},
            {"KeyMode",1},{"PortamentoTime",35},
            {"Lfo1Rate",65},{"Osc1Lfo1Amount",72},{"Lfo3FadeInTime",60},
            {"DelaySend",45},{"ReverbSend",35} } },

        { "Leads", "PWM Lead", {
            {"Osc1Shape",127},{"Osc1Pulsewidth",20},
            {"Lfo1Rate",45},{"Lfo1Shape",0},{"PwLfo1Amount",95},
            {"OscBalance",0},{"Cutoff",86},{"Filter1Resonance",22},
            {"AmpEnvAttack",18},{"AmpEnvSustain",120},{"AmpEnvRelease",55},
            {"ChorusMix",45},{"DelaySend",30} } },

        { "Leads", "Sync Lead", {
            {"Osc1Shape",64},{"Osc2Shape",64},{"Osc2Sync",1},
            {"Osc2HypersawCrossoscsyncfreq",90},{"OscBalance",90},
            {"Cutoff",90},{"Filter1Resonance",30},
            {"Osc2HypersawFilterenvPitch",100},
            {"FilterEnvAttack",0},{"FilterEnvDecay",72},{"FilterEnvSustain",20},
            {"AmpEnvAttack",5},{"AmpEnvSustain",118},{"AmpEnvRelease",45},
            {"DelaySend",40} } },

        //== Plucks ============================================================
        { "Plucks", "Nylon Pluck", {
            {"Osc1Shape",40},{"Osc1WaveSelect",20},{"OscBalance",0},
            {"Cutoff",72},{"Filter1Resonance",15},{"Filter1EnvAmt",45},
            {"FilterEnvAttack",0},{"FilterEnvDecay",58},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",72},{"AmpEnvSustain",0},{"AmpEnvRelease",50},
            {"ReverbSend",45},{"DelaySend",25} } },

        { "Plucks", "Digital Pluck", {
            {"Osc1Mode",2},{"Osc1WavetableWavetableselect",70},{"Osc1Shape",20},
            {"Cutoff",84},{"Filter1EnvAmt",40},
            {"FilterEnvAttack",0},{"FilterEnvDecay",54},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",66},{"AmpEnvSustain",0},{"AmpEnvRelease",48},
            {"DelaySend",55},{"DelayTime",60},{"ReverbSend",40} } },

        //== Bells & Mallets ===================================================
        { "Bells", "Glass Bell", {
            {"Osc1Shape",0},{"Osc1WaveSelect",0},
            {"Osc2Shape",0},{"Osc2Semitone",83},{"Osc2Detune",66},{"OscBalance",64},
            {"Osc2FmAmount",60},
            {"Cutoff",100},{"Filter1Resonance",10},
            {"AmpEnvAttack",0},{"AmpEnvDecay",92},{"AmpEnvSustain",0},{"AmpEnvRelease",85},
            {"ReverbSend",80},{"ReverbTime",100},{"DelaySend",35} } },

        { "Bells", "Tubular Bell", {
            {"Osc1Shape",0},{"Osc2Shape",0},{"Osc2Semitone",88},{"OscBalance",50},
            {"Osc2FmAmount",40},{"RingmodulatorVolume",40},
            {"Cutoff",94},{"AmpEnvAttack",0},{"AmpEnvDecay",100},{"AmpEnvSustain",0},
            {"AmpEnvRelease",95},{"ReverbSend",85},{"ReverbTime",110} } },

        { "Bells", "Music Box", {
            {"Osc1Shape",0},{"Osc1WaveSelect",10},{"Osc2Semitone",88},{"OscBalance",30},
            {"Cutoff",98},{"AmpEnvAttack",0},{"AmpEnvDecay",78},{"AmpEnvSustain",0},
            {"AmpEnvRelease",70},{"ReverbSend",70},{"ChorusMix",30} } },

        //== Keys ==============================================================
        { "Keys", "Electric Piano", {
            {"Osc1Shape",0},{"Osc1WaveSelect",6},{"Osc2Shape",0},{"Osc2Semitone",88},
            {"OscBalance",30},{"Osc2FmAmount",45},{"FmAmountVelocity",95},
            {"Cutoff",82},{"Filter1EnvAmt",30},{"Flt1EnvamtVelocity",90},
            {"FilterEnvDecay",76},{"FilterEnvSustain",20},
            {"AmpEnvAttack",0},{"AmpEnvDecay",90},{"AmpEnvSustain",40},{"AmpEnvRelease",60},
            {"AmpVelocity",95},{"ChorusMix",40},{"ReverbSend",45} } },

        { "Keys", "Clav", {
            {"Osc1Shape",120},{"Osc1Pulsewidth",70},{"OscBalance",0},
            {"Cutoff",76},{"Filter1Resonance",45},{"Filter1EnvAmt",50},
            {"FilterEnvAttack",0},{"FilterEnvDecay",52},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",70},{"AmpEnvSustain",30},{"AmpEnvRelease",38},
            {"AmpVelocity",100},{"PhaserMode",2},{"PhaserMix",50},{"PhaserRate",35} } },

        { "Keys", "Organ", {
            {"Osc1Shape",0},{"Osc1WaveSelect",0},{"Osc2Shape",0},{"Osc2Semitone",88},
            {"OscBalance",55},{"SuboscillatorVolume",60},
            {"Cutoff",92},{"AmpEnvAttack",0},{"AmpEnvDecay",127},{"AmpEnvSustain",127},
            {"AmpEnvRelease",20},{"ChorusMix",60},{"ChorusRate",50},{"ReverbSend",35} } },

        //== Strings & Brass ===================================================
        { "Strings", "Ensemble Strings", {
            {"Osc1Shape",64},{"Osc2Shape",64},{"Osc2Detune",72},{"OscBalance",64},
            {"UnisonMode",2},{"UnisonDetune",35},{"UnisonPanSpread",100},
            {"Cutoff",72},{"Filter1Resonance",12},{"Filter1EnvAmt",22},
            {"AmpEnvAttack",68},{"AmpEnvDecay",100},{"AmpEnvSustain",115},{"AmpEnvRelease",85},
            {"ChorusMix",75},{"ChorusRate",25},{"ReverbSend",70} } },

        { "Strings", "Brass Section", {
            {"Osc1Shape",64},{"Osc2Shape",100},{"Osc2Detune",68},{"OscBalance",55},
            {"Cutoff",62},{"Filter1Resonance",25},{"Filter1EnvAmt",55},
            {"FilterEnvAttack",42},{"FilterEnvDecay",80},{"FilterEnvSustain",75},
            {"AmpEnvAttack",45},{"AmpEnvSustain",120},{"AmpEnvRelease",60},
            {"AmpVelocity",90},{"ReverbSend",55} } },

        //== Flutes & Whistles =================================================
        { "Flutes", "Soft Flute", {
            {"Osc1Shape",0},{"Osc1WaveSelect",2},{"OscBalance",0},
            {"NoiseVolume",28},{"NoiseColor",100},
            {"Cutoff",78},{"Filter1Resonance",18},
            {"AmpEnvAttack",56},{"AmpEnvSustain",118},{"AmpEnvRelease",60},
            {"Lfo1Rate",62},{"Osc1Lfo1Amount",70},{"Lfo3FadeInTime",70},
            {"ReverbSend",65} } },

        { "Flutes", "Pan Whistle", {
            {"Osc1Shape",0},{"Osc1WaveSelect",0},{"OscBalance",0},
            {"NoiseVolume",45},{"NoiseColor",120},
            {"Cutoff",78},{"Filter1Resonance",30},{"Filter1Keyfollow",110},
            {"AmpEnvAttack",48},{"AmpEnvSustain",115},{"AmpEnvRelease",58},
            {"KeyMode",1},{"PortamentoTime",30},{"ReverbSend",70},{"DelaySend",35} } },

        //== Digital ===========================================================
        { "Digital", "Wavetable Sweep", {
            {"Osc1Mode",2},{"Osc1WavetableWavetableselect",60},{"Osc1Shape",64},
            {"Lfo2Rate",38},{"Lfo2Shape",1},{"ShapeLfo2Amount",100},
            {"Cutoff",92},{"Filter1Resonance",25},
            {"AmpEnvAttack",30},{"AmpEnvSustain",120},{"AmpEnvRelease",60},
            {"DelaySend",45},{"ReverbSend",45} } },

        { "Digital", "FM Seventh Stack", {
            {"Osc1Shape",0},{"Osc2Shape",0},{"Osc2Semitone",74},{"Osc2Detune",66},
            {"OscBalance",60},{"Osc2FmAmount",70},{"Osc3Mode",3},{"Osc3Volume",60},
            {"Osc3Semitone",71},
            {"Cutoff",96},{"AmpEnvAttack",10},{"AmpEnvDecay",100},{"AmpEnvSustain",90},
            {"AmpEnvRelease",70},{"ChorusMix",40},{"ReverbSend",55} } },

        { "Digital", "Bit Crushed Stab", {
            {"Osc1Shape",64},{"OscBalance",0},
            {"DistortionCurve",19},{"DistortionIntensity",45},{"PatchDistortionMix",80},
            {"PatchVolume",74},
            {"PatchDistortionTone127",90},
            {"Cutoff",88},{"Filter1EnvAmt",40},
            {"FilterEnvDecay",56},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",62},{"AmpEnvSustain",0},{"AmpEnvRelease",40},
            {"DelaySend",50} } },

        { "Digital", "Ring Mod Metal", {
            {"Osc1Shape",30},{"Osc2Shape",30},{"Osc2Semitone",79},{"Osc2Detune",72},
            {"OscBalance",64},{"RingmodulatorVolume",120},
            {"Cutoff",90},{"Filter1Resonance",30},
            {"AmpEnvAttack",0},{"AmpEnvDecay",85},{"AmpEnvSustain",30},{"AmpEnvRelease",60},
            {"ReverbSend",60},{"DelaySend",40} } },

        //== Noise & FX ========================================================
        { "Noise FX", "White Noise Sweep", {
            {"NoiseVolume",127},{"NoiseColor",80},{"OscMainvolume",0},
            {"Cutoff",40},{"Filter1Resonance",60},{"Filter1EnvAmt",85},{"PatchVolume",120},
            {"FilterEnvAttack",78},{"FilterEnvDecay",95},{"FilterEnvSustain",0},
            {"AmpEnvAttack",60},{"AmpEnvSustain",110},{"AmpEnvRelease",80},
            {"ReverbSend",80} } },

        { "Noise FX", "Wind", {
            {"NoiseVolume",127},{"NoiseColor",56},{"OscMainvolume",0},
            {"Cutoff",72},{"Filter1Resonance",30},{"PatchVolume",127},
            {"Lfo1Rate",18},{"Lfo1Shape",4},{"Cutoff1Lfo2Amount",90},
            {"Lfo2Rate",22},{"Lfo2Shape",0},
            {"AmpEnvAttack",75},{"AmpEnvSustain",115},{"AmpEnvRelease",90},
            {"ReverbSend",90},{"ReverbTime",110} } },

        { "Noise FX", "Riser", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",120},{"Osc1HypersawDetunespread",90},
            {"NoiseVolume",60},
            {"Cutoff",30},{"Filter1EnvAmt",110},
            {"FilterEnvAttack",100},{"FilterEnvDecay",110},{"FilterEnvSustain",127},
            {"AmpEnvAttack",90},{"AmpEnvSustain",120},{"AmpEnvRelease",70},
            {"Lfo1Rate",70},{"Osc1Lfo1Amount",75},
            {"ReverbSend",85},{"DelaySend",50} } },

        { "Noise FX", "Atomized Glitch", {
            {"Osc1Mode",1},{"Osc1HypersawDensity",80},
            {"Cutoff",86},{"Filter1Resonance",40},
            {"AmpEnvAttack",0},{"AmpEnvSustain",120},{"AmpEnvRelease",40},
            {"Atomizer",55},{"DelaySend",60},{"DelayTime",40},{"ReverbSend",50},
            {"PatchVolume",120} } },

        //== Arps & Sequences ==================================================
        { "Arps", "Classic Up Arp", {
            {"ArpMode",1},{"ArpOctaveRange",2},{"ArpNoteLength",70},{"ArpClock",8},
            {"Osc1Shape",100},{"Osc1Pulsewidth",30},{"OscBalance",0},
            {"Cutoff",76},{"Filter1Resonance",35},{"Filter1EnvAmt",45},
            {"FilterEnvAttack",0},{"FilterEnvDecay",52},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",60},{"AmpEnvSustain",20},{"AmpEnvRelease",35},
            {"DelaySend",50},{"DelayTime",55},{"ReverbSend",35} } },

        { "Arps", "Psy Gate Sequence", {
            {"ArpMode",1},{"ArpOctaveRange",1},{"ArpNoteLength",40},{"ArpClock",9},
            {"Osc1Mode",1},{"Osc1HypersawDensity",90},{"Osc1HypersawDetunespread",50},
            {"Cutoff",70},{"Filter1Resonance",55},{"Filter1EnvAmt",60},
            {"FilterEnvAttack",0},{"FilterEnvDecay",48},{"FilterEnvSustain",0},
            {"AmpEnvAttack",0},{"AmpEnvDecay",50},{"AmpEnvSustain",0},{"AmpEnvRelease",28},
            {"DelaySend",55},{"ReverbSend",40} } },
    };

    return presets;
}

// Applies a factory preset: everything back to its table default, then the
// preset's own values on top.
inline void applyFactoryPreset (const FactoryPreset& preset,
                                juce::AudioProcessorValueTreeState& apvts)
{
    const auto setByte = [&apvts] (const juce::String& id, int byte)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const auto range = p->getNormalisableRange();
            p->beginChangeGesture();
            p->setValueNotifyingHost (range.convertTo0to1 ((float) byte));
            p->endChangeGesture();
        }
    };

    for (const auto& info : allParameters())
        setByte (info.id, info.defaultValue);

    for (const auto& v : preset.values)
        setByte (v.first, v.second);
}

} // namespace aquavibrio
