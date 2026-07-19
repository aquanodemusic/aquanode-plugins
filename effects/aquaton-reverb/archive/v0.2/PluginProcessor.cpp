#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
constexpr std::array<int, AquatonAudioProcessor::NUM_LINES>
    AquatonAudioProcessor::BASE_DELAYS;

//==============================================================================
AquatonAudioProcessor::AquatonAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    std::array<float, NUM_LINES> ones;
    ones.fill(1.0f);
    buildMatrix(ones, ones);
}

AquatonAudioProcessor::~AquatonAudioProcessor() {}

//==============================================================================
// Normalised H8 Hadamard matrix with optional sign flips on rows/cols.
// Dr * H8 * Dc is orthogonal for any ±1 diagonal Dr, Dc.
//==============================================================================
void AquatonAudioProcessor::buildMatrix(const std::array<float, NUM_LINES>& rowSigns,
                                         const std::array<float, NUM_LINES>& colSigns)
{
    static constexpr float H8[NUM_LINES][NUM_LINES] = {
        { 1, 1, 1, 1, 1, 1, 1, 1 },
        { 1,-1, 1,-1, 1,-1, 1,-1 },
        { 1, 1,-1,-1, 1, 1,-1,-1 },
        { 1,-1,-1, 1, 1,-1,-1, 1 },
        { 1, 1, 1, 1,-1,-1,-1,-1 },
        { 1,-1, 1,-1,-1, 1,-1, 1 },
        { 1, 1,-1,-1,-1,-1, 1, 1 },
        { 1,-1,-1, 1,-1, 1, 1,-1 }
    };
    const float norm = 1.0f / std::sqrt((float)NUM_LINES);
    for (int r = 0; r < NUM_LINES; ++r)
        for (int c = 0; c < NUM_LINES; ++c)
            mixMatrix[r * NUM_LINES + c] = H8[r][c] * rowSigns[r] * colSigns[c] * norm;
}

//==============================================================================
void AquatonAudioProcessor::randomizeMatrix()
{
    std::array<float, NUM_LINES> rowSigns, colSigns;
    for (auto& s : rowSigns) s = rng.nextBool() ? 1.f : -1.f;
    for (auto& s : colSigns) s = rng.nextBool() ? 1.f : -1.f;
    buildMatrix(rowSigns, colSigns);
}

//==============================================================================
void AquatonAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    // Max size = 8, longest base delay = 6421, max mod depth = 50 → ~52000 samples
    const int maxDelaySamples = 55000;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels      = 1;

    for (auto& line : fdnLines)
        line.prepare(spec, maxDelaySamples);

    for (auto& ap : inputDiffL) ap.reset();
    for (auto& ap : inputDiffR) ap.reset();
}

void AquatonAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AquatonAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::stereo() ||
            out == juce::AudioChannelSet::mono()) && out == in;
}
#endif

//==============================================================================
void AquatonAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    //==========================================================================
    // PARAMETER LOAD (once per block)
    //==========================================================================
    const float size        = apvts.getRawParameterValue("size")->load();
    const float feedback    = apvts.getRawParameterValue("feedback")->load();
    const float lpHz        = apvts.getRawParameterValue("lpCutoff")->load();
    const float hpHz        = apvts.getRawParameterValue("hpCutoff")->load();
    const float preDiff     = apvts.getRawParameterValue("preDiffuse")->load();
    const float tankDiff    = apvts.getRawParameterValue("tankDiffuse")->load();
    const int   tankStages  = (int)apvts.getRawParameterValue("tankStages")->load();
    const float modRate     = apvts.getRawParameterValue("modRate")->load();
    const float modDepth    = apvts.getRawParameterValue("modDepth")->load();
    const float spread      = apvts.getRawParameterValue("spread")->load();
    const float mix         = apvts.getRawParameterValue("mix")->load();
    const bool  wetOnly     = apvts.getRawParameterValue("wetOnly")->load() > 0.5f;
    const float bloomAmount = apvts.getRawParameterValue("bloomAmount")->load();
    const float bloomTime   = apvts.getRawParameterValue("bloomTime")->load();
    const int   tapAmount   = juce::jlimit(0, MAX_INPUT_AP,
                               (int)apvts.getRawParameterValue("tapAmount")->load());
    const float hfWashHP   = apvts.getRawParameterValue("hfWashHP")->load();
    const float hfWashAmt  = apvts.getRawParameterValue("hfWashAmt")->load();

    //==========================================================================
    // COEFFICIENT PRE-CALCULATION
    //==========================================================================
    const float twoPiOverSr  = juce::MathConstants<float>::twoPi / (float)sr;
    const float lpCoeff      = 1.f - std::exp(-twoPiOverSr * lpHz);
    const float hpCoeff      = 1.f - std::exp(-twoPiOverSr * hpHz);
    const float hfCrossCoeff = 1.f - std::exp(-twoPiOverSr * hfWashHP);

    // Scale delay lines by Size + Spread
    for (int i = 0; i < NUM_LINES; ++i)
    {
        const float spreadOffset = (i & 1) ? (1.f + spread * 0.08f)
                                           : (1.f - spread * 0.08f);
        fdnLines[i].baseDelay = (float)BASE_DELAYS[i] * size * spreadOffset;
    }

    //==========================================================================
    // AUDIO LOOP
    //==========================================================================
    auto* chL = buffer.getWritePointer(0);
    auto* chR = (numOut > 1) ? buffer.getWritePointer(1) : nullptr;

    // Wet normalisation: 8 lines, 4 go to each channel; 1/sqrt(4) ≈ 0.5
    const float wetNorm = 0.5f;

    // Per-line LFO rate offsets (decorrelates modulation between lines)
    static constexpr float LFO_OFFSETS[NUM_LINES] = {
        1.00f, 1.03f, 0.97f, 1.07f, 0.93f, 1.11f, 0.89f, 1.15f
    };

    std::array<float, NUM_LINES> lineOuts;

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = chL[n];
        const float inR = chR ? chR[n] : inL;

        //--- Input diffusion (variable tap count) ---
        // tapAmount = 0 → no diffusion (raw echoes)
        // tapAmount = 16 → densely diffused, smooth attack
        float diffL = inL, diffR = inR;
        for (int i = 0; i < tapAmount; ++i)
        {
            diffL = inputDiffL[i].process(diffL, preDiff);
            diffR = inputDiffR[i].process(diffR, preDiff);
        }
        const float diffMono = (diffL + diffR) * 0.5f;

        //--- Hadamard matrix mix of previous outputs ---
        std::array<float, NUM_LINES> mixed;
        mixed.fill(0.f);
        for (int r = 0; r < NUM_LINES; ++r)
        {
            float sum = 0.f;
            for (int c = 0; c < NUM_LINES; ++c)
                sum += mixMatrix[r * NUM_LINES + c] * fdnLines[c].lastOut;
            mixed[r] = diffMono + sum * feedback;
        }

        //--- Process each FDN line ---
        for (int i = 0; i < NUM_LINES; ++i)
        {
            lineOuts[i] = fdnLines[i].process(
                mixed[i],
                lpCoeff, hpCoeff,
                modRate * LFO_OFFSETS[i], modDepth,
                tankDiff, tankStages,
                hfCrossCoeff, hfWashAmt,
                sr);
        }

        //--- Stereo output with Side Bloom ---
        // Each delay line's delay time determines how wide it sits in the stereo
        // field. Lines with delay < bloomTime are more central (mono-ish).
        // Lines with delay >= bloomTime are fully spread to their assigned side.
        // bloomAmount scales the maximum width.
        //
        // Pan weights per line:
        //   center weight  = (1 - bloomFactor) * 0.5  → feeds both L and R
        //   side weight    = bloomFactor               → feeds only L (even) or R (odd)
        //
        // Sum to L + R = 1.0 for any bloomFactor, preserving energy.
        float outL = 0.f, outR = 0.f;
        for (int i = 0; i < NUM_LINES; ++i)
        {
            // Delay time of this line in seconds
            const float delayTimeSec = fdnLines[i].baseDelay / (float)sr;

            // bloomFactor: 0 = fully centred, bloomAmount = fully spread
            float bloomFactor = bloomAmount;
            if (bloomTime > 0.001f)
                bloomFactor = juce::jlimit(0.f, bloomAmount,
                                           (delayTimeSec / bloomTime) * bloomAmount);

            const float ctr = 0.5f * (1.f - bloomFactor); // mono share (both channels)
            outL += lineOuts[i] * (ctr + (!(i & 1) ? bloomFactor : 0.f));
            outR += lineOuts[i] * (ctr + (  (i & 1) ? bloomFactor : 0.f));
        }
        outL *= wetNorm;
        outR *= wetNorm;

        //--- Mix ---
        if (wetOnly)
        {
            chL[n] = outL * mix;
            if (chR) chR[n] = outR * mix;
        }
        else
        {
            chL[n] = inL * (1.f - mix) + outL * mix;
            if (chR) chR[n] = inR * (1.f - mix) + outR * mix;
        }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AquatonAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&](const juce::String& id, const juce::String& name,
                        float min, float max, float def, float skew = -1.f)
    {
        auto range = juce::NormalisableRange<float>(min, max);
        if (skew > 0.f) range.setSkewForCentre(skew);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, range, def));
    };

    //--- Core reverb ---
    addFloat("size",        "Size",          0.1f,  8.f,      1.f,   1.f);
    addFloat("feedback",    "Feedback",      0.f,   1.2f,     0.7f);         // was "gravity"

    //--- Feedback filters ---
    addFloat("lpCutoff",    "LP Freq",       200.f, 20000.f,  8000.f, 3000.f);
    addFloat("hpCutoff",    "HP Freq",       20.f,  2000.f,   80.f,   200.f);

    //--- Diffusion ---
    addFloat("preDiffuse",  "Pre-Diffuse",   0.f,   0.95f,    0.62f);
    addFloat("tankDiffuse", "Tank-Diffuse",  0.f,   0.95f,    0.50f);

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "tankStages", "Tank Stages", 0, MAX_AP_TANK, 4));   // 0–256
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "tapAmount",  "Tap Amount",  0, MAX_INPUT_AP, 4));  // 0–16 input diffusion stages

    //--- Modulation ---
    addFloat("modRate",     "Mod Rate",      0.01f, 8.f,      0.25f, 1.f);
    addFloat("modDepth",    "Mod Depth",     0.f,   50.f,     4.f,   10.f);

    //--- Stereo / Output ---
    addFloat("spread",      "Spread",        0.f,   1.f,      0.3f);
    addFloat("mix",         "Mix",           0.f,   1.f,      0.5f);

    //--- Side Bloom ---
    // bloomAmount: 0 = mono, 1 = full stereo width
    // bloomTime:   delay time (s) at which bloom reaches its peak
    //              shorter = all lines bloom quickly (wide early)
    //              longer  = only long-delay (late) lines bloom
    addFloat("bloomAmount", "Bloom Amount",  0.f,   1.f,      0.f);
    addFloat("bloomTime",   "Bloom Time",    0.01f, 2.f,      0.1f,  0.3f);

    //--- HF Wash ---
    // Frequency above which high-frequency chorus/wash is applied in feedback
    addFloat("hfWashHP",    "HF Wash HP",   200.f, 15000.f,  3000.f, 2000.f);
    addFloat("hfWashAmt",   "HF Wash Amt",  0.f,   1.f,      0.f);

    layout.add(std::make_unique<juce::AudioParameterBool>("wetOnly", "Wet Only", false));

    return layout;
}

//==============================================================================
bool AquatonAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AquatonAudioProcessor::createEditor()
{
    return new AquatonAudioProcessorEditor(*this);
}

//==============================================================================
void AquatonAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AquatonAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AquatonAudioProcessor();
}
