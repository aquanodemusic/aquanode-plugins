#include "PluginProcessor.h"
#include "PluginEditor.h"

// Include Rubber Band Library
// You'll need to add RubberBandSingle.cpp to your project
// Download from: https://github.com/breakfastquay/rubberband
#ifdef USE_RUBBERBAND
    #include "rubberband/RubberBandStretcher.h"
    using namespace RubberBand;
#endif

AudioStretcherAudioProcessor::AudioStretcherAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    addParameter(pitchShiftParam = new juce::AudioParameterFloat(
        "pitchShift",
        "Pitch Shift",
        juce::NormalisableRange<float>(-36.0f, 36.0f, 0.01f),
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " st"; }
    ));

    addParameter(timeStretchParam = new juce::AudioParameterFloat(
        "timeStretch",
        "Time Stretch",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f),
        1.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + "x"; }
    ));

    addParameter(useNaiveMethodParam = new juce::AudioParameterBool(
        "useNaive",
        "Use Naive Method",
        false
    ));
}

AudioStretcherAudioProcessor::~AudioStretcherAudioProcessor()
{
}

const juce::String AudioStretcherAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioStretcherAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool AudioStretcherAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool AudioStretcherAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double AudioStretcherAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioStretcherAudioProcessor::getNumPrograms()
{
    return 1;
}

int AudioStretcherAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioStretcherAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String AudioStretcherAudioProcessor::getProgramName(int index)
{
    return {};
}

void AudioStretcherAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

void AudioStretcherAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
}

void AudioStretcherAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AudioStretcherAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void AudioStretcherAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Handle preview playback
    if (previewPlaying && previewBuffer.getNumSamples() > 0)
    {
        int numSamples = buffer.getNumSamples();
        int numChannels = juce::jmin(buffer.getNumChannels(), previewBuffer.getNumChannels());
        
        for (int i = 0; i < numSamples; ++i)
        {
            if (previewPlayPosition < previewBuffer.getNumSamples())
            {
                // Copy to all channels
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    buffer.setSample(ch, i, previewBuffer.getSample(ch, previewPlayPosition));
                }
                previewPlayPosition++; // Increment ONCE per sample, not per channel
            }
            else
            {
                // Reached end of preview
                previewPlaying = false;
                previewPlayPosition = 0;
                
                // Clear remaining samples
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    for (int j = i; j < numSamples; ++j)
                        buffer.setSample(ch, j, 0.0f);
                }
                break;
            }
        }
    }
}

bool AudioStretcherAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AudioStretcherAudioProcessor::createEditor()
{
    return new AudioStretcherAudioProcessorEditor(*this);
}

void AudioStretcherAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, true);

    stream.writeFloat(*pitchShiftParam);
    stream.writeFloat(*timeStretchParam);
    stream.writeBool(*useNaiveMethodParam);
}

void AudioStretcherAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t> (sizeInBytes), false);

    pitchShiftParam->setValueNotifyingHost(stream.readFloat());
    timeStretchParam->setValueNotifyingHost(stream.readFloat());
    
    if (!stream.isExhausted())
        useNaiveMethodParam->setValueNotifyingHost(stream.readBool() ? 1.0f : 0.0f);
}

void AudioStretcherAudioProcessor::loadAudioFile(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        loadedBuffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&loadedBuffer, 0, (int)reader->lengthInSamples, 0, true, true);
        loadedSampleRate = reader->sampleRate;
        loadedFileName = file.getFileName();
        loadedFile = file;
        
        // Initialize region to full file
        regionStart = 0;
        regionEnd = loadedBuffer.getNumSamples();
        
        // Clear any previous preview
        previewBuffer.setSize(0, 0);
        stopPreview();
    }
}

void AudioStretcherAudioProcessor::processAndExport(const juce::File& outputFile, ExportFormat format)
{
    if (loadedBuffer.getNumSamples() == 0)
        return;

    float pitch = *pitchShiftParam;
    float stretch = *timeStretchParam;
    bool useNaive = *useNaiveMethodParam;

    if (progressCallback)
        progressCallback(0.0f);
    if (stageCallback)
        stageCallback("Starting...");

    // Extract selected region with bounds checking
    int regionLength = regionEnd - regionStart;
    if (regionLength <= 0 || regionStart < 0 || regionEnd > loadedBuffer.getNumSamples())
        return;
    
    juce::AudioBuffer<float> regionBuffer(loadedBuffer.getNumChannels(), regionLength);
    
    for (int ch = 0; ch < loadedBuffer.getNumChannels(); ++ch)
    {
        regionBuffer.copyFrom(ch, 0, loadedBuffer, ch, regionStart, regionLength);
    }
    
    // Temporarily swap buffers
    juce::AudioBuffer<float> originalBuffer = std::move(loadedBuffer);
    loadedBuffer = std::move(regionBuffer);

    // Process audio
    juce::AudioBuffer<float> processedBuffer;
    
    if (useNaive)
    {
        processedBuffer = processWithNaiveMethod(pitch, stretch);
    }
    else
    {
#ifdef USE_RUBBERBAND
        processedBuffer = processWithRubberBand(pitch, stretch);
#else
        processedBuffer = processWithJUCENative(pitch, stretch);
#endif
    }
    
    // Restore original buffer
    loadedBuffer = std::move(originalBuffer);

    if (progressCallback)
        progressCallback(0.9f);
    if (stageCallback)
        stageCallback("Writing file...");

    // Write to file based on format
    if (format == ExportFormat::FLAC24)
        writeToFlac(processedBuffer, loadedSampleRate, outputFile);
    else if (format == ExportFormat::FLAC16)
        writeToFlac16(processedBuffer, loadedSampleRate, outputFile);

    if (progressCallback)
        progressCallback(1.0f);
    if (stageCallback)
        stageCallback("Complete!");
}

#ifdef USE_RUBBERBAND
juce::AudioBuffer<float> AudioStretcherAudioProcessor::processWithRubberBand(
    float pitchSemitones,
    float timeStretch)
{
    if (progressCallback)
        progressCallback(0.05f);
    if (stageCallback)
        stageCallback("Setup");
    
    using namespace RubberBand;
    
    int channels = loadedBuffer.getNumChannels();
    int inputSamples = loadedBuffer.getNumSamples();
    
    // Create Rubber Band stretcher with high quality settings
    RubberBandStretcher stretcher(
        loadedSampleRate,
        channels,
        RubberBandStretcher::OptionProcessOffline |
        RubberBandStretcher::OptionStretchPrecise |
        RubberBandStretcher::OptionPitchHighConsistency |
        RubberBandStretcher::OptionThreadingNever
    );
    
    // Set time and pitch ratios
    stretcher.setTimeRatio(timeStretch);
    stretcher.setPitchScale(std::pow(2.0, pitchSemitones / 12.0));
    
    if (progressCallback)
        progressCallback(0.15f);
    
    // Prepare input pointers
    const float** inputBuffers = new const float*[channels];
    for (int ch = 0; ch < channels; ++ch)
        inputBuffers[ch] = loadedBuffer.getReadPointer(ch);
    
    // Study phase
    if (progressCallback)
        progressCallback(0.25f);
    if (stageCallback)
        stageCallback("Study");
    stretcher.study(inputBuffers, inputSamples, true);
    
    // Process phase
    if (progressCallback)
        progressCallback(0.40f);
    if (stageCallback)
        stageCallback("Process");
    stretcher.process(inputBuffers, inputSamples, true);
    
    if (progressCallback)
        progressCallback(0.65f);
    if (stageCallback)
        stageCallback("Retrieve");
    
    // Retrieve output
    int outputSamples = stretcher.available();
    juce::AudioBuffer<float> outputBuffer(channels, outputSamples);
    
    float** outputBuffers = new float*[channels];
    for (int ch = 0; ch < channels; ++ch)
        outputBuffers[ch] = outputBuffer.getWritePointer(ch);
    
    stretcher.retrieve(outputBuffers, outputSamples);
    
    delete[] inputBuffers;
    delete[] outputBuffers;
    
    if (progressCallback)
        progressCallback(0.85f);
    
    return outputBuffer;
}
#endif


juce::AudioBuffer<float> AudioStretcherAudioProcessor::processWithNaiveMethod(
    float pitchSemitones,
    float timeStretch)
{
    // Original simple method from your first post
    if (progressCallback)
        progressCallback(0.05f);
    if (stageCallback)
        stageCallback("Time stretch");
    
    // Step 1: Time stretch using simple overlap-add (keeps pitch)
    juce::AudioBuffer<float> timeStretchedBuffer = performTimeStretchNaive(loadedBuffer, timeStretch);
    
    if (progressCallback)
        progressCallback(0.55f);
    if (stageCallback)
        stageCallback("Pitch shift");
    
    // Step 2: Pitch shift using resampling (without changing duration)
    auto pitchRatio = std::pow(2.0, pitchSemitones / 12.0);
    juce::AudioBuffer<float> finalBuffer = performPitchShift(timeStretchedBuffer, pitchRatio);
    
    if (progressCallback)
        progressCallback(0.85f);
    
    return finalBuffer;
}

juce::AudioBuffer<float> AudioStretcherAudioProcessor::performTimeStretchNaive(
    const juce::AudioBuffer<float>& input, float ratio)
{
    // Simple overlap-add time stretching (YOUR ORIGINAL METHOD)
    const int fftSize = 2048;
    const int hopSize = 512;
    const int outputHopSize = (int)(hopSize * ratio);
    
    int numChannels = input.getNumChannels();
    int inputLength = input.getNumSamples();
    int outputLength = (int)(inputLength * ratio);
    
    juce::AudioBuffer<float> output(numChannels, outputLength);
    output.clear();
    
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* inputData = input.getReadPointer(ch);
        float* outputData = output.getWritePointer(ch);
        
        juce::HeapBlock<float> windowedInput;
        windowedInput.allocate(fftSize, true);
        
        int inputPos = 0;
        int outputPos = 0;
        
        // Hanning window
        juce::HeapBlock<float> window;
        window.allocate(fftSize, true);
        for (int i = 0; i < fftSize; ++i)
            window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1)));
        
        while (inputPos < inputLength - fftSize)
        {
            // Copy and window input frame
            for (int i = 0; i < fftSize; ++i)
                windowedInput[i] = inputData[inputPos + i] * window[i];
            
            // Overlap-add to output
            if (outputPos < outputLength - fftSize)
            {
                for (int i = 0; i < fftSize; ++i)
                {
                    int outIdx = outputPos + i;
                    if (outIdx < outputLength)
                        outputData[outIdx] += windowedInput[i];
                }
            }
            
            inputPos += hopSize;
            outputPos += outputHopSize;
        }
    }
    
    // Normalize to prevent clipping
    float maxVal = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = output.getReadPointer(ch);
        for (int i = 0; i < outputLength; ++i)
            maxVal = juce::jmax(maxVal, std::abs(data[i]));
    }
    
    if (maxVal > 1.0f)
        output.applyGain(1.0f / maxVal);
    
    return output;
}

juce::AudioBuffer<float> AudioStretcherAudioProcessor::processWithJUCENative(
    float pitchSemitones,
    float timeStretch)
{
    // Step 1: Time stretch using phase vocoder (keeps pitch)
    if (progressCallback)
        progressCallback(0.05f);
    if (stageCallback)
        stageCallback("Time stretch (Phase Vocoder)");
    
    juce::AudioBuffer<float> timeStretchedBuffer = performTimeStretch(loadedBuffer, timeStretch);
    
    if (progressCallback)
        progressCallback(0.65f);
    if (stageCallback)
        stageCallback("Pitch shift");
    
    // Step 2: Pitch shift using resampling (without changing duration)
    auto pitchRatio = std::pow(2.0, pitchSemitones / 12.0);
    juce::AudioBuffer<float> finalBuffer = performPitchShift(timeStretchedBuffer, pitchRatio);
    
    if (progressCallback)
        progressCallback(0.85f);
    
    return finalBuffer;
}

juce::AudioBuffer<float> AudioStretcherAudioProcessor::performTimeStretch(
    const juce::AudioBuffer<float>& input, float ratio)
{
    const int fftSize = 4096;
    const int hopSize = fftSize / 4;  // 75% overlap
    const int outputHopSize = juce::roundToInt(hopSize * ratio);
    
    int numChannels = input.getNumChannels();
    int inputLength = input.getNumSamples();
    int outputLength = juce::roundToInt(inputLength * ratio);
    
    juce::AudioBuffer<float> output(numChannels, outputLength);
    output.clear();
    
    // Hanning window
    juce::HeapBlock<float> window;
    window.allocate(fftSize, true);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / fftSize));
    
    // FFT setup
    juce::dsp::FFT fft(juce::roundToInt(std::log2(fftSize)));
    const int fftDataSize = fftSize * 2;
    
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* inputData = input.getReadPointer(ch);
        float* outputData = output.getWritePointer(ch);
        
        // Phase vocoder buffers
        juce::HeapBlock<float> fftData;
        fftData.allocate(fftDataSize, true);
        
        juce::HeapBlock<float> lastPhase;
        lastPhase.allocate(fftSize / 2 + 1, true);
        
        juce::HeapBlock<float> sumPhase;
        sumPhase.allocate(fftSize / 2 + 1, true);
        
        int inputPos = 0;
        int outputPos = 0;
        int frameCount = 0;
        int totalFrames = (inputLength - fftSize) / hopSize;
        
        const float expectedPhaseDiff = 2.0f * juce::MathConstants<float>::pi * hopSize / fftSize;
        
        while (inputPos + fftSize <= inputLength)
        {
            // Report progress for time stretching (10% to 60% of total progress)
            if (progressCallback && frameCount % 10 == 0)
            {
                float frameProgress = (float)frameCount / juce::jmax(1, totalFrames);
                float overallProgress = 0.1f + (frameProgress * 0.5f * ((float)ch / numChannels + frameProgress / numChannels));
                progressCallback(overallProgress);
            }
            
            // Clear FFT data
            juce::FloatVectorOperations::clear(fftData.getData(), fftDataSize);
            
            // Copy and window input
            for (int i = 0; i < fftSize; ++i)
            {
                fftData[i * 2] = inputData[inputPos + i] * window[i];
            }
            
            // Forward FFT
            fft.performRealOnlyForwardTransform(fftData.getData());
            
            // Process phase vocoder
            for (int bin = 0; bin <= fftSize / 2; ++bin)
            {
                float real = fftData[bin * 2];
                float imag = fftData[bin * 2 + 1];
                
                // Calculate magnitude and phase
                float magnitude = std::sqrt(real * real + imag * imag);
                float phase = std::atan2(imag, real);
                
                // Calculate phase difference
                float phaseDiff = phase - lastPhase[bin];
                lastPhase[bin] = phase;
                
                // Subtract expected phase difference
                phaseDiff -= bin * expectedPhaseDiff;
                
                // Wrap to -PI to PI range
                while (phaseDiff > juce::MathConstants<float>::pi)
                    phaseDiff -= 2.0f * juce::MathConstants<float>::pi;
                while (phaseDiff < -juce::MathConstants<float>::pi)
                    phaseDiff += 2.0f * juce::MathConstants<float>::pi;
                
                // Calculate true frequency
                float trueFreq = 2.0f * juce::MathConstants<float>::pi * bin / fftSize + phaseDiff / hopSize;
                
                // Accumulate phase for synthesis
                sumPhase[bin] += trueFreq * outputHopSize;
                
                // Convert back to rectangular
                fftData[bin * 2] = magnitude * std::cos(sumPhase[bin]);
                fftData[bin * 2 + 1] = magnitude * std::sin(sumPhase[bin]);
            }
            
            // Inverse FFT
            fft.performRealOnlyInverseTransform(fftData.getData());
            
            // Overlap-add with window
            for (int i = 0; i < fftSize; ++i)
            {
                int outIdx = outputPos + i;
                if (outIdx >= 0 && outIdx < outputLength)
                {
                    // Scale by window and normalize by FFT size
                    outputData[outIdx] += fftData[i * 2] * window[i] * 2.0f / fftSize;
                }
            }
            
            inputPos += hopSize;
            outputPos += outputHopSize;
            frameCount++;
        }
    }
    
    // Apply gain compensation for overlap
    float overlapFactor = (float)fftSize / (float)hopSize;
    float compensation = 2.0f / overlapFactor;
    output.applyGain(compensation);
    
    // Normalize to prevent clipping
    float maxVal = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = output.getReadPointer(ch);
        for (int i = 0; i < outputLength; ++i)
            maxVal = juce::jmax(maxVal, std::abs(data[i]));
    }
    
    if (maxVal > 1.0f)
        output.applyGain(0.95f / maxVal);
    
    return output;
}

juce::AudioBuffer<float> AudioStretcherAudioProcessor::performPitchShift(
    const juce::AudioBuffer<float>& input, double pitchRatio)
{
    if (std::abs(pitchRatio - 1.0) < 0.001)
        return input;
    
    int numChannels = input.getNumChannels();
    int inputLength = input.getNumSamples();
    int outputLength = inputLength; // Keep same duration
    
    juce::AudioBuffer<float> output(numChannels, outputLength);
    output.clear();
    
    // Use higher quality resampling
    // First resample to change pitch, then resample back to original duration
    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::CatmullRomInterpolator interpolator;
        
        // Step 1: Resample at pitch ratio (this changes both pitch and duration)
        int tempLength = juce::roundToInt(inputLength / pitchRatio);
        juce::AudioBuffer<float> tempBuffer(1, tempLength + 10); // Add padding
        tempBuffer.clear();
        
        interpolator.reset();
        interpolator.process(
            pitchRatio,
            input.getReadPointer(ch),
            tempBuffer.getWritePointer(0),
            tempLength
        );
        
        // Step 2: Resample back to original duration (fixes duration, keeps pitch)
        interpolator.reset();
        double stretchRatio = (double)tempLength / outputLength;
        interpolator.process(
            stretchRatio,
            tempBuffer.getReadPointer(0),
            output.getWritePointer(ch),
            outputLength
        );
    }
    
    return output;
}

bool AudioStretcherAudioProcessor::writeToFlac(const juce::AudioBuffer<float>& buffer,
    double outputSampleRate,
    const juce::File& outputFile)
{
    outputFile.deleteFile();

    juce::FlacAudioFormat flacFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    juce::FileOutputStream* outputStream = new juce::FileOutputStream(outputFile);

    if (outputStream->openedOk())
    {
        // Set FLAC compression level to 8 (highest lossless compression) - 24-bit
        juce::StringPairArray metadataValues;
        metadataValues.set("quality", "8"); // FLAC compression level 0-8
        
        writer.reset(flacFormat.createWriterFor(
            outputStream,
            outputSampleRate,
            buffer.getNumChannels(),
            24, // 24-bit depth
            metadataValues, // Use metadata for quality setting
            0
        ));

        if (writer != nullptr)
        {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            writer->flush();
            return true;
        }
    }

    delete outputStream;
    return false;
}

bool AudioStretcherAudioProcessor::writeToFlac16(const juce::AudioBuffer<float>& buffer,
    double outputSampleRate,
    const juce::File& outputFile)
{
    outputFile.deleteFile();

    juce::FlacAudioFormat flacFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    juce::FileOutputStream* outputStream = new juce::FileOutputStream(outputFile);

    if (outputStream->openedOk())
    {
        // Set FLAC compression level to 8 (highest lossless compression) - 16-bit
        juce::StringPairArray metadataValues;
        metadataValues.set("quality", "8"); // FLAC compression level 0-8
        
        writer.reset(flacFormat.createWriterFor(
            outputStream,
            outputSampleRate,
            buffer.getNumChannels(),
            16, // 16-bit depth
            metadataValues, // Use metadata for quality setting
            0
        ));

        if (writer != nullptr)
        {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            writer->flush();
            return true;
        }
    }

    delete outputStream;
    return false;
}

void AudioStretcherAudioProcessor::setRegion(int startSample, int endSample)
{
    regionStart = juce::jlimit(0, loadedBuffer.getNumSamples(), startSample);
    regionEnd = juce::jlimit(regionStart, loadedBuffer.getNumSamples(), endSample);
    
    // Ensure we have at least 1 sample
    if (regionEnd <= regionStart)
        regionEnd = juce::jmin(regionStart + 1, loadedBuffer.getNumSamples());
    
    // Clear preview when region changes
    previewBuffer.setSize(0, 0);
    stopPreview();
}

void AudioStretcherAudioProcessor::renderPreview()
{
    if (loadedBuffer.getNumSamples() == 0 || regionStart >= regionEnd)
        return;
    
    // Note: Preview is rendered in memory at full quality (32-bit float)
    // When exporting with FLAC16, it will be dithered down to 16-bit
    
    if (progressCallback)
        progressCallback(0.0f);
    if (stageCallback)
        stageCallback("PRERENDER");
    
    stopPreview();
    
    float pitch = *pitchShiftParam;
    float stretch = *timeStretchParam;
    bool useNaive = *useNaiveMethodParam;
    
    // Extract region with bounds checking
    int regionLength = regionEnd - regionStart;
    if (regionLength <= 0 || regionStart < 0 || regionEnd > loadedBuffer.getNumSamples())
        return;
    
    juce::AudioBuffer<float> regionBuffer(loadedBuffer.getNumChannels(), regionLength);
    
    for (int ch = 0; ch < loadedBuffer.getNumChannels(); ++ch)
    {
        regionBuffer.copyFrom(ch, 0, loadedBuffer, ch, regionStart, regionLength);
    }
    
    // Temporarily swap buffers
    juce::AudioBuffer<float> originalBuffer = std::move(loadedBuffer);
    loadedBuffer = std::move(regionBuffer);
    
    // Process the region
    if (useNaive)
    {
        previewBuffer = processWithNaiveMethod(pitch, stretch);
    }
    else
    {
#ifdef USE_RUBBERBAND
        previewBuffer = processWithRubberBand(pitch, stretch);
#else
        previewBuffer = processWithJUCENative(pitch, stretch);
#endif
    }
    
    // Restore original buffer
    loadedBuffer = std::move(originalBuffer);
    
    if (progressCallback)
        progressCallback(1.0f);
    if (stageCallback)
        stageCallback("Ready to play");
}

void AudioStretcherAudioProcessor::startPreview()
{
    if (previewBuffer.getNumSamples() == 0)
        return;
    
    stopPreview();
    previewPlayPosition = 0;
    previewPlaying = true;
}

void AudioStretcherAudioProcessor::stopPreview()
{
    previewPlaying = false;
    previewPlayPosition = 0;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioStretcherAudioProcessor();
}
