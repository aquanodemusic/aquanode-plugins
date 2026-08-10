#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
/** Sound definition for the Alias Synth */
struct AliasSound : public juce::SynthesiserSound
{
    AliasSound() {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

/** The engine: Naive oscillators and nearest-neighbor sampling for maximum aliasing */
/** The engine: Now uses Frequency Folding for pure aliasing reflection */
/** * A SynthesiserVoice that building waveforms additively to
 * force individual harmonics to alias (reflect) off a custom Nyquist limit.
 */
struct AliasVoice : public juce::SynthesiserVoice
{
    AliasVoice(AliasSynthAudioProcessor* p)
        : processor(p), parameters(p->apvts)
    {
        adsr.setSampleRate(44100.0);
    }

    // --- Helper for Spectral Mirroring ---
    inline float foldFrequency(float fIn, float nyquist) const
    {
        if (nyquist >= 22000.0f) return fIn;
        if (nyquist <= 1.0f) return 0.0f;
        float virtualSR = nyquist * 2.0f;
        return std::abs(fIn - virtualSR * std::round(fIn / virtualSR));
    }

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<AliasSound*> (sound) != nullptr;
    }


    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
    {
        level = velocity;
        modPhase = 0.0f;
        naivePhase = 0.0f;
        samplePosition = 0.0;
        heldSample = 0.0f;
        sampleHoldCounter = 0;

        std::fill(harmonicPhases.begin(), harmonicPhases.end(), 0.0f);

        juce::ADSR::Parameters p;
        p.attack = *parameters.getRawParameterValue("attack");
        p.decay = *parameters.getRawParameterValue("decay");
        p.sustain = *parameters.getRawParameterValue("sustain");
        p.release = *parameters.getRawParameterValue("release");
        adsr.setParameters(p);
        adsr.noteOn();

        // --- Set up transpose smoothing ONCE per note start ---
        float transposeVal = *parameters.getRawParameterValue("transpose");
        float glideMs = *parameters.getRawParameterValue("glideTime");
        transposeSmooth.reset(voiceSampleRate, glideMs * 0.001f); // ms → seconds
        transposeSmooth.setCurrentAndTargetValue(transposeVal);
    }


    void stopNote(float, bool allowTailOff) override
    {
        adsr.noteOff();
        if (!allowTailOff) clearCurrentNote();
    }

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        if (!isVoiceActive()) return;

        // Fetch Parameters
        const int mode = (int)*parameters.getRawParameterValue("mode");
        const float fmAmount = *parameters.getRawParameterValue("fmAmount");
        const float fmRatio = *parameters.getRawParameterValue("fmRatio");
        const float masterVol = *parameters.getRawParameterValue("masterVol");
        const float nyquistLimit = *parameters.getRawParameterValue("nyquistLimit");
        const bool srMode = *parameters.getRawParameterValue("srMode") > 0.5f;
        const bool shouldFold = *parameters.getRawParameterValue("fold") > 0.5f;
        const float bitDepth = *parameters.getRawParameterValue("bitDepth");
        const float levels = std::pow(2.0f, bitDepth - 1.0f);

        // Downsampling (Sample & Hold) factor
        int holdFactor = juce::jmax(1, (int)std::round(voiceSampleRate / (nyquistLimit * 2.0f)));

        float noteFreq = juce::MidiMessage::getMidiNoteInHertz(getCurrentlyPlayingNote());
        float transposeTarget = *parameters.getRawParameterValue("transpose");

        // --- ONLY update target each block ---
        transposeSmooth.setTargetValue(transposeTarget);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float env = adsr.getNextSample();
            float out = 0.0f;

            // --- Smoothed transpose for this sample ---
            float transpose = transposeSmooth.getNextValue();
            float baseFreq = noteFreq * std::pow(2.0f, transpose);

            // --- Signal generation ---
            if (srMode)
            {
                if (sampleHoldCounter <= 0)
                {
                    heldSample = generateRawSignal(mode, baseFreq, fmAmount, fmRatio, 22050.0f);
                    sampleHoldCounter = holdFactor;
                }
                out = heldSample;
                sampleHoldCounter--;
            }
            else
            {
                out = generateRawSignal(mode, baseFreq, fmAmount, fmRatio, nyquistLimit);
            }

            if (shouldFold)
                out = std::sin(out * juce::MathConstants<float>::pi * 0.5f);

            if (bitDepth < 24.0f)
                out = std::round(out * levels) / levels;

            float finalOut = out * env * level * masterVol;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample(ch, startSample + sample, finalOut);

            if (!adsr.isActive() && mode < 3) clearCurrentNote();
        }
    }

    void setCurrentPlaybackSampleRate(double newRate)
    {
        if (newRate > 0.0)
        {
            voiceSampleRate = newRate;
            adsr.setSampleRate(newRate);

            // Initialize transpose smoothing with default 30ms
            transposeSmooth.reset(newRate, 0.03f);
            transposeSmooth.setCurrentAndTargetValue(0.0f);
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}


private:

    // Core generator used by both schemes
    float generateRawSignal(int mode, float freq, float fmAmt, float fmRatio, float nyLimit)
    {
        float modSignal = std::sin(modPhase * 2.0f * juce::MathConstants<float>::pi) * fmAmt;
        modPhase += (freq * fmRatio) / voiceSampleRate;
        if (modPhase >= 1.0f) modPhase -= 1.0f;

        float modulatedFreq = freq + (modSignal * freq);
        float sig = 0.0f;

        if (mode == 0) // NAIVE SAWTOOTH (Original)
        {
            float folded = foldFrequency(modulatedFreq, nyLimit);
            naivePhase += folded / voiceSampleRate;
            if (naivePhase >= 1.0f) naivePhase -= 1.0f;
            sig = 2.0f * naivePhase - 1.0f;
        }
        else if (mode == 1) // ADDITIVE SAW (64 Harmonics)
        {
            for (int k = 1; k <= 64; ++k)
            {
                float hFreq = foldFrequency(modulatedFreq * k, nyLimit);
                sig += (1.0f / k) * std::sin(harmonicPhases[k - 1] * 2.0f * juce::MathConstants<float>::pi);
                harmonicPhases[k - 1] += hFreq / voiceSampleRate;
                if (harmonicPhases[k - 1] >= 1.0f) harmonicPhases[k - 1] -= 1.0f;
            }
            sig *= 0.5f;
        }
        else if (mode == 2) // SINE WAVE
        {
            float folded = foldFrequency(modulatedFreq, nyLimit);
            harmonicPhases[0] += folded / voiceSampleRate;
            if (harmonicPhases[0] >= 1.0f) harmonicPhases[0] -= 1.0f;
            sig = std::sin(harmonicPhases[0] * 2.0f * juce::MathConstants<float>::pi);
        }
        else if (mode >= 3) // SAMPLE MODES
        {
            if (processor != nullptr && processor->isSampleLoaded)
            {
                auto& data = processor->sampleBuffer;
                float root = (mode == 3) ? 8.176f : (mode == 4 ? 261.63f : 8372.0f);
                float foldedFreq = foldFrequency(modulatedFreq, nyLimit);
                double speed = (foldedFreq / root) * (processor->sampleSourceRate / voiceSampleRate);

                int pos = (int)samplePosition;
                if (pos < data.getNumSamples())
                {
                    sig = data.getSample(0, pos);
                    samplePosition += speed;
                }
            }
        }
        return sig;
    }

    std::array<float, 100> harmonicPhases{ 0.0f };
    float naivePhase = 0.0f;
    float modPhase = 0.0f;
    float heldSample = 0.0f;
    int sampleHoldCounter = 0;
    double samplePosition = 0.0;
    float level = 0.0f;
    double voiceSampleRate = 44100.0;

    juce::SmoothedValue<float> transposeSmooth;

    juce::ADSR adsr;
    AliasSynthAudioProcessor* processor;
    juce::AudioProcessorValueTreeState& parameters;
};

//==============================================================================
AliasSynthAudioProcessor::AliasSynthAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    // Initialize Synthesiser with 16 voices
    for (int i = 0; i < 16; ++i)
        synth.addVoice(new AliasVoice(this));

    synth.addSound(new AliasSound());
}

AliasSynthAudioProcessor::~AliasSynthAudioProcessor()
{
}

//--- Audio & MIDI Lifecycle ---
void AliasSynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void AliasSynthAudioProcessor::releaseResources()
{
}

bool AliasSynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void AliasSynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear buffer to prevent "screaming" feedback noise
    buffer.clear();

    // The Synthesiser handles the voice management and rendering logic
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

//--- Editor & UI ---
juce::AudioProcessorEditor* AliasSynthAudioProcessor::createEditor()
{
    return new AliasSynthAudioProcessorEditor(*this);
}

bool AliasSynthAudioProcessor::hasEditor() const
{
    return true;
}

//--- DAW Information Overrides ---
const juce::String AliasSynthAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AliasSynthAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool AliasSynthAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool AliasSynthAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double AliasSynthAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//--- Programs (Presets) ---
int AliasSynthAudioProcessor::getNumPrograms()
{
    return 1;
}

int AliasSynthAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AliasSynthAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String AliasSynthAudioProcessor::getProgramName(int index)
{
    return {};
}

void AliasSynthAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//--- State Management (Persistence) ---
void AliasSynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AliasSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//--- Custom Logic & Parameter Layout ---
void AliasSynthAudioProcessor::loadSample()
{
    // 1. Reset the chooser to ensure a fresh dialog state
    chooser = std::make_unique<juce::FileChooser>("Select a Wave file...",
        juce::File{},
        "*.wav;*.aif;*.mp3");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file.existsAsFile())
            {
                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

                if (reader != nullptr)
                {
                    // 2. Temporarily stop the sampler logic to prevent OOB errors while swapping
                    isSampleLoaded = false;

                    sampleSourceRate = reader->sampleRate;

                    juce::AudioBuffer<float> tempBuffer;
                    tempBuffer.setSize(1, (int)reader->lengthInSamples);

                    // Read the actual data
                    reader->read(&tempBuffer, 0, (int)reader->lengthInSamples, 0, true, false);

                    // 3. Thread-safe swap: Copy to the main buffer
                    sampleBuffer.makeCopyOf(tempBuffer);
                    isSampleLoaded = true;
                }
            }
        });
}

juce::AudioProcessorValueTreeState::ParameterLayout AliasSynthAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- ADSR ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>("attack", "Attack", 0.0f, 5.0f, 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("decay", "Decay", 0.0f, 5.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sustain", "Sustain", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("release", "Release", 0.0f, 5.0f, 0.4f));

    // --- FM ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fmAmount", "FM Amt", 0.0f, 10.0f, 0.0f));
    // 1. Create the range: min, max, interval (0 = continuous), skew
    // A skew of 0.35f puts the value 1.0 roughly in the middle of a 0.001 to 10.0 range.
    juce::NormalisableRange<float> fmRatioRange(0.001f, 10.0f, 0.0001f, 0.35f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fmRatio",      // ID
        "FM Ratio",     // Name
        fmRatioRange,   // The range object with the skew built-in
        2.0f            // Default value
    ));

    // --- Volume ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>("masterVol", "Volume", 0.0f, 1.0f, 0.7f));

    // Transposer
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "transpose",
        "Transpose",
        juce::NormalisableRange<float>(-7.0f, 7.0f, 0.0f), // octaves
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "glideTime",
        "Glide (ms)",
        juce::NormalisableRange<float>(1.0f, 10000.0f, 0.1f),
        100.0f));

    // --- Engine Mode ---
    // Note: We reordered these to accommodate the new Saw modes.
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode",
        juce::StringArray{
            "Subtractive Saw",   // Index 0
            "Additive Saw (64)", // Index 1
            "Sine Wave",         // Index 2
            "Sample (Root C0)",  // Index 3
            "Sample (Root C5)",  // Index 4
            "Sample (Root C10)"  // Index 5
        }, 2));

    // --- Distortion & Aliasing Scheme Toggles ---
    params.push_back(std::make_unique<juce::AudioParameterBool>("fold", "Wavefold", false));

    // The "SR" parameter determines if nyquistLimit acts as 
    // Frequency Folding (false) or Sample Rate Reduction (true)
    params.push_back(std::make_unique<juce::AudioParameterBool>("srMode", "SR Mode", false));

    // --- Nyquist / Sample Rate Limit ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "nyquistLimit",
        "Nyquist Limit",
        juce::NormalisableRange<float>(100.0f, 22050.0f, 0.0f, 0.5f),
        22050.0f));

    // --- Bit Depth Reducer ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bitDepth", "Bit Depth",
        juce::NormalisableRange<float>(1.0f, 24.0f, 0.0f, 0.5f), 24.0f));

    return { params.begin(), params.end() };
}

//--- Factory Macro ---
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AliasSynthAudioProcessor();
}