#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Static member definition
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
    // Build initial (canonical) Hadamard matrix
    std::array<float, NUM_LINES> ones;
    ones.fill(1.0f);
    buildMatrix(ones, ones);
}

AquatonAudioProcessor::~AquatonAudioProcessor() {}

//==============================================================================
// Builds the normalized H8 Hadamard matrix with optional sign flips on rows/cols.
// Negating rows/cols preserves orthogonality, changing the mixing character.
//
// H8 pattern (before normalization):
//   Row 0: + + + + + + + +
//   Row 1: + - + - + - + -
//   Row 2: + + - - + + - -
//   Row 3: + - - + + - - +
//   Row 4: + + + + - - - -
//   Row 5: + - + - - + - +
//   Row 6: + + - - - - + +
//   Row 7: + - - + - + + -
//==============================================================================
void AquatonAudioProcessor::buildMatrix(const std::array<float, NUM_LINES>& rowSigns,
                                         const std::array<float, NUM_LINES>& colSigns)
{
    // Base H8 sign pattern (row-major)
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
    // Random diagonal sign matrices Dr and Dc: Dr * H8 * Dc is still orthogonal.
    // This gives 2^16 possible mixing configurations, all perfectly stable.
    std::array<float, NUM_LINES> rowSigns, colSigns;
    for (auto& s : rowSigns) s = rng.nextBool() ? 1.0f : -1.0f;
    for (auto& s : colSigns) s = rng.nextBool() ? 1.0f : -1.0f;
    buildMatrix(rowSigns, colSigns);
}

//==============================================================================
void AquatonAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    // Max size param = 8.0, longest base delay = 6421, max depth = 50
    // Max delay: 6421 * 8 + 50 + padding = ~52000 samples
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
    const float size         = apvts.getRawParameterValue("size")->load();
    const float gravity      = apvts.getRawParameterValue("gravity")->load();
    const float lpHz         = apvts.getRawParameterValue("lpCutoff")->load();
    const float hpHz         = apvts.getRawParameterValue("hpCutoff")->load();
    const float preDiff      = apvts.getRawParameterValue("preDiffuse")->load();
    const float tankDiff     = apvts.getRawParameterValue("tankDiffuse")->load();
    const int   tankStages   = (int)apvts.getRawParameterValue("tankStages")->load();
    const float modRate      = apvts.getRawParameterValue("modRate")->load();
    const float modDepth     = apvts.getRawParameterValue("modDepth")->load();
    const float spread       = apvts.getRawParameterValue("spread")->load();
    const float mix          = apvts.getRawParameterValue("mix")->load();
    const bool  wetOnly      = apvts.getRawParameterValue("wetOnly")->load() > 0.5f;

    //==========================================================================
    // COEFFICIENT PRE-CALCULATION (block-rate)
    //==========================================================================
    // One-pole LP coefficient: 1 - exp(-2π * fc / sr)
    const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float)sr;
    const float lpCoeff     = 1.0f - std::exp(-twoPiOverSr * lpHz);
    const float hpCoeff     = 1.0f - std::exp(-twoPiOverSr * hpHz);

    // Scale delay lines by Size. Spread varies each line ±spread*5% around center.
    // Even-indexed lines go slightly shorter (left-leaning), odd slightly longer.
    for (int i = 0; i < NUM_LINES; ++i)
    {
        const float spreadOffset = (i & 1) ? (1.0f + spread * 0.08f)
                                           : (1.0f - spread * 0.08f);
        fdnLines[i].baseDelay = (float)BASE_DELAYS[i] * size * spreadOffset;
    }

    //==========================================================================
    // AUDIO LOOP
    //==========================================================================
    auto* chL = buffer.getWritePointer(0);
    auto* chR = (numOut > 1) ? buffer.getWritePointer(1) : nullptr;

    // Wet output normalization: 8 lines summed, half to L half to R
    // Expected RMS of Hadamard output ≈ 1, divide by sqrt(4) = 2
    const float wetNorm = 0.5f;

    // LFO rates per line: slightly staggered to decorrelate modulation
    // (each line gets a slight LFO rate offset so they don't all pulse together)
    static constexpr float LFO_OFFSETS[NUM_LINES] = {
        1.00f, 1.03f, 0.97f, 1.07f, 0.93f, 1.11f, 0.89f, 1.15f
    };

    std::array<float, NUM_LINES> lineOuts;

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = chL[n];
        const float inR = chR ? chR[n] : inL;

        // --- Input diffuser (4-stage allpass, separates L/R) ---
        float diffL = inL;
        float diffR = inR;
        for (int i = 0; i < INPUT_AP; ++i)
        {
            diffL = inputDiffL[i].process(diffL, preDiff);
            diffR = inputDiffR[i].process(diffR, preDiff);
        }
        // Mono-sum for FDN input (the matrix spreads to stereo)
        const float diffMono = (diffL + diffR) * 0.5f;

        // --- Hadamard matrix mix of previous line outputs ---
        // mixed[r] = gravity * sum_c(M[r][c] * lastOut[c])
        std::array<float, NUM_LINES> mixed;
        mixed.fill(0.0f);
        for (int r = 0; r < NUM_LINES; ++r)
        {
            float sum = 0.0f;
            for (int c = 0; c < NUM_LINES; ++c)
                sum += mixMatrix[r * NUM_LINES + c] * fdnLines[c].lastOut;
            // Add input signal and scale by gravity
            mixed[r] = diffMono + sum * gravity;
        }

        // --- Process each FDN line ---
        for (int i = 0; i < NUM_LINES; ++i)
        {
            lineOuts[i] = fdnLines[i].process(
                mixed[i],
                lpCoeff, hpCoeff,
                modRate * LFO_OFFSETS[i],
                modDepth,
                tankDiff, tankStages,
                sr);
        }

        // --- Stereo output: even lines → L, odd lines → R ---
        float outL = 0.0f, outR = 0.0f;
        for (int i = 0; i < NUM_LINES; ++i)
        {
            if (i & 1) outR += lineOuts[i];
            else       outL += lineOuts[i];
        }
        outL *= wetNorm;
        outR *= wetNorm;

        // --- Mix ---
        if (wetOnly)
        {
            chL[n] = outL * mix;
            if (chR) chR[n] = outR * mix;
        }
        else
        {
            chL[n] = inL * (1.0f - mix) + outL * mix;
            if (chR) chR[n] = inR * (1.0f - mix) + outR * mix;
        }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AquatonAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&](const juce::String& id, const juce::String& name,
                        float min, float max, float def,
                        float skewMidpoint = -1.0f)
    {
        auto range = juce::NormalisableRange<float>(min, max);
        if (skewMidpoint > 0.0f)
            range.setSkewForCentre(skewMidpoint);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, range, def));
    };

    // --- Core reverb ---
    addFloat("size",     "Size",     0.1f, 8.0f,   1.0f,  1.0f);  // Delay line scale
    addFloat("gravity",  "Gravity",  0.0f, 1.2f,   0.7f);          // Feedback (>1 = grow)

    // --- Filters in feedback ---
    addFloat("lpCutoff", "LP Freq",  200.0f, 20000.0f, 8000.0f, 3000.0f);
    addFloat("hpCutoff", "HP Freq",  20.0f,  2000.0f,  80.0f,   200.0f);

    // --- Diffusion ---
    addFloat("preDiffuse",  "Pre-Diffuse",  0.0f, 0.95f, 0.62f);  // Input allpass coeff
    addFloat("tankDiffuse", "Tank-Diffuse", 0.0f, 0.95f, 0.50f);  // Tank allpass coeff
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "tankStages", "Tank Stages", 0, MAX_AP_TANK, 4));

    // --- Modulation ---
    addFloat("modRate",  "Mod Rate",  0.01f, 8.0f,  0.25f, 1.0f);
    addFloat("modDepth", "Mod Depth", 0.0f,  50.0f, 4.0f,  10.0f);

    // --- Stereo / Output ---
    addFloat("spread", "Spread", 0.0f, 1.0f, 0.3f);
    addFloat("mix",    "Mix",    0.0f, 1.0f, 0.5f);

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "wetOnly", "Wet Only", false));

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
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AquatonAudioProcessor();
}
