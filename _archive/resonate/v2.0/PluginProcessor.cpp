/*
  ==============================================================================

    Ableton-Style Resonator Plugin Implementation

    Previous version:
    - Center toggle (DC bias accumulation when OFF)
    - Per-resonator decay/color parameters
    - 4-stage all-pass input chain for Ableton "twang" attack character
    - Membrane wobble: sample-domain LCG noise modulator (freq-scaled organically)

    New in this version:
    - Decay curve re-skewed: 90-100 now occupies ~45% of knob travel
    - Per-resonator Pan (resN_pan). Defaults reproduce the previous hard-panned
      routing bit-for-bit; each resonator still reads exactly ONE source channel
      so its delay line / LPF / DC state evolves identically to before.
    - MIDI input: up to 5 held notes, round-robin allocated to res I..V
    - Preset save/load to standalone .xml files
    - Scratch buffers pre-allocated in prepareToPlay (no audio-thread malloc)

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>

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
    bool   useLocal, double lDecay, double lColor, bool lConst,
    double midiNote)
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
    membraneSamples = (chorusAmount / 100.0) * 1.5;

    // ── Frequency from MIDI note (if held) or knob note + pitch, then fine ─
    // midiNote >= 0 replaces the (globalNote + pitchSemitones) pair entirely;
    // fine detune still applies on top so you can still beat notes against
    // each other while playing.
    double totalNote = (midiNote >= 0.0)
        ? midiNote
        : (globalNote + static_cast<double>(pitchSemitones));

    double cents = (totalNote - 69.0) * 100.0 + fineDetune;
    targetFrequency = 440.0 * std::pow(2.0, cents / 1200.0);
    targetFrequency = juce::jlimit(20.0, 20000.0, targetFrequency);

    delayInSamples = sampleRate / targetFrequency;
    delayInSamples = juce::jlimit(2.0,
        static_cast<double>(MAX_DELAY_SAMPLES - 2),
        delayInSamples);

    // ── Feedback from decay ───────────────────────────────────────────────
    // UNCHANGED mapping from decay VALUE to feedback. Only the knob-travel-to-
    // value curve moved (see createParameterLayout), so a given displayed
    // decay number still produces exactly the same feedback as before.
    double decayNorm = effDecay / 100.0;
    feedback = std::pow(decayNorm, 0.25);
    feedback = juce::jlimit(0.0, 0.9999, feedback);

    // ── Color → damping LPF coefficient ──────────────────────────────────
    double colorNorm = effColor / 100.0;
    lpfCoeff = static_cast<float>(0.3 + colorNorm * 0.69);

    if (effConst)
    {
        double referenceFreq = 261.63;
        double freqRatio = referenceFreq / targetFrequency;
        double constCompensation = std::pow(freqRatio, 0.3);
        feedback *= constCompensation;
        feedback = juce::jlimit(0.0, 0.9999, feedback);
    }

    // ── Exp decay coefficient (freq-scaled, used when expDecay is on) ────────
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

float ResonateAudioProcessor::Resonate::processA(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    // ── 1. Compute effective delay (LFO + membrane wobble) ───────────────────
    double actualDelay = delayInSamples;

    if (lfoDepthCents > 0.001 && lfoRate > 0.0)
    {
        lfoPhase[channel] += lfoRate / currentSampleRate;
        if (lfoPhase[channel] >= 1.0) lfoPhase[channel] -= 1.0;
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);
        double freqRatio = std::pow(2.0, lfo * lfoDepthCents / 1200.0);
        actualDelay = delayInSamples / freqRatio;
    }

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

float ResonateAudioProcessor::Resonate::processB(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

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

    float delayedSample = readDelayLinear(channel, actualDelay);
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float damped = lpfState[channel];

    float output = std::tanh(input - static_cast<float>(feedback) * damped);

    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

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
// Decay knob curve
//
// The mapping from decay VALUE to feedback is unchanged (feedback = v^0.25).
// What changed is how knob travel maps onto that value, because almost all of
// the musically useful territory lives in the top ten units:
//
//     decay  60 -> feedback 0.880
//     decay  90 -> feedback 0.974
//     decay  98 -> feedback 0.995
//     decay 100 -> feedback 0.9999   (effectively infinite sustain)
//
// Breakpoints below give 90-100 about 45% of the knob, and 98-100 alone gets
// 15% -- which is where the difference between "long" and "forever" lives.
//
//     travel 0.00 - 0.30  ->  value   0 -  60
//     travel 0.30 - 0.55  ->  value  60 -  90
//     travel 0.55 - 0.85  ->  value  90 -  98
//     travel 0.85 - 1.00  ->  value  98 - 100

static const float kDecayNorms[5]  = { 0.0f, 0.30f, 0.55f, 0.85f, 1.0f };
static const float kDecayValues[5] = { 0.0f, 60.0f, 90.0f, 98.0f, 100.0f };

static float decayNormToValue(float norm)
{
    norm = juce::jlimit(0.0f, 1.0f, norm);
    for (int i = 0; i < 4; ++i)
        if (norm <= kDecayNorms[i + 1])
            return juce::jmap(norm, kDecayNorms[i], kDecayNorms[i + 1],
                                    kDecayValues[i], kDecayValues[i + 1]);
    return 100.0f;
}

static float decayValueToNorm(float value)
{
    value = juce::jlimit(0.0f, 100.0f, value);
    for (int i = 0; i < 4; ++i)
        if (value <= kDecayValues[i + 1])
            return juce::jmap(value, kDecayValues[i], kDecayValues[i + 1],
                                     kDecayNorms[i], kDecayNorms[i + 1]);
    return 1.0f;
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

    // ── Decay: expanded resolution in the 90-100 range ────────────────────
    //   JUCE NormalisableRange lambda order:
    //     1st = convertFrom0to1  (proportion 0-1  ->  value 0-100)
    //     2nd = convertTo0to1    (value 0-100     ->  proportion 0-1)
    //   The SliderAttachment copies these lambdas onto the knob, so the GUI
    //   knob and host automation share the same feel automatically.
    juce::NormalisableRange<float> decayRange(
        0.0f, 100.0f,
        [](float, float, float norm)  -> float { return decayNormToValue(norm); },
        [](float, float, float value) -> float { return decayValueToNorm(value); },
        [](float, float, float value) -> float { return juce::jlimit(0.0f, 100.0f, value); });

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

    // Per-resonator mode
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "per_res_mode", "Per Res", false));

    // ── NEW: MIDI note input enable ───────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "midi_enabled", "MIDI In", true));

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

    juce::NormalisableRange<float> panRange(-100.0f, 100.0f, 0.1f);

    // ── Resonator 1 (note-based, no pitch offset) ─────────────────────────
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "res1_enabled", "Resonator 1 On", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_fine", "Resonator 1 Fine",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_gain", "Resonator 1 Gain",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_decay", "Resonator 1 Decay", decayRange, 95.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_color", "Resonator 1 Color",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 74.2f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "res1_const", "Resonator 1 Const", false));
    // Resonator I is a true stereo source -> pan behaves as a BALANCE.
    // Centre (0) leaves both sides at unity, i.e. exactly the old behaviour.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_pan", "Resonator 1 Pan", panRange, 0.0f));

    // ── Resonators 2-5 ────────────────────────────────────────────────────
    // Default pans reproduce the original hard-panned routing:
    //   II -> L, III -> R, IV -> L, V -> R
    const float defaultPan[5] = { 0.0f, -100.0f, 100.0f, -100.0f, 100.0f };

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
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_decay", name + " Decay", decayRange, 95.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_color", name + " Color",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 74.2f));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            id + "_const", name + " Const", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_pan", name + " Pan", panRange, defaultPan[i]));
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
    for (int i = 0; i < 5; ++i)
        midiDisplayNote[i].store(-1);
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
void ResonateAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (int i = 0; i < 5; ++i)
    {
        resonators[i].prepare(sampleRate);
        resonators[i].reset();
    }
    inputFilter.reset();

    // Pre-allocate scratch so processBlock never touches the heap.
    const int blockSize = juce::jmax(1, samplesPerBlock);
    res1Buffer   .setSize(2, blockSize, false, true, true);
    res2to5Buffer.setSize(2, blockSize, false, true, true);
    wetBuffer    .setSize(2, blockSize, false, true, true);

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
// MIDI voice allocation
//
// Round robin: the first note lands in slot 0 (resonator I), the next in the
// next FREE slot walking upwards with wraparound, and so on. Releasing a key
// frees its slot for re-use but leaves the pitch in place, so the resonator
// keeps ringing at the note you played rather than snapping back -- which is
// what you want from a resonator rather than a synth voice.

void ResonateAudioProcessor::handleMidiMessages(const juce::MidiBuffer& midiMessages)
{
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            int slot = -1;

            // Walk from the round-robin pointer looking for a free slot
            for (int k = 0; k < 5; ++k)
            {
                const int idx = (rrPointer + k) % 5;
                if (!voices[idx].active) { slot = idx; break; }
            }

            // All five held -> steal the oldest
            if (slot < 0)
            {
                juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
                slot = 0;
                for (int i = 0; i < 5; ++i)
                    if (voices[i].order < oldest) { oldest = voices[i].order; slot = i; }
            }

            voices[slot].note   = msg.getNoteNumber();
            voices[slot].active = true;
            voices[slot].order  = ++voiceCounter;

            heldNote[slot] = msg.getNoteNumber();
            midiDisplayNote[slot].store(msg.getNoteNumber());

            rrPointer = (slot + 1) % 5;
        }
        else if (msg.isNoteOff())
        {
            for (int i = 0; i < 5; ++i)
                if (voices[i].active && voices[i].note == msg.getNoteNumber())
                {
                    voices[i].active = false;
                    break;
                }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int i = 0; i < 5; ++i)
                voices[i].active = false;
        }
    }
}

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
    midiEnabled = parameters.getRawParameterValue("midi_enabled")->load() > 0.5f;

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

    // Helper to read per-resonator local decay/color/const/pan
    auto getLocalDecay = [&](int idx) -> double {
        return parameters.getRawParameterValue("res" + juce::String(idx + 1) + "_decay")->load();
        };
    auto getLocalColor = [&](int idx) -> double {
        return parameters.getRawParameterValue("res" + juce::String(idx + 1) + "_color")->load();
        };
    auto getLocalConst = [&](int idx) -> bool {
        return parameters.getRawParameterValue("res" + juce::String(idx + 1) + "_const")->load() > 0.5f;
        };

    for (int i = 0; i < 5; ++i)
        resPan[i] = parameters.getRawParameterValue("res" + juce::String(i + 1) + "_pan")->load();

    // MIDI note override per resonator (-1 = follow the knobs)
    auto midiNoteFor = [&](int idx) -> double {
        if (!midiEnabled || heldNote[idx] < 0) return -1.0;
        return static_cast<double>(heldNote[idx]);
        };

    // Keep the GUI readout honest when MIDI is switched off
    if (!midiEnabled)
        for (int i = 0; i < 5; ++i)
            midiDisplayNote[i].store(-1);
    else
        for (int i = 0; i < 5; ++i)
            midiDisplayNote[i].store(heldNote[i]);

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
        perResMode, getLocalDecay(0), getLocalColor(0), getLocalConst(0),
        midiNoteFor(0));

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
            perResMode, getLocalDecay(i), getLocalColor(i), getLocalConst(i),
            midiNoteFor(i));
    }
}

//==============================================================================
void ResonateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    handleMidiMessages(midiMessages);
    updateResonateParameters();

    float masterGain = std::pow(10.0f, parameters.getRawParameterValue("gain")->load() / 20.0f);
    float dryWet = parameters.getRawParameterValue("drywet")->load() / 100.0f;
    float width = parameters.getRawParameterValue("width")->load() / 100.0f;

    float smoothNorm = 1.0f - static_cast<float>(globalSmooth / 100.0);
    float smoothCoeff = 0.01f + smoothNorm * 0.4f;

    const int numSamples = buffer.getNumSamples();
    const int nIn  = juce::jmin(totalNumInputChannels,  2);
    const int nOut = juce::jmin(totalNumOutputChannels, 2);

    if (numSamples <= 0 || nIn <= 0 || nOut <= 0)
        return;

    // Grow scratch if the host hands us a bigger block than promised
    if (res1Buffer.getNumSamples() < numSamples)
    {
        res1Buffer   .setSize(2, numSamples, false, true, true);
        res2to5Buffer.setSize(2, numSamples, false, true, true);
        wetBuffer    .setSize(2, numSamples, false, true, true);
    }

    res1Buffer.clear();
    res2to5Buffer.clear();
    wetBuffer.clear();

    // ── Pan gains ────────────────────────────────────────────────────────────
    // Resonator I is stereo: BALANCE law, unity on both sides at centre.
    // Resonators II-V are mono sources: constant-power law that reaches
    // exactly 1.0 at the extremes, so the default hard pans are bit-identical
    // to the previous fixed routing.
    float panL[5], panR[5];
    for (int i = 0; i < 5; ++i)
    {
        const double p = juce::jlimit(-1.0, 1.0, resPan[i] / 100.0);

        if (i == 0)
        {
            panL[0] = static_cast<float>(p <= 0.0 ? 1.0 : 1.0 - p);
            panR[0] = static_cast<float>(p >= 0.0 ? 1.0 : 1.0 + p);
        }
        else if (p <= -0.9995)   // snap the extremes: cos(pi/2) is ~6e-17, not 0,
        {                        // which would leak the old hard pans at -147 dB
            panL[i] = 1.0f; panR[i] = 0.0f;
        }
        else if (p >= 0.9995)
        {
            panL[i] = 0.0f; panR[i] = 1.0f;
        }
        else
        {
            const double theta = (p + 1.0) * juce::MathConstants<double>::pi * 0.25;
            panL[i] = static_cast<float>(std::cos(theta));
            panR[i] = static_cast<float>(std::sin(theta));
        }
    }

    // Which input channel each resonator reads. UNCHANGED from the original:
    // keeping this fixed is what makes each resonator's internal state evolve
    // exactly as before. Pan only redistributes the result afterwards.
    static const int sourceChannel[5] = { -1, 0, 1, 0, 1 }; // -1 = res I, both

    const float* inPtr[2] = { nullptr, nullptr };
    for (int ch = 0; ch < nIn; ++ch)
        inPtr[ch] = buffer.getReadPointer(ch);

    float* res1Ptr[2]   = { res1Buffer.getWritePointer(0),
                            res1Buffer.getNumChannels() > 1 ? res1Buffer.getWritePointer(1) : nullptr };
    float* res25Ptr[2]  = { res2to5Buffer.getWritePointer(0),
                            res2to5Buffer.getNumChannels() > 1 ? res2to5Buffer.getWritePointer(1) : nullptr };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // ── Input conditioning, per channel (state is per-channel, so the
        //    loop order swap versus the original is bit-exact) ──────────────
        float sIn[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < nIn; ++ch)
        {
            float input = inPtr[ch][sample];
            inputSmoothing[ch] += smoothCoeff * (input - inputSmoothing[ch]);
            float s = inputSmoothing[ch];
            if (filterEnabled)
                s = inputFilter.process(s, ch, filterType);
            sIn[ch] = s;
        }

        // ── Resonator I (stereo) ─────────────────────────────────────────────
        float r1L = 0.0f, r1R = 0.0f;
        for (int ch = 0; ch < nIn; ++ch)
        {
            const float o = (processingMode == ModeA)
                ? resonators[0].processA(sIn[ch], ch)
                : resonators[0].processB(sIn[ch], ch);

            if (ch == 0) r1L = o * panL[0];
            else         r1R = o * panR[0];
        }

        // ── Resonators II-V (mono sources, panned) ───────────────────────────
        float busL = 0.0f, busR = 0.0f;
        for (int idx = 1; idx < 5; ++idx)
        {
            const int ch = sourceChannel[idx];
            if (ch >= nIn) continue;   // mono input: III and V stay silent, as before

            const float o = (processingMode == ModeA)
                ? resonators[idx].processA(sIn[ch], ch)
                : resonators[idx].processB(sIn[ch], ch);

            busL += o * panL[idx];
            busR += o * panR[idx];
        }

        res1Ptr[0][sample]  = r1L;
        res25Ptr[0][sample] = busL;
        if (nOut > 1 && res1Ptr[1] != nullptr)
        {
            res1Ptr[1][sample]  = r1R;
            res25Ptr[1][sample] = busR;
        }
    }

    // Stereo width on II-V
    if (nIn >= 2 && nOut >= 2)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float left = res25Ptr[0][sample];
            float right = res25Ptr[1][sample];
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f * width;
            res25Ptr[0][sample] = mid + side;
            res25Ptr[1][sample] = mid - side;
        }
    }

    // Combine and output-smooth
    for (int channel = 0; channel < nOut; ++channel)
    {
        auto* wet = wetBuffer.getWritePointer(channel);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float combined = res1Buffer.getSample(channel, sample)
                + res2to5Buffer.getSample(channel, sample);
            outputSmoothing[channel] += smoothCoeff * (combined - outputSmoothing[channel]);
            wet[sample] = outputSmoothing[channel];
        }
    }

    // Dry / wet mix
    for (int channel = 0; channel < juce::jmin(nOut, totalNumOutputChannels); ++channel)
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

//==============================================================================
// Preset files

juce::File ResonateAudioProcessor::getDefaultPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("AquaNode")
                   .getChildFile("Resonate Presets");
    if (!dir.exists())
        dir.createDirectory();
    return dir;
}

bool ResonateAudioProcessor::savePresetToFile(const juce::File& file)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml == nullptr)
        return false;

    xml->setAttribute("presetName", file.getFileNameWithoutExtension());
    xml->setAttribute("pluginVersion", JucePlugin_VersionString);

    return xml->writeTo(file, {});
}

bool ResonateAudioProcessor::loadPresetFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return false;

    parameters.replaceState(juce::ValueTree::fromXml(*xml));
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonateAudioProcessor();
}
