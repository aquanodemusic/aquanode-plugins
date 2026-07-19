#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
TranswaveAudioProcessor::createParameterLayout()
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
        "filterPerVoice", "Per-Voice Filter", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "velToFrame", "Velocity to Frame Pos", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoPhaseCarry", "Evo Phase Carry", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));

    // --- Transwave position envelope (Fizmo-style) ---
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
        "keytrack", "Keytrack to Frame",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoTimeB", "Evo Time Osc B (s)",
        juce::NormalisableRange<float>(0.1f, 100.f, 0.001f, 0.25f), 4.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoRestart", "Evo Restart on Note",
        juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f));

    // GUI window width — stored in APVTS so the DAW saves/restores it
    // automatically. Height is always derived from the fixed 1190:804 ratio.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "guiWidth", "GUI Width",
        juce::NormalisableRange<float>(595.f, 2380.f, 1.f), 1190.f));

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
TranswaveAudioProcessor::TranswaveAudioProcessor()
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

    pEvoTime = apvts.getRawParameterValue("evoTime");
    pEvoStepped = apvts.getRawParameterValue("evoStepped");
    pEvoSteppedB = apvts.getRawParameterValue("evoSteppedB");
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
    pFrameInterp = apvts.getRawParameterValue("frameInterp");
    pFilterPerVoice = apvts.getRawParameterValue("filterPerVoice");
    pVelToFrame = apvts.getRawParameterValue("velToFrame");
    pEvoPhaseCarry = apvts.getRawParameterValue("evoPhaseCarry");
    pTwAtt = apvts.getRawParameterValue("twAtt");
    pTwDec = apvts.getRawParameterValue("twDec");
    pTwSus = apvts.getRawParameterValue("twSus");
    pTwRel = apvts.getRawParameterValue("twRel");
    pTwAmt = apvts.getRawParameterValue("twAmt");
    pTwVelAmt = apvts.getRawParameterValue("twVelAmt");
    pFrameSnap = apvts.getRawParameterValue("frameSnap");
    pTwToFilter = apvts.getRawParameterValue("twToFilter");
    pEvoBPhaseOff = apvts.getRawParameterValue("evoBPhaseOff");
    pKeytrack = apvts.getRawParameterValue("keytrack");
    pEvoTimeB = apvts.getRawParameterValue("evoTimeB");
    pEvoRestart = apvts.getRawParameterValue("evoRestart");

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

TranswaveAudioProcessor::~TranswaveAudioProcessor()
{
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        apvts.removeParameterListener("evoPoint_" + juce::String::formatted("%02d", i), this);
        apvts.removeParameterListener("evoPointB_" + juce::String::formatted("%02d", i), this);
    }
}

//==============================================================================
void TranswaveAudioProcessor::setCurvePoint(int idx, float val, int osc)
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

float TranswaveAudioProcessor::evalCurve(float t, int osc) const
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

float TranswaveAudioProcessor::getCurrentEvoFramePos(int osc) const
{
    return evalCurve(evoPlayhead.load(), osc);
}

//==============================================================================
void TranswaveAudioProcessor::prepareToPlay(double sr, int)
{
    currentSampleRate = sr;
    gainSmooth.reset(sr, 0.05);
    gainSmooth.setCurrentAndTargetValue(pGain->load());

    pitchLFOPhase = 0.0;
    std::fill(voiceEvoLFOPhase, voiceEvoLFOPhase + MAX_VOICES, 0.0);
    std::fill(voicePosLFOPhase, voicePosLFOPhase + MAX_VOICES, 0.0);

    filter.reset(); chorus.reset(); reverb.reset();

    juce::Reverb::Parameters rp;
    rp.roomSize = pReverbSize->load(); rp.damping = pReverbDamp->load();
    rp.wetLevel = pReverbWet->load();  rp.dryLevel = 1.f - pReverbWet->load();
    rp.width = 1.f; rp.freezeMode = 0.f;
    reverb.setParameters(rp);

    for (auto& v : voices)
    {
        v.active = false;
        v.envStage = TranswaveVoice::Env::Idle;
        v.fenvStage = TranswaveVoice::Env::Idle;
        v.twenvStage = TranswaveVoice::Env::Idle;
        v.envLevel = v.fenvLevel = v.twenvLevel = 0.f;
        v.phaseA = v.phaseB = 0.0;
        v.curvePhase = 0.0; v.scanDir = 1; v.curveFinished = false;
        v.frameOffset = 0.f;
    }
}

void TranswaveAudioProcessor::releaseResources() {}

//==============================================================================
void TranswaveAudioProcessor::loadWavetable(const juce::File& file, int cs, int slot)
{
    jassert(slot == 0 || slot == 1);
    juce::MemoryBlock raw;
    if (!file.loadFileAsData(raw)) return;

    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
    if (!reader) return;

    loadWavetableFromReader(std::move(reader), std::move(raw), file.getFileName(),
        file.getFullPathName(), cs, slot);
}

bool TranswaveAudioProcessor::loadWavetableFromMemory(const juce::MemoryBlock& fileData,
    const juce::String& originalFileName, int cs, int slot)
{
    jassert(slot == 0 || slot == 1);
    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    auto mis = std::make_unique<juce::MemoryInputStream>(fileData, false);
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(std::move(mis)));
    if (!reader) return false;

    return loadWavetableFromReader(std::move(reader), fileData, originalFileName,
        juce::String(), cs, slot);
}

bool TranswaveAudioProcessor::loadWavetableFromReader(std::unique_ptr<juce::AudioFormatReader> reader,
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

    for (auto& v : voices) { v.active = false; v.envStage = TranswaveVoice::Env::Idle; }
    return true;
}

bool         TranswaveAudioProcessor::isWavetableLoaded(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].loaded; }
int          TranswaveAudioProcessor::getNumFrames(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].numFrames; }
int          TranswaveAudioProcessor::getCycleSamples(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].cycleSamples; }
juce::String TranswaveAudioProcessor::getWavetableName(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].name; }
juce::String TranswaveAudioProcessor::getWavetableFilePath(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].filePath; }

bool TranswaveAudioProcessor::getFrameSamples(int slot, int fi, std::vector<float>& o) const {
    juce::ScopedLock l(wt[slot].lock);
    if (wt[slot].frames.empty() || fi < 0 || fi >= wt[slot].numFrames) return false;
    o = wt[slot].frames[(size_t)fi]; return true;
}

bool TranswaveAudioProcessor::getWavetableOverview(int slot, int dw, int, std::vector<float>& o) const {
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
float TranswaveAudioProcessor::sampleFrameRaw(const WavetableSlot& s, float fi, double ph, bool interp) const {
    if (s.frames.empty()) return 0.f;
    int i0 = juce::jlimit(0, s.numFrames - 1, (int)fi);
    int i1 = juce::jlimit(0, s.numFrames - 1, i0 + 1);
    float bl = fi - (float)i0;

    if (interp)
    {
        // Bilinear: interpolate within the cycle as well as between frames
        float fsi = (float)(ph * s.cycleSamples);
        int si0 = (int)fsi % s.cycleSamples;
        int si1 = (si0 + 1) % s.cycleSamples;
        float sfrac = fsi - (float)(int)fsi;
        float smp0 = s.frames[(size_t)i0][(size_t)si0] + sfrac * (s.frames[(size_t)i0][(size_t)si1] - s.frames[(size_t)i0][(size_t)si0]);
        float smp1 = s.frames[(size_t)i1][(size_t)si0] + sfrac * (s.frames[(size_t)i1][(size_t)si1] - s.frames[(size_t)i1][(size_t)si0]);
        return smp0 + bl * (smp1 - smp0);
    }
    else
    {
        // Original nearest-neighbour within cycle, linear between frames
        int si = juce::jlimit(0, s.cycleSamples - 1, (int)(ph * s.cycleSamples) % s.cycleSamples);
        return s.frames[(size_t)i0][(size_t)si] + bl * (s.frames[(size_t)i1][(size_t)si] - s.frames[(size_t)i0][(size_t)si]);
    }
}

float TranswaveAudioProcessor::sampleFrameNearest(int slot, float fi, double ph) {
    juce::ScopedLock l(wt[slot].lock); return sampleFrameRaw(wt[slot], fi, ph, false);
}

float TranswaveAudioProcessor::applyBitCrush(float s, float bits) {
    if (bits >= 15.9f) return s;
    float lv = std::pow(2.f, bits) - 1.f; return std::round(s * lv) / lv;
}

//==============================================================================
// Exponential envelope helper: returns new level after one sample.
// decCoeff / relCoeff are precomputed as std::exp(-isr * 5 / time) once per block.
static inline float advanceEnvExp(float level, float targetSus, TranswaveVoice::Env stage,
    float att, float sus,
    float decCoeff, float relCoeff,
    float isr,
    TranswaveVoice::Env& nextStage, float& nextReleaseStart)
{
    const float kFloor = 1e-4f;
    switch (stage)
    {
    case TranswaveVoice::Env::Attack:
        level += isr / att;
        if (level >= 1.0f) { level = 1.0f; nextStage = TranswaveVoice::Env::Decay; }
        break;
    case TranswaveVoice::Env::Decay:
        level = sus + (level - sus) * decCoeff;
        if (level <= sus + kFloor) { level = sus; nextStage = TranswaveVoice::Env::Sustain; }
        break;
    case TranswaveVoice::Env::Sustain:
        level = sus;
        break;
    case TranswaveVoice::Env::Release:
        level = level * relCoeff;
        if (level <= kFloor) { level = 0.f; nextStage = TranswaveVoice::Env::Idle; }
        break;
    default: level = 0.f; break;
    }
    (void)targetSus; (void)nextReleaseStart;
    return level;
}

//==============================================================================
void TranswaveAudioProcessor::synthesiseVoice(TranswaveVoice& v, int vi,
    float posLFOMod, double pitchMult, float& outL, float& outR,
    const BlockEnvCoeffs& bc)
{
    outL = outR = 0.f;
    if (!v.active) return;
    (void)posLFOMod;

    float isr = (float)(1.0 / currentSampleRate);

    // --- Osc 1 amplitude envelope ---
    float att = juce::jmax(0.001f, pAttack->load());
    float sus = pSustain->load();
    TranswaveVoice::Env nextAmpStage = v.envStage;
    float nextAmpRelStart = v.releaseStartLevel;
    v.envLevel = advanceEnvExp(v.envLevel, sus, v.envStage, att, sus,
        bc.decCoeff, bc.relCoeff, isr, nextAmpStage, nextAmpRelStart);
    v.envStage = nextAmpStage;

    // --- Osc 2 amplitude envelope ---
    float att2 = juce::jmax(0.001f, pAttack2->load());
    float sus2 = pSustain2->load();
    TranswaveVoice::Env nextEnv2Stage = v.env2Stage;
    float nextEnv2RelStart = v.rel2StartLevel;
    v.env2Level = advanceEnvExp(v.env2Level, sus2, v.env2Stage, att2, sus2,
        bc.dec2Coeff, bc.rel2Coeff, isr, nextEnv2Stage, nextEnv2RelStart);
    v.env2Stage = nextEnv2Stage;

    // Voice dies only when BOTH envelopes are idle
    if (v.envStage == TranswaveVoice::Env::Idle && v.env2Stage == TranswaveVoice::Env::Idle)
    {
        v.active = false; return;
    }

    // --- Filter envelope ---
    float fatt = juce::jmax(0.001f, pFilterAtt->load());
    float fsus = pFilterSus->load();
    TranswaveVoice::Env nextFEnvStage = v.fenvStage;
    float nextFRelStart = v.fenvReleaseStart;
    v.fenvLevel = advanceEnvExp(v.fenvLevel, fsus, v.fenvStage, fatt, fsus,
        bc.fdecCoeff, bc.frelCoeff, isr, nextFEnvStage, nextFRelStart);
    v.fenvStage = nextFEnvStage;

    // --- Pitch envelope ---
    float penvAmt = pPitchEnvAmt->load();
    if (!v.penvDone)
    {
        v.penvLevel *= bc.penvDecCoeff;
        if (v.penvLevel < 1e-4f) { v.penvLevel = 0.f; v.penvDone = true; }
    }
    double pitchEnvMult = std::pow(2.0, (double)(v.penvLevel * penvAmt) / 12.0);

    // --- Per-voice evo LFO ---
    voiceEvoLFOPhase[vi] += pEvoLFORate->load() / currentSampleRate;
    if (voiceEvoLFOPhase[vi] > 1.0) voiceEvoLFOPhase[vi] -= 1.0;
    float evoLFO = (float)std::sin(voiceEvoLFOPhase[vi] * juce::MathConstants<double>::twoPi);

    // --- Per-voice pos LFO ---
    voicePosLFOPhase[vi] += pPosLFORate->load() / currentSampleRate;
    if (voicePosLFOPhase[vi] > 1.0) voicePosLFOPhase[vi] -= 1.0;
    float vPosLFO = (float)std::sin(voicePosLFOPhase[vi] * juce::MathConstants<double>::twoPi);

    // --- Per-voice curve scan (single shared scan position, drives BOTH curves) ---
    float evoTime = juce::jmax(0.1f, pEvoTime->load());
    // Osc B can have an independent evo time; if evoTimeB matches evoTime (default 4s) they lock.
    float evoTimeB = juce::jmax(0.1f, pEvoTimeB->load());
    double phaseInc = 1.0 / (evoTime * currentSampleRate);
    double phaseIncB = 1.0 / (evoTimeB * currentSampleRate);
    auto scanMode = (ScanMode)(int)juce::jlimit(0.f, 4.f, pScanStyle->load());

    if (!v.curveFinished)
    {
        switch (scanMode)
        {
        case ScanMode::Forward:
            v.curvePhase += phaseInc;
            if (v.curvePhase >= 1.0) v.curvePhase -= 1.0;
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
            if (v.curvePhase <= 0.0) v.curvePhase += 1.0;
            break;
        }
    }

    // Osc B gets its own scan phase: shared A phase + user offset + independent speed drift.
    // phaseIncB - phaseInc is the per-sample drift; integrate over time via curvePhase as base.
    float bPhaseOff = pEvoBPhaseOff->load();
    double curvePhaseBraw = v.curvePhase + (double)bPhaseOff
        + (phaseIncB - phaseInc) * (v.curvePhase / juce::jmax(phaseInc, 1e-12));
    // Keep in [0,1)
    curvePhaseBraw = std::fmod(curvePhaseBraw, 1.0);
    if (curvePhaseBraw < 0.0) curvePhaseBraw += 1.0;

    // --- Keytrack to frame position ---
    // Maps MIDI note linearly: note 60 (C4) = centre (0.5), full range = ±5 octaves.
    float keytrackAmt = pKeytrack->load();
    float keytrackMod = ((float)(v.midiNote - 60) / 60.f) * keytrackAmt * 0.5f;  // ±0.5 at extremes

    // --- Transwave position envelope (Fizmo-style) ---
    // Advances independently of the curve scan; outputs a signed normalised
    // offset that is added to the curve's 0-1 position BEFORE frame scaling.
    // twAmt=0 (default) to zero contribution, existing patches unaffected.
    float twSus = pTwSus->load();
    TranswaveVoice::Env nextTwStage = v.twenvStage;
    float twRelStartUnused = v.twenvRelStart;
    v.twenvLevel = advanceEnvExp(v.twenvLevel, twSus, v.twenvStage,
        juce::jmax(0.001f, pTwAtt->load()), twSus,
        bc.twDecCoeff, bc.twRelCoeff,
        isr, nextTwStage, twRelStartUnused);
    v.twenvStage = nextTwStage;

    // Velocity can scale the envelope depth (0 = no scaling, 1 = full velocity scaling)
    float twDepth = pTwAmt->load();
    float twVel = pTwVelAmt->load();
    twDepth *= (1.f - twVel) + v.velocity * twVel;
    float twMod = v.twenvLevel * twDepth;   // normalised 0-1 range, signed by twDepth

    // Curve output — Osc A uses shared curvePhase, Osc B uses its own offset phase.
    float curveOutA = evalCurve((float)v.curvePhase, 0);
    float curveOutB = evalCurve((float)curvePhaseBraw, 1);

    // Add the transwave envelope offset + evo LFO + keytrack to both curves
    float evoLFOAmt = evoLFO * pEvoLFODepth->load() * 0.5f;
    float rawEvoPosA = juce::jlimit(0.f, 1.f, curveOutA + twMod + evoLFOAmt + keytrackMod);
    float rawEvoPosB = juce::jlimit(0.f, 1.f, curveOutB + twMod + evoLFOAmt + keytrackMod);

    // --- Frame Snap: quantise frame position to N discrete steps ---
    // frameSnap=0 to off; frameSnap=1 to coarsest (8 steps). Skews toward powers of 2.
    float snapAmt = pFrameSnap->load();
    if (snapAmt > 0.001f)
    {
        // Map 0-1 to ~128 down to 2 steps (exponential feel)
        float steps = std::pow(2.f, (1.f - snapAmt) * 6.f + 1.f);   // 128 … 2
        rawEvoPosA = std::round(rawEvoPosA * steps) / steps;
        rawEvoPosB = std::round(rawEvoPosB * steps) / steps;
    }

    float velFilterMod = v.velocity;

    // --- Glide: interpolate base frequency ---
    float glideTime = pGlide->load();
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
    float  ur = pUniDetune->load();
    float  uRand = (float)((v.midiNote * 1664525 + 1013904223) & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    double um = std::pow(2.0, (uRand * 2.f - 1.f) * ur / 1200.0);
    double fA = baseMF * bc.dm * um * pitchMult * pitchEnvMult * bc.sA * bc.octA;
    double fB = baseMF * bc.dm * um * pitchMult * pitchEnvMult * bc.sB * bc.octB;
    v.phaseA += fA / currentSampleRate;
    v.phaseB += fB / currentSampleRate;

    // --- Random jump (shared offset applied to both oscillators' positions) ---
    if (v.phaseA >= 1.0)
    {
        v.phaseA -= 1.0;
        float jp = pJumpProb->load();
        if (jp > 0.f && fastRand() < jp * 0.5f)
            v.frameOffset = fastRand() - rawEvoPosA;
    }
    if (v.phaseB >= 1.0) v.phaseB -= 1.0;

    // Apply pos LFO and jump offset to each oscillator's own frame position
    float rvA = rawEvoPosA + v.frameOffset + v.velFrameOffset + vPosLFO * pPosLFODepth->load() * 0.5f;
    rvA = std::fmod(rvA, 1.f); if (rvA < 0.f) rvA += 1.f;
    float rvB = rawEvoPosB + v.frameOffset + v.velFrameOffset + vPosLFO * pPosLFODepth->load() * 0.5f;
    rvB = std::fmod(rvB, 1.f); if (rvB < 0.f) rvB += 1.f;

    // --- Sample from both wavetable slots ---
    double lA = v.phaseA, lB = v.phaseB;

    const WavetableSlot& sA_wt = wt[0];
    const WavetableSlot& sB_wt = wt[1];
    float fiA = rvA * (float)(sA_wt.numFrames > 0 ? sA_wt.numFrames - 1 : 0);
    float fiB = rvB * (float)(sB_wt.numFrames > 0 ? sB_wt.numFrames - 1 : 0);

    // --- Grit: deterministic integer-snap playback-position quantisation ---
    // This is a TIME-domain sample & hold (decimation) on the wavetable read position,
    // not an amplitude waveshaper/distortion curve - it reduces how many distinct steps
    // per cycle the oscillator is allowed to land on, producing the harsh, "stair-stepped"
    // aliasing character of the original Fizmo GRIT control. It is always applied (not
    // probabilistic), so the amount tracks the knob directly and is clearly audible.
    float gr = pGrit->load();
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
    double phB = pStereoPhase->load();
    lB = std::fmod(lB + phB, 1.0); if (lB < 0.0) lB += 1.0;

    float smpA_L = 0.f, smpA_R = 0.f, smpB_L = 0.f, smpB_R = 0.f;
    bool doInterp = (pFrameInterp && pFrameInterp->load() > 0.5f);

    if (sA_wt.loaded)
    {
        juce::ScopedLock sl(sA_wt.lock);
        float raw = sampleFrameRaw(sA_wt, fiA, lA, doInterp);
        raw = applyBitCrush(raw, pBitCrush->load());
        float w = pStereoWidth->load();
        float rawB = sampleFrameRaw(sA_wt, fiA, std::fmod(lA + phB, 1.0), doInterp);
        rawB = applyBitCrush(rawB, pBitCrush->load());
        float mid = (raw + rawB) * 0.5f, side = (raw - rawB) * 0.5f;
        smpA_L = mid + side * w;
        smpA_R = mid - side * w;
    }

    if (sB_wt.loaded)
    {
        juce::ScopedLock sl(sB_wt.lock);
        float raw = sampleFrameRaw(sB_wt, fiB, lB, doInterp);
        raw = applyBitCrush(raw, pBitCrush->load());
        float w = pStereoWidth->load();
        float rawA2 = sampleFrameRaw(sB_wt, fiB, lA, doInterp);
        rawA2 = applyBitCrush(rawA2, pBitCrush->load());
        float mid = (raw + rawA2) * 0.5f, side = (raw - rawA2) * 0.5f;
        smpB_L = mid + side * w;
        smpB_R = mid - side * w;
    }

    // --- Ring modulation ---
    // When a slot is silent, use a sine from that osc's phase as carrier,
    // so ring mod is always audible regardless of which slots are loaded.
    // Amplitude-compensated: mix dry vs ring at equal power.
    float ringAmt = pRingMod->load();
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
    float evA = v.envLevel * v.velocity;
    float evB = v.env2Level * v.velocity;

    // --- A/B Mix with per-osc envelopes ---
    float mix = pOscMix->load();  // 0 = full A, 1 = full B
    float dryL = smpA_L * evA * (1.f - mix) + smpB_L * evB * mix;
    float dryR = smpA_R * evA * (1.f - mix) + smpB_R * evB * mix;

    // Ring mod: blend at equal power (ringAmt^2 + (1-ringAmt)^2 stays ~0.5,
    // so we compensate with a 1/sqrt(2) factor at ringAmt=0.5).
    // Simpler: scale ring output by (evA*(1-mix)+evB*mix) so level matches dry.
    float envMix = evA * (1.f - mix) + evB * mix;
    float wetL = ringL * envMix;
    float wetR = ringR * envMix;
    // Cross-fade with amplitude compensation: boost by sqrt(2) so -3dB point
    // at 0.5 matches the dry level, and the ring side doesn't duck.
    float comp = 1.f + ringAmt * (1.4142f - 1.f);   // 1 at 0, 1.414 at 1
    outL = (dryL * (1.f - ringAmt) + wetL * ringAmt) * comp;
    outR = (dryR * (1.f - ringAmt) + wetR * ringAmt) * comp;

    // --- Filter + cutoff plumbing (handled in processBlock) ---
    float baseFreq = pFilterFreq->load();
    float filterEnv = v.fenvLevel * pFilterEnvAmt->load();
    float filterLFO = blockPosLFO * pFilterLFODep->load();
    // TW Env to Filter: twMod (already velocity-scaled) routes the transwave sweep to cutoff
    float twFilterMod = twMod * pTwToFilter->load() * 4.f;  // ±4 octaves at full amount
    float velScale = 0.3f + 0.7f * velFilterMod;
    float modOctaves = filterEnv * 6.f + filterLFO * 3.f + twFilterMod;
    float cutoff = juce::jlimit(20.f, 20000.f, baseFreq * velScale * std::pow(2.f, modOctaves));
    float q = juce::jmax(0.1f, pFilterQ->load());

    // Per-voice filter mode: apply filter here per sample, skip global filter in processBlock
    if (pFilterPerVoice && pFilterPerVoice->load() > 0.5f)
    {
        // setLowpass is called once per synthesiseVoice call (once per sample per voice),
        // but the coefficients are cheap compared to exp — cutoff is already computed above.
        v.voiceFilter.setLowpass(cutoff, q, currentSampleRate);
        outL = v.voiceFilter.processL(outL);
        outR = v.voiceFilter.processR(outR);
    }
    else
    {
        (void)cutoff; (void)q;
    }
}

//==============================================================================
void TranswaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();

    // --- MIDI ---
    for (const auto md : midiMessages)
    {
        auto msg = md.getMessage();
        if (msg.isNoteOn())
        {
            bool isMono = (pMono && pMono->load() > 0.5f);
            float glideTime = pGlide ? pGlide->load() : 0.f;

            if (isMono)
            {
                // Mono mode: steal voice 0, carry frequency for glide
                double prevFreq = lastNoteFreq;
                float velAmt = (pVelToFrame && pVelToFrame->load() > 0.5f) ? 1.0f : 0.f;
                bool  carry = (pEvoPhaseCarry && pEvoPhaseCarry->load() > 0.5f)
                    && !(pEvoRestart && pEvoRestart->load() > 0.5f);
                float carryPh = carry ? (float)voices[0].curvePhase : 0.f;
                voices[0].noteOn(msg.getNoteNumber(), (float)msg.getVelocity() / 127.f,
                    prevFreq, glideTime, velAmt, carry, carryPh);
                voiceEvoLFOPhase[0] = (double)(msg.getNoteNumber() & 0xF) / 16.0;
                voicePosLFOPhase[0] = (double)((msg.getNoteNumber() * 7) & 0xF) / 16.0;
                // Kill all other voices
                for (int i = 1; i < MAX_VOICES; ++i)
                    voices[i].active = false;
                monoActive = true;
            }
            else
            {
                // Poly mode: find free voice
                int si = -1; float minL = 2.f; int minI = 0;
                for (int i = 0; i < MAX_VOICES; ++i)
                {
                    if (!voices[i].active) { si = i; break; }
                    if (voices[i].envStage == TranswaveVoice::Env::Release && voices[i].envLevel < minL)
                    {
                        minL = voices[i].envLevel; minI = i;
                    }
                }
                if (si < 0) si = minI;
                float velAmt = (pVelToFrame && pVelToFrame->load() > 0.5f) ? 1.0f : 0.f;
                bool  carry = (pEvoPhaseCarry && pEvoPhaseCarry->load() > 0.5f)
                    && !(pEvoRestart && pEvoRestart->load() > 0.5f);
                // For phase carry in poly, inherit from the most-recently-active voice
                float carryPh = 0.f;
                if (carry) {
                    for (int i = 0; i < MAX_VOICES; ++i)
                        if (voices[i].active) { carryPh = (float)voices[i].curvePhase; break; }
                }
                voices[si].noteOn(msg.getNoteNumber(), (float)msg.getVelocity() / 127.f,
                    0.0, 0.f, velAmt, carry, carryPh);   // no glide in poly
                voiceEvoLFOPhase[si] = (double)(msg.getNoteNumber() & 0xF) / 16.0;
                voicePosLFOPhase[si] = (double)((msg.getNoteNumber() * 7) & 0xF) / 16.0;
                monoActive = false;
            }
            lastNoteFreq = 440.0 * std::pow(2.0, (msg.getNoteNumber() - 69.0) / 12.0);
        }
        else if (msg.isNoteOff())
        {
            for (auto& v : voices)
                if (v.active && v.midiNote == msg.getNoteNumber() && v.envStage != TranswaveVoice::Env::Release)
                    v.noteOff();
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& v : voices) { v.active = false; v.envStage = TranswaveVoice::Env::Idle; }
        }
    }

    // Check if any slot is loaded
    bool anyLoaded = wt[0].loaded || wt[1].loaded;
    if (!anyLoaded) return;

    int    N = buffer.getNumSamples();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    gainSmooth.setTargetValue(pGain->load());

    // Read params once per block
    float pitchLFORate = pPitchLFORate->load();
    float pitchLFODep = pPitchLFO->load();
    float posLFORate = pPosLFORate->load();
    float filterFreq = pFilterFreq->load();
    float filterQ = juce::jmax(0.1f, pFilterQ->load());
    float fenvAmt = pFilterEnvAmt->load();
    float fLFODep = pFilterLFODep->load();
    float chRate = pChorusRate->load();
    float chDepth = pChorusDepth->load();

    // Update reverb params
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = pReverbSize->load(); rp.damping = pReverbDamp->load();
        rp.wetLevel = pReverbWet->load();  rp.dryLevel = 1.f - pReverbWet->load();
        rp.width = 1.f; rp.freezeMode = 0.f; reverb.setParameters(rp);
    }

    // Advance global pitch LFO and pos LFO (block-level, used for filter mod)
    // We evaluate once at block midpoint for simplicity
    pitchLFOPhase += pitchLFORate * (N * 0.5) / currentSampleRate;
    if (pitchLFOPhase > 1.0) pitchLFOPhase -= 1.0;
    double posLFOPhaseBlock = 0.0; // block-level pos LFO for filter
    posLFOPhaseBlock += posLFORate * (N * 0.5) / currentSampleRate;
    if (posLFOPhaseBlock > 1.0) posLFOPhaseBlock -= 1.0;
    blockPosLFO = (float)std::sin(posLFOPhaseBlock * juce::MathConstants<double>::twoPi);
    blockPitchLFO = (float)std::sin(pitchLFOPhase * juce::MathConstants<double>::twoPi);
    double pitchMult = std::pow(2.0, (double)(blockPitchLFO * pitchLFODep) / 12.0);

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

    // Compute average filter cutoff from active voice filter envs (for global filter)
    float avgFEnv = 0.f, avgTwEnv = 0.f; int nActive = 0;
    for (auto& v : voices)
        if (v.active) { avgFEnv += v.fenvLevel; avgTwEnv += v.twenvLevel; ++nActive; }
    if (nActive > 0) { avgFEnv /= (float)nActive; avgTwEnv /= (float)nActive; }
    float twFilterMod = avgTwEnv * (pTwToFilter ? pTwToFilter->load() : 0.f) * 4.f;
    float modOctaves = avgFEnv * fenvAmt * 6.f + blockPosLFO * fLFODep * 3.f + twFilterMod;
    float cutoff = juce::jlimit(20.f, 20000.f, filterFreq * std::pow(2.f, modOctaves));
    filter.setLowpass(cutoff, filterQ, currentSampleRate);

    // --- Precompute block-level envelope coefficients (avoids exp/pow per sample) ---
    {
        float isr = (float)(1.0 / currentSampleRate);
        auto makeDecCoeff = [&](float time) {
            return std::exp(-isr * 5.f / juce::jmax(0.001f, time));
            };
        blk.decCoeff = makeDecCoeff(pDecay->load());
        blk.relCoeff = makeDecCoeff(pRelease->load());
        blk.dec2Coeff = makeDecCoeff(pDecay2->load());
        blk.rel2Coeff = makeDecCoeff(pRelease2->load());
        blk.fdecCoeff = makeDecCoeff(pFilterDec->load());
        blk.frelCoeff = makeDecCoeff(pFilterRel->load());
        blk.penvDecCoeff = makeDecCoeff(pPitchEnvDec->load());
        blk.twDecCoeff = makeDecCoeff(pTwDec->load());
        blk.twRelCoeff = makeDecCoeff(pTwRel->load());

        blk.dm = std::pow(2.0, (double)pDetune->load() / 1200.0);
        float sc = pSpread->load();
        blk.sA = std::pow(2.0, -(double)sc / 2400.0);
        blk.sB = std::pow(2.0, (double)sc / 2400.0);
        blk.octA = std::pow(2.0, std::round((double)pOctaveA->load()));
        blk.octB = std::pow(2.0, std::round((double)pOctaveB->load()));
    }

    // --- Per-sample synthesis ---
    // Acquire wavetable locks ONCE for the entire block (not per sample).
    float noiseAmt = pNoise->load();
    {
        juce::ScopedLock la(wt[0].lock);
        juce::ScopedLock lb(wt[1].lock);
        for (int s = 0; s < N; ++s)
        {
            float mixL = 0.f, mixR = 0.f;
            for (int vi = 0; vi < MAX_VOICES; ++vi)
            {
                float vL = 0.f, vR = 0.f;
                synthesiseVoice(voices[vi], vi, blockPosLFO, pitchMult, vL, vR, blk);
                mixL += vL; mixR += vR;
            }

            // Add noise pre-filter so it gets shaped by the filter envelope
            if (noiseAmt > 0.f)
            {
                float nL = fastRand() * 2.f - 1.f;
                float nR = fastRand() * 2.f - 1.f;
                mixL += nL * noiseAmt * 0.25f;
                mixR += nR * noiseAmt * 0.25f;
            }

            // Soft clip
            const float sc = 0.3f;
            mixL = (mixL * sc) / (1.f + std::abs(mixL * sc));
            mixR = (mixR * sc) / (1.f + std::abs(mixR * sc));

            // 4-pole filter (global mode only; per-voice mode handled inside synthesiseVoice)
            if (!pFilterPerVoice || pFilterPerVoice->load() < 0.5f)
            {
                mixL = filter.processL(mixL);
                mixR = filter.processR(mixR);
            }

            float g = gainSmooth.getNextValue();
            outL[s] = mixL * g;
            outR[s] = mixR * g;
        }
    } // wavetable locks released here

    // Chorus
    if (chDepth > 0.001f)
        for (int s = 0; s < N; ++s)
            chorus.process(outL[s], outR[s], chRate, chDepth, currentSampleRate);

    // Reverb
    reverb.processStereo(outL, outR, N);
}

//==============================================================================
void TranswaveAudioProcessor::parameterChanged(const juce::String& id, float val)
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

//==============================================================================
static std::unique_ptr<juce::XmlElement> buildFullStateXml(
    juce::AudioProcessorValueTreeState& apvts, int activeSlot,
    const juce::String& pA, int csA, const juce::String& pB, int csB)
{
    auto root = std::make_unique<juce::XmlElement>("PhizmOscState");
    auto state = apvts.copyState();
    root->addChildElement(state.createXml().release());
    root->setAttribute("activeSlot", activeSlot);
    root->setAttribute("pathA", pA); root->setAttribute("cycleSizeA", csA);
    root->setAttribute("pathB", pB); root->setAttribute("cycleSizeB", csB);
    return root;
}

static void syncEvoCurvesFromApvts(TranswaveAudioProcessor& proc)
{
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String idA = "evoPoint_" + juce::String::formatted("%02d", i);
        if (auto* p = proc.apvts.getRawParameterValue(idA)) proc.evoCurve[i].store(p->load());
        juce::String idB = "evoPointB_" + juce::String::formatted("%02d", i);
        if (auto* p = proc.apvts.getRawParameterValue(idB)) proc.evoCurveB[i].store(p->load());
    }
}

// Forward declaration — defined later in this file
static std::unique_ptr<juce::XmlElement> buildPresetStateXml(
    juce::AudioProcessorValueTreeState&, int,
    const juce::String&, int, const juce::String&, int);

void TranswaveAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::String fnA, fnB; int csA = 2048, csB = 2048;
    juce::MemoryBlock dataA, dataB;
    bool hasA = false, hasB = false;
    {
        juce::ScopedLock la(wt[0].lock);
        if (wt[0].loaded) { hasA = true; fnA = wt[0].fileName; csA = wt[0].cycleSamples; dataA = wt[0].originalFileData; }
    }
    {
        juce::ScopedLock lb(wt[1].lock);
        if (wt[1].loaded) { hasB = true; fnB = wt[1].fileName; csB = wt[1].cycleSamples; dataB = wt[1].originalFileData; }
    }
    if (fnA.isEmpty() && hasA) fnA = "WavetableA.wav";
    if (fnB.isEmpty() && hasB) fnB = "WavetableB.wav";

    auto xml = buildPresetStateXml(apvts, activeSlot.load(), fnA, csA, fnB, csB);
    xml->setAttribute("sampleFolder", sampleFolder);
    xml->setAttribute("presetFolder", presetFolder);
    juce::String xmlString = xml->toString();

    juce::ZipFile::Builder zb;
    zb.addEntry(new juce::MemoryInputStream(xmlString.toUTF8(), xmlString.getNumBytesAsUTF8(), true),
        9, "state.xml", juce::Time::getCurrentTime());
    if (hasA && dataA.getSize() > 0)
        zb.addEntry(new juce::MemoryInputStream(dataA, true), 9, "A/" + fnA, juce::Time::getCurrentTime());
    if (hasB && dataB.getSize() > 0)
        zb.addEntry(new juce::MemoryInputStream(dataB, true), 9, "B/" + fnB, juce::Time::getCurrentTime());

    juce::MemoryOutputStream mos(destData, false);
    zb.writeToStream(mos, nullptr);
}

void TranswaveAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Try to read as a zip (new embedded format).
    juce::MemoryInputStream mis(data, (size_t)sizeInBytes, false);
    juce::ZipFile zip(mis);
    if (zip.getNumEntries() > 0)
    {
        int stateIdx = zip.getIndexOfFileName("state.xml");
        if (stateIdx >= 0)
        {
            std::unique_ptr<juce::InputStream> stateStream(zip.createStreamForEntry(stateIdx));
            if (stateStream)
            {
                auto xml = juce::XmlDocument::parse(stateStream->readEntireStreamAsString());
                if (xml && xml->getTagName() == "PhizmOscState")
                {
                    if (auto* c = xml->getFirstChildElement())
                    {
                        auto vt = juce::ValueTree::fromXml(*c);
                        if (vt.isValid()) apvts.replaceState(vt);
                    }
                    syncEvoCurvesFromApvts(*this);
                    activeSlot.store(xml->getIntAttribute("activeSlot", 0));
                    sampleFolder  = xml->getStringAttribute("sampleFolder");
                    presetFolder  = xml->getStringAttribute("presetFolder");
                    int csA = xml->getIntAttribute("cycleSizeA", 2048);
                    int csB = xml->getIntAttribute("cycleSizeB", 2048);

                    for (int i = 0; i < zip.getNumEntries(); ++i)
                    {
                        auto* entry = zip.getEntry(i);
                        if (!entry) continue;
                        juce::String fn = entry->filename;
                        int slot = -1; juce::String baseName;
                        if (fn.startsWith("A/")) { slot = 0; baseName = fn.substring(2); }
                        else if (fn.startsWith("B/")) { slot = 1; baseName = fn.substring(2); }
                        if (slot < 0 || baseName.isEmpty()) continue;
                        std::unique_ptr<juce::InputStream> es(zip.createStreamForEntry(i));
                        if (!es) continue;
                        juce::MemoryBlock mb;
                        es->readIntoMemoryBlock(mb);
                        loadWavetableFromMemory(mb, baseName, slot == 0 ? csA : csB, slot);
                    }
                    return;
                }
            }
        }
    }

    // Legacy fallback: old format stored as raw XML binary via copyXmlToBinary().
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml) return;
    if (auto* c = xml->getFirstChildElement())
    {
        auto vt = juce::ValueTree::fromXml(*c); if (vt.isValid()) apvts.replaceState(vt);
    }
    syncEvoCurvesFromApvts(*this);
    activeSlot.store(xml->getIntAttribute("activeSlot", 0));
    sampleFolder  = xml->getStringAttribute("sampleFolder");
    presetFolder  = xml->getStringAttribute("presetFolder");
    juce::String pA = xml->getStringAttribute("pathA"); int csA = xml->getIntAttribute("cycleSizeA", 2048);
    juce::String pB = xml->getStringAttribute("pathB"); int csB = xml->getIntAttribute("cycleSizeB", 2048);
    if (pA.isNotEmpty()) { juce::File f(pA); if (f.existsAsFile()) loadWavetable(f, csA, 0); }
    if (pB.isNotEmpty()) { juce::File f(pB); if (f.existsAsFile()) loadWavetable(f, csB, 1); }
}

// Preset state document used inside the zip - deliberately contains only
// the original file NAMES (e.g. "MyWave.wav"), never an absolute path, so
// sharing a preset never exposes the author's folder structure.
static std::unique_ptr<juce::XmlElement> buildPresetStateXml(
    juce::AudioProcessorValueTreeState& apvts, int activeSlot,
    const juce::String& fileNameA, int csA, const juce::String& fileNameB, int csB)
{
    auto root = std::make_unique<juce::XmlElement>("PhizmOscState");
    auto state = apvts.copyState();
    root->addChildElement(state.createXml().release());
    root->setAttribute("activeSlot", activeSlot);
    root->setAttribute("fileNameA", fileNameA); root->setAttribute("cycleSizeA", csA);
    root->setAttribute("fileNameB", fileNameB); root->setAttribute("cycleSizeB", csB);
    return root;
}

int TranswaveAudioProcessor::getGuiWidth() const
{
    if (auto* p = apvts.getRawParameterValue("guiWidth"))
        return juce::jlimit(595, 2380, (int)p->load());
    return 1190;
}

int TranswaveAudioProcessor::getGuiHeight() const
{
    // Height is always derived from the fixed 1190:804 aspect ratio.
    return juce::roundToInt(getGuiWidth() * (804.f / 1190.f));
}

void TranswaveAudioProcessor::setGuiWidth(int w)
{
    if (auto* p = apvts.getParameter("guiWidth"))
        p->setValueNotifyingHost(p->convertTo0to1((float)w));
}

int TranswaveAudioProcessor::getCycleSizeParam(int slot) const
{
    const char* id = (slot == 0) ? "cycleSizeA" : "cycleSizeB";
    if (auto* p = apvts.getRawParameterValue(id))
        return juce::jlimit(16, 65536, (int)p->load());
    return 2048;
}

void TranswaveAudioProcessor::setCycleSizeParam(int slot, int size)
{
    const char* id = (slot == 0) ? "cycleSizeA" : "cycleSizeB";
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1((float)size));
}

juce::File TranswaveAudioProcessor::getPresetsDirectory()
{
    auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto subDir = docs.getChildFile("PhizmOsc Presets");
    return subDir.isDirectory() ? subDir : docs;
}

juce::File TranswaveAudioProcessor::getEffectivePresetsDirectory() const
{
    if (presetFolder.isNotEmpty())
    {
        juce::File f(presetFolder);
        if (f.isDirectory()) return f;
    }
    return getPresetsDirectory();
}

bool TranswaveAudioProcessor::savePreset(const juce::File& dest)
{
    juce::String fnA, fnB; int csA = 2048, csB = 2048;
    juce::MemoryBlock dataA, dataB;
    bool hasA = false, hasB = false;
    {
        juce::ScopedLock la(wt[0].lock);
        if (wt[0].loaded) { hasA = true; fnA = wt[0].fileName; csA = wt[0].cycleSamples; dataA = wt[0].originalFileData; }
    }
    {
        juce::ScopedLock lb(wt[1].lock);
        if (wt[1].loaded) { hasB = true; fnB = wt[1].fileName; csB = wt[1].cycleSamples; dataB = wt[1].originalFileData; }
    }
    if (fnA.isEmpty() && hasA) fnA = "WavetableA.wav";
    if (fnB.isEmpty() && hasB) fnB = "WavetableB.wav";

    auto xml = buildPresetStateXml(apvts, activeSlot.load(), fnA, csA, fnB, csB);
    juce::String xmlString = xml->toString();

    juce::ZipFile::Builder zb;
    zb.addEntry(new juce::MemoryInputStream(xmlString.toUTF8(), xmlString.getNumBytesAsUTF8(), true),
        9, "state.xml", juce::Time::getCurrentTime());
    if (hasA && dataA.getSize() > 0)
        zb.addEntry(new juce::MemoryInputStream(dataA, true), 9, "A/" + fnA, juce::Time::getCurrentTime());
    if (hasB && dataB.getSize() > 0)
        zb.addEntry(new juce::MemoryInputStream(dataB, true), 9, "B/" + fnB, juce::Time::getCurrentTime());

    juce::TemporaryFile tempFile(dest);
    {
        juce::FileOutputStream out(tempFile.getFile());
        if (!out.openedOk()) return false;
        if (!zb.writeToStream(out, nullptr)) return false;
    }
    return tempFile.overwriteTargetFileWithTemporary();
}

bool TranswaveAudioProcessor::loadPreset(const juce::File& src)
{
    juce::ZipFile zip(src);
    if (zip.getNumEntries() > 0)
    {
        int stateIdx = zip.getIndexOfFileName("state.xml");
        if (stateIdx < 0) return false;
        std::unique_ptr<juce::InputStream> stateStream(zip.createStreamForEntry(stateIdx));
        if (!stateStream) return false;
        auto xml = juce::XmlDocument::parse(stateStream->readEntireStreamAsString());
        if (!xml || xml->getTagName() != "PhizmOscState") return false;

        if (auto* c = xml->getFirstChildElement())
        {
            auto vt = juce::ValueTree::fromXml(*c); if (vt.isValid()) apvts.replaceState(vt);
        }
        syncEvoCurvesFromApvts(*this);
        activeSlot.store(xml->getIntAttribute("activeSlot", 0));
        int csA = xml->getIntAttribute("cycleSizeA", 2048);
        int csB = xml->getIntAttribute("cycleSizeB", 2048);

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry(i);
            if (entry == nullptr) continue;
            juce::String fn = entry->filename;
            int slot = -1; juce::String baseName;
            if (fn.startsWith("A/")) { slot = 0; baseName = fn.substring(2); }
            else if (fn.startsWith("B/")) { slot = 1; baseName = fn.substring(2); }
            if (slot < 0 || baseName.isEmpty()) continue;

            std::unique_ptr<juce::InputStream> es(zip.createStreamForEntry(i));
            if (!es) continue;
            juce::MemoryBlock mb;
            es->readIntoMemoryBlock(mb);
            loadWavetableFromMemory(mb, baseName, slot == 0 ? csA : csB, slot);
        }
        return true;
    }

    // Legacy fallback: pre-existing flat-XML presets that referenced an
    // absolute path on disk instead of embedding the wavetable.
    auto xml = juce::XmlDocument::parse(src);
    if (!xml || xml->getTagName() != "PhizmOscState") return false;
    if (auto* c = xml->getFirstChildElement())
    {
        auto vt = juce::ValueTree::fromXml(*c); if (vt.isValid()) apvts.replaceState(vt);
    }
    syncEvoCurvesFromApvts(*this);
    activeSlot.store(xml->getIntAttribute("activeSlot", 0));
    juce::String pA = xml->getStringAttribute("pathA"); int csA = xml->getIntAttribute("cycleSizeA", 2048);
    juce::String pB = xml->getStringAttribute("pathB"); int csB = xml->getIntAttribute("cycleSizeB", 2048);
    if (pA.isNotEmpty()) { juce::File f(pA); if (f.existsAsFile()) loadWavetable(f, csA, 0); }
    if (pB.isNotEmpty()) { juce::File f(pB); if (f.existsAsFile()) loadWavetable(f, csB, 1); }
    return true;
}

juce::AudioProcessorEditor* TranswaveAudioProcessor::createEditor()
{
    return new TranswaveAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranswaveAudioProcessor();
}