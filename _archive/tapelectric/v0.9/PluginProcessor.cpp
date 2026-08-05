  #include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// APVTS Parameter Layout - All parameters defined in one clean place!
juce::AudioProcessorValueTreeState::ParameterLayout TapElectricAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // === FIXED PRECISION HELPER ===
    // Changed arguments to juce::String to allow for string concatenation in loops
    auto createPreciseParam = [](juce::String id, juce::String name, float min, float max, float defaultVal, float skew = 1.0f) {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, // Modern JUCE parameter ID with version tag
            name,
            juce::NormalisableRange<float>(min, max, 0.0001f, skew),
            defaultVal);
        };

    // === GENERAL CONTROLS ===
    layout.add(createPreciseParam(PARAM_MIX, "Hum/Noise Mult", 0.0f, 0.5f, 0.033f));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        PARAM_FREQ_MODE, "Frequency", juce::StringArray{ "50 Hz", "60 Hz" }, 0));

    // === HUM CONTROLS ===
    layout.add(createPreciseParam(PARAM_HUM_VOLUME, "Hum Volume", 0.0f, 2.0f, 0.50f));
    layout.add(createPreciseParam(PARAM_HUM_PAN, "Hum Pan", -1.0f, 1.0f, 0.0f));
    layout.add(createPreciseParam(PARAM_HUM_STEREO, "Hum Stereo", 0.0f, 1.0f, 0.0f));
    layout.add(createPreciseParam(PARAM_HUM_DRIFT, "Hum Drift", 0.0f, 10.0f, 1.0f));

    // === RESONANCES (HARMONICS 1-6) ===
    layout.add(createPreciseParam(PARAM_HARMONIC_1, "1x", 0.0f, 1.0f, 0.569f, 0.3f));
    layout.add(createPreciseParam(PARAM_HARMONIC_2, "2x", 0.0f, 1.0f, 0.091f, 0.3f));
    layout.add(createPreciseParam(PARAM_HARMONIC_3, "3x", 0.0f, 1.0f, 0.411f, 0.3f));
    layout.add(createPreciseParam(PARAM_HARMONIC_4, "4x", 0.0f, 1.0f, 0.131f, 0.3f));
    layout.add(createPreciseParam(PARAM_HARMONIC_5, "5x", 0.0f, 1.0f, 0.084f, 0.3f));
    layout.add(createPreciseParam(PARAM_HARMONIC_6, "6x", 0.0f, 1.0f, 0.020f, 0.3f));

    // === NOISE CONTROLS ===
    layout.add(createPreciseParam(PARAM_NOISE_VOLUME, "Noise Volume", 0.0f, 1.0f, 0.080f));
    layout.add(createPreciseParam(PARAM_NOISE_PAN, "Noise Pan", -1.0f, 1.0f, 0.57f));
    layout.add(createPreciseParam(PARAM_NOISE_STEREO, "Noise Stereo", 0.0f, 1.0f, 0.64f));

    // === INPUT CONTROLS ===
    layout.add(createPreciseParam(PARAM_INPUT_VOLUME, "Input Volume", 0.0f, 2.0f, 1.0f));
    layout.add(createPreciseParam(PARAM_INPUT_DRIFT, "Input Drift", 0.0f, 10.0f, 0.0f));
    layout.add(createPreciseParam(PARAM_INPUT_WOBBLE, "Wobble", 0.0f, 10.0f, 0.0f));

    // === EQ BAND CONTROLS ===
    layout.add(createPreciseParam(PARAM_EQ_FREQ_1, "EQ1 Freq", 20.0f, 20000.0f, 20.0f, 0.3f));
    layout.add(createPreciseParam(PARAM_EQ_GAIN_1, "EQ1 Gain", -18.0f, 18.0f, 17.4f));
    layout.add(createPreciseParam(PARAM_EQ_Q_1, "EQ1 Q", 0.1f, 10.0f, 0.1f));

    layout.add(createPreciseParam(PARAM_EQ_FREQ_2, "EQ2 Freq", 20.0f, 20000.0f, 6200.0f, 0.3f));
    layout.add(createPreciseParam(PARAM_EQ_GAIN_2, "EQ2 Gain", -18.0f, 18.0f, -4.0f));
    layout.add(createPreciseParam(PARAM_EQ_Q_2, "EQ2 Q", 0.1f, 10.0f, 0.3f));

    layout.add(createPreciseParam(PARAM_EQ_FREQ_3, "EQ3 Freq", 20.0f, 20000.0f, 8000.0f, 0.3f));
    layout.add(createPreciseParam(PARAM_EQ_GAIN_3, "EQ3 Gain", -18.0f, 18.0f, 0.0f));
    layout.add(createPreciseParam(PARAM_EQ_Q_3, "EQ3 Q", 0.1f, 10.0f, 1.2f));

    // === PER-HARMONIC RANDOMIZATION ===
    for (int i = 1; i <= 6; ++i)
    {
        juce::String idx = juce::String(i);
        // Note: Ensure these strings "h1RandAmt" etc. match the IDs used in your Editor setupSlider calls
        layout.add(createPreciseParam("h" + idx + "RandAmt", "H" + idx + " Rand Amt", 0.0f, 1.0f, 0.0f));
        layout.add(createPreciseParam("h" + idx + "RandSpd", "H" + idx + " Rand Spd", 0.01f, 100.0f, 0.5f, 0.3f));
    }

    return layout;
}

//==============================================================================
TapElectricAudioProcessor::TapElectricAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
     :
#endif
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Get atomic pointers to parameters for efficient audio thread access
    mixParam = apvts.getRawParameterValue(PARAM_MIX);
    freqModeParam = apvts.getRawParameterValue(PARAM_FREQ_MODE);
    
    humVolumeParam = apvts.getRawParameterValue(PARAM_HUM_VOLUME);
    humPanParam = apvts.getRawParameterValue(PARAM_HUM_PAN);
    humStereoParam = apvts.getRawParameterValue(PARAM_HUM_STEREO);
    humDriftParam = apvts.getRawParameterValue(PARAM_HUM_DRIFT);
    
    harmonic1Param = apvts.getRawParameterValue(PARAM_HARMONIC_1);
    harmonic2Param = apvts.getRawParameterValue(PARAM_HARMONIC_2);
    harmonic3Param = apvts.getRawParameterValue(PARAM_HARMONIC_3);
    harmonic4Param = apvts.getRawParameterValue(PARAM_HARMONIC_4);
    harmonic5Param = apvts.getRawParameterValue(PARAM_HARMONIC_5);
    harmonic6Param = apvts.getRawParameterValue(PARAM_HARMONIC_6);
    
    noiseVolumeParam = apvts.getRawParameterValue(PARAM_NOISE_VOLUME);
    noisePanParam = apvts.getRawParameterValue(PARAM_NOISE_PAN);
    noiseStereoParam = apvts.getRawParameterValue(PARAM_NOISE_STEREO);
    
    inputVolumeParam = apvts.getRawParameterValue(PARAM_INPUT_VOLUME);
    inputDriftParam = apvts.getRawParameterValue(PARAM_INPUT_DRIFT);
    inputWobbleParam = apvts.getRawParameterValue(PARAM_INPUT_WOBBLE);
    
    eqFreq1Param = apvts.getRawParameterValue(PARAM_EQ_FREQ_1);
    eqGain1Param = apvts.getRawParameterValue(PARAM_EQ_GAIN_1);
    eqQ1Param = apvts.getRawParameterValue(PARAM_EQ_Q_1);
    
    eqFreq2Param = apvts.getRawParameterValue(PARAM_EQ_FREQ_2);
    eqGain2Param = apvts.getRawParameterValue(PARAM_EQ_GAIN_2);
    eqQ2Param = apvts.getRawParameterValue(PARAM_EQ_Q_2);
    
    eqFreq3Param = apvts.getRawParameterValue(PARAM_EQ_FREQ_3);
    eqGain3Param = apvts.getRawParameterValue(PARAM_EQ_GAIN_3);
    eqQ3Param = apvts.getRawParameterValue(PARAM_EQ_Q_3);
    
    h1RandAmtParam = apvts.getRawParameterValue(PARAM_H1_RAND_AMT);
    h1RandSpdParam = apvts.getRawParameterValue(PARAM_H1_RAND_SPD);
    h2RandAmtParam = apvts.getRawParameterValue(PARAM_H2_RAND_AMT);
    h2RandSpdParam = apvts.getRawParameterValue(PARAM_H2_RAND_SPD);
    h3RandAmtParam = apvts.getRawParameterValue(PARAM_H3_RAND_AMT);
    h3RandSpdParam = apvts.getRawParameterValue(PARAM_H3_RAND_SPD);
    h4RandAmtParam = apvts.getRawParameterValue(PARAM_H4_RAND_AMT);
    h4RandSpdParam = apvts.getRawParameterValue(PARAM_H4_RAND_SPD);
    h5RandAmtParam = apvts.getRawParameterValue(PARAM_H5_RAND_AMT);
    h5RandSpdParam = apvts.getRawParameterValue(PARAM_H5_RAND_SPD);
    h6RandAmtParam = apvts.getRawParameterValue(PARAM_H6_RAND_AMT);
    h6RandSpdParam = apvts.getRawParameterValue(PARAM_H6_RAND_SPD);
    
    phases.resize(6, 0.0);
    delayBufferL.resize(4800, 0.0f);
    delayBufferR.resize(4800, 0.0f);
    
    randomL = juce::Random(juce::Time::currentTimeMillis());
    randomR = juce::Random(juce::Time::currentTimeMillis() + 12345);
}

TapElectricAudioProcessor::~TapElectricAudioProcessor()
{
}

bool TapElectricAudioProcessor::randomizeHarmonics()
{
    juce::StringArray targetIDs = {
        "h1", "h2", "h3", "h4", "h5", "h6",
        "eqFreq1", "eqGain1", "eqFreq2", "eqGain2", "eqFreq3", "eqGain3"
    };

    auto& random = juce::Random::getSystemRandom();
    bool isCurrentlyRandom = false;

    // 1. Prüfen, ob h1 vom Default abweicht
    if (auto* p = apvts.getParameter("h1"))
    {
        if (std::abs(p->getValue() - p->getDefaultValue()) > 0.001f)
            isCurrentlyRandom = true;
    }

    // 2. Aktion ausführen
    for (const auto& id : targetIDs)
    {
        if (auto* param = apvts.getParameter(id))
        {
            if (isCurrentlyRandom)
                param->setValueNotifyingHost(param->getDefaultValue());
            else
                param->setValueNotifyingHost(random.nextFloat());
        }
    }

    // Rückgabe: Wenn wir gerade randomisiert haben, ist der NEUE Zustand "Random" (true)
    // Wenn wir resettet haben, ist der NEUE Zustand "Default" (false)
    return !isCurrentlyRandom;
}

void TapElectricAudioProcessor::updateEQFilters()
{
    auto setupBellFilter = [this](juce::dsp::IIR::Filter<float>& filterL, 
                                   juce::dsp::IIR::Filter<float>& filterR,
                                   float freq, float gain, float q) {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, freq, q, juce::Decibels::decibelsToGain(gain));
        
        *filterL.coefficients = *coeffs;
        *filterR.coefficients = *coeffs;
    };
    
    setupBellFilter(eqFilter1L, eqFilter1R, eqFreq1Param->load(), eqGain1Param->load(), eqQ1Param->load());
    setupBellFilter(eqFilter2L, eqFilter2R, eqFreq2Param->load(), eqGain2Param->load(), eqQ2Param->load());
    setupBellFilter(eqFilter3L, eqFilter3R, eqFreq3Param->load(), eqGain3Param->load(), eqQ3Param->load());
}

const juce::String TapElectricAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TapElectricAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TapElectricAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TapElectricAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TapElectricAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TapElectricAudioProcessor::getNumPrograms()
{
    return 1;
}

int TapElectricAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TapElectricAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String TapElectricAudioProcessor::getProgramName (int index)
{
    return {};
}

void TapElectricAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void TapElectricAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    
    std::fill(phases.begin(), phases.end(), 0.0);
    humDriftPhase = 0.0;
    inputDriftPhase = 0.0;
    inputWobblePhase = 0.0;
    
    int bufferSize = static_cast<int>(sr * 0.05);
    delayBufferL.resize(bufferSize, 0.0f);
    delayBufferR.resize(bufferSize, 0.0f);
    delayWritePos = 0;
    
    // Prepare EQ filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;
    
    eqFilter1L.prepare(spec);
    eqFilter1R.prepare(spec);
    eqFilter2L.prepare(spec);
    eqFilter2R.prepare(spec);
    eqFilter3L.prepare(spec);
    eqFilter3R.prepare(spec);
    
    updateEQFilters();
}

void TapElectricAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TapElectricAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void TapElectricAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Update EQ filters
    updateEQFilters();

    // Read all parameters once using atomic loads - thread-safe and efficient!
    const float mix = mixParam->load();
    const float baseFreq = (freqModeParam->load() < 0.5f) ? 50.0f : 60.0f;
    
    const float humVol = humVolumeParam->load();
    const float humPan = humPanParam->load();
    const float humStereo = humStereoParam->load();
    const float humDrift = humDriftParam->load();
    
    const float h1 = harmonic1Param->load();
    const float h2 = harmonic2Param->load();
    const float h3 = harmonic3Param->load();
    const float h4 = harmonic4Param->load();
    const float h5 = harmonic5Param->load();
    const float h6 = harmonic6Param->load();
    
    const float noiseVol = noiseVolumeParam->load();
    const float noisePan = noisePanParam->load();
    const float noiseStereo = noiseStereoParam->load();
    
    const float inputVol = inputVolumeParam->load();
    const float inputDrift = inputDriftParam->load();
    const float inputWobble = inputWobbleParam->load();
    
    // Per-harmonic volume randomization parameters
    const float h1RandAmt = h1RandAmtParam->load();
    const float h1RandSpd = h1RandSpdParam->load();
    const float h2RandAmt = h2RandAmtParam->load();
    const float h2RandSpd = h2RandSpdParam->load();
    const float h3RandAmt = h3RandAmtParam->load();
    const float h3RandSpd = h3RandSpdParam->load();
    const float h4RandAmt = h4RandAmtParam->load();
    const float h4RandSpd = h4RandSpdParam->load();
    const float h5RandAmt = h5RandAmtParam->load();
    const float h5RandSpd = h5RandSpdParam->load();
    const float h6RandAmt = h6RandAmtParam->load();
    const float h6RandSpd = h6RandSpdParam->load();
    
    // Array of harmonic values and their randomization parameters
    const std::array<float, 6> harmonicLevels = {h1, h2, h3, h4, h5, h6};
    const std::array<float, 6> randAmounts = {h1RandAmt, h2RandAmt, h3RandAmt, h4RandAmt, h5RandAmt, h6RandAmt};
    const std::array<float, 6> randSpeeds = {h1RandSpd, h2RandSpd, h3RandSpd, h4RandSpd, h5RandSpd, h6RandSpd};
    
    // Calculate modulated harmonic volumes (updated once per buffer for smooth continuous drift)
    std::array<float, 6> modulatedHarmonics = harmonicLevels;
    
    for (int i = 0; i < 6; ++i) {
        if (randAmounts[i] > 0.001f) {
            // Each harmonic gets its own LFO with slightly different rate
            // Update phase once per buffer (not per sample) for smooth continuous modulation
            double lfoFreq = randSpeeds[i] * 0.1 * harmonicModRates[i]; // 0.1 Hz base at speed=1.0
            double phaseIncrement = juce::MathConstants<double>::twoPi * lfoFreq * buffer.getNumSamples() / sampleRate;
            harmonicModPhases[i] += phaseIncrement;
            if (harmonicModPhases[i] >= juce::MathConstants<double>::twoPi)
                harmonicModPhases[i] -= juce::MathConstants<double>::twoPi;
            
            // Create BIPOLAR modulation: -1 to +1, oscillates AROUND the set value
            double modulation = std::sin(harmonicModPhases[i]); // -1 to +1
            double dbChange = modulation * randAmounts[i] * 6.0; // Â±6dB max at full amount
            float gainMultiplier = std::pow(10.0f, dbChange / 20.0f); // Convert dB to linear
            
            modulatedHarmonics[i] *= gainMultiplier;
        }
    }
    
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        // === HUM GENERATION ===
        double humDriftLFO = std::sin(humDriftPhase) * humDrift * 0.003;
        humDriftPhase += juce::MathConstants<double>::twoPi * 0.07 / sampleRate;
        if (humDriftPhase >= juce::MathConstants<double>::twoPi)
            humDriftPhase -= juce::MathConstants<double>::twoPi;
        
        float humSignal = 0.0f;
        for (int h = 0; h < 6; ++h)
        {
            double freq = baseFreq * (h + 1) * (1.0 + humDriftLFO);
            phases[h] += juce::MathConstants<double>::twoPi * freq / sampleRate;
            if (phases[h] >= juce::MathConstants<double>::twoPi)
                phases[h] -= juce::MathConstants<double>::twoPi;
        }
        
        humSignal = std::sin(phases[0]) * modulatedHarmonics[0] +
                   std::sin(phases[1]) * modulatedHarmonics[1] +
                   std::sin(phases[2]) * modulatedHarmonics[2] +
                   std::sin(phases[3]) * modulatedHarmonics[3] +
                   std::sin(phases[4]) * modulatedHarmonics[4] +
                   std::sin(phases[5]) * modulatedHarmonics[5];
        
        // Increased hum volume scaling from 0.15 to 0.4 for more headroom
        humSignal *= humVol * 0.4f;
        
        // Apply pan
        float humPanL = std::sqrt(0.5f * (1.0f - humPan));
        float humPanR = std::sqrt(0.5f * (1.0f + humPan));
        
        // Apply stereo width
        float stereoSpread = humStereo;
        float humL = humSignal * humPanL * (1.0f + stereoSpread * 0.5f);
        float humR = humSignal * humPanR * (1.0f - stereoSpread * 0.5f);
        
        // === HARD WHITE NOISE GENERATION ===
        float whiteNoiseL = randomL.nextFloat() * 2.0f - 1.0f;
        float whiteNoiseR = randomR.nextFloat() * 2.0f - 1.0f;
        
        // Apply EQ to white noise
        float eqNoiseL = eqFilter1L.processSample(whiteNoiseL);
        eqNoiseL = eqFilter2L.processSample(eqNoiseL);
        eqNoiseL = eqFilter3L.processSample(eqNoiseL);
        
        float eqNoiseR = eqFilter1R.processSample(whiteNoiseR);
        eqNoiseR = eqFilter2R.processSample(eqNoiseR);
        eqNoiseR = eqFilter3R.processSample(eqNoiseR);
        
        // Apply pan
        float noisePanL = std::sqrt(0.5f * (1.0f - noisePan));
        float noisePanR = std::sqrt(0.5f * (1.0f + noisePan));
        
        // Apply stereo width and volume
        float noiseL = eqNoiseL * noiseVol * noisePanL * (1.0f + noiseStereo * 0.5f);
        float noiseR = eqNoiseR * noiseVol * noisePanR * (1.0f - noiseStereo * 0.5f);
        
        // === INPUT PROCESSING WITH DRIFT/WOBBLE ===
        // Drift: slow, subtle pitch variation (like tape wow)
        double driftLFO = std::sin(inputDriftPhase) * inputDrift * 0.02; // Increased from 0.002
        inputDriftPhase += juce::MathConstants<double>::twoPi * 0.1 / sampleRate;
        if (inputDriftPhase >= juce::MathConstants<double>::twoPi)
            inputDriftPhase -= juce::MathConstants<double>::twoPi;
        
        // Wobble: faster, more pronounced modulation (like chorus/vibrato)
        double wobbleLFO = std::sin(inputWobblePhase) * inputWobble * 0.05; // Increased from 0.008
        inputWobblePhase += juce::MathConstants<double>::twoPi * 0.5 / sampleRate; // Faster rate: 0.5 Hz instead of 0.03 Hz
        if (inputWobblePhase >= juce::MathConstants<double>::twoPi)
            inputWobblePhase -= juce::MathConstants<double>::twoPi;
        
        double combinedLFO = driftLFO + wobbleLFO;
        
        for (int channel = 0; channel < juce::jmin(totalNumInputChannels, 2); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            float dryInput = channelData[sample]; // Store original dry input
            float processedInput = dryInput * inputVol;
            
            // Apply drift/wobble chorus effect ONLY to the input signal
            if (inputDrift > 0.001f || inputWobble > 0.001f)
            {
                if (channel == 0)
                    delayBufferL[delayWritePos] = processedInput;
                else
                    delayBufferR[delayWritePos] = processedInput;
                
                // Modulated delay time - much larger range for audible effect
                float delayTime = 10.0f + combinedLFO * 1000.0f; // Increased range significantly
                int delayInt = static_cast<int>(delayTime);
                float delayFrac = delayTime - delayInt;
                
                auto& delayBuffer = (channel == 0) ? delayBufferL : delayBufferR;
                int bufSize = delayBuffer.size();
                
                int readPos1 = (delayWritePos - delayInt + bufSize) % bufSize;
                int readPos2 = (readPos1 - 1 + bufSize) % bufSize;
                
                float delayed = delayBuffer[readPos1] * (1.0f - delayFrac) + 
                               delayBuffer[readPos2] * delayFrac;
                
                // Calculate wet/dry mix based on drift+wobble amount
                float wetAmount = juce::jmin(0.5f, (inputDrift + inputWobble) * 0.05f);
                float dryAmount = 1.0f - wetAmount;
                
                // Mix the delayed signal with the dry input to create chorus effect
                processedInput = processedInput * dryAmount + delayed * wetAmount;
            }
            
            // Get hum and noise (these are NOT affected by drift/wobble)
            float effect = (channel == 0) ? (humL + noiseL) : (humR + noiseR);
            
            // Final output: clean dry input + (processed input with drift/wobble) + (hum/noise * mix)
            channelData[sample] = dryInput * (1.0f - mix) + processedInput * (1.0f - mix) + effect * mix;
        }
        
        delayWritePos = (delayWritePos + 1) % delayBufferL.size();
    }
}

bool TapElectricAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TapElectricAudioProcessor::createEditor()
{
    return new TapElectricAudioProcessorEditor (*this);
}

// APVTS handles state saving/loading automatically! Much simpler than before.
void TapElectricAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TapElectricAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapElectricAudioProcessor();
}
