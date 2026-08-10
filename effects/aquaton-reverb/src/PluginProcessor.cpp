#include "PluginProcessor.h"
#include "PluginEditor.h"

// Out-of-line definition required for constexpr arrays in C++14.
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

    for (int i = 0; i < NUM_LINES; ++i)
    {
        linePolarity[i] = rng.nextBool() ? 1.f : -1.f;
        customDelays[i] = (float)BASE_DELAYS[i];
    }

    // Seed audio-thread-local copies so processBlock has valid data
    // before the first randomizeMatrix() call.
    localMatrix    = mixMatrix;
    localPolarity  = linePolarity;
    localDelays    = customDelays;
    localUseCustom = useCustomDelays;
}

AquatonAudioProcessor::~AquatonAudioProcessor() {}

//==============================================================================
// Build the normalised Hadamard mixing matrix with per-row and per-column sign
// patterns.  Writes directly to mixMatrix — only called from the constructor
// (single-threaded) and from randomizeMatrix() (which builds into temporaries
// and commits under matrixLock, so this version is only for construction).
//==============================================================================
void AquatonAudioProcessor::buildMatrix(const std::array<float, NUM_LINES>& rowSigns,
                                        const std::array<float, NUM_LINES>& colSigns)
{
    const float norm = 1.0f / std::sqrt((float)NUM_LINES);
    for (int r = 0; r < NUM_LINES; ++r)
        for (int c = 0; c < NUM_LINES; ++c)
        {
            unsigned int bits = (unsigned int)(r & c);
            int pc = 0;
            while (bits) { ++pc; bits &= bits - 1u; }
            const float h = (pc & 1) ? -1.0f : 1.0f;
            mixMatrix[r * NUM_LINES + c] = h * rowSigns[r] * colSigns[c] * norm;
        }
}

//==============================================================================
// Randomise the Hadamard sign pattern and per-line polarity.
//
// All computation happens on the message thread using local temporaries.
// Only the final commit (copy into the shared arrays) is done under matrixLock,
// so the audio thread is never blocked for more than a trivial memcpy.
//==============================================================================
void AquatonAudioProcessor::randomizeMatrix()
{
    // --- Build new matrix into a local temp ---
    std::array<float, NUM_LINES> rowSigns, colSigns;
    for (auto& s : rowSigns) s = rng.nextBool() ? 1.f : -1.f;
    for (auto& s : colSigns) s = rng.nextBool() ? 1.f : -1.f;

    std::array<float, NUM_LINES * NUM_LINES> newMatrix;
    const float norm = 1.0f / std::sqrt((float)NUM_LINES);
    for (int r = 0; r < NUM_LINES; ++r)
        for (int c = 0; c < NUM_LINES; ++c)
        {
            unsigned int bits = (unsigned int)(r & c);
            int pc = 0;
            while (bits) { ++pc; bits &= bits - 1u; }
            const float h = (pc & 1) ? -1.0f : 1.0f;
            newMatrix[r * NUM_LINES + c] = h * rowSigns[r] * colSigns[c] * norm;
        }

    // --- Build new polarity ---
    std::array<float, NUM_LINES> newPolarity;
    for (int i = 0; i < NUM_LINES; ++i)
        newPolarity[i] = rng.nextBool() ? 1.f : -1.f;

    // --- Build new delay times ---
    std::array<float, NUM_LINES> newDelays;
    bool newUseCustom;
    if (rng.nextBool())
    {
        const float logMin = std::log((float)BASE_DELAYS[0]);
        const float logMax = std::log((float)BASE_DELAYS[NUM_LINES - 1]);
        for (int i = 0; i < NUM_LINES; ++i)
        {
            const float t = (float)i / (float)(NUM_LINES - 1);
            const float d = std::exp(logMin + t * (logMax - logMin));
            newDelays[i] = d * (0.90f + rng.nextFloat() * 0.20f);
        }
        newUseCustom = true;
    }
    else
    {
        for (int i = 0; i < NUM_LINES; ++i)
            newDelays[i] = (float)BASE_DELAYS[i];
        newUseCustom = false;
    }

    // --- Commit atomically under the spin lock ---
    {
        const juce::SpinLock::ScopedLockType sl(matrixLock);
        mixMatrix       = newMatrix;
        linePolarity    = newPolarity;
        customDelays    = newDelays;
        useCustomDelays = newUseCustom;
    }
    matrixDirty.store(true, std::memory_order_release);
}

//==============================================================================
void AquatonAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int maxDelaySamples    = 230000;
    const int maxPreDelaySamples = (int)(0.201 * sampleRate) + 64;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate      = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels     = 1;

    for (auto& line : fdnLines) line.prepare(spec, maxDelaySamples);
    for (auto& ap : inputDiffL) ap.reset();
    for (auto& ap : inputDiffR) ap.reset();

    // Prepare pre-delay and dry-path delay lines (all mono)
    auto prepDL = [&](auto& dl)
    {
        dl.setMaximumDelayInSamples(maxPreDelaySamples);
        dl.prepare(spec);
        dl.reset();
    };
    prepDL(preDelayLineL);
    prepDL(preDelayLineR);
    prepDL(dryDelayLineL);
    prepDL(dryDelayLineR);

    // -----------------------------------------------------------------------
    // Seed smoothers from current parameter state
    // -----------------------------------------------------------------------
    const float twoPiOverSrF = juce::MathConstants<float>::twoPi / (float)sampleRate;
    auto lpCoeffFrom = [&](float hz) {
        return 1.f - std::exp(-twoPiOverSrF * hz);
    };

    const float initSize       = apvts.getRawParameterValue("size")->load();
    const float initFeedback   = apvts.getRawParameterValue("feedback")->load();
    const float initTail       = apvts.getRawParameterValue("tail")->load();
    const float initLpHz       = apvts.getRawParameterValue("lpCutoff")->load();
    const float initHpHz       = apvts.getRawParameterValue("hpCutoff")->load();
    const float initPreDiff    = apvts.getRawParameterValue("preDiffuse")->load();
    const float initTankDiff   = apvts.getRawParameterValue("tankDiffuse")->load();
    const float initModRate    = apvts.getRawParameterValue("modRate")->load();
    const float initModDepth   = apvts.getRawParameterValue("modDepth")->load();
    const int   initFdnOrder   = juce::jlimit(1, NUM_LINES,
                                    (int)apvts.getRawParameterValue("fdnOrder")->load());
    const float initSpread     = apvts.getRawParameterValue("spread")->load();
    const float initMix        = apvts.getRawParameterValue("mix")->load();
    const float initBloomAmt   = apvts.getRawParameterValue("bloomAmount")->load();
    const float initBloomTime  = apvts.getRawParameterValue("bloomTime")->load();
    const float initHfWashHP   = apvts.getRawParameterValue("hfWashHP")->load();
    const float initHfWashAmt  = apvts.getRawParameterValue("hfWashAmt")->load();
    const float initPolarity   = apvts.getRawParameterValue("polarityAmt")->load();
    const float initApfMod     = apvts.getRawParameterValue("apfMod")->load();
    const float initPreDelayMs = apvts.getRawParameterValue("preDelay")->load();

    const float initClampedFb    = juce::jmin(
        initFeedback + initTail * juce::jmax(0.f, 0.9995f - initFeedback), 0.9995f);
    const float initMatrixScale  = std::sqrt((float)NUM_LINES / (float)initFdnOrder);
    const float initCompFeedback = initClampedFb * initMatrixScale;
    const float initWetNorm      = 1.f / std::sqrt((float)initFdnOrder * 0.5f);

    auto initSm = [&](juce::SmoothedValue<float>& sm, float v)
    {
        sm.reset(sampleRate, kSmoothTimeSec);
        sm.setCurrentAndTargetValue(v);
    };

    initSm(smSize,         initSize);
    initSm(smCompFeedback, initCompFeedback);
    initSm(smWetNorm,      initWetNorm);
    initSm(smLpCoeff,      lpCoeffFrom(initLpHz));
    initSm(smHpCoeff,      lpCoeffFrom(initHpHz));
    initSm(smHfCrossCoeff, lpCoeffFrom(initHfWashHP));
    initSm(smPreDiff,      initPreDiff);
    initSm(smTankDiff,     initTankDiff);
    initSm(smModRate,      initModRate);
    initSm(smModDepth,     initModDepth);
    initSm(smSpread,       initSpread);
    initSm(smMix,          initMix);
    initSm(smBloomAmount,  initBloomAmt);
    initSm(smBloomTime,    initBloomTime);
    initSm(smHfWashAmt,    initHfWashAmt);
    initSm(smPolarityAmt,  initPolarity);
    initSm(smApfMod,       initApfMod);
    initSm(smPreDelay,     initPreDelayMs);

    // Report initial latency (non-zero only when preDelay < 0)
    const int initLatency = (initPreDelayMs < 0.f)
        ? (int)((-initPreDelayMs * 0.001f) * sampleRate + 0.5)
        : 0;
    setLatencySamples(initLatency);

    // Reset order-tracking so the first block doesn't mistakenly zero lastOuts
    prevFdnOrder = NUM_LINES;
}

//==============================================================================
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
    // Read all parameter values once per block
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
    const float hfWashHP    = apvts.getRawParameterValue("hfWashHP")->load();
    const float hfWashAmt   = apvts.getRawParameterValue("hfWashAmt")->load();
    const int   fdnOrder    = juce::jlimit(1, NUM_LINES,
                                (int)apvts.getRawParameterValue("fdnOrder")->load());
    const float apfModAmt   = apvts.getRawParameterValue("apfMod")->load();
    const float polarityAmt = apvts.getRawParameterValue("polarityAmt")->load();
    const float tail        = apvts.getRawParameterValue("tail")->load();
    const float preDelayMs  = apvts.getRawParameterValue("preDelay")->load();
    const bool  freeze      = apvts.getRawParameterValue("freeze")->load() > 0.5f;

    //==========================================================================
    // Latency reporting — only for negative pre-delay (pre-verb / reverse mode)
    //==========================================================================
    const int newLatency = (preDelayMs < 0.f)
        ? (int)((-preDelayMs * 0.001f) * (float)sr + 0.5f)
        : 0;
    if (newLatency != getLatencySamples())
        setLatencySamples(newLatency);

    //==========================================================================
    // Thread-safe matrix update — copy pending state once per block if dirty.
    // The spin lock is contested only on a button press; steady-state is free.
    //==========================================================================
    if (matrixDirty.load(std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedLockType sl(matrixLock);
        localMatrix    = mixMatrix;
        localPolarity  = linePolarity;
        localDelays    = customDelays;
        localUseCustom = useCustomDelays;
        matrixDirty.store(false, std::memory_order_relaxed);
    }

    //==========================================================================
    // Stale lastOut fix — when fdnOrder increases, lines that were inactive
    // may have non-zero lastOut from their last active block.  Zero them before
    // they re-enter the feedback mix to avoid a click.
    //==========================================================================
    if (fdnOrder > prevFdnOrder)
        for (int i = prevFdnOrder; i < fdnOrder; ++i)
            fdnLines[i].lastOut = 0.f;
    prevFdnOrder = fdnOrder;

    //==========================================================================
    // Compute block-level smoother targets
    //==========================================================================
    const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float)sr;

    // Freeze locks the feedback loop at unity; the input gate is applied
    // per-sample below so the existing tail rings on indefinitely.
    const float clampedFeedback = freeze
        ? 1.0f
        : juce::jmin(feedback + tail * juce::jmax(0.f, 0.9995f - feedback), 0.9995f);

    const float matrixScale    = std::sqrt((float)NUM_LINES / (float)fdnOrder);
    const float targetCompFb   = clampedFeedback * matrixScale;
    const float targetWetNorm  = 1.f / std::sqrt((float)fdnOrder * 0.5f);
    const float targetLpCoeff  = 1.f - std::exp(-twoPiOverSr * lpHz);
    const float targetHpCoeff  = 1.f - std::exp(-twoPiOverSr * hpHz);
    const float targetHfCross  = 1.f - std::exp(-twoPiOverSr * hfWashHP);

    smSize        .setTargetValue(size);
    smCompFeedback.setTargetValue(targetCompFb);
    smWetNorm     .setTargetValue(targetWetNorm);
    smLpCoeff     .setTargetValue(targetLpCoeff);
    smHpCoeff     .setTargetValue(targetHpCoeff);
    smHfCrossCoeff.setTargetValue(targetHfCross);
    smPreDiff     .setTargetValue(preDiff);
    smTankDiff    .setTargetValue(tankDiff);
    smModRate     .setTargetValue(modRate);
    smModDepth    .setTargetValue(modDepth);
    smSpread      .setTargetValue(spread);
    smMix         .setTargetValue(mix);
    smBloomAmount .setTargetValue(bloomAmount);
    smBloomTime   .setTargetValue(bloomTime);
    smHfWashAmt   .setTargetValue(hfWashAmt);
    smPolarityAmt .setTargetValue(polarityAmt);
    smApfMod      .setTargetValue(apfModAmt);
    smPreDelay    .setTargetValue(preDelayMs);

    //==========================================================================
    // Per-sample audio loop
    //==========================================================================
    auto* chL = buffer.getWritePointer(0);
    auto* chR = (numOut > 1) ? buffer.getWritePointer(1) : nullptr;

    static constexpr float LFO_OFFSETS[NUM_LINES] = {
        1.00f, 1.03f, 0.97f, 1.07f, 0.93f, 1.11f, 0.89f, 1.15f,
        1.04f, 0.96f, 1.08f, 0.92f, 1.12f, 0.88f, 1.06f, 0.94f,
        1.02f, 0.98f, 1.10f, 0.90f, 1.14f, 0.86f, 1.05f, 0.95f,
        1.01f, 0.99f, 1.09f, 0.91f, 1.13f, 0.87f, 1.07f, 0.93f,
        1.00f, 1.02f, 0.98f, 1.06f, 0.94f, 1.10f, 0.90f, 1.14f,
        1.03f, 0.97f, 1.08f, 0.92f, 1.12f, 0.88f, 1.05f, 0.95f,
        1.01f, 0.99f, 1.07f, 0.93f, 1.11f, 0.89f, 1.04f, 0.96f,
        1.02f, 0.98f, 1.06f, 0.94f, 1.09f, 0.91f, 1.03f, 0.97f
    };

    std::array<float, NUM_LINES> lineOuts;

    for (int n = 0; n < numSamples; ++n)
    {
        // ------------------------------------------------------------------
        // Advance smoothers
        // ------------------------------------------------------------------
        const float curSize         = smSize        .getNextValue();
        const float curCompFb       = smCompFeedback.getNextValue();
        const float curWetNorm      = smWetNorm     .getNextValue();
        const float curLpCoeff      = smLpCoeff     .getNextValue();
        const float curHpCoeff      = smHpCoeff     .getNextValue();
        const float curHfCrossCoeff = smHfCrossCoeff.getNextValue();
        const float curPreDiff      = smPreDiff     .getNextValue();
        const float curTankDiff     = smTankDiff    .getNextValue();
        const float curModRate      = smModRate     .getNextValue();
        const float curModDepth     = smModDepth    .getNextValue();
        const float curSpread       = smSpread      .getNextValue();
        const float curMix          = smMix         .getNextValue();
        const float curBloomAmount  = smBloomAmount .getNextValue();
        const float curBloomTime    = smBloomTime   .getNextValue();
        const float curHfWashAmt    = smHfWashAmt   .getNextValue();
        const float curPolarityAmt  = smPolarityAmt .getNextValue();
        const float curApfMod       = smApfMod      .getNextValue();

        // Pre-delay in samples (may be negative; sign selects which path is delayed)
        const float curPreDelaySmp = smPreDelay.getNextValue() * 0.001f * (float)sr;
        const float wetDelaySmp    = juce::jmax(0.f,  curPreDelaySmp);  // >= 0
        const float dryDelaySmp    = juce::jmax(0.f, -curPreDelaySmp);  // >= 0

        // ------------------------------------------------------------------
        // Update baseDelay using thread-local copies
        // ------------------------------------------------------------------
        for (int i = 0; i < fdnOrder; ++i)
            fdnLines[i].baseDelay = (localUseCustom
                ? localDelays[i]
                : (float)BASE_DELAYS[i]) * curSize;

        // ------------------------------------------------------------------
        // Read input samples
        // ------------------------------------------------------------------
        const float inL = chL[n];
        const float inR = chR ? chR[n] : inL;

        // ------------------------------------------------------------------
        // Pre-delay routing
        //
        //   Positive preDelay: push into preDelayLine, pop after wetDelaySmp.
        //     The wet signal is shifted forward in time; dry is immediate.
        //     No latency needs to be reported to the host.
        //
        //   Negative preDelay: push into dryDelayLine, pop after dryDelaySmp.
        //     The wet signal fires immediately (pre-verb "reverse explosion").
        //     The dry signal catches up after |preDelay| ms.
        //     Host latency reported = |preDelay| samples (see above).
        // ------------------------------------------------------------------
        preDelayLineL.pushSample(0, inL);
        preDelayLineR.pushSample(0, inR);
        const float fdnInL = preDelayLineL.popSample(0, wetDelaySmp);
        const float fdnInR = preDelayLineR.popSample(0, wetDelaySmp);

        dryDelayLineL.pushSample(0, inL);
        dryDelayLineR.pushSample(0, inR);
        const float dryL = dryDelayLineL.popSample(0, dryDelaySmp);
        const float dryR = (chR != nullptr)
            ? dryDelayLineR.popSample(0, dryDelaySmp)
            : dryL;

        // ------------------------------------------------------------------
        // Input diffusion (operates on pre-delayed FDN input)
        // ------------------------------------------------------------------
        float diffL = fdnInL, diffR = fdnInR;
        for (int i = 0; i < tapAmount; ++i)
        {
            diffL = inputDiffL[i].process(diffL, curPreDiff);
            diffR = inputDiffR[i].process(diffR, curPreDiff);
        }

        // Freeze gates new input: the tail loops at unity with no new energy.
        float diffMono = freeze ? 0.f : (diffL + diffR) * 0.5f;

        // ------------------------------------------------------------------
        // FDN feedback mix — reads localMatrix / localPolarity exclusively
        // ------------------------------------------------------------------
        std::array<float, NUM_LINES> mixed;
        mixed.fill(0.f);
        for (int r = 0; r < fdnOrder; ++r)
        {
            float sum = 0.f;
            for (int c = 0; c < fdnOrder; ++c)
            {
                const float pol = 1.0f + curPolarityAmt * (localPolarity[c] - 1.0f);
                sum += localMatrix[r * NUM_LINES + c] * fdnLines[c].lastOut * pol;
            }
            mixed[r] = diffMono + sum * curCompFb;
        }

        // ------------------------------------------------------------------
        // Per-line processing
        // ------------------------------------------------------------------
        for (int i = 0; i < fdnOrder; ++i)
        {
            lineOuts[i] = fdnLines[i].process(
                mixed[i],
                curLpCoeff, curHpCoeff,
                curModRate * LFO_OFFSETS[i], curModDepth,
                curTankDiff, tankStages,
                curApfMod,
                curHfCrossCoeff, curHfWashAmt,
                sr);
        }

        // ------------------------------------------------------------------
        // Stereo output summing with bloom panning.
        //
        // Bloom panning is only applied when a line has a stereo partner
        // within the active range (i XOR 1 < fdnOrder).  This prevents
        // unpaired lines (e.g. fdnOrder=1, or any odd fdnOrder) from
        // drifting entirely to one channel as bloom increases.
        // ------------------------------------------------------------------
        float outL = 0.f, outR = 0.f;
        for (int i = 0; i < fdnOrder; ++i)
        {
            const float delayTimeSec = fdnLines[i].baseDelay / (float)sr;
            float bloomFactor = 1.0f;
            if (curBloomTime > 0.001f)
                bloomFactor = juce::jlimit(0.f, 1.f, delayTimeSec / curBloomTime);

            // Only spread if the stereo partner (i^1) is also in the active set
            const bool  hasPair  = (i ^ 1) < fdnOrder;
            const float panWidth = hasPair
                ? curSpread * curBloomAmount * bloomFactor
                : 0.f;

            const bool  isLeft    = !(i & 1);
            const float leftGain  = 0.5f + (isLeft  ?  panWidth * 0.5f : -panWidth * 0.5f);
            const float rightGain = 0.5f + (!isLeft ?  panWidth * 0.5f : -panWidth * 0.5f);

            outL += lineOuts[i] * leftGain;
            outR += lineOuts[i] * rightGain;
        }
        outL *= curWetNorm;
        outR *= curWetNorm;

        // ------------------------------------------------------------------
        // Dry/wet blend — uses dryL/dryR which carry any negative-preDelay
        // compensation.  This keeps dry and wet phase-aligned.
        // ------------------------------------------------------------------
        if (wetOnly)
        {
            chL[n] = outL * curMix;
            if (chR) chR[n] = outR * curMix;
        }
        else
        {
            chL[n] = dryL * (1.f - curMix) + outL * curMix;
            if (chR) chR[n] = dryR * (1.f - curMix) + outR * curMix;
        }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AquatonAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&](const juce::String& id, const juce::String& name,
        float min, float max, float def, float skewCentre = -1.f)
    {
        auto range = juce::NormalisableRange<float>(min, max);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre(skewCentre);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, range, def));
    };

    addFloat("size",     "Size",     0.1f,  8.f,     1.f,     1.f);
    addFloat("feedback", "Feedback", 0.f,   1.2f,    0.7f);
    addFloat("tail",     "Tail",     0.f,   1.f,     0.f);
    addFloat("lpCutoff", "LP Freq",  200.f, 20000.f, 20000.f, 2000.f);
    addFloat("hpCutoff", "HP Freq",  20.f,  2000.f,  20.f,    200.f);
    addFloat("preDiffuse",  "Pre-Diffuse",  0.f, 0.95f, 0.62f);
    addFloat("tankDiffuse", "Tank-Diffuse", 0.f, 0.95f, 0.50f);

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "tankStages", "Tank Stages", 0, MAX_AP_TANK, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "tapAmount", "Tap Amount", 0, MAX_INPUT_AP, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "fdnOrder", "FDN Order", 1, NUM_LINES, 8));

    addFloat("modRate",  "Mod Rate",  0.01f, 8.f,  0.25f, 1.f);
    addFloat("modDepth", "Mod Depth", 0.f,   50.f, 4.f,   10.f);
    addFloat("apfMod",   "APF Mod",   0.f,   1.f,  0.f);
    addFloat("spread",   "Spread",    0.f,   1.f,  0.3f);
    addFloat("mix",      "Mix",       0.f,   1.f,  0.5f);
    addFloat("bloomAmount", "Bloom Amount", 0.f,  1.f,  0.f);
    addFloat("bloomTime",   "Bloom Time",   0.01f, 2.f, 0.1f, 0.3f);
    addFloat("hfWashHP",  "HF Wash HP",  200.f, 15000.f, 3000.f, 2000.f);
    addFloat("hfWashAmt", "HF Wash Amt", 0.f,   1.f,     0.f);
    addFloat("polarityAmt", "Polarity Amt", 0.f, 1.f, 0.f);

    // Pre-delay: negative = pre-verb (dry delayed, reports latency to host)
    //            positive = standard wet delay
    //            centre of knob (0.5 normalised) = 0 ms
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "preDelay", "Pre-Delay",
        juce::NormalisableRange<float>(-200.f, 200.f), 0.f));

    layout.add(std::make_unique<juce::AudioParameterBool>("wetOnly", "Wet Only", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("freeze",  "Freeze",   false));

    return layout;
}

//==============================================================================
bool AquatonAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AquatonAudioProcessor::createEditor()
{
    return new AquatonAudioProcessorEditor(*this);
}

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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AquatonAudioProcessor();
}
