#include "PluginProcessor.h"
#include "PluginEditor.h"

// ---------------------------------------------------------------------------
// Parameter IDs – single source of truth used by both processor and editor
// ---------------------------------------------------------------------------
namespace ParamID
{
    static const juce::String modIndex  { "modIndex"   };
    static const juce::String modGain   { "modGain"    };
    static const juce::String carGain   { "carGain"    };
    static const juce::String dryWet    { "dryWet"     };
    static const juce::String fmType    { "fmType"     };
    static const juce::String routing   { "routingMode"};
    static const juce::String carFadeIn { "carFadeIn"  };
    static const juce::String modFadeIn { "modFadeIn"  };
}

// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
AnyFMAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::modIndex, "Modulation Depth",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 0.1f, 0.5f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::modGain, "Modulator Gain",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::carGain, "Carrier Gain",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::dryWet, "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        ParamID::fmType, "Modulation Type",
        juce::StringArray{ "Phase Mod (FM)", "Ring Mod (AM)", "Feedback (Experimental)" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        ParamID::routing, "Routing",
        juce::StringArray{
            "Main to Main (Self-FM)",
            "Main to Sidechain (Main mods SC)",
            "Sidechain to Sidechain (SC Self-FM)",
            "Sidechain to Main (SC mods Main)" }, 3));

    auto fadeRange = juce::NormalisableRange<float>(0.0f, 5000.0f, 1.0f, 0.4f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::carFadeIn, "Carrier Fade In", fadeRange, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::modFadeIn, "Modulator Fade In", fadeRange, 0.0f));

    return { params.begin(), params.end() };
}

// ---------------------------------------------------------------------------
AnyFMAudioProcessor::AnyFMAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "AnyFMState", createParameterLayout())
{}

AnyFMAudioProcessor::~AnyFMAudioProcessor() {}

const juce::String AnyFMAudioProcessor::getName() const { return JucePlugin_Name; }
bool AnyFMAudioProcessor::acceptsMidi()  const { return false; }
bool AnyFMAudioProcessor::producesMidi() const { return false; }
bool AnyFMAudioProcessor::isMidiEffect() const { return false; }
double AnyFMAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  AnyFMAudioProcessor::getNumPrograms()    { return 1; }
int  AnyFMAudioProcessor::getCurrentProgram() { return 0; }
void AnyFMAudioProcessor::setCurrentProgram(int) {}
const juce::String AnyFMAudioProcessor::getProgramName(int) { return {}; }
void AnyFMAudioProcessor::changeProgramName(int, const juce::String&) {}

// ---------------------------------------------------------------------------
void AnyFMAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    const int bufferSize = 4096;
    delayBuffer.setSize(2, bufferSize);
    delayBuffer.clear();
    writePosition = 0;
    setLatencySamples(1024);

    modulationIndexSmoothed.resize(2);
    carrierGainSmoothed    .resize(2);
    modulatorGainSmoothed  .resize(2);
    dryWetSmoothed         .resize(2);

    for (int ch = 0; ch < 2; ++ch)
    {
        modulationIndexSmoothed[ch].reset(sampleRate, 0.02);
        carrierGainSmoothed    [ch].reset(sampleRate, 0.02);
        modulatorGainSmoothed  [ch].reset(sampleRate, 0.02);
        dryWetSmoothed         [ch].reset(sampleRate, 0.02);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        carFadeState   [ch] = 0;
        carEnvelope    [ch] = 0.0f;
        carSilenceCount[ch] = 0;
        modFadeState   [ch] = 0;
        modEnvelope    [ch] = 0.0f;
        modSilenceCount[ch] = 0;
        feedbackSample [ch] = 0.0f;
    }
}

void AnyFMAudioProcessor::releaseResources() {}

bool AnyFMAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo() &&
        layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// ---------------------------------------------------------------------------
// ms -> per-sample increment. 0 ms = instant open.
static inline float msToInc(float ms, float sr)
{
    return (ms > 0.5f) ? (1.0f / (ms * 0.001f * sr)) : 2.0f;
}

// ---------------------------------------------------------------------------
// Fade-in state machine.
// SILENT(0) -> FADING_IN(1) -> OPEN(2) -> SILENT after 0.1s silence
static inline float tickFadeIn(int& state, float& env, int& silenceCount,
                                float rawSignal, float attackInc,
                                float threshold, int silenceThreshold)
{
    const bool active = (std::abs(rawSignal) > threshold);

    switch (state)
    {
        case 0: // SILENT – wait for signal
            if (active) { state = 1; env = 0.0f; silenceCount = 0; }
            return 0.0f;

        case 1: // FADING_IN – abort if signal drops before ramp finishes
            if (!active) { state = 0; env = 0.0f; silenceCount = 0; return 0.0f; }
            env += attackInc;
            if (env >= 1.0f) { env = 1.0f; state = 2; silenceCount = 0; }
            return env;

        default: // OPEN – re-arm after 0.1s of continuous silence
            if (active) { silenceCount = 0; }
            else if (++silenceCount >= silenceThreshold)
                { state = 0; env = 0.0f; silenceCount = 0; }
            return 1.0f;
    }
}

// ---------------------------------------------------------------------------
void AnyFMAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples             = buffer.getNumSamples();

    auto sidechainBuffer = getBusBuffer(buffer, true, 1);
    const bool hasSidechain = (sidechainBuffer.getNumChannels() > 0);

    const int mainInputChannels = juce::jmin(totalNumInputChannels, 2);
    juce::AudioBuffer<float> mainInputCopy(mainInputChannels, numSamples);
    for (int ch = 0; ch < mainInputChannels; ++ch)
        mainInputCopy.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // Read param values once per block (smoothers handle per-sample interpolation)
    const float targetModIndex = apvts.getRawParameterValue(ParamID::modIndex)->load();
    const float targetCarGain  = apvts.getRawParameterValue(ParamID::carGain) ->load();
    const float targetModGain  = apvts.getRawParameterValue(ParamID::modGain) ->load();
    const float targetDryWet   = apvts.getRawParameterValue(ParamID::dryWet)  ->load();
    const int   mode            = (int) apvts.getRawParameterValue(ParamID::fmType) ->load();
    const int   routing         = (int) apvts.getRawParameterValue(ParamID::routing)->load();
    const int   delayBufferLen  = delayBuffer.getNumSamples();

    const float sr               = static_cast<float>(currentSampleRate);
    const float carAttackInc     = msToInc(apvts.getRawParameterValue(ParamID::carFadeIn)->load(), sr);
    const float modAttackInc     = msToInc(apvts.getRawParameterValue(ParamID::modFadeIn)->load(), sr);
    const int   silenceThreshold = static_cast<int>(0.1f * sr);

    static constexpr float kThreshold = 0.0005f;

    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        auto* outData    = buffer.getWritePointer(channel);
        const int mainCh = juce::jmin(channel, mainInputChannels - 1);
        const float* mainIn = mainInputCopy.getReadPointer(mainCh);

        const float* scIn = nullptr;
        if (hasSidechain)
        {
            const int scCh = juce::jmin(channel, sidechainBuffer.getNumChannels() - 1);
            scIn = sidechainBuffer.getReadPointer(scCh);
        }

        float* delayData = delayBuffer.getWritePointer(juce::jmin(channel, delayBuffer.getNumChannels() - 1));

        auto& modIdxSmoother  = modulationIndexSmoothed[juce::jmin(channel, (int)modulationIndexSmoothed.size() - 1)];
        auto& carGainSmoother = carrierGainSmoothed    [juce::jmin(channel, (int)carrierGainSmoothed.size()     - 1)];
        auto& modGainSmoother = modulatorGainSmoothed  [juce::jmin(channel, (int)modulatorGainSmoothed.size()   - 1)];
        auto& dryWetSmoother  = dryWetSmoothed         [juce::jmin(channel, (int)dryWetSmoothed.size()          - 1)];

        modIdxSmoother .setTargetValue(targetModIndex);
        carGainSmoother.setTargetValue(targetCarGain);
        modGainSmoother.setTargetValue(targetModGain);
        dryWetSmoother .setTargetValue(targetDryWet);

        int&   carState   = carFadeState   [channel];
        float& carEnv     = carEnvelope    [channel];
        int&   carSilence = carSilenceCount[channel];
        int&   modState   = modFadeState   [channel];
        float& modEnv     = modEnvelope    [channel];
        int&   modSilence = modSilenceCount[channel];
        float& feedback   = feedbackSample [channel];

        int localWritePos = writePosition;

        for (int s = 0; s < numSamples; ++s)
        {
            const float mainSample = mainIn ? mainIn[s] : 0.0f;
            const float scSample   = scIn   ? scIn[s]   : 0.0f;

            // ---- Routing ----
            float rawCarrier, rawMod;
            switch (routing)
            {
                case 0:  rawCarrier = mainSample; rawMod = mainSample; break;
                case 1:  rawCarrier = scSample;   rawMod = mainSample; break;
                case 2:  rawCarrier = scSample;   rawMod = scSample;   break;
                default: rawCarrier = mainSample; rawMod = scSample;   break;
            }

            // ---- Per-sample smoothed gains ----
            const float carGainVal = carGainSmoother.getNextValue();
            const float modGainVal = modGainSmoother.getNextValue();
            const float dryWetVal  = dryWetSmoother .getNextValue();
            const float wetAmount  = std::sin(dryWetVal * juce::MathConstants<float>::halfPi);
            const float dryAmount  = std::cos(dryWetVal * juce::MathConstants<float>::halfPi);

            // ---- Carrier fade-in ----
            const float carFadeGain   = tickFadeIn(carState, carEnv, carSilence, rawCarrier, carAttackInc, kThreshold, silenceThreshold);
            const float carrierSignal = rawCarrier * carGainVal * carFadeGain;

            delayData[localWritePos] = carrierSignal;

            float wetSignal = 0.0f;

            if (mode == 0) // Phase Modulation (FM)
            {
                const float modFadeGain = tickFadeIn(modState, modEnv, modSilence, rawMod, modAttackInc, kThreshold, silenceThreshold);
                const float modSample   = rawMod * modGainVal * modFadeGain;

                const float smoothDepth = modIdxSmoother.getNextValue();
                float readPos = static_cast<float>(localWritePos) - 1024.0f + (modSample * smoothDepth);
                while (readPos <  0.0f)                               readPos += (float)delayBufferLen;
                while (readPos >= (float)delayBufferLen)              readPos -= (float)delayBufferLen;

                const int   idxA = (int)readPos;
                const int   idxB = (idxA + 1) % delayBufferLen;
                const float frac = readPos - (float)idxA;
                wetSignal = delayData[idxA] * (1.0f - frac) + delayData[idxB] * frac;
            }
            else if (mode == 1) // Ring Modulation (AM)
            {
                const float modFadeGain = tickFadeIn(modState, modEnv, modSilence, rawMod, modAttackInc, kThreshold, silenceThreshold);
                const float modSample   = rawMod * modGainVal * modFadeGain;
                wetSignal = carrierSignal * (modSample * 2.0f);
                modIdxSmoother.skip(1);
            }
            else // Feedback (Experimental)
            {
                if (std::abs(rawCarrier) < kThreshold)
                    feedback *= 0.993f;

                const float modFadeGain = tickFadeIn(modState, modEnv, modSilence, feedback, modAttackInc, kThreshold, silenceThreshold);
                const float modSample   = feedback * modGainVal * modFadeGain;

                const float smoothDepth = modIdxSmoother.getNextValue();
                float readPos = static_cast<float>(localWritePos) - 1024.0f + (modSample * smoothDepth);
                while (readPos <  0.0f)                               readPos += (float)delayBufferLen;
                while (readPos >= (float)delayBufferLen)              readPos -= (float)delayBufferLen;

                const int   idxA = (int)readPos;
                const int   idxB = (idxA + 1) % delayBufferLen;
                const float frac = readPos - (float)idxA;
                wetSignal = delayData[idxA] * (1.0f - frac) + delayData[idxB] * frac;

                feedback = wetSignal;
            }

            // Final mix – cap only for Feedback mode to prevent runaway oscillation
            float outSample = (carrierSignal * dryAmount) + (wetSignal * wetAmount);
            if (mode == 2)
                outSample = juce::jlimit(-0.75f, 0.75f, outSample);
            outData[s] = outSample;

            localWritePos = (localWritePos + 1) % delayBufferLen;
        }
    }

    writePosition = (writePosition + numSamples) % delayBuffer.getNumSamples();

    carrierLevel  .store(mainInputCopy.getMagnitude(0, 0, numSamples));
    modulatorLevel.store(hasSidechain ? sidechainBuffer.getMagnitude(0, 0, numSamples) : 0.0f);
    outputLevel   .store(buffer.getMagnitude(0, 0, numSamples));

    for (int i = totalNumOutputChannels; i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);
}

// ---------------------------------------------------------------------------
bool AnyFMAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* AnyFMAudioProcessor::createEditor()
{
    return new AnyFMAudioProcessorEditor(*this);
}

// ---------------------------------------------------------------------------
// State – APVTS handles serialisation in one call each way
void AnyFMAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AnyFMAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr)
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnyFMAudioProcessor(); }
