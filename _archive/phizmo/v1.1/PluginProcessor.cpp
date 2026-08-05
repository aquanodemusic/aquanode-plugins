#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
PhizmoAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- Evolution (shared scan engine: time / LFOs / pos LFO stay linked) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoTime", "Evo Cycle Time (s)",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.001f, 0.25f), 4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoStepped", "Evo Stepped (Osc A)",
        juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoLFORate", "Evo LFO Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoLFODepth", "Evo LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "posLFORate", "Pos LFO Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "posLFODepth", "Pos LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // --- Curve points (32) — Osc A ---
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String id = "evoPoint_" + juce::String::formatted("%02d", i);
        float def = (float)i / (float)(EVO_POINTS - 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            id, id, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    }

    // --- Curve points (32) — Osc B (independent shape, same shared scan position) ---
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String id = "evoPointB_" + juce::String::formatted("%02d", i);
        float def = (float)i / (float)(EVO_POINTS - 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            id, id, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    }
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoSteppedB", "Evo Stepped (Osc B)",
        juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f), 0.0f));
    // Evolution playhead mode, per oscillator: 0 NORM, 1 RAND, 2 LOCK, 3 FIX.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoMode", "Evo Mode (Osc A)",
        juce::NormalisableRange<float>(0.0f, 3.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoModeB", "Evo Mode (Osc B)",
        juce::NormalisableRange<float>(0.0f, 3.0f, 1.0f), 0.0f));

    // --- Pitch ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "detune", "Detune (cents)",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchLFO", "Pitch LFO Depth (st)",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchLFORate", "Pitch LFO Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchEnvAmt", "Pitch Env Amount (st)",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchEnvAtt", "Pitch Env Attack",
        juce::NormalisableRange<float>(0.001f, 4.0f, 0.001f, 0.35f), 0.05f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchEnvDec", "Pitch Env Decay",
        juce::NormalisableRange<float>(0.001f, 4.0f, 0.001f, 0.35f), 0.3f));

    // --- Amplitude Envelope (Osc 1) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Osc 1 Attack", juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Osc 1 Decay", juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sustain", "Osc 1 Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Osc 1 Release", juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.5f));

    // --- Amplitude Envelope (Osc 2) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack2", "Osc 2 Attack", juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay2", "Osc 2 Decay", juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sustain2", "Osc 2 Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release2", "Osc 2 Release", juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.5f));

    // --- Character ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bitCrush", "Bit Depth",
        juce::NormalisableRange<float>(4.0f, 16.0f, 0.01f), 16.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "grit", "Grit",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // --- Scan ---
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scanStyle", "Scan Style",
        juce::StringArray{ "Forward","Fwd Stay","Back & Forth","Bwd Stay","Backward" }, 0));
    // Oscillator B scans on its own style, so the two halves of a Voice can
    // run in opposite directions or at different rates.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scanStyleB", "Scan Style Osc B",
        juce::StringArray{ "Forward","Fwd Stay","Back & Forth","Bwd Stay","Backward" }, 0));
    // The noise source has its own filter. It used to be shaped by the shared
    // bus filter, so when that was removed it became raw full-band white noise
    // and far too loud for the same knob position.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noiseFreq", "Noise Filter Cutoff",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f), 4000.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noiseQ", "Noise Filter Reso",
        juce::NormalisableRange<float>(0.3f, 12.f, 0.01f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "noiseFilterType", "Noise Filter Type",
        juce::StringArray{ "Low Pass","Band Pass","High Pass" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "jumpProb", "Jump Probability",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // --- Filter ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterFreq", "Filter Frequency",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.3f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterQ", "Filter Resonance",
        juce::NormalisableRange<float>(0.1f, 12.0f, 0.01f, 0.5f), 0.707f));
    // Filter envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterAtt", "Filter Env Attack",
        juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterDec", "Filter Env Decay",
        juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterSus", "Filter Env Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterRel", "Filter Env Release",
        juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterEnvAmt", "Filter Env Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterLFODep", "Filter LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // --- Stereo ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "spread", "Osc Spread (cents)", juce::NormalisableRange<float>(0.f, 50.f, 0.1f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "stereoWidth", "Stereo Width", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "uniDetune", "Unison Detune", juce::NormalisableRange<float>(0.f, 50.f, 0.1f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "stereoPhase", "Osc B Phase Offset", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));

    // --- Osc Mix ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "oscMix", "Osc A/B Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    // --- Octave ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "octaveA", "Oct Osc 1",
        juce::NormalisableRange<float>(-2.f, 2.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "octaveB", "Oct Osc 2",
        juce::NormalisableRange<float>(-2.f, 2.f, 1.f), 0.f));

    // --- Glide / Mono ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "glide", "Glide",
        juce::NormalisableRange<float>(0.f, 4.f, 0.001f, 0.4f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mono", "Mono",
        juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));

    // --- Noise ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noiseLevel", "Noise",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));

    // --- FX ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusRate", "Chorus Rate", juce::NormalisableRange<float>(0.01f, 8.f, 0.01f, 0.5f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusDepth", "Chorus Depth", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ringMod", "Ring Mod", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbSize", "Reverb Size", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbDamp", "Reverb Damp", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbWet", "Reverb Wet", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));

    // --- Output ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Output Gain", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.7f));

    // --- Engine mode toggles ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "frameInterp", "Frame Interpolation", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 1.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "velToFrame", "Velocity to Frame Pos", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoPhaseCarry", "Evo Phase Carry", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));

    // --- Transwave position envelope (Phizmo-style) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twAtt", "TW Env Attack",
        juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twDec", "TW Env Decay",
        juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twSus", "TW Env Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twRel", "TW Env Release",
        juce::NormalisableRange<float>(0.001f, 8.f, 0.001f, 0.35f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twAmt", "TW Env Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampVelAmt", "Envelope Velocity",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.6f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twVelAmt", "TW Vel Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // --- Misc (row 2, cols 1-6) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "frameSnap", "Frame Snap Steps",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));   // 0=off, >0 = quantise
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "twToFilter", "TW Env to Filter",
        juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoBPhaseOff", "Osc B Curve Phase Offset",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoPhaseOff", "Osc A Curve Phase Offset",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    // Curve fade time (ms). One per oscillator, drives both the STEP-mode
    // declick and the forward/backward wrap declick. 0 = hard (clicks), a few ms
    // = smoothed. Default 4 ms matches the previous fixed behaviour.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoSlew", "Osc A Curve Slew ms",
        juce::NormalisableRange<float>(0.f, 10.f, 0.01f), 4.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoSlewB", "Osc B Curve Slew ms",
        juce::NormalisableRange<float>(0.f, 10.f, 0.01f), 4.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "keytrack", "Keytrack to Frame",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoTimeB", "Evo Time Osc B (s)",
        juce::NormalisableRange<float>(0.1f, 100.f, 0.001f, 0.25f), 4.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoRestart", "Evo Restart on Note",
        juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));

    // --- Wavetable cycle-edge smoothing (independent per slot) ---
    // 0 = off, 1 = 15% fade-in/out applied at each cycle boundary.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wtSmooth", "WT Smooth A",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wtSmoothB", "WT Smooth B",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));

    // =====================================================================
    // the original REMAKE ADDITIONS
    // Real Phizmo signal path per Sound = 2 transwave oscillators, each with
    // its own Wave(+start), Pitch(Tune/Fine), Amplitude(Level/Pan), Filter
    // (keytrack/type), LFO(shape/speed) and 3 selectable EGs. The panel's
    // "select OSC then edit" workflow is mirrored in the editor by re-pointing
    // the shared knobs at these per-slot parameters.
    // =====================================================================

    // --- Pitch: coarse Tune (±24 st) + Fine (±100 cents) per oscillator ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tuneA", "Tune Osc 1 (st)", juce::NormalisableRange<float>(-24.f, 24.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tuneB", "Tune Osc 2 (st)", juce::NormalisableRange<float>(-24.f, 24.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fineA", "Fine Osc 1 (cents)", juce::NormalisableRange<float>(-100.f, 100.f, 0.1f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fineB", "Fine Osc 2 (cents)", juce::NormalisableRange<float>(-100.f, 100.f, 0.1f), 0.f));

    // --- Wave: transwave START POINT per oscillator (the Phizmo "Wave" knob).
    // This is a base frame-position offset added before scanning. "Wave Mod"
    // amount scales how much the shared LFO/position engine shifts the start. ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "waveStartA", "Wave Start Osc 1", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "waveStartB", "Wave Start Osc 2", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "waveModA", "Wave Mod Osc 1", juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "waveModB", "Wave Mod Osc 2", juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));

    // --- Amplitude: per-oscillator Level + Pan (the Phizmo "Amplitude" section) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "levelA", "Level Osc 1", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "levelB", "Level Osc 2", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "panA", "Pan Osc 1", juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "panB", "Pan Osc 2", juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));

    // --- Filter: keyboard tracking + type (Phizmo LP4 / BP / HP) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterKeytrack", "Filter Keytrack", juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "filterType", "Filter Type", juce::StringArray{ "LP 4-pole","Band Pass","High Pass" }, 0));

    // --- Modulation: LFO shape + speed (the eight Phizmo LFO waveforms) ---
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "lfoShape", "LFO Shape",
        juce::StringArray{ "Triangle","Sine+Triangle","Sine","Rising Triangle",
                           "Rising Sine","Sawtooth","Square","Positive Ramp","Noise" }, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lfoSpeed", "LFO Speed", juce::NormalisableRange<float>(0.01f, 20.f, 0.01f, 0.4f), 1.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modSelect", "Mod Noise/LFO", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 1.f)); // 0=Noise,1=LFO

    // The LFO and the noise generator each have their own rate, and either can
    // run free or lock to the system clock (User's Guide p.25). Index 0 = Free,
    // 1..12 = the same twelve note resolutions the arpeggiator uses.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "lfoSync", "LFO Sync",
        juce::StringArray{ "Free","Whole","Whole T","Half","Half T","Qtr","Qtr T",
                           "1/8","1/8 T","1/16","1/16 T","1/32","1/32 T" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noiseRate", "Noise Rate", juce::NormalisableRange<float>(0.01f, 20.f, 0.01f, 0.4f), 1.f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "noiseSync", "Noise Sync",
        juce::StringArray{ "Free","Whole","Whole T","Half","Half T","Qtr","Qtr T",
                           "1/8","1/8 T","1/16","1/16 T","1/32","1/32 T" }, 0));

    // Keyboard velocity response — the six curves on the hardware's MIDI Edit
    // "tch" page (User's Guide p.10). 1..4 soft->hard, 5 fixed 64, 6 fixed 127.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "velCurve", "Velocity Curve",
        juce::StringArray{ "Curve 1","Curve 2","Curve 3","Curve 4","Fixed 64","Fixed 127" }, 1));

    // --- F-I-Z-M-O real-time macro knobs (bipolar around centre) ---
    // F: Effect mod   I: Wave mod   Z: Filter cutoff   M: Detune   O: Assignable
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phizF", "F : Effect Mod",  juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phizI", "I : Wave Mod",    juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phizZ", "Z : Filter",      juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phizM", "M : Detune",      juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phizO", "O : Assignable",  juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "phizODest", "O Destination",
        juce::StringArray{ "Reverb Wet","Evo Time","Resonance","Ring Mod","Chorus Depth" }, 0));

    // --- Arpeggiator ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "arpOn", "Arp On/Off", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "arpMode", "Arp Mode",
        juce::StringArray{ "Up","Down","Up/Down","Random","As Played" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "arpRange", "Arp Range",
        juce::StringArray{ "Oct 0","Oct 1","Oct 2","Oct 3","Oct 4" }, 0));
    // Note Resolution — the twelve values listed in the User's Guide (p.13),
    // ordered slowest to fastest.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "arpRate", "Arp Rate",
        juce::StringArray{ "Whole","Whole T","Half","Half T","Qtr","Qtr T",
                           "1/8","1/8 T","1/16","1/16 T","1/32","1/32 T" }, 8));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tempo", "Tempo (BPM)", juce::NormalisableRange<float>(25.f, 320.f, 1.f), 120.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "arpGate", "Arp Gate", juce::NormalisableRange<float>(0.05f, 1.f, 0.001f), 0.5f));
    // Arpeggiator Keyboard button (User's Guide p.12): on = the keyboard feeds
    // the arpeggiator, off = the keyboard bypasses it and plays the Preset.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "arpKeyboard", "Arp Keyboard", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 1.f));

    // --- Envelope Mode (Phizmo EG Mode button: Normal / Loop / One-shot) ---
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "envMode", "Env Mode", juce::StringArray{ "Normal","Finish","Repeat" }, 0));

    // --- Phizmo Preset structure: FOUR Sounds per Preset ---------------------
    // Sound 1 is enabled by default; each Sound has a keyboard zone and level
    // so Presets can be split/layered exactly like the hardware.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "soundSelect", "Sound Select", juce::NormalisableRange<float>(0.f, 3.f, 1.f), 0.f));
    for (int s = 0; s < 4; ++s)
    {
        juce::String n(s + 1);
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "soundOn" + juce::String(s), "Sound " + n + " On", s == 0));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            "soundLow" + juce::String(s), "Sound " + n + " Low Key", 0, 127, 0));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            "soundHigh" + juce::String(s), "Sound " + n + " High Key", 0, 127, 127));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "soundGain" + juce::String(s), "Sound " + n + " Level",
            juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
    }

    // --- Modulation matrix: source select + amount for Wave, Pitch and Filter,
    //     independently per oscillator (the hardware's Modulation buttons).
    {
        const juce::StringArray modSrcs = juce::StringArray{ "OFF","FULL","LFO","Stepped Noise","Smooth Noise","Envelope 1","Envelope 2","Envelope 3","Velocity","Vel+Pressure","MIDI Note","Keyboard","Pressure","Pitch Wheel","Mod Wheel","ModWheel+Press","Foot","Sustain","Sostenuto","SYS1","SYS2","SYS3 (F)","SYS4 (O)","Patch Select","Global LFO" };
        for (const char* osc : { "A", "B" })
        {
            juce::String o(osc);
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                "waveModSrc" + o, "Wave Mod Source " + o, modSrcs, 2));
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                "pitchModSrc" + o, "Pitch Mod Source " + o, modSrcs, 2));
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                "filtModSrc" + o, "Filter Mod Source " + o, modSrcs, 2));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                "pitchModAmt" + o, "Pitch Mod Amount " + o,
                juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                "filtModAmt" + o, "Filter Mod Amount " + o,
                juce::NormalisableRange<float>(-1.f, 1.f, 0.001f), 0.f));
        }
    }

    // --- OSC 1 / OSC 2 on-off (the hardware's OSC buttons) ---
    params.push_back(std::make_unique<juce::AudioParameterBool>("oscAOn", "Osc 1 On", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("oscBOn", "Osc 2 On", true));
    // Pitch bend range (manual: bEnd, factory default 2 semitones)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bendRange", "Pitch Bend Range", juce::NormalisableRange<float>(0.f, 12.f, 1.f), 2.f));

    // --- Phizmo EFFECTS: one insert algorithm + the global reverb -------------
    {
        juce::StringArray algos;
        for (int i = 0; i < phz::PhizmoInsert::NumAlgos; ++i)
            algos.add(phz::PhizmoInsert::algoName(i));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "fxAlgo", "Insert Effect", algos, 0));

        juce::StringArray revs;
        for (int i = 0; i < 8; ++i) revs.add(phz::PhizmoReverb::variationName(i));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "revVariation", "Reverb Variation", revs, 3));
    }
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fxVariation", "Effect Variation", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fxMix", "Effect Mix", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "revAmount", "Reverb Amount", juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.25f));
    // Effect Bus — assigned PER OSCILLATOR, exactly as on the hardware:
    //   inS = insert effect,  r1/r2/r3 = light/average/heavy global reverb,
    //   dry = straight to the outputs.
    {
        // Insert or dry per oscillator; reverb is a separate global send.
        juce::StringArray buses{ "Insert","Dry" };
        for (int s = 0; s < 4; ++s)
        {
            juce::String n(s + 1);
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                "fxBusA" + juce::String(s), "S" + n + " Osc1 Effect Bus", buses, 0));
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                "fxBusB" + juce::String(s), "S" + n + " Osc2 Effect Bus", buses, 0));
        }
    }

    // GUI window width — stored in APVTS so the DAW saves/restores it
    // automatically. Height is always derived from the fixed 1190:804 ratio.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "guiWidth", "GUI Width",
        juce::NormalisableRange<float>(650.f, 2600.f, 1.f), 1200.f));

    // Wavetable cycle sizes — persisted so they survive project reloads.
    // Valid range 16–65536 (any power-of-two the user might need).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cycleSizeA", "Cycle Size A",
        juce::NormalisableRange<float>(16.f, 65536.f, 1.f), 2048.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cycleSizeB", "Cycle Size B",
        juce::NormalisableRange<float>(16.f, 65536.f, 1.f), 2048.f));

    return { params.begin(), params.end() };
}

//==============================================================================
PhizmoAudioProcessor::PhizmoAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String id = "evoPoint_" + juce::String::formatted("%02d", i);
        auto* p = apvts.getRawParameterValue(id);
        evoCurve[i].store(p ? p->load() : (float)i / (float)(EVO_POINTS - 1));
    }
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String id = "evoPointB_" + juce::String::formatted("%02d", i);
        auto* p = apvts.getRawParameterValue(id);
        evoCurveB[i].store(p ? p->load() : (float)i / (float)(EVO_POINTS - 1));
    }

    // Make the playhead publish arrays safe to read the instant the editor opens,
    // even before prepareToPlay(): no note → hidden playheads, master scans at 0.
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voicePubSound[i].store(-1);
        voicePubScanA[i].store(0.f);
        voicePubScanB[i].store(0.f);
    }
    for (int s = 0; s < NUM_SOUNDS; ++s)
        for (int o = 0; o < 2; ++o)
        {
            evoFixPhase[s][o] = 0.0;
            evoFixDir[s][o]   = 1;
            evoFixPub[s][o].store(0.f);
        }

    pEvoTime = apvts.getRawParameterValue("evoTime");
    pEvoStepped = apvts.getRawParameterValue("evoStepped");
    pEvoSteppedB = apvts.getRawParameterValue("evoSteppedB");
    pEvoMode  = apvts.getRawParameterValue("evoMode");
    pEvoModeB = apvts.getRawParameterValue("evoModeB");
    pEvoLFORate = apvts.getRawParameterValue("evoLFORate");
    pEvoLFODepth = apvts.getRawParameterValue("evoLFODepth");
    pPosLFORate = apvts.getRawParameterValue("posLFORate");
    pPosLFODepth = apvts.getRawParameterValue("posLFODepth");
    pAttack = apvts.getRawParameterValue("attack");
    pDecay = apvts.getRawParameterValue("decay");
    pSustain = apvts.getRawParameterValue("sustain");
    pRelease = apvts.getRawParameterValue("release");
    pGain = apvts.getRawParameterValue("gain");
    pBitCrush = apvts.getRawParameterValue("bitCrush");
    pGrit = apvts.getRawParameterValue("grit");
    pDetune = apvts.getRawParameterValue("detune");
    pPitchLFO = apvts.getRawParameterValue("pitchLFO");
    pPitchLFORate = apvts.getRawParameterValue("pitchLFORate");
    pPitchEnvAmt = apvts.getRawParameterValue("pitchEnvAmt");
    pPitchEnvAtt = apvts.getRawParameterValue("pitchEnvAtt");
    pPitchEnvDec = apvts.getRawParameterValue("pitchEnvDec");
    pScanStyle = apvts.getRawParameterValue("scanStyle");
    pScanStyleB = apvts.getRawParameterValue("scanStyleB");
    pJumpProb = apvts.getRawParameterValue("jumpProb");
    pFilterFreq = apvts.getRawParameterValue("filterFreq");
    pFilterQ = apvts.getRawParameterValue("filterQ");
    pFilterAtt = apvts.getRawParameterValue("filterAtt");
    pFilterDec = apvts.getRawParameterValue("filterDec");
    pFilterSus = apvts.getRawParameterValue("filterSus");
    pFilterRel = apvts.getRawParameterValue("filterRel");
    pFilterEnvAmt = apvts.getRawParameterValue("filterEnvAmt");
    pFilterLFODep = apvts.getRawParameterValue("filterLFODep");
    pOscMix = apvts.getRawParameterValue("oscMix");
    pSpread = apvts.getRawParameterValue("spread");
    pStereoWidth = apvts.getRawParameterValue("stereoWidth");
    pUniDetune = apvts.getRawParameterValue("uniDetune");
    pStereoPhase = apvts.getRawParameterValue("stereoPhase");
    pChorusRate = apvts.getRawParameterValue("chorusRate");
    pChorusDepth = apvts.getRawParameterValue("chorusDepth");
    pRingMod = apvts.getRawParameterValue("ringMod");
    pReverbSize = apvts.getRawParameterValue("reverbSize");
    pReverbDamp = apvts.getRawParameterValue("reverbDamp");
    pReverbWet = apvts.getRawParameterValue("reverbWet");
    // New params
    pAttack2 = apvts.getRawParameterValue("attack2");
    pDecay2 = apvts.getRawParameterValue("decay2");
    pSustain2 = apvts.getRawParameterValue("sustain2");
    pRelease2 = apvts.getRawParameterValue("release2");
    pOctaveA = apvts.getRawParameterValue("octaveA");
    pOctaveB = apvts.getRawParameterValue("octaveB");
    pGlide = apvts.getRawParameterValue("glide");
    pMono = apvts.getRawParameterValue("mono");
    pNoise = apvts.getRawParameterValue("noiseLevel");
    pNoiseFreq = apvts.getRawParameterValue("noiseFreq");
    pNoiseQ = apvts.getRawParameterValue("noiseQ");
    pNoiseFilterType = apvts.getRawParameterValue("noiseFilterType");
    pFrameInterp = apvts.getRawParameterValue("frameInterp");
    pVelToFrame = apvts.getRawParameterValue("velToFrame");
    pEvoPhaseCarry = apvts.getRawParameterValue("evoPhaseCarry");
    pTwAtt = apvts.getRawParameterValue("twAtt");
    pTwDec = apvts.getRawParameterValue("twDec");
    pTwSus = apvts.getRawParameterValue("twSus");
    pTwRel = apvts.getRawParameterValue("twRel");
    pTwAmt = apvts.getRawParameterValue("twAmt");
    pTwVelAmt = apvts.getRawParameterValue("twVelAmt");
    pAmpVelAmt = apvts.getRawParameterValue("ampVelAmt");
    pFrameSnap = apvts.getRawParameterValue("frameSnap");
    pTwToFilter = apvts.getRawParameterValue("twToFilter");
    pEvoBPhaseOff = apvts.getRawParameterValue("evoBPhaseOff");
    pEvoPhaseOff = apvts.getRawParameterValue("evoPhaseOff");
    pEvoSlew = apvts.getRawParameterValue("evoSlew");
    pEvoSlewB = apvts.getRawParameterValue("evoSlewB");
    pKeytrack = apvts.getRawParameterValue("keytrack");
    pEvoTimeB = apvts.getRawParameterValue("evoTimeB");
    pEvoRestart = apvts.getRawParameterValue("evoRestart");
    pWtSmooth   = apvts.getRawParameterValue("wtSmooth");
    pWtSmoothB  = apvts.getRawParameterValue("wtSmoothB");

    // Phizmo remake parameter pointers
    pTuneA = apvts.getRawParameterValue("tuneA"); pTuneB = apvts.getRawParameterValue("tuneB");
    pFineA = apvts.getRawParameterValue("fineA"); pFineB = apvts.getRawParameterValue("fineB");
    pWaveStartA = apvts.getRawParameterValue("waveStartA"); pWaveStartB = apvts.getRawParameterValue("waveStartB");
    pWaveModA   = apvts.getRawParameterValue("waveModA");   pWaveModB   = apvts.getRawParameterValue("waveModB");
    pLevelA = apvts.getRawParameterValue("levelA"); pLevelB = apvts.getRawParameterValue("levelB");
    pPanA   = apvts.getRawParameterValue("panA");   pPanB   = apvts.getRawParameterValue("panB");
    pFilterKeytrack = apvts.getRawParameterValue("filterKeytrack");
    pFilterType     = apvts.getRawParameterValue("filterType");
    pLfoShape = apvts.getRawParameterValue("lfoShape");
    pLfoSpeed = apvts.getRawParameterValue("lfoSpeed");
    pLfoSync   = apvts.getRawParameterValue("lfoSync");
    pNoiseRate = apvts.getRawParameterValue("noiseRate");
    pNoiseSync = apvts.getRawParameterValue("noiseSync");
    pVelCurve  = apvts.getRawParameterValue("velCurve");
    pModSelect = apvts.getRawParameterValue("modSelect");
    pFizF = apvts.getRawParameterValue("phizF"); pFizI = apvts.getRawParameterValue("phizI");
    pFizZ = apvts.getRawParameterValue("phizZ"); pFizM = apvts.getRawParameterValue("phizM");
    pFizO = apvts.getRawParameterValue("phizO"); pFizODest = apvts.getRawParameterValue("phizODest");
    pArpOn = apvts.getRawParameterValue("arpOn"); pArpMode = apvts.getRawParameterValue("arpMode");
    pArpRange = apvts.getRawParameterValue("arpRange"); pArpRate = apvts.getRawParameterValue("arpRate");
    pArpKeyboard = apvts.getRawParameterValue("arpKeyboard");
    pTempo = apvts.getRawParameterValue("tempo"); pArpGate = apvts.getRawParameterValue("arpGate");
    pEnvMode = apvts.getRawParameterValue("envMode");
    for (int s = 0; s < NUM_SOUNDS; ++s)
    {
        juce::String si(s);
        pSoundOn[s]   = apvts.getRawParameterValue("soundOn"   + si);
        pSoundLow[s]  = apvts.getRawParameterValue("soundLow"  + si);
        pSoundHigh[s] = apvts.getRawParameterValue("soundHigh" + si);
        pSoundGain[s] = apvts.getRawParameterValue("soundGain" + si);
        pFxBusA[s] = apvts.getRawParameterValue("fxBusA" + si);
        pFxBusB[s] = apvts.getRawParameterValue("fxBusB" + si);
    }
    pFxAlgo       = apvts.getRawParameterValue("fxAlgo");
    pFxVariation  = apvts.getRawParameterValue("fxVariation");
    pFxMix        = apvts.getRawParameterValue("fxMix");
    pRevVariation = apvts.getRawParameterValue("revVariation");
    pRevAmount    = apvts.getRawParameterValue("revAmount");
    pBendRange    = apvts.getRawParameterValue("bendRange");
    pMod_waveModSrcA = apvts.getRawParameterValue("waveModSrcA");
    pMod_waveModSrcB = apvts.getRawParameterValue("waveModSrcB");
    pMod_pitchModSrcA = apvts.getRawParameterValue("pitchModSrcA");
    pMod_pitchModSrcB = apvts.getRawParameterValue("pitchModSrcB");
    pMod_pitchModAmtA = apvts.getRawParameterValue("pitchModAmtA");
    pMod_pitchModAmtB = apvts.getRawParameterValue("pitchModAmtB");
    pMod_filtModSrcA = apvts.getRawParameterValue("filtModSrcA");
    pMod_filtModSrcB = apvts.getRawParameterValue("filtModSrcB");
    pMod_filtModAmtA = apvts.getRawParameterValue("filtModAmtA");
    pMod_filtModAmtB = apvts.getRawParameterValue("filtModAmtB");
    pOscAOn       = apvts.getRawParameterValue("oscAOn");
    pOscBOn       = apvts.getRawParameterValue("oscBOn");

    std::fill(voiceEvoLFOPhase, voiceEvoLFOPhase + MAX_VOICES, 0.0);
    std::fill(voicePosLFOPhase, voicePosLFOPhase + MAX_VOICES, 0.0);

    // Without these, the evoPoint_XX / evoPointB_XX parameters can be changed
    // by host automation (or by loading a project) but parameterChanged()
    // would never be called, so the evoCurve[]/evoCurveB[] atomics used by
    // both synthesis and the curve-editor GUI would silently go stale.
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        apvts.addParameterListener("evoPoint_" + juce::String::formatted("%02d", i), this);
        apvts.addParameterListener("evoPointB_" + juce::String::formatted("%02d", i), this);
    }
}

PhizmoAudioProcessor::~PhizmoAudioProcessor()
{
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        apvts.removeParameterListener("evoPoint_" + juce::String::formatted("%02d", i), this);
        apvts.removeParameterListener("evoPointB_" + juce::String::formatted("%02d", i), this);
    }
}

//==============================================================================
void PhizmoAudioProcessor::setCurvePoint(int idx, float val, int osc)
{
    idx = juce::jlimit(0, EVO_POINTS - 1, idx);
    val = juce::jlimit(0.0f, 1.0f, val);
    auto* arr = (osc == 0) ? evoCurve : evoCurveB;
    arr[idx].store(val);
    juce::String id = (osc == 0 ? juce::String("evoPoint_") : juce::String("evoPointB_"))
        + juce::String::formatted("%02d", idx);
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(val);
}

float PhizmoAudioProcessor::evalCurve(float t, int osc) const
{
    t = juce::jlimit(0.0f, 1.0f, t);
    float fi = t * (float)(EVO_POINTS - 1);
    int i0 = juce::jlimit(0, EVO_POINTS - 1, (int)fi);
    int i1 = juce::jlimit(0, EVO_POINTS - 1, i0 + 1);
    float frac = fi - (float)i0;
    const auto& arr = (osc == 0) ? evoCurve : evoCurveB;
    float v0 = arr[i0].load(), v1 = arr[i1].load();
    auto* steppedParam = (osc == 0) ? pEvoStepped : pEvoSteppedB;
    if (steppedParam && steppedParam->load() > 0.5f)
        return v0;
    return v0 + frac * (v1 - v0);
}

// slot = sound * 2 + oscillator. Evaluate that Sound's own curve at that
// oscillator's own scan position — the previous version passed the raw slot
// index straight in as the oscillator number, so every slot above 1 read
// oscillator B's curve from the edit buffer regardless of which Sound it was.
float PhizmoAudioProcessor::getCurrentEvoFramePos(int slot) const
{
    // The true frame position (post-modulation) — for the wavetable viewer.
    slot = juce::jlimit(0, NUM_SOUNDS * 2 - 1, slot);
    return evoFrameSlot[slot].load();
}

float PhizmoAudioProcessor::getCurrentScanPhase(int slot) const
{
    // The raw scan phase along the curve (0-1) — for the evolution editor dot.
    slot = juce::jlimit(0, NUM_SOUNDS * 2 - 1, slot);
    return evoScanSlot[slot].load();
}

//==============================================================================
void PhizmoAudioProcessor::prepareToPlay(double sr, int samplesPerBlockExpected)
{
    currentSampleRate = sr;
    gainSmooth.reset(sr, 0.05);
    gainSmooth.setCurrentAndTargetValue(pGain->load());

    pitchLFOPhase = 0.0;
    std::fill(voiceEvoLFOPhase, voiceEvoLFOPhase + MAX_VOICES, 0.0);
    std::fill(voicePosLFOPhase, voicePosLFOPhase + MAX_VOICES, 0.0);

    // No note is sounding yet → every per-voice playhead starts hidden (-1).
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voicePubSound[i].store(-1);
        voicePubScanA[i].store(0.f);
        voicePubScanB[i].store(0.f);
    }
    // FIX-mode master scans start at the beginning, moving forward.
    for (int s = 0; s < NUM_SOUNDS; ++s)
        for (int o = 0; o < 2; ++o)
        {
            evoFixPhase[s][o] = 0.0;
            evoFixDir[s][o]   = 1;
            evoFixPub[s][o].store(0.f);
        }

    chorus.reset(); reverb.reset(); noiseFilter.reset();

    // Phizmo effects
    if (!soundsInitialised) { initialiseAllSounds(); soundsInitialised = true; }
    fxBusBuf.setSize(4, juce::jmax(16, samplesPerBlockExpected));
    fxBusBuf.clear();
    phizReverb.prepare(sr);
    phizInsert.prepare(sr);
    lastRevVariation = -1;

    for (auto& v : voices)
    {
        v.active = false;
        v.envStage = PhizmoVoice::Env::Idle;
        v.fenvStage = PhizmoVoice::Env::Idle;
        v.twenvStage = PhizmoVoice::Env::Idle;
        v.envLevel = v.fenvLevel = v.twenvLevel = 0.f;
        v.phaseA = v.phaseB = 0.0;
        v.curvePhase = 0.0; v.curvePhaseB = 0.0;
        v.scanDir = 1; v.scanDirB = 1;
        v.curveFinished = false; v.curveFinishedB = false;
        v.frameOffset = 0.f;
    }
}

void PhizmoAudioProcessor::releaseResources() {}

//==============================================================================
void PhizmoAudioProcessor::loadWavetable(const juce::File& file, int cs, int slot)
{
    jassert(slot >= 0 && slot < NUM_SOUNDS * 2);
    juce::MemoryBlock raw;
    if (!file.loadFileAsData(raw)) return;

    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
    if (!reader) return;

    loadWavetableFromReader(std::move(reader), std::move(raw), file.getFileName(),
        file.getFullPathName(), cs, slot);
}

bool PhizmoAudioProcessor::loadWavetableFromMemory(const juce::MemoryBlock& fileData,
    const juce::String& originalFileName, int cs, int slot)
{
    jassert(slot >= 0 && slot < NUM_SOUNDS * 2);
    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    auto mis = std::make_unique<juce::MemoryInputStream>(fileData, false);
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(std::move(mis)));
    if (!reader) return false;

    return loadWavetableFromReader(std::move(reader), fileData, originalFileName,
        juce::String(), cs, slot);
}

bool PhizmoAudioProcessor::loadWavetableFromReader(std::unique_ptr<juce::AudioFormatReader> reader,
    juce::MemoryBlock originalBytes, const juce::String& originalFileName,
    const juce::String& displayFilePath, int cs, int slot)
{
    if (!reader) return false;

    juce::AudioBuffer<float> buf(1, (int)reader->lengthInSamples);
    reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, true);
    int total = buf.getNumSamples();
    cs = juce::jlimit(16, juce::jmax(16, total), cs);
    int nf = total / cs; if (nf < 1) return false;

    auto q16 = [](float s) { return juce::jlimit(-1.f, 1.f, std::round(s * 32767.f) / 32767.f); };
    std::vector<std::vector<float>> frames((size_t)nf, std::vector<float>((size_t)cs));
    const float* src = buf.getReadPointer(0);
    for (int f = 0; f < nf; ++f)
        for (int s = 0; s < cs; ++s)
            frames[(size_t)f][(size_t)s] = q16(src[f * cs + s]);

    {
        juce::ScopedLock sl(wt[slot].lock);
        wt[slot].frames = std::move(frames); wt[slot].numFrames = nf;
        wt[slot].cycleSamples = cs; wt[slot].loaded = true;
        wt[slot].name = juce::File(originalFileName).getFileNameWithoutExtension();
        if (wt[slot].name.isEmpty()) wt[slot].name = originalFileName;
        wt[slot].filePath = displayFilePath;
        wt[slot].fileName = originalFileName;
        wt[slot].originalFileData = std::move(originalBytes);
    }

    for (auto& v : voices) { v.active = false; v.envStage = PhizmoVoice::Env::Idle; }
    return true;
}

bool         PhizmoAudioProcessor::isWavetableLoaded(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].loaded; }
int          PhizmoAudioProcessor::getNumFrames(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].numFrames; }
int          PhizmoAudioProcessor::getCycleSamples(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].cycleSamples; }
juce::String PhizmoAudioProcessor::getWavetableName(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].name; }
juce::String PhizmoAudioProcessor::getWavetableFilePath(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].filePath; }

bool PhizmoAudioProcessor::getFrameSamples(int slot, int fi, std::vector<float>& o) const {
    juce::ScopedLock l(wt[slot].lock);
    if (wt[slot].frames.empty() || fi < 0 || fi >= wt[slot].numFrames) return false;
    o = wt[slot].frames[(size_t)fi]; return true;
}

bool PhizmoAudioProcessor::getWavetableOverview(int slot, int dw, int, std::vector<float>& o) const {
    juce::ScopedLock l(wt[slot].lock);
    if (wt[slot].frames.empty() || dw <= 0) return false;
    o.resize((size_t)dw);
    for (int px = 0; px < dw; ++px) {
        float t = (float)px / (float)(dw - 1);
        int f0 = juce::jlimit(0, wt[slot].numFrames - 1, (int)(t * (wt[slot].numFrames - 1)));
        float pk = 0.f; for (float s : wt[slot].frames[(size_t)f0]) pk = juce::jmax(pk, std::abs(s));
        o[(size_t)px] = pk;
    }
    return true;
}

//==============================================================================
float PhizmoAudioProcessor::sampleFrameRaw(const WavetableSlot& s, float fi, double ph, bool interp, float smoothAmt) const {
    if (s.frames.empty()) return 0.f;
    int i0 = juce::jlimit(0, s.numFrames - 1, (int)fi);
    int i1 = juce::jlimit(0, s.numFrames - 1, i0 + 1);
    float bl = fi - (float)i0;

    float result;
    if (interp)
    {
        // Bilinear: interpolate within the cycle as well as between frames
        float fsi = (float)(ph * s.cycleSamples);
        int si0 = (int)fsi % s.cycleSamples;
        int si1 = (si0 + 1) % s.cycleSamples;
        float sfrac = fsi - (float)(int)fsi;
        float smp0 = s.frames[(size_t)i0][(size_t)si0] + sfrac * (s.frames[(size_t)i0][(size_t)si1] - s.frames[(size_t)i0][(size_t)si0]);
        float smp1 = s.frames[(size_t)i1][(size_t)si0] + sfrac * (s.frames[(size_t)i1][(size_t)si1] - s.frames[(size_t)i1][(size_t)si0]);
        result = smp0 + bl * (smp1 - smp0);
    }
    else
    {
        // Original nearest-neighbour within cycle, linear between frames
        int si = juce::jlimit(0, s.cycleSamples - 1, (int)(ph * s.cycleSamples) % s.cycleSamples);
        result = s.frames[(size_t)i0][(size_t)si] + bl * (s.frames[(size_t)i1][(size_t)si] - s.frames[(size_t)i0][(size_t)si]);
    }

    // --- Cycle-edge smoothing: brief linear fade-in/out up to 15% of cycle length ---
    // Avoids clicks when the user enters a cycle size that doesn't match the content.
    if (smoothAmt > 0.001f)
    {
        float fadeLen = smoothAmt * 0.15f;
        float env = 1.f;
        if (ph < (double)fadeLen)
            env = (float)(ph / (double)fadeLen);                 // fade in  [0 → fadeLen]
        else if (ph > 1.0 - (double)fadeLen)
            env = (float)((1.0 - ph) / (double)fadeLen);        // fade out [1-fadeLen → 1]
        result *= env;
    }

    return result;
}

float PhizmoAudioProcessor::sampleFrameNearest(int slot, float fi, double ph) {
    juce::ScopedLock l(wt[slot].lock); return sampleFrameRaw(wt[slot], fi, ph, false, 0.f);
}

float PhizmoAudioProcessor::applyBitCrush(float s, float bits) {
    if (bits >= 15.9f) return s;
    float lv = std::pow(2.f, bits) - 1.f; return std::round(s * lv) / lv;
}

//==============================================================================
// Exponential envelope helper: returns new level after one sample.
// decCoeff / relCoeff are precomputed as std::exp(-isr * 5 / time) once per block.
// envMode: 0 = Normal, 1 = Finish (runs through all stages, ignores note-off),
//          2 = Repeat (loops back to Attack instead of sustaining)
static inline float advanceEnvExp(float level, float targetSus, PhizmoVoice::Env stage,
    float att, float sus,
    float decCoeff, float relCoeff,
    float isr,
    PhizmoVoice::Env& nextStage, float& nextReleaseStart, int envMode = 0)
{
    const float kFloor = 1e-4f;
    switch (stage)
    {
    case PhizmoVoice::Env::Attack:
        level += isr / att;
        if (level >= 1.0f) { level = 1.0f; nextStage = PhizmoVoice::Env::Decay; }
        break;
    case PhizmoVoice::Env::Decay:
        level = sus + (level - sus) * decCoeff;
        if (level <= sus + kFloor)
        {
            level = sus;
            if (envMode == 2)      { nextStage = PhizmoVoice::Env::Attack; level = 0.f; }
            else if (envMode == 1) { nextStage = PhizmoVoice::Env::Release; }
            else                     nextStage = PhizmoVoice::Env::Sustain;
        }
        break;
    case PhizmoVoice::Env::Sustain:
        level = sus;
        break;
    case PhizmoVoice::Env::Release:
        level = level * relCoeff;
        if (level <= kFloor) { level = 0.f; nextStage = PhizmoVoice::Env::Idle; }
        break;
    case PhizmoVoice::Env::Idle: level = 0.f; break;
    }
    (void)targetSus; (void)nextReleaseStart;
    return level;
}


//==============================================================================
// the original PRESET STRUCTURE — four Sounds, one edit buffer
//
// The live APVTS parameters are the *edit buffer* for whichever Sound is in
// focus. captureEditBuffer() freezes it into that Sound's snapshot (called
// every block, so edits and host automation are picked up); applySound-
// ToEditBuffer() does the reverse when the user selects a different Sound.
//==============================================================================
void PhizmoAudioProcessor::captureEditBuffer(int sound)
{
    if (sound < 0 || sound >= NUM_SOUNDS) return;
    SoundParams& sp = soundParams[sound];
    sp.attack = pAttack ? pAttack->load() : 0.f;
    sp.attack2 = pAttack2 ? pAttack2->load() : 0.f;
    sp.bitCrush = pBitCrush ? pBitCrush->load() : 0.f;
    sp.decay = pDecay ? pDecay->load() : 0.f;
    sp.decay2 = pDecay2 ? pDecay2->load() : 0.f;
    sp.detune = pDetune ? pDetune->load() : 0.f;
    sp.evoBPhaseOff = pEvoBPhaseOff ? pEvoBPhaseOff->load() : 0.f;
    sp.evoPhaseOff = pEvoPhaseOff ? pEvoPhaseOff->load() : 0.f;
    sp.evoSlew = pEvoSlew ? pEvoSlew->load() : 4.f;
    sp.evoSlewB = pEvoSlewB ? pEvoSlewB->load() : 4.f;
    sp.evoLFODepth = pEvoLFODepth ? pEvoLFODepth->load() : 0.f;
    sp.evoLFORate = pEvoLFORate ? pEvoLFORate->load() : 0.f;
    sp.evoTime = pEvoTime ? pEvoTime->load() : 0.f;
    sp.evoTimeB = pEvoTimeB ? pEvoTimeB->load() : 0.f;
    sp.filterAtt = pFilterAtt ? pFilterAtt->load() : 0.f;
    sp.filterDec = pFilterDec ? pFilterDec->load() : 0.f;
    sp.filterEnvAmt = pFilterEnvAmt ? pFilterEnvAmt->load() : 0.f;
    sp.filterFreq = pFilterFreq ? pFilterFreq->load() : 0.f;
    sp.filterKeytrack = pFilterKeytrack ? pFilterKeytrack->load() : 0.f;
    sp.filterLFODep = pFilterLFODep ? pFilterLFODep->load() : 0.f;
    sp.filterQ = pFilterQ ? pFilterQ->load() : 0.f;
    sp.filterRel = pFilterRel ? pFilterRel->load() : 0.f;
    sp.filterSus = pFilterSus ? pFilterSus->load() : 0.f;
    sp.filterType = pFilterType ? pFilterType->load() : 0.f;
    sp.fineA = pFineA ? pFineA->load() : 0.f;
    sp.fineB = pFineB ? pFineB->load() : 0.f;
    sp.frameInterp = pFrameInterp ? pFrameInterp->load() : 0.f;
    sp.frameSnap = pFrameSnap ? pFrameSnap->load() : 0.f;
    sp.glide = pGlide ? pGlide->load() : 0.f;
    sp.grit = pGrit ? pGrit->load() : 0.f;
    sp.jumpProb = pJumpProb ? pJumpProb->load() : 0.f;
    sp.keytrack = pKeytrack ? pKeytrack->load() : 0.f;
    sp.levelA = pLevelA ? pLevelA->load() : 0.f;
    sp.levelB = pLevelB ? pLevelB->load() : 0.f;
    sp.lfoShape = pLfoShape ? pLfoShape->load() : 0.f;
    sp.evoStepped  = pEvoStepped  ? pEvoStepped->load()  : 0.f;
    sp.evoSteppedB = pEvoSteppedB ? pEvoSteppedB->load() : 0.f;
    sp.evoMode     = pEvoMode     ? pEvoMode->load()     : 0.f;
    sp.evoModeB    = pEvoModeB    ? pEvoModeB->load()    : 0.f;
    sp.scanStyleB  = pScanStyleB  ? pScanStyleB->load()  : 0.f;
    sp.lfoSpeed = pLfoSpeed ? pLfoSpeed->load() : 0.f;
    sp.lfoSync = pLfoSync ? pLfoSync->load() : 0.f;
    sp.noiseRate = pNoiseRate ? pNoiseRate->load() : 1.f;
    sp.noiseSync = pNoiseSync ? pNoiseSync->load() : 0.f;
    sp.octaveA = pOctaveA ? pOctaveA->load() : 0.f;
    sp.octaveB = pOctaveB ? pOctaveB->load() : 0.f;
    sp.oscMix = pOscMix ? pOscMix->load() : 0.f;
    sp.panA = pPanA ? pPanA->load() : 0.f;
    sp.panB = pPanB ? pPanB->load() : 0.f;
    sp.pitchEnvAmt = pPitchEnvAmt ? pPitchEnvAmt->load() : 0.f;
    sp.pitchEnvDec = pPitchEnvDec ? pPitchEnvDec->load() : 0.f;
    sp.posLFODepth = pPosLFODepth ? pPosLFODepth->load() : 0.f;
    sp.release = pRelease ? pRelease->load() : 0.f;
    sp.release2 = pRelease2 ? pRelease2->load() : 0.f;
    sp.ringMod = pRingMod ? pRingMod->load() : 0.f;
    sp.scanStyle = pScanStyle ? pScanStyle->load() : 0.f;
    sp.spread = pSpread ? pSpread->load() : 0.f;
    sp.stereoPhase = pStereoPhase ? pStereoPhase->load() : 0.f;
    sp.stereoWidth = pStereoWidth ? pStereoWidth->load() : 0.f;
    sp.sustain = pSustain ? pSustain->load() : 0.f;
    sp.sustain2 = pSustain2 ? pSustain2->load() : 0.f;
    sp.tuneA = pTuneA ? pTuneA->load() : 0.f;
    sp.tuneB = pTuneB ? pTuneB->load() : 0.f;
    sp.twAmt = pTwAmt ? pTwAmt->load() : 0.f;
    sp.twAtt = pTwAtt ? pTwAtt->load() : 0.f;
    sp.twDec = pTwDec ? pTwDec->load() : 0.f;
    sp.twRel = pTwRel ? pTwRel->load() : 0.f;
    sp.twSus = pTwSus ? pTwSus->load() : 0.f;
    sp.twToFilter = pTwToFilter ? pTwToFilter->load() : 0.f;
    sp.twVelAmt = pTwVelAmt ? pTwVelAmt->load() : 0.f;
    sp.ampVelAmt = pAmpVelAmt ? pAmpVelAmt->load() : 0.6f;
    sp.uniDetune = pUniDetune ? pUniDetune->load() : 0.f;
    sp.waveModA = pWaveModA ? pWaveModA->load() : 0.f;
    sp.waveModB = pWaveModB ? pWaveModB->load() : 0.f;
    sp.waveStartA = pWaveStartA ? pWaveStartA->load() : 0.f;
    sp.waveStartB = pWaveStartB ? pWaveStartB->load() : 0.f;
    sp.wtSmooth = pWtSmooth ? pWtSmooth->load() : 0.f;
    sp.wtSmoothB = pWtSmoothB ? pWtSmoothB->load() : 0.f;
    sp.waveModSrcA = pMod_waveModSrcA ? pMod_waveModSrcA->load() : sp.waveModSrcA;
    sp.waveModSrcB = pMod_waveModSrcB ? pMod_waveModSrcB->load() : sp.waveModSrcB;
    sp.pitchModSrcA = pMod_pitchModSrcA ? pMod_pitchModSrcA->load() : sp.pitchModSrcA;
    sp.pitchModSrcB = pMod_pitchModSrcB ? pMod_pitchModSrcB->load() : sp.pitchModSrcB;
    sp.pitchModAmtA = pMod_pitchModAmtA ? pMod_pitchModAmtA->load() : sp.pitchModAmtA;
    sp.pitchModAmtB = pMod_pitchModAmtB ? pMod_pitchModAmtB->load() : sp.pitchModAmtB;
    sp.filtModSrcA = pMod_filtModSrcA ? pMod_filtModSrcA->load() : sp.filtModSrcA;
    sp.filtModSrcB = pMod_filtModSrcB ? pMod_filtModSrcB->load() : sp.filtModSrcB;
    sp.filtModAmtA = pMod_filtModAmtA ? pMod_filtModAmtA->load() : sp.filtModAmtA;
    sp.filtModAmtB = pMod_filtModAmtB ? pMod_filtModAmtB->load() : sp.filtModAmtB;
    sp.oscAOn = pOscAOn ? pOscAOn->load() : 1.f;
    sp.oscBOn = pOscBOn ? pOscBOn->load() : 1.f;
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        sp.curveA[i] = evoCurve[i].load();
        sp.curveB[i] = evoCurveB[i].load();
    }
}


// Every Sound starts life as a copy of the default edit buffer. Without this,
// Sounds 2-4 hold an all-zero snapshot (level 0, zero-length envelopes) and are
// silent until they happen to be selected for editing.
void PhizmoAudioProcessor::initialiseAllSounds()
{
    captureEditBuffer(editSound);
    for (int i = 0; i < NUM_SOUNDS; ++i)
        if (i != editSound)
        {
            const bool en = soundParams[i].enabled;
            const int  lo = soundParams[i].lowKey, hi = soundParams[i].highKey;
            const float lg = soundParams[i].layerGain;
            soundParams[i] = soundParams[editSound];   // same patch...
            soundParams[i].enabled = en;               // ...but keep layer settings
            soundParams[i].lowKey = lo; soundParams[i].highKey = hi;
            soundParams[i].layerGain = lg;
        }
}

void PhizmoAudioProcessor::applySoundToEditBuffer(int sound)
{
    if (sound < 0 || sound >= NUM_SOUNDS) return;
    const SoundParams& sp = soundParams[sound];
    auto setP = [this](const juce::String& id, float v)
    {
        if (auto* pr = apvts.getParameter(id))
            pr->setValueNotifyingHost(pr->convertTo0to1(v));
    };
    setP("attack", sp.attack);
    setP("attack2", sp.attack2);
    setP("bitCrush", sp.bitCrush);
    setP("decay", sp.decay);
    setP("decay2", sp.decay2);
    setP("detune", sp.detune);
    setP("evoBPhaseOff", sp.evoBPhaseOff);
    setP("evoPhaseOff", sp.evoPhaseOff);
    setP("evoSlew", sp.evoSlew);
    setP("evoSlewB", sp.evoSlewB);
    setP("evoLFODepth", sp.evoLFODepth);
    setP("evoLFORate", sp.evoLFORate);
    setP("evoTime", sp.evoTime);
    setP("evoTimeB", sp.evoTimeB);
    setP("filterAtt", sp.filterAtt);
    setP("filterDec", sp.filterDec);
    setP("filterEnvAmt", sp.filterEnvAmt);
    setP("filterFreq", sp.filterFreq);
    setP("filterKeytrack", sp.filterKeytrack);
    setP("filterLFODep", sp.filterLFODep);
    setP("filterQ", sp.filterQ);
    setP("filterRel", sp.filterRel);
    setP("filterSus", sp.filterSus);
    setP("filterType", sp.filterType);
    setP("fineA", sp.fineA);
    setP("fineB", sp.fineB);
    setP("frameInterp", sp.frameInterp);
    setP("frameSnap", sp.frameSnap);
    setP("glide", sp.glide);
    setP("grit", sp.grit);
    setP("jumpProb", sp.jumpProb);
    setP("keytrack", sp.keytrack);
    setP("levelA", sp.levelA);
    setP("levelB", sp.levelB);
    setP("lfoShape", sp.lfoShape);
    setP("evoStepped", sp.evoStepped);
    setP("evoSteppedB", sp.evoSteppedB);
    setP("evoMode", sp.evoMode);
    setP("evoModeB", sp.evoModeB);
    setP("scanStyleB", sp.scanStyleB);
    setP("lfoSpeed", sp.lfoSpeed);
    setP("lfoSync", sp.lfoSync);
    setP("noiseRate", sp.noiseRate);
    setP("noiseSync", sp.noiseSync);
    setP("octaveA", sp.octaveA);
    setP("octaveB", sp.octaveB);
    setP("oscMix", sp.oscMix);
    setP("panA", sp.panA);
    setP("panB", sp.panB);
    setP("pitchEnvAmt", sp.pitchEnvAmt);
    setP("pitchEnvDec", sp.pitchEnvDec);
    setP("posLFODepth", sp.posLFODepth);
    setP("release", sp.release);
    setP("release2", sp.release2);
    setP("ringMod", sp.ringMod);
    setP("scanStyle", sp.scanStyle);
    setP("spread", sp.spread);
    setP("stereoPhase", sp.stereoPhase);
    setP("stereoWidth", sp.stereoWidth);
    setP("sustain", sp.sustain);
    setP("sustain2", sp.sustain2);
    setP("tuneA", sp.tuneA);
    setP("tuneB", sp.tuneB);
    setP("twAmt", sp.twAmt);
    setP("twAtt", sp.twAtt);
    setP("twDec", sp.twDec);
    setP("twRel", sp.twRel);
    setP("twSus", sp.twSus);
    setP("twToFilter", sp.twToFilter);
    setP("twVelAmt", sp.twVelAmt);
    setP("ampVelAmt", sp.ampVelAmt);
    setP("uniDetune", sp.uniDetune);
    setP("waveModA", sp.waveModA);
    setP("waveModB", sp.waveModB);
    setP("waveStartA", sp.waveStartA);
    setP("waveStartB", sp.waveStartB);
    setP("wtSmooth", sp.wtSmooth);
    setP("wtSmoothB", sp.wtSmoothB);
    setP("waveModSrcA", sp.waveModSrcA);
    setP("waveModSrcB", sp.waveModSrcB);
    setP("pitchModSrcA", sp.pitchModSrcA);
    setP("pitchModSrcB", sp.pitchModSrcB);
    setP("pitchModAmtA", sp.pitchModAmtA);
    setP("pitchModAmtB", sp.pitchModAmtB);
    setP("filtModSrcA", sp.filtModSrcA);
    setP("filtModSrcB", sp.filtModSrcB);
    setP("filtModAmtA", sp.filtModAmtA);
    setP("filtModAmtB", sp.filtModAmtB);
    setP("oscAOn", sp.oscAOn);
    setP("oscBOn", sp.oscBOn);
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        setP("evoPoint_"  + juce::String::formatted("%02d", i), sp.curveA[i]);
        setP("evoPointB_" + juce::String::formatted("%02d", i), sp.curveB[i]);
        evoCurve[i].store(sp.curveA[i]);
        evoCurveB[i].store(sp.curveB[i]);
    }
}

void PhizmoAudioProcessor::setEditSound(int s)
{
    s = juce::jlimit(0, NUM_SOUNDS - 1, s);
    if (s == editSound) return;
    soundSwapInProgress.store(true);  // pause the per-block capture during the swap
    captureEditBuffer(editSound);     // freeze what the knobs currently show
    editSound = s;
    applySoundToEditBuffer(s);        // and load the newly selected Sound
    soundSwapInProgress.store(false);
}

bool PhizmoAudioProcessor::isSoundEnabled(int s) const
{
    if (s < 0 || s >= NUM_SOUNDS) return false;
    if (auto* p = apvts.getRawParameterValue("soundOn" + juce::String(s)))
        return p->load() > 0.5f;
    return s == 0;
}

// Curve evaluation against a Sound's own stored curve (not the edit buffer).
float PhizmoAudioProcessor::evalCurveS(float t, int osc, const SoundParams& sp) const
{
    t = juce::jlimit(0.f, 1.f, t);
    const float* arr = (osc == 0) ? sp.curveA : sp.curveB;
    float fi = t * (float)(EVO_POINTS - 1);
    int i0 = (int)fi;
    int i1 = juce::jmin(i0 + 1, EVO_POINTS - 1);
    float fr = fi - (float)i0;
    // The stepped flag lives in the Sound snapshot. It used to be fetched with
    // apvts.getRawParameterValue(), a string lookup, once per oscillator per
    // sample — by far the most expensive thing in the render loop.
    if ((osc == 0 ? sp.evoStepped : sp.evoSteppedB) > 0.5f) return arr[i0];
    return arr[i0] + (arr[i1] - arr[i0]) * fr;
}


//==============================================================================
// Serialise the four Sound snapshots. The focused Sound also lives in the
// APVTS (the edit buffer), but the other three exist only here — without this
// they would be lost on reload.
//==============================================================================
juce::XmlElement* PhizmoAudioProcessor::createSoundsXml() const
{
    auto* root = new juce::XmlElement("SOUNDS");
    root->setAttribute("editSound", editSound);
    for (int i = 0; i < NUM_SOUNDS; ++i)
    {
        const SoundParams& sp = soundParams[i];
        auto* e = root->createNewChildElement("SOUND");
        e->setAttribute("index", i);
        e->setAttribute("enabled", sp.enabled ? 1 : 0);
        e->setAttribute("lowKey", sp.lowKey);
        e->setAttribute("highKey", sp.highKey);
        e->setAttribute("layerGain", (double)sp.layerGain);
        e->setAttribute("waveModSrcA", (double)sp.waveModSrcA);
        e->setAttribute("waveModSrcB", (double)sp.waveModSrcB);
        e->setAttribute("pitchModSrcA", (double)sp.pitchModSrcA);
        e->setAttribute("pitchModSrcB", (double)sp.pitchModSrcB);
        e->setAttribute("pitchModAmtA", (double)sp.pitchModAmtA);
        e->setAttribute("pitchModAmtB", (double)sp.pitchModAmtB);
        e->setAttribute("filtModSrcA", (double)sp.filtModSrcA);
        e->setAttribute("filtModSrcB", (double)sp.filtModSrcB);
        e->setAttribute("filtModAmtA", (double)sp.filtModAmtA);
        e->setAttribute("filtModAmtB", (double)sp.filtModAmtB);
        e->setAttribute("oscAOn", (double)sp.oscAOn);
        e->setAttribute("oscBOn", (double)sp.oscBOn);
        e->setAttribute("attack", (double)sp.attack);
        e->setAttribute("attack2", (double)sp.attack2);
        e->setAttribute("bitCrush", (double)sp.bitCrush);
        e->setAttribute("decay", (double)sp.decay);
        e->setAttribute("decay2", (double)sp.decay2);
        e->setAttribute("detune", (double)sp.detune);
        e->setAttribute("evoBPhaseOff", (double)sp.evoBPhaseOff);
        e->setAttribute("evoPhaseOff", (double)sp.evoPhaseOff);
        e->setAttribute("evoSlew", (double)sp.evoSlew);
        e->setAttribute("evoSlewB", (double)sp.evoSlewB);
        e->setAttribute("evoLFODepth", (double)sp.evoLFODepth);
        e->setAttribute("evoLFORate", (double)sp.evoLFORate);
        e->setAttribute("evoTime", (double)sp.evoTime);
        e->setAttribute("evoTimeB", (double)sp.evoTimeB);
        e->setAttribute("filterAtt", (double)sp.filterAtt);
        e->setAttribute("filterDec", (double)sp.filterDec);
        e->setAttribute("filterEnvAmt", (double)sp.filterEnvAmt);
        e->setAttribute("filterFreq", (double)sp.filterFreq);
        e->setAttribute("filterKeytrack", (double)sp.filterKeytrack);
        e->setAttribute("filterLFODep", (double)sp.filterLFODep);
        e->setAttribute("filterQ", (double)sp.filterQ);
        e->setAttribute("filterRel", (double)sp.filterRel);
        e->setAttribute("filterSus", (double)sp.filterSus);
        e->setAttribute("filterType", (double)sp.filterType);
        e->setAttribute("fineA", (double)sp.fineA);
        e->setAttribute("fineB", (double)sp.fineB);
        e->setAttribute("frameInterp", (double)sp.frameInterp);
        e->setAttribute("frameSnap", (double)sp.frameSnap);
        e->setAttribute("glide", (double)sp.glide);
        e->setAttribute("grit", (double)sp.grit);
        e->setAttribute("jumpProb", (double)sp.jumpProb);
        e->setAttribute("keytrack", (double)sp.keytrack);
        e->setAttribute("levelA", (double)sp.levelA);
        e->setAttribute("levelB", (double)sp.levelB);
        e->setAttribute("evoStepped", (double)sp.evoStepped);
        e->setAttribute("evoSteppedB", (double)sp.evoSteppedB);
        e->setAttribute("evoMode", (double)sp.evoMode);
        e->setAttribute("evoModeB", (double)sp.evoModeB);
        e->setAttribute("scanStyleB", (double)sp.scanStyleB);
        e->setAttribute("lfoShape", (double)sp.lfoShape);
        e->setAttribute("lfoSync", (double)sp.lfoSync);
        e->setAttribute("noiseRate", (double)sp.noiseRate);
        e->setAttribute("noiseSync", (double)sp.noiseSync);
        e->setAttribute("lfoSpeed", (double)sp.lfoSpeed);
        e->setAttribute("octaveA", (double)sp.octaveA);
        e->setAttribute("octaveB", (double)sp.octaveB);
        e->setAttribute("oscMix", (double)sp.oscMix);
        e->setAttribute("panA", (double)sp.panA);
        e->setAttribute("panB", (double)sp.panB);
        e->setAttribute("pitchEnvAmt", (double)sp.pitchEnvAmt);
        e->setAttribute("pitchEnvDec", (double)sp.pitchEnvDec);
        e->setAttribute("posLFODepth", (double)sp.posLFODepth);
        e->setAttribute("release", (double)sp.release);
        e->setAttribute("release2", (double)sp.release2);
        e->setAttribute("ringMod", (double)sp.ringMod);
        e->setAttribute("scanStyle", (double)sp.scanStyle);
        e->setAttribute("spread", (double)sp.spread);
        e->setAttribute("stereoPhase", (double)sp.stereoPhase);
        e->setAttribute("stereoWidth", (double)sp.stereoWidth);
        e->setAttribute("sustain", (double)sp.sustain);
        e->setAttribute("sustain2", (double)sp.sustain2);
        e->setAttribute("tuneA", (double)sp.tuneA);
        e->setAttribute("tuneB", (double)sp.tuneB);
        e->setAttribute("twAmt", (double)sp.twAmt);
        e->setAttribute("twAtt", (double)sp.twAtt);
        e->setAttribute("twDec", (double)sp.twDec);
        e->setAttribute("twRel", (double)sp.twRel);
        e->setAttribute("twSus", (double)sp.twSus);
        e->setAttribute("twToFilter", (double)sp.twToFilter);
        e->setAttribute("twVelAmt", (double)sp.twVelAmt);
        e->setAttribute("ampVelAmt", (double)sp.ampVelAmt);
        e->setAttribute("uniDetune", (double)sp.uniDetune);
        e->setAttribute("waveModA", (double)sp.waveModA);
        e->setAttribute("waveModB", (double)sp.waveModB);
        e->setAttribute("waveStartA", (double)sp.waveStartA);
        e->setAttribute("waveStartB", (double)sp.waveStartB);
        e->setAttribute("wtSmooth", (double)sp.wtSmooth);
        e->setAttribute("wtSmoothB", (double)sp.wtSmoothB);
        juce::String ca, cb;
        for (int k = 0; k < EVO_POINTS; ++k)
        {
            ca += juce::String(sp.curveA[k], 5) + (k < EVO_POINTS - 1 ? "," : "");
            cb += juce::String(sp.curveB[k], 5) + (k < EVO_POINTS - 1 ? "," : "");
        }
        e->setAttribute("curveA", ca);
        e->setAttribute("curveB", cb);
    }
    return root;
}

void PhizmoAudioProcessor::restoreSoundsXml(const juce::XmlElement* root)
{
    if (root == nullptr) return;
    int es = root->getIntAttribute("editSound", 0);
    for (auto* e : root->getChildWithTagNameIterator("SOUND"))
    {
        int i = e->getIntAttribute("index", -1);
        if (i < 0 || i >= NUM_SOUNDS) continue;
        SoundParams& sp = soundParams[i];
        sp.enabled  = e->getIntAttribute("enabled", i == 0 ? 1 : 0) != 0;
        sp.lowKey   = e->getIntAttribute("lowKey", 0);
        sp.highKey  = e->getIntAttribute("highKey", 127);
        sp.layerGain = (float)e->getDoubleAttribute("layerGain", 1.0);
        sp.waveModSrcA = (float)e->getDoubleAttribute("waveModSrcA", (double)sp.waveModSrcA);
        sp.waveModSrcB = (float)e->getDoubleAttribute("waveModSrcB", (double)sp.waveModSrcB);
        sp.pitchModSrcA = (float)e->getDoubleAttribute("pitchModSrcA", (double)sp.pitchModSrcA);
        sp.pitchModSrcB = (float)e->getDoubleAttribute("pitchModSrcB", (double)sp.pitchModSrcB);
        sp.pitchModAmtA = (float)e->getDoubleAttribute("pitchModAmtA", (double)sp.pitchModAmtA);
        sp.pitchModAmtB = (float)e->getDoubleAttribute("pitchModAmtB", (double)sp.pitchModAmtB);
        sp.filtModSrcA = (float)e->getDoubleAttribute("filtModSrcA", (double)sp.filtModSrcA);
        sp.filtModSrcB = (float)e->getDoubleAttribute("filtModSrcB", (double)sp.filtModSrcB);
        sp.filtModAmtA = (float)e->getDoubleAttribute("filtModAmtA", (double)sp.filtModAmtA);
        sp.filtModAmtB = (float)e->getDoubleAttribute("filtModAmtB", (double)sp.filtModAmtB);
        sp.oscAOn = (float)e->getDoubleAttribute("oscAOn", 1.0);
        sp.oscBOn = (float)e->getDoubleAttribute("oscBOn", 1.0);
        sp.attack = (float)e->getDoubleAttribute("attack", (double)sp.attack);
        sp.attack2 = (float)e->getDoubleAttribute("attack2", (double)sp.attack2);
        sp.bitCrush = (float)e->getDoubleAttribute("bitCrush", (double)sp.bitCrush);
        sp.decay = (float)e->getDoubleAttribute("decay", (double)sp.decay);
        sp.decay2 = (float)e->getDoubleAttribute("decay2", (double)sp.decay2);
        sp.detune = (float)e->getDoubleAttribute("detune", (double)sp.detune);
        // Offsets default to 0 when absent so loading a preset that predates
        // the OFS knobs (or leaves them at zero) actually resets them, rather
        // than keeping whatever the knob happened to be on.
        sp.evoBPhaseOff = (float)e->getDoubleAttribute("evoBPhaseOff", 0.0);
        sp.evoPhaseOff = (float)e->getDoubleAttribute("evoPhaseOff", 0.0);
        sp.evoSlew = (float)e->getDoubleAttribute("evoSlew", 4.0);
        sp.evoSlewB = (float)e->getDoubleAttribute("evoSlewB", 4.0);
        sp.evoLFODepth = (float)e->getDoubleAttribute("evoLFODepth", (double)sp.evoLFODepth);
        sp.evoLFORate = (float)e->getDoubleAttribute("evoLFORate", (double)sp.evoLFORate);
        sp.evoTime = (float)e->getDoubleAttribute("evoTime", (double)sp.evoTime);
        sp.evoTimeB = (float)e->getDoubleAttribute("evoTimeB", (double)sp.evoTimeB);
        sp.filterAtt = (float)e->getDoubleAttribute("filterAtt", (double)sp.filterAtt);
        sp.filterDec = (float)e->getDoubleAttribute("filterDec", (double)sp.filterDec);
        sp.filterEnvAmt = (float)e->getDoubleAttribute("filterEnvAmt", (double)sp.filterEnvAmt);
        sp.filterFreq = (float)e->getDoubleAttribute("filterFreq", (double)sp.filterFreq);
        sp.filterKeytrack = (float)e->getDoubleAttribute("filterKeytrack", (double)sp.filterKeytrack);
        sp.filterLFODep = (float)e->getDoubleAttribute("filterLFODep", (double)sp.filterLFODep);
        sp.filterQ = (float)e->getDoubleAttribute("filterQ", (double)sp.filterQ);
        sp.filterRel = (float)e->getDoubleAttribute("filterRel", (double)sp.filterRel);
        sp.filterSus = (float)e->getDoubleAttribute("filterSus", (double)sp.filterSus);
        sp.filterType = (float)e->getDoubleAttribute("filterType", (double)sp.filterType);
        sp.fineA = (float)e->getDoubleAttribute("fineA", (double)sp.fineA);
        sp.fineB = (float)e->getDoubleAttribute("fineB", (double)sp.fineB);
        sp.frameInterp = (float)e->getDoubleAttribute("frameInterp", (double)sp.frameInterp);
        sp.frameSnap = (float)e->getDoubleAttribute("frameSnap", (double)sp.frameSnap);
        sp.glide = (float)e->getDoubleAttribute("glide", (double)sp.glide);
        sp.grit = (float)e->getDoubleAttribute("grit", (double)sp.grit);
        sp.jumpProb = (float)e->getDoubleAttribute("jumpProb", (double)sp.jumpProb);
        sp.keytrack = (float)e->getDoubleAttribute("keytrack", (double)sp.keytrack);
        sp.levelA = (float)e->getDoubleAttribute("levelA", (double)sp.levelA);
        sp.levelB = (float)e->getDoubleAttribute("levelB", (double)sp.levelB);
        sp.evoStepped = (float)e->getDoubleAttribute("evoStepped", (double)sp.evoStepped);
        sp.evoSteppedB = (float)e->getDoubleAttribute("evoSteppedB", (double)sp.evoSteppedB);
        // Presets/projects saved before EVO modes existed carry no evoMode
        // attribute. Default them HARD to 0.0 (NORM) — never to the current
        // in-memory value — so a legacy patch can never inherit a stray
        // RAND/LOCK/FIX left behind by a previously loaded Sound. This restores
        // the exact pre-feature behaviour for every old preset.
        sp.evoMode  = (float)e->getDoubleAttribute("evoMode",  0.0);
        sp.evoModeB = (float)e->getDoubleAttribute("evoModeB", 0.0);
        sp.scanStyleB = (float)e->getDoubleAttribute("scanStyleB", (double)sp.scanStyleB);
        sp.lfoShape = (float)e->getDoubleAttribute("lfoShape", (double)sp.lfoShape);
        sp.lfoSync = (float)e->getDoubleAttribute("lfoSync", (double)sp.lfoSync);
        sp.noiseRate = (float)e->getDoubleAttribute("noiseRate", (double)sp.noiseRate);
        sp.noiseSync = (float)e->getDoubleAttribute("noiseSync", (double)sp.noiseSync);
        sp.lfoSpeed = (float)e->getDoubleAttribute("lfoSpeed", (double)sp.lfoSpeed);
        sp.octaveA = (float)e->getDoubleAttribute("octaveA", (double)sp.octaveA);
        sp.octaveB = (float)e->getDoubleAttribute("octaveB", (double)sp.octaveB);
        sp.oscMix = (float)e->getDoubleAttribute("oscMix", (double)sp.oscMix);
        sp.panA = (float)e->getDoubleAttribute("panA", (double)sp.panA);
        sp.panB = (float)e->getDoubleAttribute("panB", (double)sp.panB);
        sp.pitchEnvAmt = (float)e->getDoubleAttribute("pitchEnvAmt", (double)sp.pitchEnvAmt);
        sp.pitchEnvDec = (float)e->getDoubleAttribute("pitchEnvDec", (double)sp.pitchEnvDec);
        sp.posLFODepth = (float)e->getDoubleAttribute("posLFODepth", (double)sp.posLFODepth);
        sp.release = (float)e->getDoubleAttribute("release", (double)sp.release);
        sp.release2 = (float)e->getDoubleAttribute("release2", (double)sp.release2);
        sp.ringMod = (float)e->getDoubleAttribute("ringMod", (double)sp.ringMod);
        sp.scanStyle = (float)e->getDoubleAttribute("scanStyle", (double)sp.scanStyle);
        sp.spread = (float)e->getDoubleAttribute("spread", (double)sp.spread);
        sp.stereoPhase = (float)e->getDoubleAttribute("stereoPhase", (double)sp.stereoPhase);
        sp.stereoWidth = (float)e->getDoubleAttribute("stereoWidth", (double)sp.stereoWidth);
        sp.sustain = (float)e->getDoubleAttribute("sustain", (double)sp.sustain);
        sp.sustain2 = (float)e->getDoubleAttribute("sustain2", (double)sp.sustain2);
        sp.tuneA = (float)e->getDoubleAttribute("tuneA", (double)sp.tuneA);
        sp.tuneB = (float)e->getDoubleAttribute("tuneB", (double)sp.tuneB);
        sp.twAmt = (float)e->getDoubleAttribute("twAmt", (double)sp.twAmt);
        sp.twAtt = (float)e->getDoubleAttribute("twAtt", (double)sp.twAtt);
        sp.twDec = (float)e->getDoubleAttribute("twDec", (double)sp.twDec);
        sp.twRel = (float)e->getDoubleAttribute("twRel", (double)sp.twRel);
        sp.twSus = (float)e->getDoubleAttribute("twSus", (double)sp.twSus);
        sp.twToFilter = (float)e->getDoubleAttribute("twToFilter", (double)sp.twToFilter);
        sp.twVelAmt = (float)e->getDoubleAttribute("twVelAmt", (double)sp.twVelAmt);
        sp.ampVelAmt = (float)e->getDoubleAttribute("ampVelAmt", (double)sp.ampVelAmt);
        sp.uniDetune = (float)e->getDoubleAttribute("uniDetune", (double)sp.uniDetune);
        sp.waveModA = (float)e->getDoubleAttribute("waveModA", (double)sp.waveModA);
        sp.waveModB = (float)e->getDoubleAttribute("waveModB", (double)sp.waveModB);
        sp.waveStartA = (float)e->getDoubleAttribute("waveStartA", (double)sp.waveStartA);
        sp.waveStartB = (float)e->getDoubleAttribute("waveStartB", (double)sp.waveStartB);
        sp.wtSmooth = (float)e->getDoubleAttribute("wtSmooth", (double)sp.wtSmooth);
        sp.wtSmoothB = (float)e->getDoubleAttribute("wtSmoothB", (double)sp.wtSmoothB);
        auto parse = [](const juce::String& s, float* dst)
        {
            juce::StringArray toks;
            toks.addTokens(s, ",", "");
            for (int k = 0; k < EVO_POINTS && k < toks.size(); ++k)
                dst[k] = toks[k].getFloatValue();
        };
        parse(e->getStringAttribute("curveA"), sp.curveA);
        parse(e->getStringAttribute("curveB"), sp.curveB);
    }
    soundsInitialised = true;      // the preset provides all four snapshots
    editSound = juce::jlimit(0, NUM_SOUNDS - 1, es);
    applySoundToEditBuffer(editSound);   // make the knobs show the focused Sound
}



//==============================================================================
// Wavetable embedding for ALL 8 slots (4 Sounds x 2 oscillators). The original
// format only stored slots 0 and 1, so Sounds 2-4 lost their waves on reload.
//==============================================================================
void PhizmoAudioProcessor::addAllSlotsToZip(juce::ZipFile::Builder& zb, juce::XmlElement& xml)
{
    for (int i = 0; i < NUM_SOUNDS * 2; ++i)
    {
        juce::String fn; int cs = 2048; juce::MemoryBlock data; bool has = false;
        {
            juce::ScopedLock l(wt[i].lock);
            if (wt[i].loaded)
            {
                has = true;
                fn = wt[i].fileName;
                cs = wt[i].cycleSamples;
                data = wt[i].originalFileData;
            }
        }
        if (!has) continue;
        if (fn.isEmpty()) fn = "Wavetable" + juce::String(i) + ".wav";
        xml.setAttribute("slotFile"  + juce::String(i), fn);
        xml.setAttribute("slotCycle" + juce::String(i), cs);
        if (data.getSize() > 0)
            zb.addEntry(new juce::MemoryInputStream(data, true), 9,
                        "S" + juce::String(i) + "/" + fn, juce::Time::getCurrentTime());
    }
}

void PhizmoAudioProcessor::restoreAllSlotsFromZip(juce::ZipFile& zip, const juce::XmlElement& xml)
{
    for (int e = 0; e < zip.getNumEntries(); ++e)
    {
        auto* entry = zip.getEntry(e);
        if (!entry) continue;
        const juce::String fn = entry->filename;
        int slot = -1; juce::String baseName;
        for (int i = 0; i < NUM_SOUNDS * 2; ++i)
        {
            const juce::String pre = "S" + juce::String(i) + "/";
            if (fn.startsWith(pre)) { slot = i; baseName = fn.substring(pre.length()); break; }
        }
        if (slot < 0 || baseName.isEmpty()) continue;
        std::unique_ptr<juce::InputStream> es(zip.createStreamForEntry(e));
        if (!es) continue;
        juce::MemoryBlock mb;
        es->readIntoMemoryBlock(mb);
        const int cs = xml.getIntAttribute("slotCycle" + juce::String(slot), 2048);
        loadWavetableFromMemory(mb, baseName, cs, slot);
    }
}

//==============================================================================
// The twelve note resolutions shared by the arpeggiator, the LFO and the noise
// generator, expressed in beats (quarter notes). Order matches the parameter
// StringArrays: slowest to fastest, each plain value followed by its triplet.
const float PhizmoAudioProcessor::kNoteBeats[12] =
{
    4.0f, 4.0f / 1.5f,          // whole, whole triplet
    2.0f, 2.0f / 1.5f,          // half, half triplet
    1.0f, 1.0f / 1.5f,          // quarter, quarter triplet
    0.5f, 0.5f / 1.5f,          // eighth, eighth triplet
    0.25f, 0.25f / 1.5f,        // sixteenth, sixteenth triplet
    0.125f, 0.125f / 1.5f       // thirty-second, thirty-second triplet
};

// Turns a sync selector into a rate in Hz. index 0 = free running, in which
// case the supplied free-running rate is returned unchanged.
float PhizmoAudioProcessor::syncedRate(int syncIndex, float freeHz, float bpm)
{
    if (syncIndex <= 0) return freeHz;
    const float beats = kNoteBeats[juce::jlimit(0, 11, syncIndex - 1)];
    const float seconds = beats * (60.f / juce::jmax(1.f, bpm));
    return 1.f / juce::jmax(0.0001f, seconds);
}

// Applies one of the six hardware velocity curves (User's Guide p.10).
float PhizmoAudioProcessor::applyVelocityCurve(float vel) const
{
    const int c = pVelCurve ? juce::jlimit(0, 5, (int)pVelCurve->load()) : 1;
    vel = juce::jlimit(0.f, 1.f, vel);
    switch (c)
    {
    case 0:  return std::pow(vel, 0.55f);   // Curve 1 — for players with a light touch
    case 1:  return std::pow(vel, 0.80f);   // Curve 2
    case 2:  return vel;                    // Curve 3 — linear
    case 3:  return std::pow(vel, 1.45f);   // Curve 4 — for players with a heavy touch
    case 4:  return 64.f / 127.f;           // Curve 5 — fixed medium
    default: return 1.f;                    // Curve 6 — fixed maximum
    }
}

//==============================================================================
// The Phizmo modulation matrix. Every modulatable destination picks one of these
// 25 sources (User's Guide, "Modulation") and scales it with an Amount knob.
// Returns roughly -1..1; unipolar sources return 0..1.
//==============================================================================
float PhizmoAudioProcessor::modSource(int src, const PhizmoVoice& v, float lfoVal) const
{
    switch (src)
    {
    case 0:  return 0.f;                                    // OFF
    case 1:  return 1.f;                                    // FULL (Amount sets the value)
    case 2:  return lfoVal;                                 // LFO
    case 3:  return v.sahNoise;                             // Stepped noise  (noiS)
    case 4:  return v.lpfNoise;                             // Smooth noise   (LPF)
    case 5:  return v.penvLevel;                            // Envelope 1 (pitch)
    case 6:  return v.fenvLevel;                            // Envelope 2 (filter)
    case 7:  return v.envLevel;                             // Envelope 3 (amp)
    case 8:  return v.velocity;                             // Velocity (tch)
    case 9:  return juce::jlimit(0.f, 1.f, v.velocity + chanPressure.load());   // tPrS
    case 10: return (float)v.midiNote / 127.f;              // MIDI note
    case 11: return ((float)v.midiNote - 60.f) / 60.f;      // Keyboard (BrD), bipolar at C4
    case 12: return chanPressure.load();                    // Pressure
    case 13: return bendNorm.load();                        // Pitch wheel
    case 14: return modWheel.load();                        // Mod wheel (CtL1)
    case 15: return juce::jlimit(0.f, 1.f, modWheel.load() + chanPressure.load()); // CtPr
    case 16: return footCtl.load();                         // Foot controller
    case 17: return sustainPed.load();                      // Sustain pedal
    case 18: return sostenutoPed.load();                    // Sostenuto pedal
    case 19: return sys1.load();                            // SYS1
    case 20: return sys2.load();                            // SYS2
    case 21: return pFizF ? pFizF->load() : 0.f;            // SYS3 = the F knob
    case 22: return pFizO ? pFizO->load() : 0.f;            // SYS4 = the O knob
    case 23: return patchSel.load();                        // Patch Select
    case 24: return globalLfo;                              // Global LFO
    default: return 0.f;
    }
}

//==============================================================================
// The eight Phizmo LFO waveforms (+ noise), evaluated from a 0..1 phase.
// Returns a bipolar value in [-1, 1].
float PhizmoAudioProcessor::lfoValue(float p, int shape) const
{
    p -= std::floor(p);                                  // wrap 0..1
    const float twoPi = juce::MathConstants<float>::twoPi;
    switch (shape)
    {
    case 0: // Triangle
        return (p < 0.5f) ? (4.f * p - 1.f) : (3.f - 4.f * p);
    case 1: // Rounded triangle (soft-clipped triangle)
    {
        float tri = (p < 0.5f) ? (4.f * p - 1.f) : (3.f - 4.f * p);
        return std::tanh(tri * 1.6f) / std::tanh(1.6f);
    }
    case 2: // Sine
        return std::sin(p * twoPi);
    case 3: // Rising triangle — positive-going only: rises then falls back
        return (p < 0.5f) ? (2.f * p) : (2.f - 2.f * p);
    case 4: // Rising sine — positive-going only
        return std::sin(p * juce::MathConstants<float>::pi);
    case 5: // Sawtooth (falling)
        return 1.f - 2.f * p;
    case 6: // Square — positive-going only (manual: "a positive-going-only square wave")
        return (p < 0.5f) ? 1.f : 0.f;
    case 7: // Positive ramp — starts at peak and falls back to the initial value
        return 1.f - p;
    case 8: // Noise (sample & hold-ish per call)
    default:
        return fastRand() * 2.f - 1.f;
    }
}

// Expand the currently held notes into an arpeggio play order, honouring the
// octave range and the selected mode.
void PhizmoAudioProcessor::rebuildArpSequence()
{
    arpSequence.clear();
    if (arpHeldNotes.isEmpty()) { arpIndex = 0; return; }

    int rangeOct = 1 + (pArpRange ? (int)pArpRange->load() : 0);   // 1..4
    std::vector<int> base;
    for (int i = 0; i < arpHeldNotes.size(); ++i) base.push_back(arpHeldNotes[i]);

    // Build across octaves (ascending)
    std::vector<int> up;
    for (int o = 0; o < rangeOct; ++o)
        for (int n : base) up.push_back(n + 12 * o);

    int mode = pArpMode ? (int)pArpMode->load() : 0;   // Up/Down/UpDown/Random/AsPlayed
    switch (mode)
    {
    case 0: arpSequence = up; break;                                   // Up
    case 1: arpSequence.assign(up.rbegin(), up.rend()); break;         // Down
    case 2: {                                                          // Up/Down
        arpSequence = up;
        for (int i = (int)up.size() - 2; i > 0; --i) arpSequence.push_back(up[(size_t)i]);
        break;
    }
    case 3: arpSequence = up; break;                                   // Random (index chosen at step)
    case 4: default: {                                                 // As played
        arpSequence.clear();
        for (int o = 0; o < rangeOct; ++o)
            for (int n : base) arpSequence.push_back(n + 12 * o);
        break;
    }
    }
    if (arpIndex >= (int)arpSequence.size()) arpIndex = 0;
}

//==============================================================================
// Sets a freshly triggered voice's scan start point according to each
// oscillator's EVO playhead mode. NORM leaves noteOn()'s start (0 or the
// existing phase-carry) untouched; the other modes override it per osc.
void PhizmoAudioProcessor::applyEvoStart(int vi, int snd)
{
    if (vi < 0 || vi >= MAX_VOICES) return;
    snd = juce::jlimit(0, NUM_SOUNDS - 1, snd);
    PhizmoVoice& v = voices[vi];
    const SoundParams& sp = soundParams[snd];

    auto startFor = [&](int osc, double& phase)
    {
        const int mode = (int)std::round(osc == 0 ? sp.evoMode : sp.evoModeB);
        switch (mode)
        {
        case 1: // RAND — each note begins at a random point on the curve
            phase = (double)fastRand();
            break;
        case 2: // LOCK — begin where the anchor (oldest held voice) currently is
        {
            int anchor = -1; unsigned long long best = ~0ULL;
            for (int i = 0; i < MAX_VOICES; ++i)
            {
                if (i == vi || !voices[i].active || voices[i].soundIndex != snd) continue;
                if (voices[i].evoOrder < best) { best = voices[i].evoOrder; anchor = i; }
            }
            if (anchor >= 0)
                phase = (osc == 0 ? voices[anchor].curvePhase : voices[anchor].curvePhaseB);
            // else this note IS the anchor → keep the NORM start noteOn() gave it
            break;
        }
        case 3: // FIX — begin on the perpetual master (it keeps following it)
            phase = evoFixPhase[snd][osc == 0 ? 0 : 1];
            break;
        default: break; // NORM
        }
    };

    startFor(0, v.curvePhase);
    startFor(1, v.curvePhaseB);
    v.evoOrder = ++evoOrderCounter;   // stamp order so later LOCK notes can anchor
}

//==============================================================================
void PhizmoAudioProcessor::synthesiseVoice(PhizmoVoice& v, int vi,
    float posLFOMod, double pitchMult, float& outL, float& outR,
    float& outBL, float& outBR,
    const BlockEnvCoeffs& bc, const SoundParams& SP)
{
    outL = outR = outBL = outBR = 0.f;
    if (!v.active) { voicePubSound[vi].store(-1); return; }   // silent → no playhead
    (void)posLFOMod;

    float isr = (float)(1.0 / currentSampleRate);

    // --- Osc 1 amplitude envelope ---
    float att = juce::jmax(0.001f, SP.attack);
    float sus = SP.sustain;
    PhizmoVoice::Env nextAmpStage = v.envStage;
    float nextAmpRelStart = v.releaseStartLevel;
    v.envLevel = advanceEnvExp(v.envLevel, sus, v.envStage, att, sus,
        bc.decCoeff, bc.relCoeff, isr, nextAmpStage, nextAmpRelStart,
        pEnvMode ? (int)pEnvMode->load() : 0);
    v.envStage = nextAmpStage;

    // --- Osc 2 amplitude envelope ---
    float att2 = juce::jmax(0.001f, SP.attack2);
    float sus2 = SP.sustain2;
    PhizmoVoice::Env nextEnv2Stage = v.env2Stage;
    float nextEnv2RelStart = v.rel2StartLevel;
    v.env2Level = advanceEnvExp(v.env2Level, sus2, v.env2Stage, att2, sus2,
        bc.dec2Coeff, bc.rel2Coeff, isr, nextEnv2Stage, nextEnv2RelStart);
    v.env2Stage = nextEnv2Stage;

    // Voice dies only when BOTH envelopes are idle
    if (v.envStage == PhizmoVoice::Env::Idle && v.env2Stage == PhizmoVoice::Env::Idle)
    {
        v.active = false; voicePubSound[vi].store(-1); return;   // silent → no playhead
    }

    // --- Filter envelope ---
    float fatt = juce::jmax(0.001f, SP.filterAtt);
    float fsus = SP.filterSus;
    PhizmoVoice::Env nextFEnvStage = v.fenvStage;
    float nextFRelStart = v.fenvReleaseStart;
    v.fenvLevel = advanceEnvExp(v.fenvLevel, fsus, v.fenvStage, fatt, fsus,
        bc.fdecCoeff, bc.frelCoeff, isr, nextFEnvStage, nextFRelStart);
    v.fenvStage = nextFEnvStage;

    // --- Pitch envelope ---
    float penvAmt = SP.pitchEnvAmt;
    if (!v.penvDone)
    {
        v.penvLevel *= bc.penvDecCoeff;
        if (v.penvLevel < 1e-4f) { v.penvLevel = 0.f; v.penvDone = true; }
    }
    double pitchEnvMult = std::pow(2.0, (double)(v.penvLevel * penvAmt) / 12.0);

    // --- the original "O" assignable macro (destinations read live below) ---
    float phizOval  = pFizO ? pFizO->load() : 0.f;
    int   phizODest = pFizODest ? (int)pFizODest->load() : 0;

    // --- Phizmo Modulation LFO: shape + speed drive the position/pitch/filter mod.
    //     Both the LFO and the noise generator can lock to the system clock.
    const float sysBpm = pTempo ? pTempo->load() : 120.f;
    int   lfoShape = pLfoShape ? (int)SP.lfoShape : 2;
    float lfoSpeed = syncedRate((int)SP.lfoSync, SP.lfoSpeed, sysBpm);
    float noiseHz  = syncedRate((int)SP.noiseSync, SP.noiseRate, sysBpm);

    // --- Per-voice evo LFO (keeps its own rate, uses the selected shape) ---
    voiceEvoLFOPhase[vi] += SP.evoLFORate / currentSampleRate;
    if (voiceEvoLFOPhase[vi] > 1.0) voiceEvoLFOPhase[vi] -= 1.0;
    float evoLFO = lfoValue((float)voiceEvoLFOPhase[vi], lfoShape);

    // --- Per-voice noise generators feeding the noiS / LPF modulators ---
    //     These run at the Modulation section's own Noise Rate, independent of
    //     the LFO, matching the hardware's separate LFO / Noise selection.
    v.noisePhase += noiseHz / currentSampleRate;
    if (v.noisePhase >= 1.0)
    {
        v.noisePhase -= 1.0;
        v.sahNoise = fastRand() * 2.f - 1.f;          // stepped: large random jumps
    }
    // smooth (low-pass filtered) noise tracks the same rate, heavily damped
    {
        const float sm = juce::jlimit(0.0002f, 0.2f, (float)(noiseHz * 6.0 / currentSampleRate));
        v.lpfNoise += sm * ((fastRand() * 2.f - 1.f) - v.lpfNoise);
    }

    // --- Per-voice pos LFO — this is the Phizmo "Modulation" LFO (shape + speed) ---
    voicePosLFOPhase[vi] += lfoSpeed / currentSampleRate;
    if (voicePosLFOPhase[vi] > 1.0) voicePosLFOPhase[vi] -= 1.0;
    float vPosLFO = lfoValue((float)voicePosLFOPhase[vi], lfoShape);

    // --- Per-voice curve scan (single shared scan position, drives BOTH curves) ---
    float evoTime = juce::jmax(0.1f, SP.evoTime);
    if (phizODest == 1) evoTime = juce::jmax(0.1f, evoTime * std::pow(2.f, -phizOval * 2.f)); // O: Evo Time
    // Osc B can have an independent evo time; if evoTimeB matches evoTime (default 4s) they lock.
    float evoTimeB = juce::jmax(0.1f, SP.evoTimeB);
    double phaseInc = 1.0 / (evoTime * currentSampleRate);
    double phaseIncB = 1.0 / (evoTimeB * currentSampleRate);
    auto scanMode = (ScanMode)(int)juce::jlimit(0.f, 4.f, SP.scanStyle);

    if (!v.curveFinished)
    {
        switch (scanMode)
        {
        case ScanMode::Forward:
            v.curvePhase += phaseInc;
            if (v.curvePhase >= 1.0) { v.curvePhase -= 1.0; v.curveWrapA = true; }
            break;
        case ScanMode::FwdStay:
            v.curvePhase += phaseInc;
            if (v.curvePhase >= 1.0) { v.curvePhase = 1.0; v.curveFinished = true; }
            break;
        case ScanMode::BackForth:
            v.curvePhase += phaseInc * v.scanDir;
            if (v.curvePhase >= 1.0) { v.curvePhase = 1.0; v.scanDir = -1; }
            else if (v.curvePhase <= 0.0) { v.curvePhase = 0.0; v.scanDir = 1; }
            break;
        case ScanMode::BwdStay:
            v.curvePhase -= phaseInc;
            if (v.curvePhase <= 0.0) { v.curvePhase = 0.0; v.curveFinished = true; }
            break;
        case ScanMode::Backward:
            v.curvePhase -= phaseInc;
            if (v.curvePhase <= 0.0) { v.curvePhase += 1.0; v.curveWrapA = true; }
            break;
        }
    }

    // Osc B gets its own scan phase: shared A phase + user offset + independent speed drift.
    // phaseIncB - phaseInc is the per-sample drift; integrate over time via curvePhase as base.
    float bPhaseOff = SP.evoBPhaseOff;
    // Oscillator B runs its own scan: its own time, its own style, its own
    // direction state. It used to be derived from A by scaling with
    // (curvePhase / phaseInc), which blows up as phaseInc gets small — that
    // was what made the B scan jitter and stall part-way through the table.
    const auto scanModeB = (ScanMode)(int)juce::jlimit(0.f, 4.f, SP.scanStyleB);
    if (!v.curveFinishedB)
    {
        switch (scanModeB)
        {
        case ScanMode::Forward:
            v.curvePhaseB += phaseIncB;
            if (v.curvePhaseB >= 1.0) { v.curvePhaseB -= 1.0; v.curveWrapB = true; }
            break;
        case ScanMode::FwdStay:
            v.curvePhaseB += phaseIncB;
            if (v.curvePhaseB >= 1.0) { v.curvePhaseB = 1.0; v.curveFinishedB = true; }
            break;
        case ScanMode::BackForth:
            v.curvePhaseB += phaseIncB * v.scanDirB;
            if (v.curvePhaseB >= 1.0) { v.curvePhaseB = 1.0; v.scanDirB = -1; }
            else if (v.curvePhaseB <= 0.0) { v.curvePhaseB = 0.0; v.scanDirB = 1; }
            break;
        case ScanMode::BwdStay:
            v.curvePhaseB -= phaseIncB;
            if (v.curvePhaseB <= 0.0) { v.curvePhaseB = 0.0; v.curveFinishedB = true; }
            break;
        default:
            v.curvePhaseB -= phaseIncB;
            if (v.curvePhaseB <= 0.0) { v.curvePhaseB += 1.0; v.curveWrapB = true; }
            break;
        }
    }
    double curvePhaseBraw = v.curvePhaseB + (double)bPhaseOff;
    // Keep in [0,1)
    curvePhaseBraw = std::fmod(curvePhaseBraw, 1.0);
    if (curvePhaseBraw < 0.0) curvePhaseBraw += 1.0;

    // --- EVO playhead mode --------------------------------------------------
    // NORM(0)/RAND(1)/LOCK(2) only affect a note's *start* point (set in
    // applyEvoStart); their scan then advances per the block above. FIX(3)
    // locks the voice to the Sound's perpetual master scan every sample, so
    // every FIX note rides the one indefinitely-cycling playhead.
    const int evoModeA = (int)std::round(SP.evoMode);
    const int evoModeB = (int)std::round(SP.evoModeB);
    if (evoModeA == 3)
    {
        const double m = evoFixPhase[v.soundIndex][0];
        if (std::abs(m - v.curvePhase) > 0.5) v.curveWrapA = true;  // declick loop wrap
        v.curvePhase = m;   // curvePhaseAraw is rebuilt from this further down
    }
    if (evoModeB == 3)
    {
        const double m = evoFixPhase[v.soundIndex][1];
        if (std::abs(m - v.curvePhaseB) > 0.5) v.curveWrapB = true;
        v.curvePhaseB = m;
        curvePhaseBraw = m + (double)bPhaseOff;
        curvePhaseBraw = std::fmod(curvePhaseBraw, 1.0);
        if (curvePhaseBraw < 0.0) curvePhaseBraw += 1.0;
    }

    // --- Keytrack to frame position ---
    // Maps MIDI note linearly: note 60 (C4) = centre (0.5), full range = ±5 octaves.
    float keytrackAmt = SP.keytrack;
    float keytrackMod = ((float)(v.midiNote - 60) / 60.f) * keytrackAmt * 0.5f;  // ±0.5 at extremes

    // --- Transwave position envelope (Phizmo-style) ---
    // Advances independently of the curve scan; outputs a signed normalised
    // offset that is added to the curve's 0-1 position BEFORE frame scaling.
    // twAmt=0 (default) to zero contribution, existing patches unaffected.
    float twSus = SP.twSus;
    PhizmoVoice::Env nextTwStage = v.twenvStage;
    float twRelStartUnused = v.twenvRelStart;
    v.twenvLevel = advanceEnvExp(v.twenvLevel, twSus, v.twenvStage,
        juce::jmax(0.001f, SP.twAtt), twSus,
        bc.twDecCoeff, bc.twRelCoeff,
        isr, nextTwStage, twRelStartUnused);
    v.twenvStage = nextTwStage;

    // Velocity can scale the envelope depth (0 = no scaling, 1 = full velocity scaling)
    float twDepth = SP.twAmt;
    float twVel = SP.twVelAmt;
    twDepth *= (1.f - twVel) + v.velocity * twVel;
    float twMod = v.twenvLevel * twDepth;   // normalised 0-1 range, signed by twDepth

    // Curve output — Osc A uses shared curvePhase, Osc B uses its own offset phase.
    // In STEP mode the raw value jumps from one frame to the next, which snaps
    // the scan position and clicks. Slew the stepped output over a few ms so
    // the steps stay obvious but the discontinuity is gone. (Interpolated mode
    // is already continuous, so it passes straight through.)
    // Osc A now has its own start/phase offset too (evoPhaseOff), mirroring B.
    double curvePhaseAraw = (double)v.curvePhase + (double)SP.evoPhaseOff;
    curvePhaseAraw = std::fmod(curvePhaseAraw, 1.0);
    if (curvePhaseAraw < 0.0) curvePhaseAraw += 1.0;
    float curveTargetA = evalCurveS((float)curvePhaseAraw, 0, SP);
    float curveTargetB = evalCurveS((float)curvePhaseBraw, 1, SP);
    const bool stepA = SP.evoStepped  > 0.5f;
    const bool stepB = SP.evoSteppedB > 0.5f;
    // Fade coefficient from the per-osc SLW knob (ms). 0 ms -> hard (coeff 1),
    // a few ms -> smoothed. Same time is used for STEP steps and wrap declicks.
    auto slewCoeff = [sr = (float)currentSampleRate](float ms)
    {
        const float tau = ms * 0.001f;
        return (tau < 1.0e-5f) ? 1.f : 1.f - std::exp(-1.f / (tau * sr));
    };
    const float slewA = slewCoeff(SP.evoSlew);
    const float slewB = slewCoeff(SP.evoSlewB);
    // STEP mode always slews. In non-stepped modes the curve is continuous
    // except at a forward/backward end-to-end wrap, which clicks — so slew just
    // through that jump (until the smoothed value has caught the target again),
    // then pass straight through. Ping-pong/one-shot modes never wrap, so they
    // are unaffected.
    if (stepA || v.curveWrapA)
    {
        v.curveSmoothA += slewA * (curveTargetA - v.curveSmoothA);
        if (v.curveWrapA && std::abs(curveTargetA - v.curveSmoothA) < 1.0e-4f) v.curveWrapA = false;
    }
    else { v.curveSmoothA = curveTargetA; }
    if (stepB || v.curveWrapB)
    {
        v.curveSmoothB += slewB * (curveTargetB - v.curveSmoothB);
        if (v.curveWrapB && std::abs(curveTargetB - v.curveSmoothB) < 1.0e-4f) v.curveWrapB = false;
    }
    else { v.curveSmoothB = curveTargetB; }
    float curveOutA = v.curveSmoothA;
    float curveOutB = v.curveSmoothB;

    // Add the transwave envelope offset + evo LFO + keytrack to both curves
    float evoLFOAmt = evoLFO * SP.evoLFODepth * 0.5f;

    // --- Phizmo Wave section: per-osc START POINT offset + "I" macro wave-mod ---
    float phizIWave  = pFizI ? pFizI->load() * 0.5f : 0.f;   // ±0.5 position from I knob
    phizIWaveMod.store(phizIWave);
    float wStartA = (SP.waveStartA) + phizIWave;
    float wStartB = (SP.waveStartB) + phizIWave;
    // Wave-mod amount routes the LFO into the start position, per oscillator
    float wModA   = SP.waveModA * modSource((int)SP.waveModSrcA, v, vPosLFO) * 0.5f;
    float wModB   = SP.waveModB * modSource((int)SP.waveModSrcB, v, vPosLFO) * 0.5f;

    float rawEvoPosA = juce::jlimit(0.f, 1.f, curveOutA + twMod + evoLFOAmt + keytrackMod + wStartA + wModA);
    float rawEvoPosB = juce::jlimit(0.f, 1.f, curveOutB + twMod + evoLFOAmt + keytrackMod + wStartB + wModB);

    // --- Frame Snap: quantise frame position to N discrete steps ---
    // frameSnap=0 to off; frameSnap=1 to coarsest (8 steps). Skews toward powers of 2.
    float snapAmt = SP.frameSnap;
    if (snapAmt > 0.001f)
    {
        // Map 0-1 to ~128 down to 2 steps (exponential feel)
        float steps = std::pow(2.f, (1.f - snapAmt) * 6.f + 1.f);   // 128 … 2
        rawEvoPosA = std::round(rawEvoPosA * steps) / steps;
        rawEvoPosB = std::round(rawEvoPosB * steps) / steps;
    }

    // Publish two positions per oscillator:
    //  • the true FRAME being played (post-modulation) — for the wavetable
    //    viewer, so it shows exactly what is heard;
    //  • the raw SCAN PHASE (0-1 sweep along the curve) — for the evolution
    //    editor's playhead dot, which lives on the curve's own X axis.
    // Conflating the two is what made the editor dot fly around: the modulated
    // frame position is not a point on the drawn curve.
    evoFrameSlot[v.soundIndex * 2 + 0].store(rawEvoPosA);
    evoFrameSlot[v.soundIndex * 2 + 1].store(rawEvoPosB);
    evoScanSlot [v.soundIndex * 2 + 0].store((float)v.curvePhase);
    evoScanSlot [v.soundIndex * 2 + 1].store((float)juce::jlimit(0.0, 1.0, v.curvePhaseB));

    // Per-voice publish: each sounding note owns its own playhead, so a chord
    // shows one bar per note instead of every voice fighting over one slot.
    voicePubScanA[vi].store((float)juce::jlimit(0.0, 1.0, v.curvePhase));
    voicePubScanB[vi].store((float)juce::jlimit(0.0, 1.0, v.curvePhaseB));
    voicePubSound[vi].store(v.soundIndex);

    float velFilterMod = v.velocity;

    // --- Glide: interpolate base frequency ---
    float glideTime = SP.glide;
    double baseMF;
    if (v.glideProgress < 1.0f && glideTime > 0.001f)
    {
        v.glideProgress += isr / glideTime;
        if (v.glideProgress > 1.0f) v.glideProgress = 1.0f;
        // Interpolate in log (pitch) space for perceptually linear glide
        double logStart = std::log(juce::jmax(1.0, v.glideStartFreq));
        double logTarget = std::log(juce::jmax(1.0, v.glideTargetFreq));
        baseMF = std::exp(logStart + (double)v.glideProgress * (logTarget - logStart));
    }
    else
    {
        baseMF = v.glideTargetFreq;
    }

    // --- Pitch ---
    // dm, sA, sB, octA, octB are all constant within a block — use precomputed values.
    // uRand is per-voice (hash of note number) so compute once here too.
    float  ur = SP.uniDetune;
    float  uRand = (float)((v.midiNote * 1664525 + 1013904223) & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    double um = std::pow(2.0, (uRand * 2.f - 1.f) * ur / 1200.0);

    // --- Phizmo Pitch section: per-osc Tune (st) + Fine (cents) ---
    // tuneMA/tuneMB only depend on SP.tuneA/fineA/tuneB/fineB, which are
    // constant for the whole block, so they're precomputed once per block
    // in bc (BlockEnvCoeffs) rather than recalculated with std::pow here.
    double tuneMA = bc.tuneMA;
    double tuneMB = bc.tuneMB;
    // --- the original "M" macro: pushes the two oscillators apart in pitch (detune) ---
    double phizM   = pFizM ? (double)pFizM->load() : 0.0;
    double phizMA  = std::pow(2.0, ( phizM * 0.5) / 12.0);   // +0.5 st at full
    double phizMB  = std::pow(2.0, (-phizM * 0.5) / 12.0);   // -0.5 st at full

    // Modulation matrix -> Pitch (per oscillator, +/-12 semitones at full amount)
    const double pmA = std::pow(2.0, (double)(modSource((int)SP.pitchModSrcA, v, vPosLFO)
                                              * SP.pitchModAmtA * 12.f) / 12.0);
    const double pmB = std::pow(2.0, (double)(modSource((int)SP.pitchModSrcB, v, vPosLFO)
                                              * SP.pitchModAmtB * 12.f) / 12.0);
    double fA = baseMF * bc.dm * um * pitchMult * pitchEnvMult * bc.sA * bc.octA * tuneMA * phizMA * pmA;
    double fB = baseMF * bc.dm * um * pitchMult * pitchEnvMult * bc.sB * bc.octB * tuneMB * phizMB * pmB;
    v.phaseA += fA / currentSampleRate;
    v.phaseB += fB / currentSampleRate;

    // --- Random jump (shared offset applied to both oscillators' positions) ---
    if (v.phaseA >= 1.0)
    {
        v.phaseA -= 1.0;
        float jp = SP.jumpProb;
        if (jp > 0.f && fastRand() < jp * 0.5f)
            v.frameOffset = fastRand() - rawEvoPosA;
    }
    if (v.phaseB >= 1.0) v.phaseB -= 1.0;

    // Apply pos LFO and jump offset to each oscillator's own frame position.
    // Velocity-to-frame only makes sense in NORM: RAND/LOCK/FIX already dictate
    // the scan position, so the vel offset is switched off for those oscs.
    const float velOffA = (evoModeA == 0) ? v.velFrameOffset : 0.f;
    const float velOffB = (evoModeB == 0) ? v.velFrameOffset : 0.f;
    float rvA = rawEvoPosA + v.frameOffset + velOffA + vPosLFO * SP.posLFODepth * 0.5f;
    rvA = std::fmod(rvA, 1.f); if (rvA < 0.f) rvA += 1.f;
    float rvB = rawEvoPosB + v.frameOffset + velOffB + vPosLFO * SP.posLFODepth * 0.5f;
    rvB = std::fmod(rvB, 1.f); if (rvB < 0.f) rvB += 1.f;

    // --- Sample from both wavetable slots ---
    double lA = v.phaseA, lB = v.phaseB;

    // A Sound with no wave of its own falls back to Sound 1's wave, so enabling
    // a layer always makes sound; load a wave into it to give it its own voice.
    const int rawA = v.soundIndex * 2 + 0, rawB = v.soundIndex * 2 + 1;
    const WavetableSlot& sA_wt = wt[wt[rawA].loaded ? rawA : 0];
    const WavetableSlot& sB_wt = wt[wt[rawB].loaded ? rawB : 1];
    float fiA = rvA * (float)(sA_wt.numFrames > 0 ? sA_wt.numFrames - 1 : 0);
    float fiB = rvB * (float)(sB_wt.numFrames > 0 ? sB_wt.numFrames - 1 : 0);

    // --- Grit: deterministic integer-snap playback-position quantisation ---
    // This is a TIME-domain sample & hold (decimation) on the wavetable read position,
    // not an amplitude waveshaper/distortion curve - it reduces how many distinct steps
    // per cycle the oscillator is allowed to land on, producing the harsh, "stair-stepped"
    // aliasing character of the original Phizmo GRIT control. It is always applied (not
    // probabilistic), so the amount tracks the knob directly and is clearly audible.
    float gr = SP.grit;
    if (gr > 0.0f)
    {
        // gr=0 -> full resolution (inaudible); gr=1 -> as few as 6 steps per cycle (extreme).
        // The cubic falloff keeps the first ~30% of the knob subtle/vintage and the rest
        // increasingly harsh/digital, mirroring how the hardware control behaves.
        float resoFrac = std::pow(1.0f - gr, 3.0f);
        int   fullStepsA = juce::jmax(6, sA_wt.cycleSamples);
        int   fullStepsB = juce::jmax(6, sB_wt.cycleSamples);
        int   qStepsA = juce::jmax(6, (int)(fullStepsA * resoFrac));
        int   qStepsB = juce::jmax(6, (int)(fullStepsB * resoFrac));
        lA = std::floor(lA * qStepsA) / (double)qStepsA;
        lB = std::floor(lB * qStepsB) / (double)qStepsB;
    }
    while (lA < 0.0) lA += 1.0; while (lA >= 1.0) lA -= 1.0;
    while (lB < 0.0) lB += 1.0; while (lB >= 1.0) lB -= 1.0;
    double phB = SP.stereoPhase;
    lB = std::fmod(lB + phB, 1.0); if (lB < 0.0) lB += 1.0;

    float smpA_L = 0.f, smpA_R = 0.f, smpB_L = 0.f, smpB_R = 0.f;
    bool doInterp = (pFrameInterp && SP.frameInterp > 0.5f);
    float smoothA = SP.wtSmooth;
    float smoothB = SP.wtSmoothB;

    if (sA_wt.loaded)
    {
        // Note: no lock needed here — processBlock() already holds every
        // wt[] slot's lock for the duration of the per-sample loop.
        float raw = sampleFrameRaw(sA_wt, fiA, lA, doInterp, smoothA);
        raw = applyBitCrush(raw, SP.bitCrush);
        float w = SP.stereoWidth;
        float rawB = sampleFrameRaw(sA_wt, fiA, std::fmod(lA + phB, 1.0), doInterp, smoothA);
        rawB = applyBitCrush(rawB, SP.bitCrush);
        float mid = (raw + rawB) * 0.5f, side = (raw - rawB) * 0.5f;
        smpA_L = mid + side * w;
        smpA_R = mid - side * w;
    }

    if (sB_wt.loaded)
    {
        // Note: no lock needed here — processBlock() already holds every
        // wt[] slot's lock for the duration of the per-sample loop.
        float raw = sampleFrameRaw(sB_wt, fiB, lB, doInterp, smoothB);
        raw = applyBitCrush(raw, SP.bitCrush);
        float w = SP.stereoWidth;
        float rawA2 = sampleFrameRaw(sB_wt, fiB, lA, doInterp, smoothB);
        rawA2 = applyBitCrush(rawA2, SP.bitCrush);
        float mid = (raw + rawA2) * 0.5f, side = (raw - rawA2) * 0.5f;
        smpB_L = mid + side * w;
        smpB_R = mid - side * w;
    }

    // --- Ring modulation ---
    // When a slot is silent, use a sine from that osc's phase as carrier,
    // so ring mod is always audible regardless of which slots are loaded.
    // Amplitude-compensated: mix dry vs ring at equal power.
    float ringAmt = SP.ringMod;
    if (phizODest == 3) ringAmt = juce::jlimit(0.f, 1.f, ringAmt + phizOval); // O: Ring Mod
    float ringL = 0.f, ringR = 0.f;
    if (ringAmt > 0.f)
    {
        const float twoPif = (float)juce::MathConstants<double>::twoPi;
        float carrL = sA_wt.loaded ? smpA_L : std::sin((float)lA * twoPif);
        float carrR = sA_wt.loaded ? smpA_R : std::sin((float)lA * twoPif);
        float modL = sB_wt.loaded ? smpB_L : std::sin((float)lB * twoPif);
        float modR = sB_wt.loaded ? smpB_R : std::sin((float)lB * twoPif);
        ringL = carrL * modL;
        ringR = carrR * modR;
    }

    // --- Per-osc envelope levels ---
    // The Envelope VELOCITY knob sets how much velocity scales loudness:
    // 0 = velocity ignored (constant level), 1 = fully velocity-dependent.
    const float aVel = SP.ampVelAmt;
    const float velGain = (1.f - aVel) + v.velocity * aVel;
    float evA = v.envLevel  * velGain;
    float evB = v.env2Level * velGain;

    // --- Phizmo Amplitude section: per-oscillator Level + Pan.
    // Both transwaves simply SUM (each with its own level & pan) as on the
    // hardware. Equal-power pan law. The legacy oscMix param is kept only as a
    // gentle A/B balance so old-style patches still behave.
    // OSC 1 / OSC 2 on-off buttons gate each oscillator
    float lvA = SP.levelA * (SP.oscAOn > 0.5f ? 1.f : 0.f);
    float lvB = SP.levelB * (SP.oscBOn > 0.5f ? 1.f : 0.f);
    float paA = SP.panA;
    float paB = SP.panB;
    auto panGains = [](float pan, float& gl, float& gr) {
        float x = (juce::jlimit(-1.f, 1.f, pan) + 1.f) * 0.25f * juce::MathConstants<float>::pi;
        gl = std::cos(x); gr = std::sin(x);
        };
    float glA, grA, glB, grB;
    panGains(paA, glA, grA); panGains(paB, glB, grB);

    float mix = SP.oscMix;   // 0..1 balance (0.5 = both equal)
    float balA = 1.f - juce::jmax(0.f, mix - 0.5f) * 2.f;   // fade A out for mix>0.5
    float balB = 1.f - juce::jmax(0.f, 0.5f - mix) * 2.f;   // fade B out for mix<0.5

    float aL = smpA_L * evA * lvA * glA * balA;
    float aR = smpA_R * evA * lvA * grA * balA;
    float bL = smpB_L * evB * lvB * glB * balB;
    float bR = smpB_R * evB * lvB * grB * balB;

    // Each oscillator keeps its own stream so it can be routed to its own
    // Effect Bus (the hardware assigns a bus per oscillator, not per Sound).
    float sAL = aL * 0.75f, sAR = aR * 0.75f;
    float sBL = bL * 0.75f, sBR = bR * 0.75f;

    // Ring mod: blend at equal power (ringAmt^2 + (1-ringAmt)^2 stays ~0.5,
    // so we compensate with a 1/sqrt(2) factor at ringAmt=0.5).
    // Simpler: scale ring output by (evA*(1-mix)+evB*mix) so level matches dry.
    float envMix = (evA * lvA * balA + evB * lvB * balB) * 0.5f;
    float wetL = ringL * envMix;
    float wetR = ringR * envMix;
    // Cross-fade with amplitude compensation: boost by sqrt(2) so -3dB point
    // at 0.5 matches the dry level, and the ring side doesn't duck.
    float comp = 1.f + ringAmt * (1.4142f - 1.f);   // 1 at 0, 1.414 at 1
    // The ring-modulated product is a combination of both oscillators, so it
    // follows oscillator A's bus assignment.
    outL  = (sAL * (1.f - ringAmt) + wetL * ringAmt) * comp;
    outR  = (sAR * (1.f - ringAmt) + wetR * ringAmt) * comp;
    outBL = sBL * (1.f - ringAmt) * comp;
    outBR = sBR * (1.f - ringAmt) * comp;

    // --- Filter + cutoff plumbing (handled in processBlock) ---
    float baseFreq = SP.filterFreq;
    float filterEnv = v.fenvLevel * SP.filterEnvAmt;
    float filterLFO = blockPosLFO * SP.filterLFODep;
    // TW Env to Filter: twMod (already velocity-scaled) routes the transwave sweep to cutoff
    float twFilterMod = twMod * SP.twToFilter * 4.f;  // ±4 octaves at full amount
    float velScale = 0.3f + 0.7f * velFilterMod;
    // Phizmo Filter: keyboard tracking + "Z" macro (real-time filter cutoff)
    float ktOct  = ((float)(v.midiNote - 60) / 12.f) * (SP.filterKeytrack);
    float phizZOct = pFizZ ? pFizZ->load() * 4.f : 0.f;      // ±4 octaves from Z knob
    // Modulation matrix -> Filter, independently per oscillator (+/-4 octaves)
    const float fmA = modSource((int)SP.filtModSrcA, v, vPosLFO) * SP.filtModAmtA * 4.f;
    const float fmB = modSource((int)SP.filtModSrcB, v, vPosLFO) * SP.filtModAmtB * 4.f;
    float modOctaves = filterEnv * 6.f + filterLFO * 3.f + twFilterMod + ktOct + phizZOct;
    float cutoff  = juce::jlimit(20.f, 20000.f, baseFreq * velScale * fastExp2(modOctaves + fmA));
    float cutoffB = juce::jlimit(20.f, 20000.f, baseFreq * velScale * fastExp2(modOctaves + fmB));
    float q = juce::jmax(0.1f, SP.filterQ);
    if (phizODest == 2) q = juce::jlimit(0.1f, 12.f, q + phizOval * 6.f); // O: Resonance

    // Each Sound carries its own filter settings, so filtering has to happen
    // per voice. The old alternative — one filter across the summed bus, set
    // from the edit buffer — meant the focused Sound's cutoff and resonance
    // coloured every Sound at once, so merely selecting a different Sound
    // changed the sound of the Preset.
    {
        // setLowpass is called once per synthesiseVoice call (once per sample per voice),
        // but the coefficients are cheap compared to exp — cutoff is already computed above.
        const int ft = (int)SP.filterType;
        v.voiceFilter.setType(ft, cutoff, q, currentSampleRate);
        outL = v.voiceFilter.processL(outL);
        outR = v.voiceFilter.processR(outR);
        v.voiceFilterB.setType(ft, cutoffB, q, currentSampleRate);
        outBL = v.voiceFilterB.processL(outBL);
        outBR = v.voiceFilterB.processR(outBR);
    }
}

//==============================================================================
void PhizmoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();

    // --- Voice trigger / release helpers (shared by direct MIDI and the arp) ---
    auto startVoice = [this](int note, float vel)
    {
        bool isMono = (pMono && pMono->load() > 0.5f);
        float velAmt = (pVelToFrame && pVelToFrame->load() > 0.5f) ? 1.0f : 0.f;
        bool  carry = (pEvoPhaseCarry && pEvoPhaseCarry->load() > 0.5f)
            && !(pEvoRestart && pEvoRestart->load() > 0.5f);

        // A Phizmo Preset layers up to FOUR Sounds: every enabled Sound whose
        // keyboard zone contains this note gets its own voice.
        for (int snd = 0; snd < NUM_SOUNDS; ++snd)
        {
            const SoundParams& sp = soundParams[snd];
            if (!sp.enabled) continue;
            if (note < sp.lowKey || note > sp.highKey) continue;

            // Glide is a per-Sound setting, so read it from this Sound rather
            // than from whichever Sound the panel happens to be editing.
            const float glideTime = sp.glide;

            int si;
            if (isMono)
            {
                si = snd;                       // one dedicated voice per Sound
                for (int i = 0; i < MAX_VOICES; ++i)
                    if (i != snd && voices[i].soundIndex == snd) voices[i].active = false;
            }
            else
            {
                si = -1; float minL = 2.f; int minI = 0;
                for (int i = 0; i < MAX_VOICES; ++i)
                {
                    if (!voices[i].active) { si = i; break; }
                    if (voices[i].envStage == PhizmoVoice::Env::Release && voices[i].envLevel < minL)
                    { minL = voices[i].envLevel; minI = i; }
                }
                if (si < 0) si = minI;
            }

            float carryPh = 0.f;
            if (carry)
            {
                if (isMono) carryPh = (float)voices[si].curvePhase;
                else for (int i = 0; i < MAX_VOICES; ++i)
                    if (voices[i].active && voices[i].soundIndex == snd)
                    { carryPh = (float)voices[i].curvePhase; break; }
            }

            const bool legato = isMono && glideTime > 0.001f
                && voices[si].active
                && voices[si].envStage != PhizmoVoice::Env::Release;
            voices[si].soundIndex = snd;
            voices[si].noteOn(note, vel, isMono ? lastNoteFreq : 0.0,
                              isMono ? glideTime : 0.f, velAmt, carry, carryPh, legato);
            if (!legato) applyEvoStart(si, snd);   // RAND/LOCK/FIX start override
            voiceEvoLFOPhase[si] = (double)(note & 0xF) / 16.0;
            voicePosLFOPhase[si] = (double)((note * 7) & 0xF) / 16.0;
        }
        monoActive = isMono;
        lastNoteFreq = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
    };
    auto stopVoice = [this](int note)
    {
        // "Finish" mode ignores note-off: the envelope runs through all of its
        // stages as programmed (manual, Envelope Modes).
        if (pEnvMode && (int)pEnvMode->load() == 1) return;
        for (auto& v : voices)
            if (v.active && v.midiNote == note && v.envStage != PhizmoVoice::Env::Release)
                v.noteOff();
    };

    const bool arpActive = (pArpOn && pArpOn->load() > 0.5f);

    // --- MIDI ---
    for (const auto md : midiMessages)
    {
        auto msg = md.getMessage();
        // The Keyboard button decides whether played notes are handed to the
        // arpeggiator or bypass it and sound the Preset directly.
        const bool toArp = arpActive
            && (pArpKeyboard == nullptr || pArpKeyboard->load() > 0.5f);

        if (msg.isNoteOn())
        {
            if (toArp)
            {
                arpHeldNotes.add(msg.getNoteNumber());
                arpCurrentVel = applyVelocityCurve((float)msg.getVelocity() / 127.f);
                rebuildArpSequence();
            }
            else
                startVoice(msg.getNoteNumber(), applyVelocityCurve((float)msg.getVelocity() / 127.f));
        }
        else if (msg.isNoteOff())
        {
            if (toArp) { arpHeldNotes.removeValue(msg.getNoteNumber()); rebuildArpSequence(); }
            else        stopVoice(msg.getNoteNumber());
        }
        else if (msg.isPitchWheel())
        {
            bendNorm.store(((float)msg.getPitchWheelValue() - 8192.f) / 8192.f);
        }
        else if (msg.isChannelPressure())
        {
            chanPressure.store((float)msg.getChannelPressureValue() / 127.f);
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const float val = (float)msg.getControllerValue() / 127.f;
            if (cc == 1)  modWheel.store(val);        // mod wheel  (CtL1)
            if (cc == 4)  footCtl.store(val);         // foot pedal (Foot)
            if (cc == 64) sustainPed.store(val);      // sustain    (SuSt)
            if (cc == 66) sostenutoPed.store(val);    // sostenuto  (SOSt)
            if (cc == 16) sys1.store(val);            // System Controller 1
            if (cc == 17) sys2.store(val);            // System Controller 2
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            arpHeldNotes.clear(); arpSequence.clear();
            for (auto& v : voices) { v.active = false; v.envStage = PhizmoVoice::Env::Idle; }
        }
    }

    // --- Arpeggiator clock ---
    if (arpActive)
    {
        int   rateIdx = pArpRate ? juce::jlimit(0, 11, (int)pArpRate->load()) : 8;
        float bpm     = pTempo ? pTempo->load() : 120.f;
        double stepSamples = (double)kNoteBeats[rateIdx] * (60.0 / bpm) * currentSampleRate;
        double gate = pArpGate ? (double)pArpGate->load() : 0.5;
        int blockN = buffer.getNumSamples();

        for (int s = 0; s < blockN; ++s)
        {
            // release the sounding arp note when its gate elapses
            if (arpCurrentNote >= 0 && arpGateSamples > 0.0)
            {
                arpGateSamples -= 1.0;
                if (arpGateSamples <= 0.0) { stopVoice(arpCurrentNote); arpCurrentNote = -1; }
            }
            arpSampleCounter -= 1.0;
            if (arpSampleCounter <= 0.0)
            {
                arpSampleCounter += stepSamples;
                if (!arpSequence.empty())
                {
                    if (arpCurrentNote >= 0) { stopVoice(arpCurrentNote); arpCurrentNote = -1; }
                    int idx;
                    int mode = pArpMode ? (int)pArpMode->load() : 0;
                    if (mode == 3) idx = juce::Random::getSystemRandom().nextInt((int)arpSequence.size());
                    else { idx = arpIndex % (int)arpSequence.size(); ++arpIndex; }
                    int note = arpSequence[(size_t)idx];
                    startVoice(note, arpCurrentVel);
                    arpCurrentNote = note;
                    arpGateSamples = juce::jmax(1.0, stepSamples * gate);
                }
            }
        }
    }
    else if (arpCurrentNote >= 0)
    {
        stopVoice(arpCurrentNote); arpCurrentNote = -1; arpSampleCounter = 0.0;
    }

    // Check if any slot is loaded
    bool anyLoaded = false;
    for (int i = 0; i < NUM_SOUNDS * 2; ++i) anyLoaded = anyLoaded || wt[i].loaded;
    if (!anyLoaded) return;

    int    N = buffer.getNumSamples();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    // Effect Bus scratch (grows if the host sends a larger block than expected)
    if (fxBusBuf.getNumSamples() < N) fxBusBuf.setSize(4, N, false, false, true);
    fxBusBuf.clear();
    float* fxBusL = fxBusBuf.getWritePointer(0);
    float* fxBusR = fxBusBuf.getWritePointer(1);
    float* revBufL = fxBusBuf.getWritePointer(2);
    float* revBufR = fxBusBuf.getWritePointer(3);
    gainSmooth.setTargetValue(pGain->load());

    // Read params once per block
    float pitchLFORate = pPitchLFORate->load();
    float pitchLFODep = pPitchLFO->load();
    float posLFORate = pPosLFORate->load();
    float chRate = pChorusRate->load();
    float chDepth = pChorusDepth->load();

    // --- the original real-time macros that affect the FX bus (F = effect mod, O = assignable) ---
    float phizF = pFizF ? pFizF->load() : 0.f;
    float phizO = pFizO ? pFizO->load() : 0.f;
    int   phizODest = pFizODest ? (int)pFizODest->load() : 0;
    // --- Phizmo effects: insert algorithm + global reverb --------------------
    // The F realtime knob is the hardware's "effect mod": it sweeps the insert
    // effect's character. O can be assigned to reverb wet or effect mix.
    const int   fxAlgo = pFxAlgo ? (int)pFxAlgo->load() : 0;
    float fxVariation  = pFxVariation ? pFxVariation->load() : 0.35f;
    float fxMix        = pFxMix ? pFxMix->load() : 0.35f;
    float reverbAmount = juce::jlimit(0.f, 1.f, (pRevAmount ? pRevAmount->load() : 0.25f));
    if (phizODest == 0) reverbAmount = juce::jlimit(0.f, 1.f, reverbAmount + phizO * 0.5f);
    if (phizODest == 4) fxMix        = juce::jlimit(0.f, 1.f, fxMix + phizO * 0.5f);

    const int revVar = pRevVariation ? (int)pRevVariation->load() : 3;
    if (revVar != lastRevVariation) { phizReverb.setVariation(revVar); lastRevVariation = revVar; }

    // Advance global pitch LFO and pos LFO (block-level, used for filter mod)
    // We evaluate once at block midpoint for simplicity
    pitchLFOPhase += pitchLFORate * (N * 0.5) / currentSampleRate;
    if (pitchLFOPhase > 1.0) pitchLFOPhase -= 1.0;
    double posLFOPhaseBlock = 0.0; // block-level pos LFO for filter
    posLFOPhaseBlock += posLFORate * (N * 0.5) / currentSampleRate;
    if (posLFOPhaseBlock > 1.0) posLFOPhaseBlock -= 1.0;
    blockPosLFO = (float)std::sin(posLFOPhaseBlock * juce::MathConstants<double>::twoPi);
    blockPitchLFO = (float)std::sin(pitchLFOPhase * juce::MathConstants<double>::twoPi);
    // Pitch bend wheel (manual: bEnd, factory default +/-2 semitones)
    const double bendSemis = (double)bendNorm.load() * (pBendRange ? (double)pBendRange->load() : 2.0);
    // Global LFO — "optimised for producing vibrato when the modulation wheel
    // is pushed forward" (manual). Mod wheel and aftertouch both open it up.
    globalLfoPhase += 5.2 / currentSampleRate * buffer.getNumSamples();
    globalLfoPhase -= std::floor(globalLfoPhase);
    globalLfo = (float)std::sin(globalLfoPhase * juce::MathConstants<double>::twoPi);
    const double vib = (double)globalLfo
                     * ((double)modWheel.load() + (double)chanPressure.load() * 0.5) * 0.5;
    double pitchMult = std::pow(2.0, ((double)(blockPitchLFO * pitchLFODep) + bendSemis + vib) / 12.0);

    // Update display playhead from first active voice
    for (auto& v : voices)
    {
        if (v.active)
        {
            evoPlayhead.store((float)v.curvePhase);
            // Update activeSlot display based on mix
            activeSlot.store(pOscMix->load() > 0.5f ? 1 : 0);
            break;
        }
    }

    // --- Refresh the edit buffer into the focused Sound, then mirror each
    //     Sound's layer settings (enable / key zone / level) ------------------
    if (!soundSwapInProgress.load() && !presetLoading.load())
        captureEditBuffer(editSound);
    for (int i = 0; i < NUM_SOUNDS; ++i)
    {
        soundParams[i].enabled   = pSoundOn[i]   ? pSoundOn[i]->load() > 0.5f : (i == 0);
        soundParams[i].lowKey    = pSoundLow[i]  ? (int)pSoundLow[i]->load()  : 0;
        soundParams[i].highKey   = pSoundHigh[i] ? (int)pSoundHigh[i]->load() : 127;
        soundParams[i].layerGain = pSoundGain[i] ? pSoundGain[i]->load()      : 1.f;
    }

    // --- Per-Sound block-level envelope coefficients (avoids exp/pow per sample) ---
    {
        float isr = (float)(1.0 / currentSampleRate);
        auto makeDecCoeff = [&](float time) {
            return std::exp(-isr * 5.f / juce::jmax(0.001f, time));
            };
        for (int i = 0; i < NUM_SOUNDS; ++i)
        {
            const SoundParams& sp = soundParams[i];
            BlockEnvCoeffs& b = blkS[i];
            b.decCoeff  = makeDecCoeff(sp.decay);
            b.relCoeff  = makeDecCoeff(sp.release);
            b.dec2Coeff = makeDecCoeff(sp.decay2);
            b.rel2Coeff = makeDecCoeff(sp.release2);
            b.fdecCoeff = makeDecCoeff(sp.filterDec);
            b.frelCoeff = makeDecCoeff(sp.filterRel);
            b.penvDecCoeff = makeDecCoeff(sp.pitchEnvDec);
            b.twDecCoeff   = makeDecCoeff(sp.twDec);
            b.twRelCoeff   = makeDecCoeff(sp.twRel);

            b.dm = std::pow(2.0, (double)sp.detune / 1200.0);
            float sc = sp.spread;
            b.sA = std::pow(2.0, -(double)sc / 2400.0);
            b.sB = std::pow(2.0, (double)sc / 2400.0);
            b.octA = std::pow(2.0, std::round((double)sp.octaveA));
            b.octB = std::pow(2.0, std::round((double)sp.octaveB));
            b.tuneMA = std::pow(2.0, ((double)sp.tuneA + (double)sp.fineA * 0.01) / 12.0);
            b.tuneMB = std::pow(2.0, ((double)sp.tuneB + (double)sp.fineB * 0.01) / 12.0);
        }
    }

    // --- Per-sample synthesis ---
    // Acquire wavetable locks ONCE for the entire block (not per sample).
    float noiseAmt = pNoise->load();
    noiseFilter.setType(pNoiseFilterType ? (int)pNoiseFilterType->load() : 0,
                        pNoiseFreq ? pNoiseFreq->load() : 4000.f,
                        juce::jmax(0.3f, pNoiseQ ? pNoiseQ->load() : 0.7f),
                        currentSampleRate);
    {
        // Lock every slot in use (8 = 4 Sounds x 2 oscillators)
        for (int i = 0; i < NUM_SOUNDS * 2; ++i) wt[i].lock.enter();

        // FIX-mode master scan: one perpetual, looping playhead per Sound+osc,
        // advanced every sample whether or not a note is playing.
        auto advanceFixMaster = [sr = currentSampleRate]
            (double& ph, int& dir, float timeSec, int scanStyleInt)
        {
            const double inc = 1.0 / ((double)juce::jmax(0.1f, timeSec) * sr);
            switch ((ScanMode)juce::jlimit(0, 4, scanStyleInt))
            {
            case ScanMode::BackForth:
                ph += inc * dir;
                if      (ph >= 1.0) { ph = 1.0; dir = -1; }
                else if (ph <= 0.0) { ph = 0.0; dir =  1; }
                break;
            case ScanMode::Backward:
            case ScanMode::BwdStay:
                ph -= inc; if (ph <= 0.0) ph += 1.0;
                break;
            default:                                  // Forward / FwdStay → loop
                ph += inc; if (ph >= 1.0) ph -= 1.0;
                break;
            }
        };

        for (int s = 0; s < N; ++s)
        {
            for (int snd = 0; snd < NUM_SOUNDS; ++snd)
            {
                const SoundParams& sp = soundParams[snd];
                if ((int)std::round(sp.evoMode)  == 3)
                    advanceFixMaster(evoFixPhase[snd][0], evoFixDir[snd][0],
                                     sp.evoTime,  (int)sp.scanStyle);
                if ((int)std::round(sp.evoModeB) == 3)
                    advanceFixMaster(evoFixPhase[snd][1], evoFixDir[snd][1],
                                     sp.evoTimeB, (int)sp.scanStyleB);
            }
            float mixL = 0.f, mixR = 0.f;    // dry bus
            float busL = 0.f, busR = 0.f;    // insert-effect bus
            float revL = 0.f, revR = 0.f;    // global-reverb send (everything feeds it)
            for (int vi = 0; vi < MAX_VOICES; ++vi)
            {
                float vL = 0.f, vR = 0.f, vBL = 0.f, vBR = 0.f;
                const int sIdx = juce::jlimit(0, NUM_SOUNDS - 1, voices[vi].soundIndex);
                synthesiseVoice(voices[vi], vi, blockPosLFO, pitchMult, vL, vR, vBL, vBR,
                                blkS[sIdx], soundParams[sIdx]);
                const float lg = soundParams[sIdx].layerGain;
                vL *= lg; vR *= lg; vBL *= lg; vBR *= lg;

                // Each oscillator either goes through the Insert effect or stays
                // dry. Global reverb is a separate send that always receives the
                // full signal, so the Reverb Amount knob works on its own with
                // nothing else to set up first.
                auto route = [&](int bus, float l, float r)
                {
                    if (bus == 0) { busL += l; busR += r; }   // Insert
                    else          { mixL += l; mixR += r; }   // Dry
                    revL += l; revR += r;                     // always feed reverb
                };
                route(pFxBusA[sIdx] ? (int)pFxBusA[sIdx]->load() : 0, vL,  vR);
                route(pFxBusB[sIdx] ? (int)pFxBusB[sIdx]->load() : 0, vBL, vBR);
            }

            // Noise through its own filter, at a level that sits under the
            // oscillators rather than on top of them.
            if (noiseAmt > 0.0001f)
            {
                float nL = noiseFilter.processL(fastRand() * 2.f - 1.f);
                float nR = noiseFilter.processR(fastRand() * 2.f - 1.f);
                mixL += nL * noiseAmt * noiseAmt * 0.08f;
                mixR += nR * noiseAmt * noiseAmt * 0.08f;
            }

            // Soft clip on all three buses. x / sqrt(1 + x^2) has unity slope
            // at zero and asymptotes to 1.0, so normal-level material passes
            // through untouched instead of being attenuated ~10 dB the way the
            // old scaled version was. That lost headroom was what made the
            // gain-compensated effects sound quiet by comparison.
            auto softClip = [](float x) { return x / std::sqrt(1.f + x * x); };
            mixL = softClip(mixL); mixR = softClip(mixR);
            busL = softClip(busL); busR = softClip(busR);
            revL = softClip(revL); revR = softClip(revR);

            float g = gainSmooth.getNextValue();
            outL[s] = mixL * g;
            outR[s] = mixR * g;
            fxBusL[s] = busL * g;
            fxBusR[s] = busR * g;
            revBufL[s] = revL * g;
            revBufR[s] = revR * g;
        }
        for (int i = NUM_SOUNDS * 2 - 1; i >= 0; --i) wt[i].lock.exit();
    } // wavetable locks released here

    // Mirror the FIX-mode master scans for the editor's always-on playhead.
    for (int snd = 0; snd < NUM_SOUNDS; ++snd)
    {
        evoFixPub[snd][0].store((float)evoFixPhase[snd][0]);
        evoFixPub[snd][1].store((float)evoFixPhase[snd][1]);
    }

    // --- Insert effect on the Effect Bus, summed back, then global reverb ---
    for (int s = 0; s < N; ++s)
    {
        // Insert effect operates on its own bus
        float bL = fxBusL[s], bR = fxBusR[s];
        phizInsert.process(bL, bR, fxAlgo, fxVariation, fxMix, phizF);
        outL[s] += bL;
        outR[s] += bR;

        // Global reverb: a true send. The dry signal is already in outL/outR
        // (via the dry and insert buses); here we add only the wet return,
        // scaled by the Reverb Amount knob. Amount 0 = no reverb, and no other
        // control has to be set for it to be audible.
        float rL = revBufL[s], rR = revBufR[s];
        phizReverb.process(rL, rR, 1.0f);     // fully wet tail
        outL[s] += rL * reverbAmount;
        outR[s] += rR * reverbAmount;

        // --- Output limiter -------------------------------------------------
        // Dry + insert + reverb are three independent paths summed together,
        // so the total can exceed full scale even when each one behaves. A
        // shared gain-reduction envelope keeps the stereo image intact.
        const float peak = juce::jmax(std::abs(outL[s]), std::abs(outR[s]));
        const float kCeiling = 0.98f;
        const float target = (peak > kCeiling) ? (kCeiling / peak) : 1.f;
        // fast attack, slow release
        limGain += (target < limGain ? 0.35f : 0.0008f) * (target - limGain);
        limGain = juce::jlimit(0.05f, 1.f, limGain);
        outL[s] *= limGain;
        outR[s] *= limGain;
    }
}

//==============================================================================
void PhizmoAudioProcessor::parameterChanged(const juce::String& id, float val)
{
    if (id.startsWith("evoPointB_"))
    {
        int idx = id.substring(10).getIntValue();
        if (idx >= 0 && idx < EVO_POINTS) evoCurveB[idx].store(val);
    }
    else if (id.startsWith("evoPoint_"))
    {
        int idx = id.substring(9).getIntValue();
        if (idx >= 0 && idx < EVO_POINTS) evoCurve[idx].store(val);
    }
}


static void syncEvoCurvesFromApvts(PhizmoAudioProcessor& proc)
{
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String idA = "evoPoint_" + juce::String::formatted("%02d", i);
        if (auto* p = proc.apvts.getRawParameterValue(idA)) proc.evoCurve[i].store(p->load());
        juce::String idB = "evoPointB_" + juce::String::formatted("%02d", i);
        if (auto* p = proc.apvts.getRawParameterValue(idB)) proc.evoCurveB[i].store(p->load());
    }
}

void PhizmoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    writePresetZip(destData, true);        // host state keeps the local folders
}

void PhizmoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream mis(data, (size_t)sizeInBytes, false);
    readPresetZip(mis, true);
}

int PhizmoAudioProcessor::getGuiWidth() const
{
    if (auto* p = apvts.getRawParameterValue("guiWidth"))
        return juce::jlimit(595, 2380, (int)p->load());
    return 1200;
}

int PhizmoAudioProcessor::getGuiHeight() const
{
    // Height is derived from the fixed design aspect ratio.
    return juce::roundToInt(getGuiWidth() * (714.f / 1300.f));
}

void PhizmoAudioProcessor::setGuiWidth(int w)
{
    if (auto* p = apvts.getParameter("guiWidth"))
        p->setValueNotifyingHost(p->convertTo0to1((float)w));
}

int PhizmoAudioProcessor::getCycleSizeParam(int slot) const
{
    const char* id = (slot == 0) ? "cycleSizeA" : "cycleSizeB";
    if (auto* p = apvts.getRawParameterValue(id))
        return juce::jlimit(16, 65536, (int)p->load());
    return 2048;
}

void PhizmoAudioProcessor::setCycleSizeParam(int slot, int size)
{
    const char* id = (slot == 0) ? "cycleSizeA" : "cycleSizeB";
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1((float)size));
}

// Documents/Phizmo Presets and Documents/Phizmo Samples, created on first use
// so the file dialogs always have somewhere real to open. A chooser pointed at
// a folder that does not exist is what made the Load buttons look dead.
juce::File PhizmoAudioProcessor::getPresetsDirectory()
{
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                 .getChildFile("Phizmo").getChildFile("Presets");
    if (!d.isDirectory()) d.createDirectory();   // creates Phizmo/ too
    return d.isDirectory() ? d
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
}

juce::File PhizmoAudioProcessor::getSamplesDirectory()
{
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                 .getChildFile("Phizmo").getChildFile("Samples");
    if (!d.isDirectory()) d.createDirectory();   // creates Phizmo/ too
    return d.isDirectory() ? d
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
}

juce::File PhizmoAudioProcessor::getEffectivePresetsDirectory() const
{
    if (presetFolder.isNotEmpty())
    {
        juce::File f(presetFolder);
        if (f.isDirectory()) return f;
    }
    return getPresetsDirectory();
}

//==============================================================================
// Unified preset container. One writer and one reader, used by savePreset(),
// loadPreset(), getStateInformation() and setStateInformation() alike.
//
// Layout:   state.xml          full APVTS + the four Sound snapshots
//           S0/..S7/<name>.wav raw bytes of each loaded wavetable
//
// Only file NAMES are stored, never absolute paths, so a shared preset never
// exposes the author's folder structure.
//==============================================================================
void PhizmoAudioProcessor::writePresetZip(juce::MemoryBlock& dest,
                                             bool includeMachineLocalSettings)
{
    captureEditBuffer(editSound);          // freeze what the knobs currently show

    auto xml = std::make_unique<juce::XmlElement>("PhizmoState");
    xml->addChildElement(apvts.copyState().createXml().release());
    xml->setAttribute("version", kPresetFormatVersion);
    xml->setAttribute("activeSlot", activeSlot.load());
    xml->addChildElement(createSoundsXml());

    if (includeMachineLocalSettings)
    {
        xml->setAttribute("sampleFolder", sampleFolder);
        xml->setAttribute("presetFolder", presetFolder);
        xml->setAttribute("presetName",   currentPresetName);
    }

    juce::ZipFile::Builder zb;
    addAllSlotsToZip(zb, *xml);            // adds the slotFile/slotCycle attributes

    const juce::String xmlString = xml->toString();   // serialise AFTER the slot attributes
    zb.addEntry(new juce::MemoryInputStream(xmlString.toUTF8(),
                                            xmlString.getNumBytesAsUTF8(), true),
                9, "state.xml", juce::Time::getCurrentTime());

    juce::MemoryOutputStream mos(dest, false);
    zb.writeToStream(mos, nullptr);
}

bool PhizmoAudioProcessor::readPresetZip(juce::InputStream& src,
                                            bool includeMachineLocalSettings)
{
    juce::ZipFile zip(&src, false);
    if (zip.getNumEntries() <= 0) return false;

    const int stateIdx = zip.getIndexOfFileName("state.xml");
    if (stateIdx < 0) return false;

    std::unique_ptr<juce::InputStream> stateStream(zip.createStreamForEntry(stateIdx));
    if (!stateStream) return false;

    auto xml = juce::XmlDocument::parse(stateStream->readEntireStreamAsString());
    if (!xml || xml->getTagName() != "PhizmoState") return false;

    // Hold off the audio thread's edit-buffer snapshot for the whole install,
    // otherwise processBlock can overwrite the Sound we are restoring.
    presetLoading.store(true);

    if (auto* c = xml->getFirstChildElement())
    {
        auto vt = juce::ValueTree::fromXml(*c);
        if (vt.isValid()) apvts.replaceState(vt);
    }
    syncEvoCurvesFromApvts(*this);
    restoreSoundsXml(xml->getChildByName("SOUNDS"));
    activeSlot.store(xml->getIntAttribute("activeSlot", 0));

    if (includeMachineLocalSettings)
    {
        sampleFolder      = xml->getStringAttribute("sampleFolder");
        presetFolder      = xml->getStringAttribute("presetFolder");
        currentPresetName = xml->getStringAttribute("presetName");
    }

    restoreAllSlotsFromZip(zip, *xml);     // all 8 slots (S0..S7)

    // The SOUNDS block is authoritative, so push the focused Sound back out
    // into the APVTS edit buffer. Without this the panel and the engine can
    // disagree about the Sound that is being edited.
    const int es = juce::jlimit(0, NUM_SOUNDS - 1,
        xml->getChildByName("SOUNDS") != nullptr
            ? xml->getChildByName("SOUNDS")->getIntAttribute("editSound", 0) : 0);
    editSound = es;
    applySoundToEditBuffer(es);

    presetLoading.store(false);
    return true;
}

bool PhizmoAudioProcessor::savePreset(const juce::File& dest)
{
    juce::MemoryBlock mb;
    writePresetZip(mb, false);             // shared preset: no local folder paths
    if (mb.getSize() == 0) return false;

    juce::TemporaryFile tempFile(dest);
    {
        juce::FileOutputStream out(tempFile.getFile());
        if (!out.openedOk()) return false;
        if (!out.write(mb.getData(), mb.getSize())) return false;
    }
    const bool ok = tempFile.overwriteTargetFileWithTemporary();
    if (ok) storeCompareSnapshot();
    return ok;
}

bool PhizmoAudioProcessor::loadPreset(const juce::File& src)
{
    juce::FileInputStream in(src);
    if (!in.openedOk()) return false;
    if (!readPresetZip(in, false)) return false;
    storeCompareSnapshot();
    return true;
}

//==============================================================================
// A/B compare. storeCompareSnapshot() captures the reference version (called
// whenever a Preset is loaded or saved); toggleCompare() swaps the live state
// with it, so pressing twice returns exactly where you started.
//==============================================================================
void PhizmoAudioProcessor::storeCompareSnapshot()
{
    compareSnapshot.reset();
    writePresetZip(compareSnapshot, false);
    comparing = false;
}

void PhizmoAudioProcessor::toggleCompare()
{
    if (compareSnapshot.getSize() == 0) { storeCompareSnapshot(); return; }

    juce::MemoryBlock live;
    writePresetZip(live, false);

    juce::MemoryInputStream mis(compareSnapshot, false);
    if (readPresetZip(mis, false))
    {
        compareSnapshot = live;      // so the next press swaps back
        comparing = !comparing;
    }
}

//==============================================================================
// Split: lay the enabled Sounds out across the keyboard in the factory zones,
// then extend each one to fill the gap left by any disabled Sound above it,
// and drop the lowest one to the bottom of the keyboard (User's Guide p.17).
//==============================================================================
void PhizmoAudioProcessor::applySplitZones()
{
    // Factory boundaries: Sound 1 owns the bottom two octaves, then one zone
    // each up the keyboard. The upper edge of each zone is derived from the
    // next enabled Sound rather than stored, so gaps close automatically.
    static const int lows[NUM_SOUNDS] = { 0, 48, 72, 96 };

    int enabled[NUM_SOUNDS], n = 0;
    for (int i = 0; i < NUM_SOUNDS; ++i)
        if (isSoundEnabled(i)) enabled[n++] = i;
    if (n == 0) return;

    // Each enabled Sound starts at its own factory boundary, except the lowest
    // which drops to the bottom of the keyboard...
    int lo[NUM_SOUNDS], hi[NUM_SOUNDS];
    for (int k = 0; k < n; ++k)
        lo[k] = (k == 0) ? 0 : lows[enabled[k]];

    // ...and each one runs up to just below the next enabled Sound, so any gap
    // left by a disabled Sound is absorbed by the one beneath it.
    for (int k = 0; k < n; ++k)
        hi[k] = (k == n - 1) ? 127 : juce::jmax(lo[k], lo[k + 1] - 1);

    for (int k = 0; k < n; ++k)
    {
        const juce::String idx(enabled[k]);
        if (auto* pl = apvts.getParameter("soundLow"  + idx))
            pl->setValueNotifyingHost(pl->convertTo0to1((float)lo[k]));
        if (auto* ph = apvts.getParameter("soundHigh" + idx))
            ph->setValueNotifyingHost(ph->convertTo0to1((float)hi[k]));
    }
}

//==============================================================================
juce::AudioProcessorEditor* PhizmoAudioProcessor::createEditor()
{
    return new PhizmoAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhizmoAudioProcessor();
}