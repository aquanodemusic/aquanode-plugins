/*
  ==============================================================================

    Ableton-Style Resonator Plugin Implementation

    Changes in this version:
    - Center toggle (DC bias accumulation when OFF)
    - Per-resonator decay/color parameters
    - 4-stage all-pass input chain for Ableton "twang" attack character
    - Membrane wobble: sample-domain LCG noise modulator (freq-scaled organically)
    - Decay knob curve expanded around the 20-35 musically interesting range
    - New APVTS parameters: center_mode, per_res_mode, res1-5 _decay / _color

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// State Variable Filter

void ResonateAudioProcessor::StateVariableFilter::setFrequency(float normalizedFreq, float resonance)
{
    freq = juce::jlimit(0.0f, 0.99f, normalizedFreq);
    q = juce::jlimit(0.1f, 10.0f, resonance);
}

float ResonateAudioProcessor::StateVariableFilter::process(float input, int channel, FilterType type)
{
    float fb = q + q / (1.0f - freq);
    low[channel] += freq * band[channel];
    float high = input - low[channel] - fb * band[channel];
    band[channel] += freq * high;

    switch (type)
    {
    case Lowpass:  return low[channel];
    case Highpass: return high;
    case Bandpass: return band[channel];
    case Notch:    return low[channel] + high;
    default:       return low[channel];
    }
}

void ResonateAudioProcessor::StateVariableFilter::reset()
{
    low[0] = low[1] = 0.0f;
    band[0] = band[1] = 0.0f;
}

//==============================================================================
// Resonator helpers

void ResonateAudioProcessor::Resonate::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        delayBuffer[ch].resize(MAX_DELAY_SAMPLES, 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        lfoPhase[ch] = 0.0;
        membraneFilter[ch] = 0.0f;
        dcEnv[ch] = 0.0f;
        expEnv[ch] = 0.0f;
    }
}

void ResonateAudioProcessor::Resonate::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill(delayBuffer[ch].begin(), delayBuffer[ch].end(), 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        membraneFilter[ch] = 0.0f;
        dcEnv[ch] = 0.0f;
        expEnv[ch] = 0.0f;
    }
}

void ResonateAudioProcessor::Resonate::updateParameters(
    double sampleRate,
    double globalDecay, double globalNote,
    double color,
    ProcessingMode mode, bool constMode,
    double chorusAmount, int resonatorIndex,
    double userLfoRate, double userLfoDepth,
    bool   center,
    bool   useLocal, double lDecay, double lColor, bool lConst)
{
    if (!enabled || sampleRate <= 0.0)
    {
        feedback = 0.0;
        return;
    }

    currentSampleRate = sampleRate;
    centerMode = center;
    // expDecay is set directly from the processor before updateParameters is called
    useLocalParams = useLocal;
    localDecay = lDecay;
    localColor = lColor;
    localConst = lConst;

    // Effective decay, color, const (local overrides global when per-res mode on)
    double effDecay = useLocalParams ? localDecay : globalDecay;
    double effColor = useLocalParams ? localColor : color;
    bool   effConst = useLocalParams ? localConst : constMode;

    // ── LFO for chorus/beating ────────────────────────────────────────────
    const double rateOffsets[5] = { 0.0, 0.13, 0.27, 0.41, 0.19 };
    double baseRate = userLfoRate;
    lfoRate = (baseRate + rateOffsets[resonatorIndex % 5] * 0.3)
        * (chorusAmount / 100.0);
    lfoDepthCents = userLfoDepth * (chorusAmount / 100.0);

    // ── Membrane depth (sample-domain, scales with frequency naturally) ───
    // At max chorus, about 1.5 delay samples of wobble.
    // Because this is in *samples* not cents, higher resonances wobble
    // more in absolute Hz — matching the spectrogram behaviour you showed.
    membraneSamples = (chorusAmount / 100.0) * 1.5;

    // ── Frequency from MIDI note + pitch + fine ───────────────────────────
    double totalNote = globalNote + static_cast<double>(pitchSemitones);
    double cents = (totalNote - 69.0) * 100.0 + fineDetune;
    targetFrequency = 440.0 * std::pow(2.0, cents / 1200.0);
    targetFrequency = juce::jlimit(20.0, 20000.0, targetFrequency);

    delayInSamples = sampleRate / targetFrequency;
    delayInSamples = juce::jlimit(2.0,
        static_cast<double>(MAX_DELAY_SAMPLES - 2),
        delayInSamples);

    // ── Feedback from decay ───────────────────────────────────────────────
    // New curve: give the 20-35 range ~40% of knob travel.
    // Breakpoints: norm=0.20→value=20, norm=0.60→value=35, norm=1.0→value=100
    // (This mirrors the NormalisableRange in createParameterLayout.)
    double decayNorm = effDecay / 100.0;
    feedback = std::pow(decayNorm, 0.25);
    feedback = juce::jlimit(0.0, 0.9999, feedback);

    // ── Color → damping LPF coefficient ──────────────────────────────────
    double colorNorm = effColor / 100.0;
    lpfCoeff = static_cast<float>(0.3 + colorNorm * 0.69);

    if (effConst)
    {
        // Original: lower frequencies get more feedback to sustain as long
        // as higher ones. referenceFreq/targetFreq > 1 for low notes → more feedback.
        double referenceFreq = 261.63;
        double freqRatio = referenceFreq / targetFrequency;
        double constCompensation = std::pow(freqRatio, 0.3);
        feedback *= constCompensation;
        feedback = juce::jlimit(0.0, 0.9999, feedback);
    }

    // ── Exp decay coefficient (freq-scaled, used when expDecay is on) ────────
    // Higher frequencies decay faster: coeff = feedback^(targetFreq/refFreq)
    // At referenceFreq: coeff == feedback (unchanged).
    // Above: exponent > 1 → coeff < feedback → faster decay per sample.
    // Below: exponent < 1 → coeff > feedback → slower decay per sample.
    {
        double referenceFreq = 261.63;
        double exponent = targetFrequency / referenceFreq;
        expCoeff = static_cast<float>(
            juce::jlimit(0.0, 0.9999, std::pow(feedback, exponent)));
    }
}

float ResonateAudioProcessor::Resonate::readDelayLinear(int channel, double delaySamples)
{
    double readPos = static_cast<double>(writeIndex[channel]) - delaySamples;
    while (readPos < 0.0)
        readPos += static_cast<double>(MAX_DELAY_SAMPLES);

    int   index1 = static_cast<int>(readPos) % MAX_DELAY_SAMPLES;
    int   index2 = (index1 + 1) % MAX_DELAY_SAMPLES;
    float frac = static_cast<float>(readPos - std::floor(readPos));

    return delayBuffer[channel][index1] + frac * (delayBuffer[channel][index2]
        - delayBuffer[channel][index1]);
}

// ── Shared helper: apply all-pass chain + membrane + (optionally) DC ────────
// Kept as a static lambda-like inline to avoid duplicating between processA/B.

float ResonateAudioProcessor::Resonate::processA(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    // ── 1. Compute effective delay (LFO + membrane wobble) ───────────────────
    double actualDelay = delayInSamples;

    // Conventional sine LFO (cents-domain, normalised)
    if (lfoDepthCents > 0.001 && lfoRate > 0.0)
    {
        lfoPhase[channel] += lfoRate / currentSampleRate;
        if (lfoPhase[channel] >= 1.0) lfoPhase[channel] -= 1.0;
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);
        double freqRatio = std::pow(2.0, lfo * lfoDepthCents / 1200.0);
        actualDelay = delayInSamples / freqRatio;
    }

    // Membrane wobble: heavily-filtered LCG noise in *sample* space.
    // Using a one-pole LPF with coeff 0.0003 → ~7ms time-constant @ 44100 Hz,
    // giving the slow organic drift you see on a detuned physical membrane.
    if (membraneSamples > 0.001)
    {
        float noise = fastNoise(channel);
        membraneFilter[channel] += 0.0003f * (noise - membraneFilter[channel]);
        actualDelay += static_cast<double>(membraneFilter[channel]) * membraneSamples;
    }

    actualDelay = juce::jlimit(2.0,
        static_cast<double>(MAX_DELAY_SAMPLES - 2),
        actualDelay);

    // ── 3. Classic comb filter with LPF damping ───────────────────────────────
    float delayedSample = readDelayLinear(channel, actualDelay);

    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float dampedFeedback = lpfState[channel];

    float output = input + static_cast<float>(feedback) * dampedFeedback;
    output = std::tanh(output);

    // Write CLEAN signal to delay buffer — DC must NOT feed back or it
    // gets squashed by tanh every cycle and the effect disappears.
    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    // ── 4. DC bias applied to OUTPUT only (not the feedback path) ────────────
    if (!centerMode)
    {
        float absOut = std::abs(output);
        float attack = 0.05f;
        float release = 0.0002f;
        dcEnv[channel] += (absOut > dcEnv[channel])
            ? attack * (absOut - dcEnv[channel])
            : release * (absOut - dcEnv[channel]);
        output += dcEnv[channel] * 0.65f;
    }

    // ── 5. Exponential decay envelope (freq-scaled) ──────────────────────────
    // expCoeff < feedback for high frequencies → they decay faster.
    // expCoeff > feedback for low  frequencies → they decay slower.
    if (expDecay)
    {
        float absOut = std::abs(output);
        if (absOut > expEnv[channel])
            expEnv[channel] = absOut;          // instant attack
        else
            expEnv[channel] *= expCoeff;       // freq-scaled exp decay
        if (expEnv[channel] < 1e-6f) expEnv[channel] = 0.0f;
        output *= (expEnv[channel] > 0.0f) ? expEnv[channel] / (expEnv[channel] + 0.001f) : 0.0f;
    }

    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

float ResonateAudioProcessor::Resonate::processB(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    // ── 1. Delay with LFO + membrane ─────────────────────────────────────────
    double baseModeDelay = delayInSamples * 0.5;
    double actualDelay = baseModeDelay;

    if (lfoDepthCents > 0.001 && lfoRate > 0.0)
    {
        lfoPhase[channel] += lfoRate / currentSampleRate;
        if (lfoPhase[channel] >= 1.0) lfoPhase[channel] -= 1.0;
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);
        double freqRatio = std::pow(2.0, lfo * lfoDepthCents / 1200.0);
        actualDelay = baseModeDelay / freqRatio;
    }

    if (membraneSamples > 0.001)
    {
        float noise = fastNoise(channel);
        membraneFilter[channel] += 0.0003f * (noise - membraneFilter[channel]);
        actualDelay += static_cast<double>(membraneFilter[channel]) * membraneSamples;
    }

    actualDelay = std::max(1.0, actualDelay);

    // ── 3. Negative-feedback comb (Mode B square-wave character) ─────────────
    float delayedSample = readDelayLinear(channel, actualDelay);
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float damped = lpfState[channel];

    float output = std::tanh(input - static_cast<float>(feedback) * damped);

    // Write CLEAN signal to delay buffer — DC must not feed back
    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    // ── 4. DC bias applied to OUTPUT only (not the feedback path) ────────────
    if (!centerMode)
    {
        float absOut = std::abs(output);
        float attack = 0.05f;
        float release = 0.0002f;
        dcEnv[channel] += (absOut > dcEnv[channel])
            ? attack * (absOut - dcEnv[channel])
            : release * (absOut - dcEnv[channel]);
        output += dcEnv[channel] * 0.65f;
    }

    // ── 5. Exponential decay envelope (freq-scaled) ──────────────────────────
    if (expDecay)
    {
        float absOut = std::abs(output);
        if (absOut > expEnv[channel])
            expEnv[channel] = absOut;
        else
            expEnv[channel] *= expCoeff;
        if (expEnv[channel] < 1e-6f) expEnv[channel] = 0.0f;
        output *= (expEnv[channel] > 0.0f) ? expEnv[channel] / (expEnv[channel] + 0.001f) : 0.0f;
    }

    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

//==============================================================================
// Parameter layout

static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Filter
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "filter_enabled", "Filter On", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "filter_freq", "Filter Frequency",
        juce::NormalisableRange<float>(20.0f, 12000.0f, 1.0f, 0.3f), 1000.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "filter_type", "Filter Type",
        juce::StringArray{ "Lowpass", "Highpass", "Bandpass", "Notch" }, 0));

    // Mode
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", juce::StringArray{ "A", "B" }, 0));

    // ── Decay: expanded resolution in the 20-35 range ────────────────────
    //   Knob travel:
    //     0 – 20%  →  value  0 – 20   (coarse low end)
    //    20 – 60%  →  value 20 – 35   (expanded musical zone gets 40% of travel)
    //    60 – 100% →  value 35 – 100  (normal upper range)
    //
    //   JUCE NormalisableRange lambda order:
    //     1st = convertFrom0to1  (proportion 0-1  →  value 0-100)
    //     2nd = convertTo0to1    (value 0-100      →  proportion 0-1)
    juce::NormalisableRange<float> decayRange(
        0.0f, 100.0f,
        // convertFrom0to1: proportion → value
        [](float /*start*/, float /*end*/, float norm) -> float
        {
            if (norm < 0.20f)
                return juce::jmap(norm, 0.0f, 0.20f, 0.0f, 20.0f);
            else if (norm < 0.60f)
                return juce::jmap(norm, 0.20f, 0.60f, 20.0f, 35.0f);
            else
                return juce::jmap(norm, 0.60f, 1.0f, 35.0f, 100.0f);
        },
        // convertTo0to1: value → proportion
        [](float /*start*/, float /*end*/, float value) -> float
        {
            if (value < 20.0f)
                return juce::jmap(value, 0.0f, 20.0f, 0.0f, 0.20f);
            else if (value < 35.0f)
                return juce::jmap(value, 20.0f, 35.0f, 0.20f, 0.60f);
            else
                return juce::jmap(value, 35.0f, 100.0f, 0.60f, 1.0f);
        });
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay", decayRange, 95.0f));

    // Const mode
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "const_mode", "Const", false));

    // Color
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "color", "Color",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 74.2f));

    // Center mode
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "center_mode", "Center", true));

    // Exponential decay envelope shaping
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "exp_decay", "Exp Decay", false));

    // ── NEW: Per-resonator mode ───────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "per_res_mode", "Per Res", false));

    // Smooth
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "smooth", "Smooth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    // Chorus + LFO
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chorus", "Chorus",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfo_rate", "LFO Rate",
        juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfo_depth", "LFO Depth",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 8.0f));

    // Resonator 1 note
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_note", "Resonator 1 Note",
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), 60.0f));

    // Width / master gain / dry-wet / wet-only
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "drywet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "wet_only", "Wet Only", false));

    // ── Resonator 1 (note-based, no pitch offset) ─────────────────────────
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "res1_enabled", "Resonator 1 On", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_fine", "Resonator 1 Fine",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_gain", "Resonator 1 Gain",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));

    // ── NEW: Per-resonator decay and color for Resonator 1 ───────────────
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_decay", "Resonator 1 Decay",
        decayRange, 95.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_color", "Resonator 1 Color",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 74.2f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "res1_const", "Resonator 1 Const", false));

    // ── Resonators 2-5 ────────────────────────────────────────────────────
    for (int i = 1; i < 5; ++i)
    {
        juce::String id = "res" + juce::String(i + 1);
        juce::String name = "Resonator " + juce::String(i + 1);

        layout.add(std::make_unique<juce::AudioParameterBool>(
            id + "_enabled", name + " On", true));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            id + "_pitch", name + " Pitch", -24, 24, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_fine", name + " Fine",
            juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_gain", name + " Gain",
            juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));

        // NEW: per-resonator decay and color
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_decay", name + " Decay",
            decayRange, 95.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_color", name + " Color",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 74.2f));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            id + "_const", name + " Const", false));
    }

    return layout;
}

//==============================================================================
ResonateAudioProcessor::ResonateAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

ResonateAudioProcessor::~ResonateAudioProcessor() {}

//==============================================================================
const juce::String ResonateAudioProcessor::getName() const { return JucePlugin_Name; }

bool ResonateAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}
bool ResonateAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}
bool ResonateAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}
double ResonateAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    ResonateAudioProcessor::getNumPrograms() { return 1; }
int    ResonateAudioProcessor::getCurrentProgram() { return 0; }
void   ResonateAudioProcessor::setCurrentProgram(int) {}
const  juce::String ResonateAudioProcessor::getProgramName(int) { return {}; }
void   ResonateAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void ResonateAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    for (int i = 0; i < 5; ++i)
    {
        resonators[i].prepare(sampleRate);
        resonators[i].reset();
    }
    inputFilter.reset();
    updateResonateParameters();
}

void ResonateAudioProcessor::releaseResources()
{
    for (int i = 0; i < 5; ++i)
        resonators[i].reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ResonateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    return true;
#endif
}
#endif

//==============================================================================
void ResonateAudioProcessor::updateResonateParameters()
{
    globalNote = parameters.getRawParameterValue("res1_note")->load();
    globalDecay = parameters.getRawParameterValue("decay")->load();
    globalColor = parameters.getRawParameterValue("color")->load();
    globalSmooth = parameters.getRawParameterValue("smooth")->load();
    globalChorus = parameters.getRawParameterValue("chorus")->load();
    filterEnabled = parameters.getRawParameterValue("filter_enabled")->load() > 0.5f;
    filterFrequency = parameters.getRawParameterValue("filter_freq")->load();
    constMode = parameters.getRawParameterValue("const_mode")->load() > 0.5f;
    wetOnly = parameters.getRawParameterValue("wet_only")->load() > 0.5f;
    centerMode = parameters.getRawParameterValue("center_mode")->load() > 0.5f;
    bool   expDecayOn = parameters.getRawParameterValue("exp_decay")->load() > 0.5f;
    perResMode = parameters.getRawParameterValue("per_res_mode")->load() > 0.5f;

    double lfoRate = parameters.getRawParameterValue("lfo_rate")->load();
    double lfoDepth = parameters.getRawParameterValue("lfo_depth")->load();

    int modeIndex = static_cast<int>(parameters.getRawParameterValue("mode")->load());
    processingMode = (modeIndex == 0) ? ModeA : ModeB;

    int filterTypeIndex = static_cast<int>(parameters.getRawParameterValue("filter_type")->load());
    filterType = static_cast<FilterType>(filterTypeIndex);

    if (filterEnabled && currentSampleRate > 0.0)
    {
        float normFreq = juce::jlimit(0.0f, 0.99f,
            static_cast<float>(filterFrequency / (currentSampleRate * 0.5)));
        inputFilter.setFrequency(normFreq, 0.7f);
    }

    // Helper to read per-resonator local decay/color/const
    auto getLocalDecay = [&](int idx) -> double {
        juce::String id = "res" + juce::String(idx + 1);
        return parameters.getRawParameterValue(id + "_decay")->load();
        };
    auto getLocalColor = [&](int idx) -> double {
        juce::String id = "res" + juce::String(idx + 1);
        return parameters.getRawParameterValue(id + "_color")->load();
        };
    auto getLocalConst = [&](int idx) -> bool {
        juce::String id = "res" + juce::String(idx + 1);
        return parameters.getRawParameterValue(id + "_const")->load() > 0.5f;
        };

    // Resonator 1
    resonators[0].enabled = parameters.getRawParameterValue("res1_enabled")->load() > 0.5f;
    resonators[0].pitchSemitones = 0;
    resonators[0].fineDetune = parameters.getRawParameterValue("res1_fine")->load();
    resonators[0].gain = parameters.getRawParameterValue("res1_gain")->load();
    resonators[0].expDecay = expDecayOn;
    resonators[0].updateParameters(currentSampleRate, globalDecay, globalNote,
        globalColor, processingMode, constMode,
        globalChorus, 0, lfoRate, lfoDepth,
        centerMode,
        perResMode, getLocalDecay(0), getLocalColor(0), getLocalConst(0));

    // Resonators 2-5
    for (int i = 1; i < 5; ++i)
    {
        juce::String id = "res" + juce::String(i + 1);
        resonators[i].enabled = parameters.getRawParameterValue(id + "_enabled")->load() > 0.5f;
        resonators[i].pitchSemitones = static_cast<int>(
            parameters.getRawParameterValue(id + "_pitch")->load());
        resonators[i].fineDetune = parameters.getRawParameterValue(id + "_fine")->load();
        resonators[i].gain = parameters.getRawParameterValue(id + "_gain")->load();
        resonators[i].expDecay = expDecayOn;
        resonators[i].updateParameters(currentSampleRate, globalDecay, globalNote,
            globalColor, processingMode, constMode,
            globalChorus, i, lfoRate, lfoDepth,
            centerMode,
            perResMode, getLocalDecay(i), getLocalColor(i), getLocalConst(i));
    }
}

//==============================================================================
void ResonateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    updateResonateParameters();

    float masterGain = std::pow(10.0f, parameters.getRawParameterValue("gain")->load() / 20.0f);
    float dryWet = parameters.getRawParameterValue("drywet")->load() / 100.0f;
    float width = parameters.getRawParameterValue("width")->load() / 100.0f;

    float smoothNorm = 1.0f - static_cast<float>(globalSmooth / 100.0);
    float smoothCoeff = 0.01f + smoothNorm * 0.4f;

    int numSamples = buffer.getNumSamples();

    juce::AudioBuffer<float> res1Buffer(totalNumOutputChannels, numSamples);
    juce::AudioBuffer<float> res2to5Buffer(totalNumOutputChannels, numSamples);
    res1Buffer.clear();
    res2to5Buffer.clear();

    for (int channel = 0; channel < juce::jmin(totalNumInputChannels, 2); ++channel)
    {
        auto* dryData = buffer.getReadPointer(channel);
        auto* res1Data = res1Buffer.getWritePointer(channel);
        auto* res2to5Data = res2to5Buffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float input = dryData[sample];

            inputSmoothing[channel] += smoothCoeff * (input - inputSmoothing[channel]);
            float smoothedInput = inputSmoothing[channel];

            if (filterEnabled)
                smoothedInput = inputFilter.process(smoothedInput, channel, filterType);

            // Resonator I (both channels)
            float res1Output = (processingMode == ModeA)
                ? resonators[0].processA(smoothedInput, channel)
                : resonators[0].processB(smoothedInput, channel);
            res1Data[sample] = res1Output;

            // Resonators II-V (stereo routing)
            float res2to5Output = 0.0f;
            if (channel == 0)
            {
                for (int resIdx : {1, 3})
                    res2to5Output += (processingMode == ModeA)
                    ? resonators[resIdx].processA(smoothedInput, channel)
                    : resonators[resIdx].processB(smoothedInput, channel);
            }
            if (channel == 1)
            {
                for (int resIdx : {2, 4})
                    res2to5Output += (processingMode == ModeA)
                    ? resonators[resIdx].processA(smoothedInput, channel)
                    : resonators[resIdx].processB(smoothedInput, channel);
            }
            res2to5Data[sample] = res2to5Output;
        }
    }

    // Stereo width on II-V
    if (totalNumInputChannels >= 2 && totalNumOutputChannels >= 2)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float left = res2to5Buffer.getSample(0, sample);
            float right = res2to5Buffer.getSample(1, sample);
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f * width;
            res2to5Buffer.setSample(0, sample, mid + side);
            res2to5Buffer.setSample(1, sample, mid - side);
        }
    }

    // Combine and output-smooth
    juce::AudioBuffer<float> wetBuffer(totalNumOutputChannels, numSamples);
    wetBuffer.clear();

    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float combined = res1Buffer.getSample(channel, sample)
                + res2to5Buffer.getSample(channel, sample);
            outputSmoothing[channel] += smoothCoeff * (combined - outputSmoothing[channel]);
            wetBuffer.setSample(channel, sample, outputSmoothing[channel]);
        }
    }

    // Dry / wet mix
    for (int channel = 0; channel < juce::jmin(totalNumInputChannels, totalNumOutputChannels); ++channel)
    {
        auto* dryData = buffer.getWritePointer(channel);
        auto* wetData = wetBuffer.getReadPointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float dry = dryData[sample];
            float wet = wetData[sample] * masterGain;
            dryData[sample] = wetOnly
                ? wet * dryWet
                : dry * (1.0f - dryWet) + wet * dryWet;
        }
    }
}

//==============================================================================
bool                           ResonateAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* ResonateAudioProcessor::createEditor()
{
    return new ResonateAudioProcessorEditor(*this);
}

void ResonateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}
void ResonateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonateAudioProcessor();
}