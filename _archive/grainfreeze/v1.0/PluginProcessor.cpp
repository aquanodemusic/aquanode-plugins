#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Constructor
//==============================================================================

GrainfreezeAudioProcessor::GrainfreezeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Time stretching factor (0.1 = 10x faster, 1.0 = normal, 4.0 = 4x slower)
    addParameter(timeStretch = new juce::AudioParameterFloat(
        "timeStretch", "Time Stretch",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f, 0.5f),
        1.0f));

    // Grain size (reserved for future enhancements)
    addParameter(grainSizeParam = new juce::AudioParameterFloat(
        "grainSize", "Grain Size",
        juce::NormalisableRange<float>(512.0f, 8192.0f, 1.0f, 0.3f),
        2048.0f));

    // Hop size as divisor of FFT size (lower = more overlap = smoother)
    addParameter(hopSizeParam = new juce::AudioParameterFloat(
        "hopSize", "Hop Size",
        juce::NormalisableRange<float>(2.0f, 16.0f, 0.5f),
        4.0f));

    // FFT Size (larger = better frequency resolution, more latency)
    juce::StringArray fftSizeChoices;
    fftSizeChoices.add("512");
    fftSizeChoices.add("1024");
    fftSizeChoices.add("2048");
    fftSizeChoices.add("4096");
    fftSizeChoices.add("8192");
    fftSizeChoices.add("16384");
    fftSizeChoices.add("32768");
    fftSizeChoices.add("65536");

    addParameter(fftSizeParam = new juce::AudioParameterChoice(
        "fftSize", "FFT Size",
        fftSizeChoices,
        3));

    // Freeze mode toggle
    addParameter(freezeModeParam = new juce::AudioParameterBool(
        "freezeMode", "Freeze Mode",
        false));

    // Glide time for freeze mode position changes (0-1000ms)
    addParameter(glideParam = new juce::AudioParameterFloat(
        "glide", "Glide",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 1.0f, 0.5f),
        100.0f));

    // High-frequency boost to compensate for phase vocoder roll-off (0-100%)
    addParameter(hfBoostParam = new juce::AudioParameterFloat(
        "hfBoost", "HF Boost",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        10.0f));

    // Micro-movement amount in freeze mode to reduce artifacts (0-100%)
    addParameter(microMovementParam = new juce::AudioParameterFloat(
        "microMovement", "Micro Movement",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        20.0f));

    // Window function type selection
    juce::StringArray windowChoices;
    windowChoices.add("Hann");
    windowChoices.add("Blackman-Harris");

    addParameter(windowTypeParam = new juce::AudioParameterChoice(
        "windowType", "Window Type",
        windowChoices,
        1));

    // Crossfade length for smooth playhead jumps (1-8 hops)
    addParameter(crossfadeLengthParam = new juce::AudioParameterFloat(
        "crossfadeLength", "Crossfade Length",
        juce::NormalisableRange<float>(1.0f, 8.0f, 0.5f),
        2.0f));

    updateFftSize();
}

GrainfreezeAudioProcessor::~GrainfreezeAudioProcessor()
{
}

//==============================================================================
// Plugin Info
//==============================================================================

const juce::String GrainfreezeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GrainfreezeAudioProcessor::acceptsMidi() const { return false; }
bool GrainfreezeAudioProcessor::producesMidi() const { return false; }
bool GrainfreezeAudioProcessor::isMidiEffect() const { return false; }
double GrainfreezeAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int GrainfreezeAudioProcessor::getNumPrograms() { return 1; }
int GrainfreezeAudioProcessor::getCurrentProgram() { return 0; }
void GrainfreezeAudioProcessor::setCurrentProgram(int index) {}
const juce::String GrainfreezeAudioProcessor::getProgramName(int index) { return {}; }
void GrainfreezeAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

//==============================================================================
// Audio Processing Setup
//==============================================================================

void GrainfreezeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    playbackPosition = 0.0;
    outputWritePos = 0;

    // Initialize freeze mode smoothing
    smoothedFreezePosition.reset(sampleRate, 0.1);
    smoothedFreezePosition.setCurrentAndTargetValue(0.0);
    freezeCurrentPosition = 0.0;
    freezeTargetPosition = 0.0;
    freezeMicroMovement = 0.0f;
    freezeMicroCounter = 0;

    // Clear all processing buffers
    std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
    std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
    std::fill(synthesisPhase.begin(), synthesisPhase.end(), 0.0f);
}

void GrainfreezeAudioProcessor::releaseResources()
{
}

bool GrainfreezeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only support stereo output
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
// Main Audio Processing
//==============================================================================

void GrainfreezeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    // Only process if audio is loaded and playback is active
    if (!audioLoaded || !playing)
        return;

    processTimeStretch(buffer, buffer.getNumSamples());
}

//==============================================================================
// Time Stretching and Freeze Mode
//==============================================================================

void GrainfreezeAudioProcessor::processTimeStretch(juce::AudioBuffer<float>& outputBuffer, int numSamples)
{
    // Check if FFT size changed (requires buffer reallocation)
    static int lastFftSizeIndex = -1;
    bool fftSizeChanged = (fftSizeParam->getIndex() != lastFftSizeIndex);

    if (fftSizeChanged)
    {
        lastFftSizeIndex = fftSizeParam->getIndex();
        updateFftSize();
    }

    // Check if hop size changed (updates immediately for responsive control)
    static float lastHopSize = -1.0f;
    float currentHopParam = hopSizeParam->get();

    if (currentHopParam != lastHopSize)
    {
        lastHopSize = currentHopParam;
        updateHopSize();

        // Clear accumulator to prevent artifacts
        std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
        outputWritePos = 0;
        grainCounter = 0;
    }

    // Check if window type changed (updates window function)
    static int lastWindowType = -1;
    int currentWindowType = windowTypeParam->getIndex();

    if (currentWindowType != lastWindowType)
    {
        lastWindowType = currentWindowType;
        createWindow();
    }

    // Update freeze mode state
    isInFreezeMode = freezeModeParam->get();

    // Update glide time when changed
    static float lastGlideTime = -1.0f;
    float currentGlideTime = glideParam->get();
    if (currentGlideTime != lastGlideTime)
    {
        lastGlideTime = currentGlideTime;
        smoothedFreezePosition.reset(currentSampleRate, currentGlideTime / 1000.0);
    }

    // Get time stretch factor and calculate playback speed
    float stretch = timeStretch->get();
    float playbackSpeed = 1.0f / stretch;

    // FREEZE MODE processing
    if (isInFreezeMode)
    {
        // Set target position for smooth gliding
        smoothedFreezePosition.setTargetValue(freezeTargetPosition);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Smoothly glide toward target position
            freezeCurrentPosition = smoothedFreezePosition.getNextValue();

            // Add micro-movement to reduce static artifacts
            freezeMicroCounter++;
            if (freezeMicroCounter >= currentHopSize / 4)
            {
                freezeMicroCounter = 0;
                // Scale random movement by parameter (0-100%)
                float movementAmount = microMovementParam->get() / 100.0f;
                freezeMicroMovement = (static_cast<float>(rand()) / RAND_MAX - 0.5f) *
                    0.0002f * movementAmount;
            }

            // Apply playback position with micro-movement
            playbackPosition = freezeCurrentPosition + (freezeMicroMovement * loadedAudio.getNumSamples());

            // Wrap position within audio bounds
            if (playbackPosition >= loadedAudio.getNumSamples())
                playbackPosition = fmod(playbackPosition, static_cast<double>(loadedAudio.getNumSamples()));
            if (playbackPosition < 0.0)
                playbackPosition = 0.0;

            // Process new grain when counter reaches zero
            if (grainCounter == 0)
            {
                performPhaseVocoder();
                grainCounter = currentHopSize;
            }

            // Read output sample from accumulator
            float outputSample = 0.0f;
            if (outputWritePos < outputAccum.size())
            {
                outputSample = outputAccum[outputWritePos];
                outputAccum[outputWritePos] = 0.0f;
            }

            // Write to all output channels
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.setSample(ch, sample, outputSample);

            outputWritePos = (outputWritePos + 1) % outputAccum.size();
            grainCounter--;

            // Update UI playhead position
            playheadPosition.store(static_cast<float>(freezeCurrentPosition) / loadedAudio.getNumSamples());
        }
    }
    // NORMAL PLAYBACK MODE
    else
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Process new grain when counter reaches zero
            if (grainCounter == 0)
            {
                performPhaseVocoder();
                grainCounter = currentHopSize;
            }

            // Read output sample from accumulator
            float outputSample = 0.0f;
            if (outputWritePos < outputAccum.size())
            {
                outputSample = outputAccum[outputWritePos];
                outputAccum[outputWritePos] = 0.0f;
            }

            // Write to all output channels
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.setSample(ch, sample, outputSample);

            outputWritePos = (outputWritePos + 1) % outputAccum.size();
            grainCounter--;

            // Advance playback position
            playbackPosition += playbackSpeed;

            // Wrap playback position
            if (playbackPosition >= loadedAudio.getNumSamples())
                playbackPosition = fmod(playbackPosition, static_cast<double>(loadedAudio.getNumSamples()));

            // Update UI playhead position
            playheadPosition.store(static_cast<float>(playbackPosition) / loadedAudio.getNumSamples());
        }
    }
}

//==============================================================================
// Phase Vocoder (main DSP algorithm)
//==============================================================================

void GrainfreezeAudioProcessor::performPhaseVocoder()
{
    if (!audioLoaded)
        return;

    // Calculate read position and clamp to valid bounds
    int readPos = static_cast<int>(playbackPosition);
    readPos = juce::jlimit(0, loadedAudio.getNumSamples() - currentFftSize, readPos);

    // Get source audio pointers
    const float* sourceData = loadedAudio.getReadPointer(0);

    // Mix stereo to mono for richer frequency content if available
    bool isStereo = loadedAudio.getNumChannels() > 1;
    const float* sourceDataR = isStereo ? loadedAudio.getReadPointer(1) : nullptr;

    // Fill analysis frame with windowed samples
    for (int i = 0; i < currentFftSize; ++i)
    {
        int sampleIndex = readPos + i;
        if (sampleIndex < loadedAudio.getNumSamples())
        {
            float sample = sourceData[sampleIndex];

            // Mix stereo to mono
            if (isStereo && sourceDataR != nullptr)
            {
                sample = (sample + sourceDataR[sampleIndex]) * 0.5f;
            }

            // Apply window function
            analysisFrame[i] = sample * window[i];
        }
        else
        {
            analysisFrame[i] = 0.0f;
        }
    }

    // Copy to FFT buffer
    std::copy(analysisFrame.begin(), analysisFrame.end(), fftBuffer.begin());

    // Forward FFT (time domain to frequency domain)
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer.data(), true);

    // Get time stretch factor
    float stretch = timeStretch->get();

    // Calculate expected phase advance per bin for one hop
    float expectedPhaseAdvance = juce::MathConstants<float>::twoPi * currentHopSize / currentFftSize;

    // Process each frequency bin
    for (int bin = 0; bin <= currentFftSize / 2; ++bin)
    {
        // Extract real and imaginary components
        float real = fftBuffer[bin * 2];
        float imag = fftBuffer[bin * 2 + 1];

        // Calculate magnitude and phase
        float magnitude = std::sqrt(real * real + imag * imag);
        float phase = std::atan2(imag, real);

        // Apply high-frequency boost (scaled by parameter)
        float freqRatio = static_cast<float>(bin) / (currentFftSize / 2);
        float hfBoostAmount = hfBoostParam->get() / 100.0f;
        float hfBoost = 1.0f + (freqRatio * hfBoostAmount);
        magnitude *= hfBoost;

        // Calculate phase difference from previous frame
        float phaseDiff = phase - previousPhase[bin];
        previousPhase[bin] = phase;

        // Subtract expected phase advance to get deviation
        float deviation = phaseDiff - (bin * expectedPhaseAdvance);

        // Wrap deviation to [-π, π] range
        while (deviation > juce::MathConstants<float>::pi)
            deviation -= juce::MathConstants<float>::twoPi;
        while (deviation < -juce::MathConstants<float>::pi)
            deviation += juce::MathConstants<float>::twoPi;

        // Calculate true instantaneous frequency
        float trueFreq = bin * expectedPhaseAdvance + deviation;

        // Advance synthesis phase with time-stretched frequency
        synthesisPhase[bin] += trueFreq * stretch;

        // Wrap synthesis phase to [-π, π]
        while (synthesisPhase[bin] > juce::MathConstants<float>::pi)
            synthesisPhase[bin] -= juce::MathConstants<float>::twoPi;
        while (synthesisPhase[bin] < -juce::MathConstants<float>::pi)
            synthesisPhase[bin] += juce::MathConstants<float>::twoPi;

        // Reconstruct complex spectrum with modified phase
        fftBuffer[bin * 2] = magnitude * std::cos(synthesisPhase[bin]);
        fftBuffer[bin * 2 + 1] = magnitude * std::sin(synthesisPhase[bin]);
    }

    // Inverse FFT (frequency domain to time domain)
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer.data());

    // Calculate normalization factor based on overlap
    float overlapFactor = static_cast<float>(currentFftSize) / currentHopSize;
    float normalization = 2.0f / overlapFactor;

    // Overlap-add to output accumulator with windowing and normalization
    for (int i = 0; i < currentFftSize; ++i)
    {
        int outIndex = (outputWritePos + i) % outputAccum.size();
        outputAccum[outIndex] += fftBuffer[i] * window[i] * normalization;
    }
}

//==============================================================================
// FFT Configuration
//==============================================================================

void GrainfreezeAudioProcessor::updateFftSize()
{
    // Map parameter index to actual FFT size
    int fftSizes[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    int selectedIndex = fftSizeParam->getIndex();
    currentFftSize = fftSizes[selectedIndex];

    // Update hop size based on new FFT size
    updateHopSize();

    // Calculate FFT order (log2 of size)
    int fftOrder = 0;
    int temp = currentFftSize;
    while (temp > 1)
    {
        temp >>= 1;
        fftOrder++;
    }

    // Create FFT objects
    fftAnalysis = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Resize all buffers
    fftBuffer.resize(currentFftSize * 2, 0.0f);
    analysisFrame.resize(currentFftSize, 0.0f);
    synthesisFrame.resize(currentFftSize, 0.0f);
    outputAccum.resize(currentFftSize * 8, 0.0f);
    crossfadeBuffer.resize(currentFftSize * 2, 0.0f);

    previousPhase.resize(currentFftSize / 2 + 1, 0.0f);
    synthesisPhase.resize(currentFftSize / 2 + 1, 0.0f);
    window.resize(currentFftSize);

    // Create window function
    createWindow();

    // Reset all state
    std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
    std::fill(crossfadeBuffer.begin(), crossfadeBuffer.end(), 0.0f);
    std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
    std::fill(synthesisPhase.begin(), synthesisPhase.end(), 0.0f);

    outputWritePos = 0;
    grainCounter = 0;
    needsCrossfade = false;
    crossfadeCounter = 0;

    // Update crossfade length based on parameter
    crossfadeSamples = static_cast<int>(currentHopSize * crossfadeLengthParam->get());
}

void GrainfreezeAudioProcessor::updateHopSize()
{
    // Calculate hop size from FFT size and divisor parameter
    float hopDivisor = hopSizeParam->get();
    currentHopSize = static_cast<int>(currentFftSize / hopDivisor);

    // Ensure minimum hop size
    currentHopSize = juce::jmax(1, currentHopSize);
}

//==============================================================================
// Window Functions
//==============================================================================

void GrainfreezeAudioProcessor::createWindow()
{
    // Create window based on selected type
    int windowType = windowTypeParam->getIndex();

    if (windowType == 0)
    {
        createHannWindow();
    }
    else
    {
        createBlackmanHarrisWindow();
    }
}

void GrainfreezeAudioProcessor::createHannWindow()
{
    // Standard Hann window (good general purpose)
    for (int i = 0; i < currentFftSize; ++i)
    {
        float n = static_cast<float>(i) / (currentFftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void GrainfreezeAudioProcessor::createBlackmanHarrisWindow()
{
    // Blackman-Harris window (better frequency resolution)
    for (int i = 0; i < currentFftSize; ++i)
    {
        float n = static_cast<float>(i) / (currentFftSize - 1);

        // 4-term Blackman-Harris coefficients
        const float a0 = 0.35875f;
        const float a1 = 0.48829f;
        const float a2 = 0.14128f;
        const float a3 = 0.01168f;

        window[i] = a0
            - a1 * std::cos(2.0f * juce::MathConstants<float>::pi * n)
            + a2 * std::cos(4.0f * juce::MathConstants<float>::pi * n)
            - a3 * std::cos(6.0f * juce::MathConstants<float>::pi * n);
    }
}

//==============================================================================
// Audio File Loading
//==============================================================================

void GrainfreezeAudioProcessor::loadAudioFile(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        // Read audio file into buffer
        loadedAudio.setSize(static_cast<int>(reader->numChannels),
            static_cast<int>(reader->lengthInSamples));
        reader->read(&loadedAudio, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        // Reset playback state
        audioLoaded = true;
        playheadPosition.store(0.0f);
        playbackPosition = 0.0;
        freezeCurrentPosition = 0.0;
        freezeTargetPosition = 0.0;
        smoothedFreezePosition.setCurrentAndTargetValue(0.0);
        grainCounter = 0;

        // Reset phase vocoder state
        std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
        std::fill(synthesisPhase.begin(), synthesisPhase.end(), 0.0f);
    }
}

//==============================================================================
// Playback Control
//==============================================================================

void GrainfreezeAudioProcessor::setPlayheadPosition(float normalizedPosition)
{
    float clampedPosition = juce::jlimit(0.0f, 1.0f, normalizedPosition);

    // In freeze mode, set target for smooth gliding
    if (isInFreezeMode || freezeModeParam->get())
    {
        freezeTargetPosition = clampedPosition * loadedAudio.getNumSamples();
    }
    else
    {
        // In normal mode, jump immediately
        playbackPosition = clampedPosition * loadedAudio.getNumSamples();
        playheadPosition.store(clampedPosition);
    }
}

void GrainfreezeAudioProcessor::setPlaying(bool shouldPlay)
{
    playing = shouldPlay;
}

//==============================================================================
// Editor
//==============================================================================

bool GrainfreezeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* GrainfreezeAudioProcessor::createEditor()
{
    return new GrainfreezeAudioProcessorEditor(*this);
}

//==============================================================================
// State Persistence
//==============================================================================

void GrainfreezeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save all parameter states
    auto state = juce::ValueTree("GrainfreezeState");

    state.setProperty("timeStretch", timeStretch->get(), nullptr);
    state.setProperty("hopSize", hopSizeParam->get(), nullptr);
    state.setProperty("fftSize", fftSizeParam->getIndex(), nullptr);
    state.setProperty("freezeMode", freezeModeParam->get(), nullptr);
    state.setProperty("glide", glideParam->get(), nullptr);
    state.setProperty("hfBoost", hfBoostParam->get(), nullptr);
    state.setProperty("microMovement", microMovementParam->get(), nullptr);
    state.setProperty("windowType", windowTypeParam->getIndex(), nullptr);
    state.setProperty("crossfadeLength", crossfadeLengthParam->get(), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void GrainfreezeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore all parameter states
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));

    if (state.isValid())
    {
        *timeStretch = state.getProperty("timeStretch", 1.0f);
        *hopSizeParam = state.getProperty("hopSize", 4.0f);
        fftSizeParam->operator=(state.getProperty("fftSize", 3));
        *freezeModeParam = state.getProperty("freezeMode", false);
        *glideParam = state.getProperty("glide", 100.0f);
        *hfBoostParam = state.getProperty("hfBoost", 10.0f);
        *microMovementParam = state.getProperty("microMovement", 20.0f);
        windowTypeParam->operator=(state.getProperty("windowType", 1));
        *crossfadeLengthParam = state.getProperty("crossfadeLength", 2.0f);
    }
}

//==============================================================================
// Plugin Factory
//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrainfreezeAudioProcessor();
}