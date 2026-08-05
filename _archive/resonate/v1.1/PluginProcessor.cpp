/*
  ==============================================================================

    Ableton-Style Resonator Plugin Implementation
    
    - Accurate pitch calculation
    - Mode A (simple comb), Mode B (Diffusion/Square wave)
    - Multi-mode input filter (Lowpass, Highpass, Bandpass, Notch)
    - Color parameter for damping
    - Global Note parameter
    - Const mode for pitch-independent decay
    - Wet-only mode
    - Improved decay range (focus on 60-100)
    - Correct signal routing (Res I: both L+R, II/IV: L, III/V: R)
    - Fixed width (affects only wet signal from Res II-V)
    - Fixed smooth (inverted: 0%=max smooth, 100%=no smooth)
    - Chorus/LFO modulation for beating effects

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// State Variable Filter Implementation

void ResonateAudioProcessor::StateVariableFilter::setFrequency(float normalizedFreq, float resonance)
{
    freq = juce::jlimit(0.0f, 0.99f, normalizedFreq);
    q = juce::jlimit(0.1f, 10.0f, resonance);
}

float ResonateAudioProcessor::StateVariableFilter::process(float input, int channel, FilterType type)
{
    // State-variable filter (2-pole)
    float fb = q + q / (1.0f - freq);
    low[channel] += freq * band[channel];
    float high = input - low[channel] - fb * band[channel];
    band[channel] += freq * high;
    
    switch (type)
    {
        case Lowpass:
            return low[channel];
        case Highpass:
            return high;
        case Bandpass:
            return band[channel];
        case Notch:
            return low[channel] + high;
        default:
            return low[channel];
    }
}

void ResonateAudioProcessor::StateVariableFilter::reset()
{
    low[0] = low[1] = 0.0f;
    band[0] = band[1] = 0.0f;
}

//==============================================================================
// Resonator methods

void ResonateAudioProcessor::Resonate::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
    
    for (int ch = 0; ch < 2; ++ch)
    {
        delayBuffer[ch].resize(MAX_DELAY_SAMPLES, 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        allpassState[ch] = 0.0f;
        lfoPhase[ch] = 0.0;
    }
}

void ResonateAudioProcessor::Resonate::updateParameters(double sampleRate, double globalDecay, 
                                                          double globalNote, double color, ProcessingMode mode,
                                                          bool constMode, double chorusAmount, int resonatorIndex,
                                                          double userLfoRate, double userLfoDepth)
{
    if (!enabled || sampleRate <= 0.0)
    {
        feedback = 0.0;
        return;
    }
    
    currentSampleRate = sampleRate;
    
    // Set LFO parameters for chorus/beating effect
    // Each resonator gets a slightly different LFO rate to create complex beating
    const double rateOffsets[5] = {0.0, 0.13, 0.27, 0.41, 0.19}; // Different rates per resonator
    
    // Use chorus amount as a master multiplier, then apply user LFO rate
    // LFO rate: Use the exposed parameter directly
    double baseRate = userLfoRate;
    lfoRate = (baseRate + rateOffsets[resonatorIndex % 5] * 0.3) * (chorusAmount / 100.0);
    
    // LFO depth: Use the exposed parameter directly (in cents)
    lfoDepthCents = userLfoDepth * (chorusAmount / 100.0);
    
    // Calculate frequency from global note + pitch offset + fine tuning
    // MIDI note to frequency: f = 440 * 2^((note - 69)/12)
    double totalNote = globalNote + static_cast<double>(pitchSemitones);
    double cents = (totalNote - 69.0) * 100.0 + fineDetune;
    targetFrequency = 440.0 * std::pow(2.0, cents / 1200.0);
    
    // Clamp to reasonable range
    targetFrequency = juce::jlimit(20.0, 20000.0, targetFrequency);
    
    // Calculate delay length: delay = sampleRate / frequency
    delayInSamples = sampleRate / targetFrequency;
    delayInSamples = juce::jlimit(2.0, static_cast<double>(MAX_DELAY_SAMPLES - 2), delayInSamples);
    
    // Map decay (0-100) to feedback with improved curve
    // Focus the useful range in 60-100 region
    double decayNorm = globalDecay / 100.0;
    
    // Use exponential curve to make lower values less sensitive
    // and upper values (80-100) more useful
    feedback = std::pow(decayNorm, 0.25); // Fourth root for slower increase
    feedback = juce::jlimit(0.0, 0.9999, feedback);
    
    // Const mode: adjust feedback based on frequency to maintain consistent decay time
    if (constMode)
    {
        // Lower frequencies need more feedback to sustain as long as higher frequencies
        // Normalize to middle C (261.63 Hz)
        double referenceFreq = 261.63;
        double freqRatio = referenceFreq / targetFrequency; // Inverted - lower freq needs more
        
        // Compensate: lower notes get more feedback, higher notes get less
        double constCompensation = std::pow(freqRatio, 0.3); // Increased power for stronger effect
        feedback *= constCompensation;
        feedback = juce::jlimit(0.0, 0.9999, feedback);
    }
    
    // Color parameter controls damping filter
    // 0 = heavy damping (dark), 100 = no damping (bright)
    double colorNorm = color / 100.0;
    lpfCoeff = 0.3 + colorNorm * 0.69; // 0.3 to 0.99
}

float ResonateAudioProcessor::Resonate::readDelayLinear(int channel, double delaySamples)
{
    double readPos = static_cast<double>(writeIndex[channel]) - delaySamples;
    
    while (readPos < 0.0)
        readPos += static_cast<double>(MAX_DELAY_SAMPLES);
    
    int index1 = static_cast<int>(readPos) % MAX_DELAY_SAMPLES;
    int index2 = (index1 + 1) % MAX_DELAY_SAMPLES;
    float frac = static_cast<float>(readPos - std::floor(readPos));
    
    float sample1 = delayBuffer[channel][index1];
    float sample2 = delayBuffer[channel][index2];
    
    return sample1 + frac * (sample2 - sample1);
}

float ResonateAudioProcessor::Resonate::processA(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    // Calculate delay time (with optional LFO modulation)
    double actualDelay = delayInSamples;
    
    // Only apply LFO modulation if there's actual depth
    if (lfoDepthCents > 0.001)
    {
        // Advance LFO (separate phase per channel for stereo width)
        if (lfoRate > 0.0 && currentSampleRate > 0.0)
        {
            lfoPhase[channel] += lfoRate / currentSampleRate;
            if (lfoPhase[channel] >= 1.0)
                lfoPhase[channel] -= 1.0;
        }

        // Generate LFO value (sine wave)
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);

        // Convert cents modulation to frequency ratio
        double centsMod = lfo * lfoDepthCents;
        double freqRatio = std::pow(2.0, centsMod / 1200.0);

        // Modulate delay time
        actualDelay = delayInSamples / freqRatio;
        actualDelay = juce::jlimit(2.0, static_cast<double>(MAX_DELAY_SAMPLES - 2), actualDelay);
    }

    // Mode A: Classic comb filter with damping
    float delayedSample = readDelayLinear(channel, actualDelay);
    
    // One-pole lowpass in feedback path for damping
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float dampedFeedback = lpfState[channel];
    
    // Comb filter equation: y[n] = x[n] + g * y[n-D]
    float feedbackSignal = static_cast<float>(feedback) * dampedFeedback;
    float output = input + feedbackSignal;
    
    // Soft saturation to prevent runaway
    output = std::tanh(output);
    
    delayBuffer[channel][writeIndex[channel]] = output;
    
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;
    
    // Apply gain
    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

float ResonateAudioProcessor::Resonate::processB(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    // Calculate delay time (with optional LFO modulation)
    // Mode B uses half delay for square wave character
    double baseModeDelay = delayInSamples * 0.5;
    double actualDelay = baseModeDelay;
    
    // Only apply LFO modulation if there's actual depth
    if (lfoDepthCents > 0.001)
    {
        // Advance LFO (separate phase per channel for stereo width)
        if (lfoRate > 0.0 && currentSampleRate > 0.0)
        {
            lfoPhase[channel] += lfoRate / currentSampleRate;
            if (lfoPhase[channel] >= 1.0)
                lfoPhase[channel] -= 1.0;
        }

        // Generate LFO value (sine wave)
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);

        // Convert cents modulation to frequency ratio
        double centsMod = lfo * lfoDepthCents;
        double freqRatio = std::pow(2.0, centsMod / 1200.0);

        // Modulate delay time
        actualDelay = baseModeDelay / freqRatio;
    }
    
    actualDelay = std::max(1.0, actualDelay);

    // Read from delay
    float delayedSample = readDelayLinear(channel, actualDelay);

    // Apply damping (standard one-pole lowpass)
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float damped = lpfState[channel];

    // Negative feedback for Mode B (creates square wave character)
    float feedbackSignal = -static_cast<float>(feedback) * damped;

    // Sum input and feedback
    float rawOutput = input + feedbackSignal;

    // Stability saturation
    float saturatedOutput = std::tanh(rawOutput);

    // Write to buffer
    delayBuffer[channel][writeIndex[channel]] = saturatedOutput;

    // Increment buffer index
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    // Apply gain
    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return saturatedOutput * gainLinear;
}

void ResonateAudioProcessor::Resonate::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill(delayBuffer[ch].begin(), delayBuffer[ch].end(), 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        allpassState[ch] = 0.0f;
    }
}

//==============================================================================
// Parameter layout

static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    // Filter section
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "filter_enabled", "Filter On", false));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "filter_freq", "Filter Frequency",
        juce::NormalisableRange<float>(20.0f, 12000.0f, 1.0f, 0.3f), 1000.0f));
    
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "filter_type", "Filter Type",
        juce::StringArray{"Lowpass", "Highpass", "Bandpass", "Notch"}, 0));
    
    // Mode selection (A or B)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode",
        juce::StringArray{"A", "B"}, 0));
    
    // Decay control skewed to 80 - 100 range
    juce::NormalisableRange<float> decayRange(
        0.0f, 100.0f,
        [](float start, float end, float normalised)
        {
            if (normalised < 0.5f)
            {
                // Left half: 0–80
                return juce::jmap(normalised, 0.0f, 0.5f, 0.0f, 80.0f);
            }
            else
            {
                // Right half: 80–100
                return juce::jmap(normalised, 0.5f, 1.0f, 80.0f, 100.0f);
            }
        },
        [](float start, float end, float value)
        {
            if (value < 80.0f)
            {
                return juce::jmap(value, 0.0f, 80.0f, 0.0f, 0.5f);
            }
            else
            {
                return juce::jmap(value, 80.0f, 100.0f, 0.5f, 1.0f);
            }
        }
    );
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay",
        decayRange,
        95.0f));

    
    // Const mode switch
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "const_mode", "Const", false));
    
    // Color (damping)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "color", "Color",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 74.2f));
    
    // Smooth parameter - input/output smoothing (0=max smooth, 100=no smooth)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "smooth", "Smooth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    
    // Chorus parameter - beating/detuning effect (0=off, 100=max)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chorus", "Chorus",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    
    // LFO Rate parameter - controls the speed of the chorus modulation
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfo_rate", "LFO Rate",
        juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f), 0.5f));
    
    // LFO Depth parameter - controls the intensity of the chorus modulation (in cents)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfo_depth", "LFO Depth",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 8.0f));
    
    // Note parameter - now part of first resonator channel
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_note", "Resonator 1 Note",
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), 60.0f));
    
    // Stereo Width (affects only wet signal from Res II-V)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));
    
    // Master Gain
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
    
    // Dry/Wet
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "drywet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    
    // Wet Only switch
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "wet_only", "Wet Only", false));
    
    // 5 Resonators (Resonator 1 has note parameter, others have pitch offset)
    // Resonator 1
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "res1_enabled", "Resonator 1 On", true));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_fine", "Resonator 1 Fine",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "res1_gain", "Resonator 1 Gain",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
    
    // Resonators 2-5 have pitch offset from Resonator 1
    for (int i = 1; i < 5; ++i)
    {
        juce::String id = "res" + juce::String(i + 1);
        juce::String name = "Resonator " + juce::String(i + 1);
        
        layout.add(std::make_unique<juce::AudioParameterBool>(
            id + "_enabled", name + " On", true));
        
        layout.add(std::make_unique<juce::AudioParameterInt>(
            id + "_pitch", name + " Pitch",
            -24, 24, 0));
        
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_fine", name + " Fine",
            juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
        
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id + "_gain", name + " Gain",
            juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
    }
    
    return layout;
}

//==============================================================================
ResonateAudioProcessor::ResonateAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

ResonateAudioProcessor::~ResonateAudioProcessor()
{
}

//==============================================================================
const juce::String ResonateAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

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

double ResonateAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ResonateAudioProcessor::getNumPrograms()
{
    return 1;
}

int ResonateAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ResonateAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ResonateAudioProcessor::getProgramName (int index)
{
    return {};
}

void ResonateAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void ResonateAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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
    {
        resonators[i].reset();
    }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ResonateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
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

void ResonateAudioProcessor::updateResonateParameters()
{
    // Get global note from resonator 1
    globalNote = parameters.getRawParameterValue("res1_note")->load();
    globalDecay = parameters.getRawParameterValue("decay")->load();
    globalColor = parameters.getRawParameterValue("color")->load();
    globalSmooth = parameters.getRawParameterValue("smooth")->load();
    globalChorus = parameters.getRawParameterValue("chorus")->load();
    filterEnabled = parameters.getRawParameterValue("filter_enabled")->load() > 0.5f;
    filterFrequency = parameters.getRawParameterValue("filter_freq")->load();
    constMode = parameters.getRawParameterValue("const_mode")->load() > 0.5f;
    wetOnly = parameters.getRawParameterValue("wet_only")->load() > 0.5f;
    
    // Get LFO parameters
    double lfoRate = parameters.getRawParameterValue("lfo_rate")->load();
    double lfoDepth = parameters.getRawParameterValue("lfo_depth")->load();
    
    int modeIndex = static_cast<int>(parameters.getRawParameterValue("mode")->load());
    processingMode = (modeIndex == 0) ? ModeA : ModeB;
    
    int filterTypeIndex = static_cast<int>(parameters.getRawParameterValue("filter_type")->load());
    filterType = static_cast<FilterType>(filterTypeIndex);
    
    // Update filter
    if (filterEnabled && currentSampleRate > 0.0)
    {
        float normalizedFreq = juce::jlimit(0.0f, 0.99f, 
            static_cast<float>(filterFrequency / (currentSampleRate * 0.5)));
        inputFilter.setFrequency(normalizedFreq, 0.7f);
    }
    
    // Update resonator 1 (has note parameter, no pitch offset)
    resonators[0].enabled = parameters.getRawParameterValue("res1_enabled")->load() > 0.5f;
    resonators[0].pitchSemitones = 0; // Always 0 for resonator 1
    resonators[0].fineDetune = parameters.getRawParameterValue("res1_fine")->load();
    resonators[0].gain = parameters.getRawParameterValue("res1_gain")->load();
    resonators[0].updateParameters(currentSampleRate, globalDecay, globalNote, 
                                   globalColor, processingMode, constMode, globalChorus, 0, lfoRate, lfoDepth);
    
    // Update resonators 2-5 (have pitch offset from resonator 1)
    for (int i = 1; i < 5; ++i)
    {
        juce::String id = "res" + juce::String(i + 1);
        
        resonators[i].enabled = parameters.getRawParameterValue(id + "_enabled")->load() > 0.5f;
        resonators[i].pitchSemitones = static_cast<int>(
            parameters.getRawParameterValue(id + "_pitch")->load());
        resonators[i].fineDetune = parameters.getRawParameterValue(id + "_fine")->load();
        resonators[i].gain = parameters.getRawParameterValue(id + "_gain")->load();
        
        resonators[i].updateParameters(currentSampleRate, globalDecay, globalNote, 
                                       globalColor, processingMode, constMode, globalChorus, i, lfoRate, lfoDepth);
    }
}

void ResonateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateResonateParameters();
    
    float masterGain = std::pow(10.0f, parameters.getRawParameterValue("gain")->load() / 20.0f);
    float dryWet = parameters.getRawParameterValue("drywet")->load() / 100.0f;
    float width = parameters.getRawParameterValue("width")->load() / 100.0f;
    
    // Calculate smoothing coefficient from smooth parameter (0-100)
    // FIXED: 0% = maximum smoothing, 100% = no smoothing (inverted)
    float smoothNorm = 1.0f - (globalSmooth / 100.0f); // Invert: 0 becomes 1, 100 becomes 0
    float smoothCoeff = 0.01f + smoothNorm * 0.4f; // 0.01 to 0.41
    
    int numSamples = buffer.getNumSamples();
    
    // Separate buffers for resonator I and resonators II-V
    juce::AudioBuffer<float> res1Buffer(totalNumOutputChannels, numSamples);
    juce::AudioBuffer<float> res2to5Buffer(totalNumOutputChannels, numSamples);
    res1Buffer.clear();
    res2to5Buffer.clear();
    
    // Process each channel
    for (int channel = 0; channel < juce::jmin(totalNumInputChannels, 2); ++channel)
    {
        auto* dryData = buffer.getReadPointer(channel);
        auto* res1Data = res1Buffer.getWritePointer(channel);
        auto* res2to5Data = res2to5Buffer.getWritePointer(channel);
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float input = dryData[sample];
            
            // Apply input smoothing
            inputSmoothing[channel] += smoothCoeff * (input - inputSmoothing[channel]);
            float smoothedInput = inputSmoothing[channel];
            
            // Apply input filter if enabled
            if (filterEnabled)
            {
                smoothedInput = inputFilter.process(smoothedInput, channel, filterType);
            }
            
            // Process resonator I (gets both L+R channels)
            float res1Output = 0.0f;
            if (processingMode == ModeA)
                res1Output = resonators[0].processA(smoothedInput, channel);
            else
                res1Output = resonators[0].processB(smoothedInput, channel);
            
            res1Data[sample] = res1Output;
            
            // Process resonators II-V with correct channel routing:
            // Resonator II (index 1) and IV (index 3) = left channel only
            // Resonator III (index 2) and V (index 4) = right channel only
            float res2to5Output = 0.0f;
            
            // Resonators II (1) and IV (3) - left channel
            if (channel == 0)
            {
                for (int resIdx : {1, 3})
                {
                    if (processingMode == ModeA)
                        res2to5Output += resonators[resIdx].processA(smoothedInput, channel);
                    else
                        res2to5Output += resonators[resIdx].processB(smoothedInput, channel);
                }
            }
            
            // Resonators III (2) and V (4) - right channel
            if (channel == 1)
            {
                for (int resIdx : {2, 4})
                {
                    if (processingMode == ModeA)
                        res2to5Output += resonators[resIdx].processA(smoothedInput, channel);
                    else
                        res2to5Output += resonators[resIdx].processB(smoothedInput, channel);
                }
            }
            
            res2to5Data[sample] = res2to5Output;
        }
    }
    
    // Apply stereo width ONLY to resonators II-V (not resonator I)
    // Width affects only the wet signal and blends L+R outputs into mono if set to 0
    if (totalNumInputChannels >= 2 && totalNumOutputChannels >= 2)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float left = res2to5Buffer.getSample(0, sample);
            float right = res2to5Buffer.getSample(1, sample);
            
            // Convert to Mid/Side
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f;
            
            // Apply width to side signal (0 = mono, 100 = full stereo)
            side *= width;
            
            // Convert back to Left/Right
            res2to5Buffer.setSample(0, sample, mid + side);
            res2to5Buffer.setSample(1, sample, mid - side);
        }
    }
    
    // Combine resonator I and resonators II-V outputs
    juce::AudioBuffer<float> wetBuffer(totalNumOutputChannels, numSamples);
    wetBuffer.clear();
    
    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float combinedOutput = res1Buffer.getSample(channel, sample) + 
                                  res2to5Buffer.getSample(channel, sample);
            
            // Apply output smoothing
            outputSmoothing[channel] += smoothCoeff * (combinedOutput - outputSmoothing[channel]);
            wetBuffer.setSample(channel, sample, outputSmoothing[channel]);
        }
    }
    
    // Mix dry and wet
    for (int channel = 0; channel < juce::jmin(totalNumInputChannels, totalNumOutputChannels); ++channel)
    {
        auto* dryData = buffer.getWritePointer(channel);
        auto* wetData = wetBuffer.getReadPointer(channel);
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float dry = dryData[sample];
            float wet = wetData[sample] * masterGain;
            
            if (wetOnly)
            {
                // Wet only mode - output only wet signal scaled by dry/wet
                dryData[sample] = wet * dryWet;
            }
            else
            {
                // Normal dry/wet mix
                dryData[sample] = dry * (1.0f - dryWet) + wet * dryWet;
            }
        }
    }
}

//==============================================================================
bool ResonateAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* ResonateAudioProcessor::createEditor()
{
    return new ResonateAudioProcessorEditor (*this);
}

//==============================================================================
void ResonateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ResonateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonateAudioProcessor();
}