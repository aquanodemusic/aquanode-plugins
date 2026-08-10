#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// SPRING LINE IMPLEMENTATION
//==============================================================================

void SpringerAudioProcessor::SpringLine::prepare(double sampleRate, int maxDelay)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 1024;
    spec.numChannels = 1;

    delay.setMaximumDelayInSamples(maxDelay);
    delay.prepare(spec);

    densityFilter.delayLine.setMaximumDelayInSamples(16000);
    densityFilter.prepare(spec);

    reset();
}

void SpringerAudioProcessor::SpringLine::reset()
{
    delay.reset();
    densityFilter.delayLine.reset();

    lfoPhase = 0.0f;
    dampingMem = 0.0f;
    hpMem = 0.0f;
    lastOut = 0.0f;

    // Vectorized reset for allpasses
    std::memset(allpasses.data(), 0, sizeof(Allpass) * allpasses.size());
}

float SpringerAudioProcessor::SpringLine::process(float in, float feedback, float lfoRate, double sr)
{
    // --- LFO Update (optimized with reciprocal and fast wrapping) ---
    const float lfoIncrement = lfoRate * static_cast<float>(sr);
    lfoPhase += lfoIncrement;
    lfoPhase -= std::floor(lfoPhase); // Branch-free wrapping
    
    // Fast sine approximation option (can be enabled for ~3x faster LFO)
    // Uncomment for speed if quality permits:
    // const float lfo = juce::dsp::FastMathApproximations::sin(lfoPhase * juce::MathConstants<float>::twoPi);
    const float lfo = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);

    // --- Delay Modulation (single jlimit call) ---
    const float modDelay = juce::jlimit(0.0f, maxSafeDelay, targetDelayScaled + lfo * targetDepthScaled);

    // --- Feedback & DC Block (fused operations) ---
    const float feedbackSig = lastOut * feedback;
    hpMem += 0.06f * (feedbackSig - hpMem);
    const float inner = in + feedbackSig - hpMem;

    // --- Delay Line ---
    delay.pushSample(0, inner);
    float out = delay.popSample(0, modDelay);

    // --- Density Filter ---
    out = densityFilter.process(out);

    // --- Allpass Dispersion Chain ---
    // Loop is simple enough for compiler auto-vectorization
    for (int i = 0; i < numDispersionStages; ++i)
    {
        out = allpasses[i].process(out, cachedCoeffs[i]);
    }

    // --- Damping (one-pole lowpass) ---
    dampingMem += dampingCoeff * (out - dampingMem);
    out = dampingMem;

    // --- Soft Saturation ---
    out = std::tanh(out * 1.2f);
    
    lastOut = out;
    return out;
}

//==============================================================================
// AUDIO PROCESSOR IMPLEMENTATION
//==============================================================================

SpringerAudioProcessor::SpringerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Pre-allocate all data structures
    constexpr int numCoils = 7;
    constexpr int numStages = 256;
    
    allCoilCoeffs.resize(numCoils);
    for (auto& coeffs : allCoilCoeffs)
    {
        coeffs.resize(numStages);
        std::fill(coeffs.begin(), coeffs.end(), 0.7f);
    }
    
    springs.reserve(numCoils);
}

SpringerAudioProcessor::~SpringerAudioProcessor() {}

//==============================================================================
void SpringerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    invSampleRate = 1.0 / sampleRate;

    // Safety calculation:
    // Max Width (50,000) * Max Coil Scale (4.0) = 200,000 samples at pitch 0
    // At Pitch -32st, frequency is ~0.15x, delay length is ~6.3x longer
    // 200,000 * 6.3 ≈ 1.26M samples. Using 1.5M for safety margin
    constexpr int maxDelaySamples = 1500000;

    springs.resize(7);
    for (auto& spring : springs)
    {
        spring.prepare(sampleRate, maxDelaySamples);
        spring.maxSafeDelay = static_cast<float>(maxDelaySamples - 32);
    }

    randomizeSprings();
}

void SpringerAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SpringerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    const auto& mainInput = layouts.getMainInputChannelSet();
    
    return (mainOutput == juce::AudioChannelSet::mono() || 
            mainOutput == juce::AudioChannelSet::stereo()) &&
           (mainOutput == mainInput);
}
#endif

//==============================================================================
void SpringerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear excess output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    //==========================================================================
    // 1. PARAMETER LOADING (atomic loads, done once per block)
    //==========================================================================
    const float baseWidth = apvts.getRawParameterValue("width")->load();
    const float resonance = apvts.getRawParameterValue("resonance")->load();
    const float coupling = apvts.getRawParameterValue("coupling")->load();
    const float damping = apvts.getRawParameterValue("damping")->load();
    const float spread = apvts.getRawParameterValue("spread")->load();
    const float chirp = apvts.getRawParameterValue("chirp")->load();
    const float lfoRate = apvts.getRawParameterValue("lfoRate")->load();
    const float lfoDepth = apvts.getRawParameterValue("lfoDepth")->load();
    const float wet = apvts.getRawParameterValue("wet")->load();
    const bool muteDry = apvts.getRawParameterValue("muteDry")->load() > 0.5f;
    const float pitchSemitones = apvts.getRawParameterValue("pitch")->load();
    const int stages = static_cast<int>(apvts.getRawParameterValue("numStages")->load());
    const int activeCoils = static_cast<int>(apvts.getRawParameterValue("numCoils")->load());

    //==========================================================================
    // 2. PRE-CALCULATED CONSTANTS (reduces redundant math)
    //==========================================================================
    constexpr float totalGainLimit = 1.02f;
    constexpr float resonanceScale = totalGainLimit / 1.2f;
    constexpr float couplingResonanceScale = 0.05f;
    constexpr float couplingScale = 0.15f; // 0.45 / 3.0

    const float feedback = juce::jlimit(0.0f, totalGainLimit, 
        resonance * resonanceScale - coupling * couplingResonanceScale);
    const float xCouple = coupling * couplingScale;

    // Pitch scaling (expensive pow operation done once)
    const float pitchScale = std::pow(2.0f, -pitchSemitones / 12.0f);
    const float pitchDampingOffset = (pitchScale - 1.0f) * 0.15f;
    const float pitchChirpShift = (pitchScale - 1.0f) * 0.08f;
    const float chirpOffset = (chirp - 0.5f) * 0.4f;

    //==========================================================================
    // 3. PER-COIL PARAMETER CACHING (block-rate setup)
    //==========================================================================
    const int safeActiveCoils = juce::jmin(activeCoils, static_cast<int>(springs.size()));
    const float invCoilCount = (safeActiveCoils > 1) ? 1.0f / (safeActiveCoils - 1) : 1.0f;
    
    // Load density parameters once
    const float densityA = apvts.getRawParameterValue("densityA")->load();
    const float densityB = apvts.getRawParameterValue("densityB")->load();
    
    for (int c = 0; c < safeActiveCoils; ++c)
    {
        auto& spring = springs[c];
        
        const float coilLenMult = apvts.getRawParameterValue("coilLen" + juce::String(c + 1))->load();
        
        spring.numDispersionStages = stages;
        
        // Cache damping coefficient (used in hot loop)
        const float effectiveDamping = juce::jlimit(0.0f, 0.99f, damping + pitchDampingOffset);
        spring.dampingCoeff = 1.0f - effectiveDamping;
        
        spring.targetDepthScaled = lfoDepth * pitchScale;

        // Spread calculation (optimized)
        float spreadFactor = 1.0f;
        if (safeActiveCoils > 1)
        {
            const float spreadOffset = static_cast<float>(c) * invCoilCount;
            spreadFactor = 1.0f + (spreadOffset - 0.5f) * spread * 0.15f;
        }

        spring.targetDelayScaled = baseWidth * coilLenMult * pitchScale * spreadFactor;

        // Density filter (alternating A/B, bitwise test)
        const float baseDens = (c & 1) ? densityB : densityA;
        spring.densityFilter.setDelay(baseDens * pitchScale);

        // Cache dispersion coefficients
        const int clampedStages = juce::jmin(stages, 256);
        const float combinedShift = pitchChirpShift + chirpOffset;
        
        for (int i = 0; i < clampedStages; ++i)
        {
            spring.cachedCoeffs[i] = juce::jlimit(0.05f, 0.985f,
                allCoilCoeffs[c][i] + combinedShift);
        }
    }

    //==========================================================================
    // 4. AUDIO SAMPLE LOOP (ultra-optimized hot path)
    //==========================================================================
    const int numSamples = buffer.getNumSamples();
    auto* channelDataL = buffer.getWritePointer(0);
    auto* channelDataR = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    // Pre-calculate all gains
    const float normFactor = 1.2f / (1.0f + std::sqrt(static_cast<float>(safeActiveCoils)) * 0.4f);
    const float wetGain = wet * normFactor;
    constexpr float inputDrive = 0.75f; // 0.5 * 1.5

    // Determine if we're in mono or stereo mode
    const bool isStereo = (safeActiveCoils > 1);
    
    // OPTIMIZATION: Separate loops for muteDry to reduce branching
    if (muteDry)
    {
        // Wet-only path
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float inL = channelDataL[sample];
            const float inR = channelDataR ? channelDataR[sample] : inL;
            const float driveIn = std::tanh((inL + inR) * inputDrive);

            float sumOutL = 0.0f;
            float sumOutR = 0.0f;

            for (int c = 0; c < safeActiveCoils; ++c)
            {
                const int prevIdx = (c == 0) ? (safeActiveCoils - 1) : (c - 1);
                const float coupled = driveIn + springs[prevIdx].lastOut * xCouple;
                const float out = springs[c].process(coupled, feedback, lfoRate, sr);

                if (isStereo)
                {
                    if (c & 1) sumOutR += out;
                    else sumOutL += out;
                }
                else
                {
                    sumOutL += out;
                    sumOutR += out;
                }
            }

            channelDataL[sample] = sumOutL * wetGain;
            if (channelDataR) channelDataR[sample] = sumOutR * wetGain;
        }
    }
    else
    {
        // Wet + Dry path
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float inL = channelDataL[sample];
            const float inR = channelDataR ? channelDataR[sample] : inL;
            const float driveIn = std::tanh((inL + inR) * inputDrive);

            float sumOutL = 0.0f;
            float sumOutR = 0.0f;

            for (int c = 0; c < safeActiveCoils; ++c)
            {
                const int prevIdx = (c == 0) ? (safeActiveCoils - 1) : (c - 1);
                const float coupled = driveIn + springs[prevIdx].lastOut * xCouple;
                const float out = springs[c].process(coupled, feedback, lfoRate, sr);

                if (isStereo)
                {
                    if (c & 1) sumOutR += out;
                    else sumOutL += out;
                }
                else
                {
                    sumOutL += out;
                    sumOutR += out;
                }
            }

            channelDataL[sample] = inL + sumOutL * wetGain;
            if (channelDataR) channelDataR[sample] = inR + sumOutR * wetGain;
        }
    }
}

//==============================================================================
void SpringerAudioProcessor::randomizeSprings()
{
    constexpr int numCoils = 7;
    constexpr int numStages = 256;
    constexpr float invNumStages = 1.0f / 255.0f;

    for (int c = 0; c < numCoils; ++c)
    {
        for (int i = 0; i < numStages; ++i)
        {
            const float norm = static_cast<float>(i) * invNumStages;
            const float normSq = norm * norm;
            const float curve = 0.65f + 0.3f * normSq;
            const float variance = random.nextFloat() * 0.04f - 0.02f;
            
            allCoilCoeffs[c][i] = juce::jlimit(0.1f, 0.95f, curve + variance);
        }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SpringerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [](const juce::String& id, const juce::String& name, 
                         float min, float max, float def)
    {
        return std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(min, max), def);
    };

    // --- Main Physical Parameters ---
    layout.add(floatParam("width", "Size", 0.001f, 50000.0f, 500.0f));
    layout.add(floatParam("resonance", "Feedback", 0.0f, 1.2001f, 0.65f));
    layout.add(floatParam("coupling", "X-Couple", 0.0f, 3.001f, 0.2f));
    layout.add(floatParam("damping", "Damping", 0.0f, 1.0f, 0.4f));
    layout.add(floatParam("spread", "Spread", 0.0f, 1.0f, 0.2f));
    layout.add(floatParam("chirp", "Chirp", 0.0f, 1.0f, 0.5f));

    // --- Character & Modulation ---
    layout.add(std::make_unique<juce::AudioParameterInt>("numCoils", "Coils", 1, 7, 2));
    layout.add(floatParam("lfoRate", "Mod Rate", 0.0f, 100.0f, 0.1f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoDepth", "Mod Depth",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.01f, 0.3f), 0.2f));

    // --- Density Filters ---
    layout.add(floatParam("densityA", "Pre-Diffuse", 50.0f, 1000.0f, 223.0f));
    layout.add(floatParam("densityB", "Tank-Diffuse", 50.0f, 1000.0f, 307.0f));

    // --- Utility ---
    layout.add(floatParam("wet", "Mix", 0.0f, 2.0f, 0.5f));
    layout.add(std::make_unique<juce::AudioParameterBool>("muteDry", "Wet Only", false));
    layout.add(std::make_unique<juce::AudioParameterInt>("numStages", "Stages", 1, 256, 32));
    layout.add(floatParam("pitch", "Pitch", -32.0f, 32.0f, 0.0f));

    // --- Individual Coil Scales (1-7) ---
    for (int i = 1; i <= 7; ++i)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "coilLen" + juce::String(i), 
            "Coil Scale " + juce::String(i),
            juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f), 
            1.0f));
    }

    return layout;
}

//==============================================================================
bool SpringerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpringerAudioProcessor::createEditor()
{
    return new SpringerAudioProcessorEditor(*this);
}

void SpringerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpringerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpringerAudioProcessor();
}
