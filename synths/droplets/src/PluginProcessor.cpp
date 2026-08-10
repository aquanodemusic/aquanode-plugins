#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
/** Sound definition for the Droplets Synth */
struct DropletsSound : public juce::SynthesiserSound
{
    DropletsSound() {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

//==============================================================================
/** Individual water droplet event with simple fade-in/fade-out envelope */
struct Droplet
{
    float frequency = 0.0f;
    float amplitude = 0.0f;
    float decay = 0.0f;
    float phase = 0.0f;
    float pitchRise = 0.0f;
    float pitchRiseRate = 0.0f;
    float currentFreq = 0.0f;
    float pan = 0.5f;
    bool active = false;
    int sampleCount = 0;
    
    // Simple fade-in/fade-out envelope (no sustain needed for short bubbles)
    float fadeInSamples = 0.0f;
    float fadeOutSamples = 0.0f;
    float totalLifetimeSamples = 0.0f;
    float envelopeLevel = 0.0f;
};

//==============================================================================
/** Voice that generates water droplet sounds using physics-based synthesis */
struct DropletsVoice : public juce::SynthesiserVoice
{
    DropletsVoice(DropletsAudioProcessor* p)
        : processor(p), parameters(p->apvts)
    {
        modEnvelope.setSampleRate(44100.0);
        volumeEnvelope.setSampleRate(44100.0);
    }

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<DropletsSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
    {
        level = velocity;
        noteActive = true;
        clockPhase = 0.0f;
        
        // Setup modulation envelope
        juce::ADSR::Parameters envParams;
        envParams.attack = *parameters.getRawParameterValue("modAttack");
        envParams.decay = *parameters.getRawParameterValue("modDecay");
        envParams.sustain = *parameters.getRawParameterValue("modSustain");
        envParams.release = *parameters.getRawParameterValue("modRelease");
        modEnvelope.setParameters(envParams);
        modEnvelope.noteOn();
        
        // Setup volume envelope
        juce::ADSR::Parameters volParams;
        volParams.attack = *parameters.getRawParameterValue("volAttack");
        volParams.decay = *parameters.getRawParameterValue("volDecay");
        volParams.sustain = *parameters.getRawParameterValue("volSustain");
        volParams.release = *parameters.getRawParameterValue("volRelease");
        volumeEnvelope.setParameters(volParams);
        volumeEnvelope.noteOn();
        
        // Clear all droplets
        for (auto& droplet : droplets)
            droplet.active = false;
    }

    void stopNote(float, bool allowTailOff) override
    {
        noteActive = false;
        modEnvelope.noteOff();
        volumeEnvelope.noteOff();
        
        if (!allowTailOff)
        {
            for (auto& droplet : droplets)
                droplet.active = false;
            clearCurrentNote();
        }
    }

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        if (!noteActive && !hasActiveDroplets() && !volumeEnvelope.isActive())
        {
            clearCurrentNote();
            return;
        }

        // Fetch Parameters
        const float radius = *parameters.getRawParameterValue("radius");
        const float radiusBW = *parameters.getRawParameterValue("radiusBW");
        const float depth = *parameters.getRawParameterValue("depth");
        const float depthBW = *parameters.getRawParameterValue("depthBW");
        const float pitchRise = *parameters.getRawParameterValue("pitchRise");
        const float rate = *parameters.getRawParameterValue("rate");
        const float rateBW = *parameters.getRawParameterValue("rateBW");
        const float brightness = *parameters.getRawParameterValue("brightness");
        const float width = *parameters.getRawParameterValue("width");
        const float field = *parameters.getRawParameterValue("field");
        const int maxDroplets = (int)*parameters.getRawParameterValue("maxDroplets");
        const float masterVol = *parameters.getRawParameterValue("masterVol");
        const bool secondaryEvent = *parameters.getRawParameterValue("secondaryEvent") > 0.5f;
        const float modAmount = *parameters.getRawParameterValue("modAmount");
        const float secondaryProb = *parameters.getRawParameterValue("secondaryProb");
        const float secondaryDelayMax = *parameters.getRawParameterValue("secondaryDelay");
        const float ampScale = *parameters.getRawParameterValue("ampScale");
        const float phaseOffset = *parameters.getRawParameterValue("phaseOffset");
        
        // Get per-droplet fade parameters
        const float fadeIn = *parameters.getRawParameterValue("dropletAttack");
        const float fadeOut = *parameters.getRawParameterValue("dropletRelease");
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Get modulation envelope value
            float modEnv = modEnvelope.getNextSample();
            float modulation = modEnv * modAmount;
            
            // Get volume envelope value
            float volEnv = volumeEnvelope.getNextSample();
            
            // Apply modulation to rate (faster drips with modulation)
            float modulatedRate = rate * (1.0f - modulation * 0.9f); // Up to 90% faster
            // Add a subtle minimum to prevent extreme rapid-fire at very low settings
            modulatedRate = juce::jmax(0.004f, modulatedRate); // Minimum 4ms between droplets
            
            // Clock for generating new droplets (continue while volume envelope is active)
            if (noteActive || volumeEnvelope.isActive())
            {
                float rateHz = 1.0f / modulatedRate;
                float clockIncrement = rateHz / (float)voiceSampleRate;
                
                // Apply rate bandwidth (gaussian variation)
                float rateVariation = gaussianRandom() * rateBW * 0.5f;
                clockIncrement *= (1.0f + rateVariation);
                
                clockPhase += clockIncrement;
                
                if (clockPhase >= 1.0f)
                {
                    clockPhase -= 1.0f;
                    triggerDroplet(radius, radiusBW, depth, depthBW, pitchRise, 
                                   brightness, width, field, maxDroplets, ampScale, phaseOffset,
                                   fadeIn, fadeOut);
                    
                    // Secondary droplet event (occasional double-drip)
                    if (secondaryEvent && random.nextFloat() < secondaryProb)
                    {
                        float secondaryDelay = random.nextFloat() * secondaryDelayMax;
                        secondaryEventTimer = (int)(secondaryDelay * voiceSampleRate);
                    }
                }
            }
            
            // Trigger secondary droplet if timer expired
            if (secondaryEventTimer > 0)
            {
                secondaryEventTimer--;
                if (secondaryEventTimer == 0)
                {
                    triggerDroplet(radius, radiusBW, depth, depthBW, pitchRise,
                                   brightness, width, field, maxDroplets, ampScale, phaseOffset,
                                   fadeIn, fadeOut);
                }
            }

            float outL = 0.0f;
            float outR = 0.0f;

            // Render all active droplets
            for (auto& droplet : droplets)
            {
                if (!droplet.active)
                    continue;

                // Calculate simple fade-in/fade-out envelope
                float envLevel = 1.0f;
                
                if (droplet.sampleCount < droplet.fadeInSamples)
                {
                    // Fade in (linear or smoothstep)
                    float t = droplet.sampleCount / droplet.fadeInSamples;
                    // Smoothstep for nicer curve: 3t^2 - 2t^3
                    envLevel = t * t * (3.0f - 2.0f * t);
                }
                else if (droplet.sampleCount > (droplet.totalLifetimeSamples - droplet.fadeOutSamples))
                {
                    // Fade out (linear or smoothstep)
                    float fadeOutProgress = (droplet.totalLifetimeSamples - droplet.sampleCount) / droplet.fadeOutSamples;
                    fadeOutProgress = juce::jlimit(0.0f, 1.0f, fadeOutProgress);
                    // Smoothstep
                    envLevel = fadeOutProgress * fadeOutProgress * (3.0f - 2.0f * fadeOutProgress);
                }
                
                droplet.envelopeLevel = envLevel;

                // Apply pitch rise (pitch modulation over time)
                if (droplet.pitchRiseRate > 0.0f)
                {
                    droplet.currentFreq += droplet.pitchRiseRate;
                    droplet.currentFreq = juce::jmin(droplet.currentFreq, (float)voiceSampleRate * 0.5f);
                }

                // Generate sine wave with envelope
                float sampleValue = droplet.amplitude * std::sin(droplet.phase) * droplet.envelopeLevel;
                
                // Apply exponential decay (for natural physics decay)
                droplet.amplitude *= droplet.decay;
                
                // Advance phase
                droplet.phase += 2.0f * juce::MathConstants<float>::pi * droplet.currentFreq / (float)voiceSampleRate;
                
                // Wrap phase
                while (droplet.phase >= 2.0f * juce::MathConstants<float>::pi)
                    droplet.phase -= 2.0f * juce::MathConstants<float>::pi;
                
                droplet.sampleCount++;
                
                // Apply stereo panning
                float panL = std::cos(droplet.pan * juce::MathConstants<float>::halfPi);
                float panR = std::sin(droplet.pan * juce::MathConstants<float>::halfPi);
                
                outL += sampleValue * panL;
                outR += sampleValue * panR;
                
                // Deactivate if we've exceeded lifetime or amplitude is negligible
                if (droplet.sampleCount >= droplet.totalLifetimeSamples || droplet.amplitude < 0.0001f)
                {
                    droplet.active = false;
                }
            }

            // Apply volume envelope to output
            float finalOutL = outL * level * masterVol * volEnv;
            float finalOutR = outR * level * masterVol * volEnv;
            
            // Apply DC blocker (simple 1-pole high-pass at ~5Hz)
            // y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
            const float dcCoeff = 0.995f;
            float filteredL = finalOutL - dcBlockerX1L + dcCoeff * dcBlockerY1L;
            float filteredR = finalOutR - dcBlockerX1R + dcCoeff * dcBlockerY1R;
            
            dcBlockerX1L = finalOutL;
            dcBlockerX1R = finalOutR;
            dcBlockerY1L = filteredL;
            dcBlockerY1R = filteredR;

            if (buffer.getNumChannels() > 0)
                buffer.addSample(0, startSample + sample, filteredL);
            if (buffer.getNumChannels() > 1)
                buffer.addSample(1, startSample + sample, filteredR);
        }
    }

    void setCurrentPlaybackSampleRate(double newRate) override
    {
        if (newRate > 0.0)
        {
            voiceSampleRate = newRate;
            modEnvelope.setSampleRate(newRate);
            volumeEnvelope.setSampleRate(newRate);
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

private:
    void triggerDroplet(float radius, float radiusBW, float depth, float depthBW,
                        float pitchRise, float brightness, float width, float field,
                        int maxDroplets, float ampScale, float phaseOffset,
                        float fadeInTime, float fadeOutTime)
    {
        // Find inactive droplet slot
        Droplet* droplet = nullptr;
        int activeCount = 0;
        
        for (auto& d : droplets)
        {
            if (!d.active)
            {
                if (droplet == nullptr)
                    droplet = &d;
            }
            else
            {
                activeCount++;
            }
        }
        
        // Don't exceed max droplets (CPU limiting)
        if (activeCount >= maxDroplets || droplet == nullptr)
            return;

        // Calculate radius with gaussian bandwidth variation
        float actualRadius = radius * std::pow(10.0f, gaussianRandom() * radiusBW * 0.5f);
        actualRadius = juce::jlimit(0.00015f, 0.02f, actualRadius); // 0.15mm to 2cm
        
        // Frequency = 3 / radius (meters)
        float baseFreq = 3.0f / actualRadius;
        baseFreq = juce::jlimit(20.0f, 20000.0f, baseFreq);
        
        // Apply depth modulation (pitch rise coefficient)
        // Center position = sqrt(2), max = 2*sqrt(2)
        float depthCoeff = 1.0f + depth * std::sqrt(2.0f);
        depthCoeff *= (1.0f + gaussianRandom() * depthBW * 0.3f);
        
        droplet->frequency = baseFreq;
        droplet->currentFreq = baseFreq;
        
        // Amplitude = radius * sqrt(radius) - physical model
        float baseAmplitude = actualRadius * std::sqrt(actualRadius);
        
        // Apply brightness (distance perception)
        baseAmplitude *= std::pow(10.0f, (brightness - 0.5f) * 2.0f);
        
        // Apply field variation (distance randomization)
        if (field > 0.01f)
        {
            float fieldVariation = random.nextFloat() * field;
            baseAmplitude *= (1.0f - fieldVariation);
        }
        
        droplet->amplitude = baseAmplitude * ampScale; // Scale for audibility (user adjustable)
        
        // Decay rate based on radius - MUCH faster for realistic "ping" sound
        // Smaller bubbles decay very quickly (few milliseconds)
        // Larger bubbles can sustain slightly longer but still brief
        float decayTimeSeconds = 0.001f + (actualRadius * 2.0f); // 1ms to ~40ms
        droplet->decay = std::exp(-1.0f / (decayTimeSeconds * (float)voiceSampleRate));
        
        // Pitch rise rate - should be VERY fast (complete within the droplet duration)
        if (pitchRise > 0.01f)
        {
            // Calculate how fast frequency should rise
            float targetFreq = baseFreq * depthCoeff;
            // Rise should complete in just a few milliseconds
            float riseDuration = decayTimeSeconds * (1.5f - pitchRise); // Faster rise with higher pitchRise
            riseDuration = juce::jmax(0.0005f, riseDuration);
            droplet->pitchRiseRate = (targetFreq - baseFreq) / (riseDuration * (float)voiceSampleRate);
        }
        else
        {
            droplet->pitchRiseRate = 0.0f;
        }
        
        // Stereo positioning
        if (width < 0.01f)
        {
            droplet->pan = 0.5f; // Mono
        }
        else
        {
            float panRange = juce::jmin(1.0f, width);
            droplet->pan = 0.5f + (random.nextFloat() - 0.5f) * panRange;
            droplet->pan = juce::jlimit(0.0f, 1.0f, droplet->pan);
        }
        
        // Phase offset: 1.0 = random, 0.0-0.999 = specific offset (0-360 degrees)
        if (phaseOffset >= 0.999f)
        {
            droplet->phase = random.nextFloat() * 2.0f * juce::MathConstants<float>::pi; // Random phase
        }
        else
        {
            droplet->phase = phaseOffset * 2.0f * juce::MathConstants<float>::pi; // Specific offset
        }
        
        // Setup fade-in/fade-out envelope
        droplet->fadeInSamples = fadeInTime * (float)voiceSampleRate;
        droplet->fadeOutSamples = fadeOutTime * (float)voiceSampleRate;
        
        // Total lifetime is based on the natural decay time plus some extra for fade out
        // This ensures the envelope doesn't cut off too early
        float naturalLifetime = decayTimeSeconds * 5.0f; // 5x the decay time constant
        droplet->totalLifetimeSamples = (naturalLifetime + fadeOutTime) * (float)voiceSampleRate;
        
        // Make sure fade out doesn't exceed total lifetime
        if (droplet->fadeOutSamples > droplet->totalLifetimeSamples * 0.5f)
        {
            droplet->fadeOutSamples = droplet->totalLifetimeSamples * 0.5f;
        }
        
        droplet->sampleCount = 0;
        droplet->active = true;
    }

    bool hasActiveDroplets() const
    {
        for (const auto& d : droplets)
        {
            if (d.active)
                return true;
        }
        return false;
    }

    // Gaussian random number generator (Box-Muller transform)
    float gaussianRandom()
    {
        float u1 = random.nextFloat();
        float u2 = random.nextFloat();
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * juce::MathConstants<float>::pi * u2);
    }

    std::array<Droplet, 128> droplets;
    float level = 0.0f;
    double voiceSampleRate = 44100.0;
    bool noteActive = false;
    float clockPhase = 0.0f;
    int secondaryEventTimer = 0;
    
    // DC blocker filter state (simple high-pass)
    float dcBlockerX1L = 0.0f, dcBlockerX1R = 0.0f;
    float dcBlockerY1L = 0.0f, dcBlockerY1R = 0.0f;
    
    juce::ADSR modEnvelope;
    juce::ADSR volumeEnvelope;  // Global volume ADSR
    juce::Random random;
    DropletsAudioProcessor* processor;
    juce::AudioProcessorValueTreeState& parameters;
};

//==============================================================================
DropletsAudioProcessor::DropletsAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Add voices to the synthesiser
    for (int i = 0; i < 8; ++i)
        synth.addVoice(new DropletsVoice(this));

    synth.addSound(new DropletsSound());
}

DropletsAudioProcessor::~DropletsAudioProcessor()
{
}

void DropletsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void DropletsAudioProcessor::releaseResources()
{
}

bool DropletsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void DropletsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DropletsAudioProcessor::createEditor()
{
    return new DropletsAudioProcessorEditor(*this);
}

bool DropletsAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String DropletsAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DropletsAudioProcessor::acceptsMidi() const
{
    return true;
}

bool DropletsAudioProcessor::producesMidi() const
{
    return false;
}

bool DropletsAudioProcessor::isMidiEffect() const
{
    return false;
}

double DropletsAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DropletsAudioProcessor::getNumPrograms()
{
    return 1;
}

int DropletsAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DropletsAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String DropletsAudioProcessor::getProgramName(int index)
{
    return {};
}

void DropletsAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

void DropletsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DropletsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout DropletsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // TOP ROW - Physical Parameters
    
    // Radius (0.15mm to 2cm) - controls frequency
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "radius", "Radius",
        juce::NormalisableRange<float>(0.00015f, 0.02f, 0.0f, 0.3f),
        0.005f));
    
    // Radius Bandwidth (0.1 to 10 range variation)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "radiusBW", "Radius BW",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.3f));
    
    // Depth (pitch modulation depth)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "depth", "Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.5f));
    
    // Depth Bandwidth
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "depthBW", "Depth BW",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.2f));
    
    // Pitch Rise Rate
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchRise", "Pitch Rise",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.5f));
    
    // Rate (2 samples to 2 seconds between events)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rate", "Rate",
        juce::NormalisableRange<float>(0.002f, 2.0f, 0.0f, 0.5f),
        0.1f));
    
    // Rate Bandwidth
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rateBW", "Rate BW",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.3f));
    
    // Brightness (amplitude/distance)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "brightness", "Bright",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.5f));
    
    // Width (stereo width)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 1.5f, 0.0f),
        0.5f));
    
    // Field (amplitude variation for distance perception)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "field", "Field",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.5f));
    
    // Secondary Event (occasional double-drip)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "secondaryEvent", "Secondary", true));
    
    // ADSR Modulation Envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modAttack", "Mod Attack",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.0f, 0.5f),
        0.01f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modDecay", "Mod Decay",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.0f, 0.5f),
        0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modSustain", "Mod Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.7f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modRelease", "Mod Release",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.0f, 0.5f),
        1.0f));
    
    // Modulation Amount (global mod depth)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modAmount", "Mod Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.0f));
    
    // Max Droplets (CPU control)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "maxDroplets", "CPU",
        1, 128, 32));
    
    // Master Volume
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "masterVol", "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.5f));
    
    // Global Volume ADSR
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volAttack", "Vol Attack",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.0f, 0.5f),
        0.01f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volDecay", "Vol Decay",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.0f, 0.5f),
        0.2f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volSustain", "Vol Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        1.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volRelease", "Vol Release",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.0f, 0.5f),
        1.0f));
    
    // Secondary Event Probability (0-100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "secondaryProb", "Secondary Prob",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.3f));
    
    // Secondary Event Delay Max (0-200ms)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "secondaryDelay", "Secondary Delay",
        juce::NormalisableRange<float>(0.0f, 0.2f, 0.0f),
        0.08f));
    
    // Amplitude Scale (gain makeup)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampScale", "Amp Scale",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.0f, 0.3f),
        200.0f));
    
    // Phase Offset (0-360 degrees, or random)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaseOffset", "Phase Offset",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        1.0f)); // 1.0 = random, 0.0-0.999 = specific offset
    
    // PER-DROPLET FADE-IN/FADE-OUT - Simplified from ADSR
    // Now only uses Attack (fade-in) and Release (fade-out)
    // Decay and Sustain are removed since each droplet has natural decay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dropletAttack", "Droplet Fade In",
        juce::NormalisableRange<float>(0.0001f, 0.05f, 0.0f, 0.5f),
        0.001f)); // 0.1ms to 50ms, default 1ms
    
    // Decay parameter removed - not needed
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dropletDecay", "Droplet Decay",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.0f, 0.5f),
        0.05f)); // Keep for backward compatibility but not used
    
    // Sustain parameter removed - not needed
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dropletSustain", "Droplet Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        1.0f)); // Keep for backward compatibility but not used
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dropletRelease", "Droplet Fade Out",
        juce::NormalisableRange<float>(0.0001f, 0.5f, 0.0f, 0.5f),
        0.01f)); // 0.1ms to 500ms, default 10ms

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DropletsAudioProcessor();
}
