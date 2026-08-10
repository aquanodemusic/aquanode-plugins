#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VocodeAudioProcessor::VocodeAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Carrier",   juce::AudioChannelSet::stereo(), true)
        .withInput  ("Modulator", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    for (auto& l : bandLevels)     l.store (0.f);
    for (auto& b : bandwidthCurve) b = 0.5f;
}

VocodeAudioProcessor::~VocodeAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocodeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Extended upper limit from 100 → 256.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "numBands", "Bands",
        juce::NormalisableRange<float> (1.f, 256.f, 1.f), 16.f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float> (1.f, 500.f, 0.1f, 0.35f), 10.f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "release", "Release",
        juce::NormalisableRange<float> (10.f, 2000.f, 0.1f, 0.35f), 120.f));

    // 1–4 cascaded biquad stages per band.  Higher order = steeper skirts.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "bandOrder", "Band Order",
        juce::NormalisableRange<float> (1.f, 4.f, 1.f), 1.f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "compression", "Compression",
        juce::NormalisableRange<float> (0.f, 1.f), 0.f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "dryWet", "Dry/Wet",
        juce::NormalisableRange<float> (0.f, 1.f), 1.f));

    // Output gain applied after the dry/wet blend, 0–+24 dB.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float> (0.f, 24.f, 0.1f), 0.f));

    return { params.begin(), params.end() };
}

//==============================================================================
bool VocodeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto& mainIn = layouts.getMainInputChannelSet();
    if (mainIn != juce::AudioChannelSet::mono() &&
        mainIn != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void VocodeAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    currentNumBands   = juce::jlimit (1, kMaxBands,
        (int) *apvts.getRawParameterValue ("numBands"));
    compEnv = 0.f;
    rebuildFilters();
}

void VocodeAudioProcessor::releaseResources() {}

//==============================================================================
void VocodeAudioProcessor::rebuildFilters()
{
    constexpr float kMinFreq = 80.f;
    constexpr float kMaxFreq = 12000.f;
    const float     logRatio = std::log (kMaxFreq / kMinFreq);
    const float     sr       = (float) currentSampleRate;

    const float baseQ   = juce::jmax (1.f, (float) currentNumBands * 0.35f);
    const float globalQ = baseQ * 2.f;

    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& band = bands[b];

        const float t = (currentNumBands > 1)
            ? juce::jlimit (0.f, 1.f, (float) b / (currentNumBands - 1))
            : 0.f;

        band.centerFreq = kMinFreq * std::exp (logRatio * t);
        band.envelope   = 0.f;

        const float bwScale = juce::jlimit (0.f, 1.f, bandwidthCurve[b]);
        const float qMult   = juce::jmap  (bwScale, 0.f, 1.f, 2.f, 0.5f);
        const float q       = juce::jmax  (0.1f, globalQ * qMult);

        for (int ord = 0; ord < VocoderBand::kMaxOrder; ++ord)
        {
            band.modFilter [ord].setBandpass (band.centerFreq, q, sr);
            band.carFilterL[ord].setBandpass (band.centerFreq, q, sr);
            band.carFilterR[ord].setBandpass (band.centerFreq, q, sr);
            band.modFilter [ord].reset();
            band.carFilterL[ord].reset();
            band.carFilterR[ord].reset();
        }
    }

    for (auto& l : bandLevels) l.store (0.f);
}

//==============================================================================
void VocodeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    //--------------------------------------------------------------------------
    // Read parameters
    //--------------------------------------------------------------------------
    const int   numBands   = juce::jlimit (1, kMaxBands,
                                 (int) *apvts.getRawParameterValue ("numBands"));
    const float attackMs   = *apvts.getRawParameterValue ("attack");
    const float relMs      = *apvts.getRawParameterValue ("release");
    const int   bandOrder  = juce::jlimit (1, VocoderBand::kMaxOrder,
                                 (int) *apvts.getRawParameterValue ("bandOrder"));
    const float compAmt    = *apvts.getRawParameterValue ("compression");
    const float dryWet     = *apvts.getRawParameterValue ("dryWet");
    const float outputGainDb  = *apvts.getRawParameterValue ("outputGain");
    const float outputGainLin = std::pow (10.f, outputGainDb / 20.f);
    const float sr            = (float) currentSampleRate;

    //--------------------------------------------------------------------------
    // Rebuild filter bank if band count changed
    //--------------------------------------------------------------------------
    if (numBands != currentNumBands)
    {
        currentNumBands = numBands;
        rebuildFilters();
    }

    //--------------------------------------------------------------------------
    // Envelope coefficients
    //--------------------------------------------------------------------------
    attackCoeff  = std::exp (-1.f / juce::jmax (1.f, attackMs * 0.001f * sr));
    releaseCoeff = std::exp (-1.f / juce::jmax (1.f, relMs    * 0.001f * sr));

    //--------------------------------------------------------------------------
    const float baseQ   = juce::jmax (1.f, (float) numBands * 0.35f);
    const float globalQ = baseQ * 2.f;

    //--------------------------------------------------------------------------
    // Per-band level compensation, precomputed once per block.
    //--------------------------------------------------------------------------
    float levelComp[kMaxBands] = {};
    const float orderComp = std::sqrt ((float) bandOrder);

    for (int b = 0; b < numBands; ++b)
    {
        const float bwScale   = juce::jlimit (0.f, 1.f, bandwidthCurve[b]);
        const float qMult     = juce::jmap  (bwScale, 0.f, 1.f, 2.f, 0.5f);
        const float q         = juce::jmax  (0.1f, globalQ * qMult);
        const float freqTilt  = std::pow (bands[b].centerFreq / 1000.f, 0.30f);

        levelComp[b] = std::sqrt (qMult) * orderComp * freqTilt;

        for (int ord = 0; ord < VocoderBand::kMaxOrder; ++ord)
        {
            bands[b].modFilter [ord].setBandpass (bands[b].centerFreq, q, sr);
            bands[b].carFilterL[ord].setBandpass (bands[b].centerFreq, q, sr);
            bands[b].carFilterR[ord].setBandpass (bands[b].centerFreq, q, sr);
        }
    }

    //--------------------------------------------------------------------------
    // Copy carrier input before in-place output overwrites the buffer.
    //--------------------------------------------------------------------------
    const auto inBus = getBusBuffer (buffer, true, 0);
    const int  inCh  = inBus.getNumChannels();

    juce::AudioBuffer<float> carrier (2, numSamples);
    carrier.copyFrom (0, 0, inBus, 0,                  0, numSamples);
    carrier.copyFrom (1, 0, inBus, (inCh > 1 ? 1 : 0), 0, numSamples);

    //--------------------------------------------------------------------------
    // AUTO-VOCODE MODE
    //
    // Determined once per block from the bus topology — no per-sample energy
    // scan.  If the host has not assigned any channels to the sidechain bus
    // (getNumChannels() == 0), or if there is no second input bus at all, we
    // use the carrier as its own modulator.
    //
    // In auto-vocode mode the plugin gates each carrier band by that band's
    // own amplitude envelope, producing a spectrally focused, "robotic"
    // self-vocoding effect without needing an external voice signal.
    //
    // For a classic vocoder sound, patch:
    //   Bus 0  (carrier)   → harmonically rich source: sawtooth, buzz, noise
    //   Bus 1  (sidechain) → the signal whose formants you want to imprint
    //--------------------------------------------------------------------------
    const bool autoVocode = (getBusCount (true) < 2 ||
                             getBusBuffer (buffer, true, 1).getNumChannels() == 0);

    isAutoVocoding.store (autoVocode, std::memory_order_relaxed);

    const float* modL = nullptr;
    const float* modR = nullptr;

    if (!autoVocode)
    {
        const auto mb = getBusBuffer (buffer, true, 1);
        if (mb.getNumChannels() > 0) modL = mb.getReadPointer (0);
        if (mb.getNumChannels() > 1) modR = mb.getReadPointer (1);
        if (modR == nullptr)         modR = modL;
    }

    if (autoVocode || modL == nullptr)
    {
        modL = carrier.getReadPointer (0);
        modR = carrier.getReadPointer (1);
    }

    const float* carL = carrier.getReadPointer (0);
    const float* carR = carrier.getReadPointer (1);

    //--------------------------------------------------------------------------
    // Per-sample vocoder loop
    //--------------------------------------------------------------------------
    juce::AudioBuffer<float> wet (2, numSamples);
    wet.clear();

    float* wetL = wet.getWritePointer (0);
    float* wetR = wet.getWritePointer (1);

    float levelAccum[kMaxBands] = {};

    // kEnvSens: brings raw bandpass envelope values (~0.005–0.10) into [0,1].
    constexpr float kEnvSens = 15.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        const float modSamp = (modL[s] + modR[s]) * 0.5f;

        float sumL = 0.f, sumR = 0.f;

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[b];

            // ── Analysis ──────────────────────────────────────────────────
            float filtered = modSamp;
            for (int ord = 0; ord < bandOrder; ++ord)
                filtered = band.modFilter[ord].process (filtered);

            const float rect = std::abs (filtered);
            float& env = band.envelope;
            env = (rect > env)
                ? attackCoeff  * env + (1.f - attackCoeff)  * rect
                : releaseCoeff * env + (1.f - releaseCoeff) * rect;

            levelAccum[b] += env;

            const float envMod = juce::jmin (1.0f, env * kEnvSens * levelComp[b]);

            // ── Synthesis ─────────────────────────────────────────────────
            float cL = carL[s], cR = carR[s];
            for (int ord = 0; ord < bandOrder; ++ord)
            {
                cL = band.carFilterL[ord].process (cL);
                cR = band.carFilterR[ord].process (cR);
            }

            sumL += cL * envMod;
            sumR += cR * envMod;
        }

        wetL[s] = sumL;
        wetR[s] = sumR;
    }

    //--------------------------------------------------------------------------
    // Update display atomics
    //--------------------------------------------------------------------------
    const float invSamples = 1.f / (float) numSamples;
    for (int b = 0;        b < numBands;  ++b) bandLevels[b].store (levelAccum[b] * invSamples);
    for (int b = numBands; b < kMaxBands; ++b) bandLevels[b].store (0.f);

    //--------------------------------------------------------------------------
    // Post-vocoder compressor
    //
    //   compAmt = 0 → 1:1 (bypass)
    //   compAmt = 1 → 20:1 + 10 dB makeup, threshold –12 dBFS
    //
    // Fixed 5 ms attack / 100 ms release — separate from the vocoder A/R.
    //--------------------------------------------------------------------------
    if (compAmt > 0.001f)
    {
        constexpr float kThreshold = 0.25f;   // –12 dBFS
        const float ratio      = juce::jmap (compAmt, 0.f, 1.f,  1.f, 20.f);
        const float makeupGain = std::pow   (10.f,
                                    juce::jmap (compAmt, 0.f, 1.f, 0.f, 10.f) / 20.f);

        const float cAttCoeff = std::exp (-1.f / (0.005f * sr));
        const float cRelCoeff = std::exp (-1.f / (0.100f * sr));

        for (int s = 0; s < numSamples; ++s)
        {
            const float inputPeak = std::max (std::abs (wetL[s]), std::abs (wetR[s]));

            compEnv = (inputPeak > compEnv)
                ? cAttCoeff * compEnv + (1.f - cAttCoeff) * inputPeak
                : cRelCoeff * compEnv + (1.f - cRelCoeff) * inputPeak;

            float gainReduction = 1.f;
            if (compEnv > kThreshold)
            {
                const float overDb    = 20.f * std::log10 (compEnv / kThreshold);
                const float reducedDb = overDb * (1.f - 1.f / ratio);
                gainReduction = std::pow (10.f, -reducedDb / 20.f);
            }

            wetL[s] *= gainReduction * makeupGain;
            wetR[s] *= gainReduction * makeupGain;
        }
    }

    //--------------------------------------------------------------------------
    // Dry / wet blend + output gain — written to the output bus.
    //--------------------------------------------------------------------------
    auto outBus     = getBusBuffer (buffer, false, 0);
    const int outCh = juce::jmin (2, outBus.getNumChannels());

    for (int ch = 0; ch < outCh; ++ch)
    {
        const float* dry = carrier.getReadPointer (ch);
        const float* wt  = wet.getReadPointer     (ch);
        float*       dst = outBus.getWritePointer  (ch);

        for (int s = 0; s < numSamples; ++s)
            dst[s] = ((1.f - dryWet) * dry[s] + dryWet * wt[s]) * outputGainLin;
    }
}

//==============================================================================
void VocodeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::ValueTree curveTree ("BandwidthCurve");
    for (int i = 0; i < kMaxBands; ++i)
        curveTree.setProperty ("b" + juce::String (i), bandwidthCurve[i], nullptr);
    state.addChild (curveTree, -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void VocodeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);

    if (auto curve = state.getChildWithName ("BandwidthCurve"); curve.isValid())
    {
        for (int i = 0; i < kMaxBands; ++i)
            bandwidthCurve[i] = (float) curve.getProperty (
                "b" + juce::String (i), 0.5f);
    }
}

//==============================================================================
juce::AudioProcessorEditor* VocodeAudioProcessor::createEditor()
{
    return new VocodeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocodeAudioProcessor();
}
