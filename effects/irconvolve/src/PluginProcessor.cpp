#include "PluginProcessor.h"
#include "PluginEditor.h"

IRConvolverAudioProcessor::IRConvolverAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Add parameters
    addParameter (mixParam = new juce::AudioParameterFloat (
        "mix", "Mix", 0.0f, 1.0f, 1.0f));
    
    addParameter (gainParam = new juce::AudioParameterFloat (
        "gain", "Gain", juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f));
}

IRConvolverAudioProcessor::~IRConvolverAudioProcessor()
{
}

const juce::String IRConvolverAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool IRConvolverAudioProcessor::acceptsMidi() const
{
    return false;
}

bool IRConvolverAudioProcessor::producesMidi() const
{
    return false;
}

bool IRConvolverAudioProcessor::isMidiEffect() const
{
    return false;
}

double IRConvolverAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int IRConvolverAudioProcessor::getNumPrograms()
{
    return 1;
}

int IRConvolverAudioProcessor::getCurrentProgram()
{
    return 0;
}

void IRConvolverAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String IRConvolverAudioProcessor::getProgramName (int index)
{
    return {};
}

void IRConvolverAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void IRConvolverAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    
    convolution.prepare (spec);
    
    dryBuffer.setSize (spec.numChannels, samplesPerBlock);
}

void IRConvolverAudioProcessor::releaseResources()
{
}

bool IRConvolverAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void IRConvolverAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (!irLoaded.load())
        return;

    // Store dry signal
    dryBuffer.makeCopyOf (buffer);

    // Process convolution
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    convolution.process (context);

    // Apply gain
    float gainValue = juce::Decibels::decibelsToGain (gainParam->get());
    buffer.applyGain (gainValue);

    // Mix dry and wet
    float mixValue = mixParam->get();
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* wetData = buffer.getWritePointer (channel);
        auto* dryData = dryBuffer.getReadPointer (channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            wetData[sample] = dryData[sample] * (1.0f - mixValue) + wetData[sample] * mixValue;
        }
    }
}

bool IRConvolverAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* IRConvolverAudioProcessor::createEditor()
{
    return new IRConvolverAudioProcessorEditor (*this);
}

void IRConvolverAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, true);
    
    stream.writeFloat (*mixParam);
    stream.writeFloat (*gainParam);
    
    juce::String irName = getCurrentIRName();
    stream.writeString (irName);
}

void IRConvolverAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, static_cast<size_t> (sizeInBytes), false);
    
    mixParam->setValueNotifyingHost (stream.readFloat());
    gainParam->setValueNotifyingHost (stream.readFloat());
    
    juce::String irPath = stream.readString();
    if (irPath.isNotEmpty())
    {
        juce::File irFile (irPath);
        if (irFile.existsAsFile())
            loadImpulseResponse (irFile);
    }
}

juce::String IRConvolverAudioProcessor::getCurrentIRName() const
{
    const juce::ScopedLock lock (irNameLock);
    return currentIRName;
}

void IRConvolverAudioProcessor::copyIRBufferTo(juce::AudioBuffer<float>& destination)
{
    const juce::ScopedLock lock (irBufferLock);
    
    if (irBuffer.getNumChannels() > 0 && irBuffer.getNumSamples() > 0)
    {
        destination.setSize(irBuffer.getNumChannels(), irBuffer.getNumSamples(), false, false, false);
        
        for (int channel = 0; channel < irBuffer.getNumChannels(); ++channel)
        {
            destination.copyFrom(channel, 0, irBuffer, channel, 0, irBuffer.getNumSamples());
        }
    }
    else
    {
        // Return empty buffer
        destination.setSize(1, 1);
        destination.clear();
    }
}

void IRConvolverAudioProcessor::loadImpulseResponse (const juce::File& file)
{
    if (!file.existsAsFile())
    {
        DBG("IR file does not exist: " + file.getFullPathName());
        return;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    
    if (reader == nullptr)
    {
        DBG("Failed to create reader for: " + file.getFullPathName());
        return;
    }
    
    // Validate reader data
    if (reader->numChannels <= 0 || reader->numChannels > 8)
    {
        DBG("Invalid channel count: " + juce::String(reader->numChannels));
        return;
    }
    
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > 50000000)
    {
        DBG("Invalid sample count: " + juce::String(reader->lengthInSamples));
        return;
    }
    
    // Load the audio data
    juce::AudioBuffer<float> loadedBuffer (static_cast<int>(reader->numChannels), 
                                          static_cast<int>(reader->lengthInSamples));
    
    if (!reader->read (&loadedBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true))
    {
        DBG("Failed to read audio data from: " + file.getFullPathName());
        return;
    }
    
    // Store a copy for visualization (thread-safe)
    {
        const juce::ScopedLock lock (irBufferLock);
        irBuffer.setSize(loadedBuffer.getNumChannels(), loadedBuffer.getNumSamples(), false, false, false);
        
        for (int channel = 0; channel < loadedBuffer.getNumChannels(); ++channel)
        {
            irBuffer.copyFrom(channel, 0, loadedBuffer, channel, 0, loadedBuffer.getNumSamples());
        }
    }
    
    // Load into convolution engine
    convolution.loadImpulseResponse (std::move (loadedBuffer),
                                    reader->sampleRate,
                                    juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::yes,
                                    juce::dsp::Convolution::Normalise::yes);
    
    // Update state (thread-safe)
    {
        const juce::ScopedLock lock (irNameLock);
        currentIRName = file.getFullPathName();
    }
    
    irLoaded.store(true);
    
    DBG ("Successfully loaded IR: " + file.getFileName());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IRConvolverAudioProcessor();
}
