#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Gentle safety limiter: fully transparent below ~-1 dBFS, soft-saturates
    // above that so aggressive Morph/Max Boost/Mix combinations can't produce
    // hard digital overs. This is a safety net, not a creative limiter.
    inline float safetyLimit(float x) noexcept
    {
        constexpr float threshold = 0.891f;          // ~ -1 dBFS
        constexpr float range = 1.0f - threshold;
        const float ax = std::abs(x);
        if (ax <= threshold)
            return x;
        const float over = ax - threshold;
        return (x < 0.0f ? -1.0f : 1.0f) * (threshold + range * std::tanh(over / range));
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SpectralMorphAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "morph", 1 }, "Morph",
        NormalisableRange<float>(0.0f, 1.5f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(v * 100.0f, 0) + " %"; })));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "clarity", 1 }, "Clarity",
        NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(v * 100.0f, 0) + " %"; })));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "smooth", 1 }, "Smooth",
        NormalisableRange<float>(0.0f, 0.95f, 0.001f), 0.0f));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "maxBoost", 1 }, "Max Boost",
        NormalisableRange<float>(3.0f, 72.0f, 0.1f), 72.0f,
        AudioParameterFloatAttributes().withLabel("dB")));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "mix", 1 }, "Mix",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(v * 100.0f, 0) + " %"; })));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "outGain", 1 }, "Output",
        NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));

    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{ "fftSize", 1 }, "FFT Size",
        StringArray{ "512", "1024", "2048", "4096", "8192", "16384", "32768" }, 3));   // default = 4096

    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{ "overlap", 1 }, "Overlap",
        StringArray{ "2x", "4x", "8x" }, 1));   // default = 4x

    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "flip", 1 }, "Flip", false));

    // Replaces the old Level Match toggle. 0 = fully preserve the carrier's
    // own frame energy (old ON); 1 = apply no correction, so the modulator's
    // loudness contour passes through untouched (old OFF).
    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "dynamics", 1 }, "Dynamics",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(v * 100.0f, 0) + " %"; })));

    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "freezeSide", 1 }, "Freeze Side", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "bypass", 1 }, "Bypass", false));

    // Appending to this list is state-safe: APVTS stores the choice index, so
    // sessions saved with the two-mode build still load as Cepstral / Spectral.
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{ "morphMode", 1 }, "Morph Mode",
        StringArray{ "Cepstral", "Spectral", "Vocoder", "Inject", "Partials" }, 0));

    //--------------------------------------------------------------------------
    //  Vocoder / Inject / Partials
    //--------------------------------------------------------------------------
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return String(v * 100.0f, 0) + " %"; });

    // Real time constants, unlike the legacy Smooth knob whose meaning drifted
    // with FFT size. Fast attack is what keeps consonant onsets intact.
    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "attack", 1 }, "Attack",
        NormalisableRange<float>(0.0f, 80.0f, 0.01f, 0.35f), 5.0f,
        AudioParameterFloatAttributes().withLabel("ms")));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "release", 1 }, "Release",
        NormalisableRange<float>(0.0f, 500.0f, 0.1f, 0.40f), 60.0f,
        AudioParameterFloatAttributes().withLabel("ms")));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "flatten", 1 }, "Flatten",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.65f, pct));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "sibilance", 1 }, "Sibilance",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.60f, pct));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "fill", 1 }, "Fill",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.70f, pct));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "fold", 1 }, "Fold",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(v * 100.0f, 0) + " % oct"; })));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "glide", 1 }, "Glide",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.50f, pct));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "lock", 1 }, "Lock",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f, pct));

    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ "peakFloor", 1 }, "Peaks",
        NormalisableRange<float>(-90.0f, -20.0f, 0.5f), -60.0f,
        AudioParameterFloatAttributes().withLabel("dB")));

    return { p.begin(), p.end() };
}

//==============================================================================
SpectralMorphAudioProcessor::SpectralMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "SpectralMorph", createParameterLayout())
{
    pMorph = apvts.getRawParameterValue("morph");
    pClarity = apvts.getRawParameterValue("clarity");
    pSmooth = apvts.getRawParameterValue("smooth");
    pMaxBoost = apvts.getRawParameterValue("maxBoost");
    pMix = apvts.getRawParameterValue("mix");
    pOutGain = apvts.getRawParameterValue("outGain");
    pFlip = apvts.getRawParameterValue("flip");
    pDynamics = apvts.getRawParameterValue("dynamics");
    pFreeze = apvts.getRawParameterValue("freezeSide");
    pFftSize = apvts.getRawParameterValue("fftSize");
    pOverlap = apvts.getRawParameterValue("overlap");
    pBypass = apvts.getRawParameterValue("bypass");
    pMorphMode = apvts.getRawParameterValue("morphMode");
    pAttack = apvts.getRawParameterValue("attack");
    pRelease = apvts.getRawParameterValue("release");
    pFlatten = apvts.getRawParameterValue("flatten");
    pSibilance = apvts.getRawParameterValue("sibilance");
    pFill = apvts.getRawParameterValue("fill");
    pFold = apvts.getRawParameterValue("fold");
    pGlide = apvts.getRawParameterValue("glide");
    pLock = apvts.getRawParameterValue("lock");
    pPeakFloor = apvts.getRawParameterValue("peakFloor");

    apvts.addParameterListener("fftSize", this);
    apvts.addParameterListener("overlap", this);
}

SpectralMorphAudioProcessor::~SpectralMorphAudioProcessor()
{
    apvts.removeParameterListener("fftSize", this);
    apvts.removeParameterListener("overlap", this);
}

//==============================================================================
void SpectralMorphAudioProcessor::parameterChanged(const juce::String&, float)
{
    triggerAsyncUpdate();     // latency update must happen off the audio thread
}

void SpectralMorphAudioProcessor::handleAsyncUpdate()
{
    const int l = pendingLatency.exchange(-1);
    if (l >= 0)
        setLatencySamples(l);
}

//==============================================================================
bool SpectralMorphAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != out)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SpectralMorphAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);

    const int order = 9 + (int)*pFftSize;                 // 512 -> order 9
    static constexpr int kOverlaps[] = { 2, 4, 8 };
    const int overlap = kOverlaps[juce::jlimit(0, 2, juce::roundToInt((float)*pOverlap))];
    engine.setFFTSize(order, overlap, true);
    activeOrder = order;
    activeOverlap = overlap;
    setLatencySamples(engine.getLatencySamples());

    mainScratch.setSize(2, samplesPerBlock, false, true, true);
    sideScratch.setSize(2, samplesPerBlock, false, true, true);

    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(pOutGain->load()));
}

//==============================================================================
void SpectralMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer(buffer, true, 0);
    auto sideBus = getBusBuffer(buffer, true, 1);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    const int numMain = juce::jmax(1, mainBus.getNumChannels());
    const int numSide = sideBus.getNumChannels();

    // --- FFT / overlap switching (no allocation, cheap) ---------------------
    const int wantOrder = 9 + (int)*pFftSize;
    static constexpr int kOverlaps[] = { 2, 4, 8 };
    const int wantOverlap = kOverlaps[juce::jlimit(0, 2, juce::roundToInt((float)*pOverlap))];
    if (wantOrder != activeOrder || wantOverlap != activeOverlap)
    {
        engine.setFFTSize(wantOrder, wantOverlap);
        activeOrder = wantOrder;
        activeOverlap = wantOverlap;
        pendingLatency.store(engine.getLatencySamples());
        triggerAsyncUpdate();
    }

    // --- copy the main bus into a private stereo scratch ---------------------
    if (mainScratch.getNumSamples() < numSamples)
        mainScratch.setSize(2, numSamples, false, false, true);
    if (sideScratch.getNumSamples() < numSamples)
        sideScratch.setSize(2, numSamples, false, false, true);

    for (int ch = 0; ch < 2; ++ch)
    {
        mainScratch.copyFrom(ch, 0, mainBus, juce::jmin(ch, numMain - 1), 0, numSamples);
        if (numSide > 0)
            sideScratch.copyFrom(ch, 0, sideBus, juce::jmin(ch, numSide - 1), 0, numSamples);
        else
            sideScratch.clear(ch, 0, numSamples);
    }

    sideConnected.store(numSide > 0 && sideScratch.getMagnitude(0, numSamples) > 0.0f);

    // --- morph ---------------------------------------------------------------
    if (*pBypass < 0.5f)
    {
        MorphEngine::Params p;
        p.morph = pMorph->load();
        p.clarity = pClarity->load();
        p.smooth = pSmooth->load();
        p.maxBoostDb = pMaxBoost->load();
        p.mix = pMix->load();
        p.flip = *pFlip > 0.5f;
        p.dynamics = pDynamics->load();
        p.freezeSide = *pFreeze > 0.5f;
        p.mode = juce::jlimit(0, (int)MorphEngine::numModes - 1,
                              juce::roundToInt(pMorphMode->load()));

        p.attackMs = pAttack->load();
        p.releaseMs = pRelease->load();
        p.flatten = pFlatten->load();
        p.unvoiced = pSibilance->load();
        p.fill = pFill->load();
        p.fold = pFold->load();
        p.glide = pGlide->load();
        p.lock = pLock->load();
        p.peakFloorDb = pPeakFloor->load();

        juce::AudioBuffer<float> mainView(mainScratch.getArrayOfWritePointers(), 2, numSamples);
        juce::AudioBuffer<float> sideView(sideScratch.getArrayOfWritePointers(), 2, numSamples);
        engine.process(mainView, sideView, p);
    }

    outputGain.setTargetValue(juce::Decibels::decibelsToGain(pOutGain->load()));
    mainScratch.applyGainRamp(0, 0, numSamples, outputGain.getCurrentValue(), outputGain.getNextValue());
    mainScratch.applyGainRamp(1, 0, numSamples, outputGain.getCurrentValue(), outputGain.getNextValue());
    outputGain.skip(numSamples);

    // safety net: keeps large Max Boost / Morph / Mix combinations from
    // producing hard digital overs
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = mainScratch.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = safetyLimit(d[i]);
    }

    for (int ch = 0; ch < outBus.getNumChannels(); ++ch)
        outBus.copyFrom(ch, 0, mainScratch, juce::jmin(ch, 1), 0, numSamples);

    // clear anything the host handed us that we do not write
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* SpectralMorphAudioProcessor::createEditor()
{
    return new SpectralMorphAudioProcessorEditor(*this);
}

void SpectralMorphAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SpectralMorphAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralMorphAudioProcessor();
}