/*
  ==============================================================================
    SpectralSmooth — implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralSmoothAudioProcessor::SpectralSmoothAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
}

SpectralSmoothAudioProcessor::~SpectralSmoothAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SpectralSmoothAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray fftChoices { "512", "1024", "2048", "4096", "8192", "16384" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        pid::fftSize, "FFT Size", fftChoices, 2)); // default 2048

    // Character = cepstral lifter cutoff. Low = only the coarse envelope
    // survives every hop -> smooth/blurred smear. High = closer to the
    // source's own harmonic detail -> tighter, more resonant.
    params.push_back(std::make_unique<P>(pid::character, "Character",
        Range(0.0f, 1.0f, 0.0f), 0.25f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        pid::freeze, "Freeze", false));

    // Six modes: Static / Evolving / Continuous magnitude behaviour, each in
    // a Hold variant (gain-match target freezes with the spectrum, so the
    // frozen level stays exactly where it was when freeze engaged) and a
    // Follow variant (gain-match keeps tracking the live dry signal while
    // frozen, so the frozen drone rises and falls with whatever else is
    // playing -- can go quiet if the dry signal does).
    juce::StringArray modeChoices { "Static Hold", "Evolving Hold", "Continuous Hold",
                                     "Static Follow", "Evolving Follow", "Continuous Follow",
                                     "Stretch" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        pid::freezeMode, "Freeze Mode", modeChoices, 0)); // default Static Hold

    params.push_back(std::make_unique<P>(pid::evolveRate, "Evolve Rate",
        Range(0.0f, 1.0f, 0.0f), 0.05f));

    params.push_back(std::make_unique<P>(pid::diffusion, "Diffusion",
        Range(0.0f, 1.0f, 0.0f), 0.3f));

    // Stretch mode's grain period, in seconds. Skewed so the lower end of
    // the range (short, choppier grains) has more turn to it than the top.
    Range stretchTimeRange(0.25f, 4.0f, 0.0f);
    stretchTimeRange.setSkewForCentre(1.0f);
    params.push_back(std::make_unique<P>(pid::stretchTime, "Stretch Time",
        stretchTimeRange, 1.0f));

    params.push_back(std::make_unique<P>(pid::mix, "Mix",
        Range(0.0f, 1.0f, 0.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        pid::gainMatch, "Gain Match", true));

    return { params.begin(), params.end() };
}

//==============================================================================
bool SpectralSmoothAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void SpectralSmoothAudioProcessor::prepareToPlay(double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    dryScratch.setSize(2, samplesPerBlock);
    mixSmoothed.reset(sampleRate, 0.02);
    matchGainSmoothed.reset(sampleRate, 0.05);
    matchGainSmoothed.setCurrentAndTargetValue(1.0f);

    rebuildEngine();
}

void SpectralSmoothAudioProcessor::releaseResources() {}

//==============================================================================
void SpectralSmoothAudioProcessor::rebuildEngine()
{
    static const int sizes[] = { 512, 1024, 2048, 4096, 8192, 16384 };
    const int index = juce::jlimit(0, 5, (int) apvts.getRawParameterValue(pid::fftSize)->load());
    currentFftSize = sizes[index];
    currentHopSize = currentFftSize / 4; // fixed 75% overlap

    int order = 0;
    for (int n = currentFftSize; n > 1; n >>= 1) ++order;
    fft = std::make_unique<juce::dsp::FFT>(order);

    window.resize((size_t) currentFftSize);
    for (int i = 0; i < currentFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
            * (float) i / (float) (currentFftSize - 1));

    const int half = currentFftSize / 2;
    for (auto& cs : channels)
    {
        cs.inputQueue.clear();
        cs.outputQueue.clear();
        cs.analysisFrame.assign((size_t) currentFftSize, 0.0f);
        cs.outAccum.assign((size_t) currentFftSize, 0.0f);
        cs.previousPhase.assign((size_t) half + 1, 0.0f);
        cs.synthesisPhase.assign((size_t) half + 1, 0.0f);
        cs.heldMag.assign((size_t) half + 1, 0.0f);
        cs.holdIncrement.assign((size_t) half + 1, 0.0f);
        cs.grainLogMagA.assign((size_t) half + 1, 0.0f);
        cs.grainLogMagB.assign((size_t) half + 1, 0.0f);
        cs.grainIncrementA.assign((size_t) half + 1, 0.0f);
        cs.grainIncrementB.assign((size_t) half + 1, 0.0f);
        cs.grainHopCounter = 0;
        cs.wasFrozen = false;
        cs.holdInitialised = false;
    }

    // report latency: one full analysis frame is needed before output is meaningful
    setLatencySamples(currentFftSize);
}

//==============================================================================
// Cepstral lifter: smooths a log-magnitude spectrum by keeping only its low
// quefrency cepstral coefficients (real cepstrum -> lifter -> back to log
// magnitude). Same construction as IRForge's IRBuilder, applied per hop here
// instead of once offline.
//==============================================================================
void SpectralSmoothAudioProcessor::cepstralSmoothMagnitude(const std::vector<float>& logMag,
                                                            std::vector<float>& outSmoothedLogMag,
                                                            juce::dsp::FFT& fftEngine, int fftSize,
                                                            float characterNorm)
{
    const int half = fftSize / 2;
    std::vector<juce::dsp::Complex<float>> buf((size_t) fftSize), tmp((size_t) fftSize);

    for (int i = 0; i <= half; ++i)          buf[(size_t) i] = { logMag[(size_t) i], 0.0f };
    for (int i = half + 1; i < fftSize; ++i) buf[(size_t) i] = { logMag[(size_t)(fftSize - i)], 0.0f };
    fftEngine.perform(buf.data(), tmp.data(), true); // inverse -> real cepstrum

    std::vector<float> cep((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) cep[(size_t) i] = tmp[(size_t) i].real();

    std::vector<float> lift((size_t) fftSize, 0.0f);
    const int minK = 2, maxK = half;
    const int k = juce::jlimit(minK, maxK,
        (int) juce::jmap(characterNorm, 0.0f, 1.0f, (float) minK, (float) maxK));

    lift[0] = cep[0];
    for (int i = 1; i < k; ++i)
        lift[(size_t) i] = cep[(size_t) i] * 2.0f;
    if (k >= half) lift[(size_t) half] = cep[(size_t) half];

    for (int i = 0; i < fftSize; ++i) buf[(size_t) i] = { lift[(size_t) i], 0.0f };
    fftEngine.perform(buf.data(), tmp.data(), false); // forward -> smoothed log spectrum

    outSmoothedLogMag.resize((size_t) half + 1);
    for (int i = 0; i <= half; ++i)
        outSmoothedLogMag[(size_t) i] = tmp[(size_t) i].real(); // real part only; phase discarded
}

//==============================================================================
void SpectralSmoothAudioProcessor::processHop(ChannelState& cs)
{
    const int fftSize = currentFftSize;
    const int hop = currentHopSize;
    const int half = fftSize / 2;

    // pull `hop` new input samples and slide the analysis window
    std::rotate(cs.analysisFrame.begin(), cs.analysisFrame.begin() + hop, cs.analysisFrame.end());
    for (int i = 0; i < hop; ++i)
    {
        cs.analysisFrame[(size_t)(fftSize - hop + i)] = cs.inputQueue.front();
        cs.inputQueue.pop_front();
    }

    // windowed FFT
    std::vector<juce::dsp::Complex<float>> buf((size_t) fftSize), tmp((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        buf[(size_t) i] = { cs.analysisFrame[(size_t) i] * window[(size_t) i], 0.0f };
    fft->perform(buf.data(), tmp.data(), false);

    std::vector<float> mag((size_t) half + 1), phase((size_t) half + 1), logMag((size_t) half + 1);
    for (int i = 0; i <= half; ++i)
    {
        const auto& c = tmp[(size_t) i];
        mag[(size_t) i] = std::abs(c);
        phase[(size_t) i] = std::arg(c);
        logMag[(size_t) i] = std::log(std::max(1.0e-8f, mag[(size_t) i]));
    }

    std::vector<float> smoothedLogMag;
    cepstralSmoothMagnitude(logMag, smoothedLogMag, *fft, fftSize,
        apvts.getRawParameterValue(pid::character)->load());

    const bool freeze = apvts.getRawParameterValue(pid::freeze)->load() > 0.5f;
    const auto mode = (FreezeMode) (int) apvts.getRawParameterValue(pid::freezeMode)->load();
    const int modeBase = freezeModeBase(mode); // 0 = Static, 1 = Evolving, 2 = Continuous
    const float evolveRate = apvts.getRawParameterValue(pid::evolveRate)->load();
    const float diffusion = apvts.getRawParameterValue(pid::diffusion)->load();

    const bool justFroze = freeze && !cs.wasFrozen;
    const bool isStretch = (mode == FreezeMode::Stretch);
    const float binAngularStep = juce::MathConstants<float>::twoPi / (float) fftSize;

    // Instantaneous per-bin frequency estimate for this hop, derived the
    // same way the original justFroze capture did it: how far the phase
    // moved beyond what the bin's raw centre frequency alone predicts.
    // Computed once per hop up front so both the freeze-edge capture and
    // Stretch's periodic grain re-snapshots can reuse it.
    std::vector<float> instIncrement((size_t) half + 1);
    for (int i = 0; i <= half; ++i)
    {
        float delta = phase[(size_t) i] - cs.previousPhase[(size_t) i] - binAngularStep * i * hop;
        delta = std::remainder(delta, juce::MathConstants<float>::twoPi);
        instIncrement[(size_t) i] = binAngularStep * i * hop + delta;
    }

    // --- Stretch grain lifecycle (once per hop, not per bin) ---------------
    // Continuous crossfade the whole period, not just the tail: A is the
    // grain the period started on, B is the grain it's gliding toward.
    // grainCrossfadeFrac sweeps 0 -> 1 across the *entire* period (eased
    // with a raised-cosine so the handoff at the loop point has zero slope
    // in both directions), so magnitude and phase increment are always
    // audibly in motion -- no static plateau, no snap. At the period
    // boundary B becomes A and a fresh B is captured live in the same hop,
    // right where the previous blend left off, so the seam is continuous.
    bool grainCrossfading = false;
    float grainCrossfadeFrac = 0.0f;

    if (freeze && isStretch)
    {
        const float stretchTimeSec = apvts.getRawParameterValue(pid::stretchTime)->load();
        const int periodHops = juce::jmax(2, (int) std::round(stretchTimeSec * (float) sampleRate / (float) hop));

        if (justFroze)
        {
            // Nothing to glide toward yet -- both ends of the blend start
            // out identical, so the first period plays static until the
            // first real target grain lands at its far end. Every period
            // after this one crossfades the whole way through.
            cs.grainLogMagA = smoothedLogMag;
            cs.grainIncrementA = instIncrement;
            cs.grainLogMagB = smoothedLogMag;
            cs.grainIncrementB = instIncrement;
            cs.grainHopCounter = 0;
        }
        else
        {
            ++cs.grainHopCounter;

            if (cs.grainHopCounter >= periodHops)
            {
                cs.grainLogMagA = cs.grainLogMagB;
                cs.grainIncrementA = cs.grainIncrementB;
                cs.grainLogMagB = smoothedLogMag;
                cs.grainIncrementB = instIncrement;
                cs.grainHopCounter = 0;
            }
        }

        grainCrossfading = true;
        const float rawFrac = juce::jlimit(0.0f, 1.0f, (float) cs.grainHopCounter / (float) periodHops);
        grainCrossfadeFrac = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * rawFrac);
    }

    std::vector<float> outMag((size_t) half + 1), outPhase((size_t) half + 1);

    for (int i = 0; i <= half; ++i)
    {
        const float liveMag = std::exp(smoothedLogMag[(size_t) i]);

        if (justFroze)
        {
            // capture the instantaneous per-bin frequency estimate right at
            // the freeze edge, so the held drone matches the pitch playing
            // at that instant rather than the bin's raw centre frequency.
            cs.holdIncrement[(size_t) i] = instIncrement[(size_t) i];
            cs.heldMag[(size_t) i] = liveMag;
            cs.synthesisPhase[(size_t) i] = phase[(size_t) i];
            cs.holdInitialised = true;
        }

        if (!freeze)
        {
            outMag[(size_t) i] = liveMag;
            outPhase[(size_t) i] = phase[(size_t) i]; // 1:1 rate -> analysis phase is correct as-is
            cs.synthesisPhase[(size_t) i] = phase[(size_t) i];
        }
        else if (isStretch)
        {
            const float lerpLogMag = grainCrossfading
                ? cs.grainLogMagA[(size_t) i] + (cs.grainLogMagB[(size_t) i] - cs.grainLogMagA[(size_t) i]) * grainCrossfadeFrac
                : cs.grainLogMagA[(size_t) i];
            const float lerpIncrement = grainCrossfading
                ? cs.grainIncrementA[(size_t) i] + (cs.grainIncrementB[(size_t) i] - cs.grainIncrementA[(size_t) i]) * grainCrossfadeFrac
                : cs.grainIncrementA[(size_t) i];

            outMag[(size_t) i] = std::exp(lerpLogMag);
            cs.synthesisPhase[(size_t) i] += lerpIncrement;
            outPhase[(size_t) i] = cs.synthesisPhase[(size_t) i];
        }
        else
        {
            switch (modeBase)
            {
                case 0: // Static (Hold or Follow)
                    outMag[(size_t) i] = cs.heldMag[(size_t) i];
                    cs.synthesisPhase[(size_t) i] += cs.holdIncrement[(size_t) i];
                    outPhase[(size_t) i] = cs.synthesisPhase[(size_t) i];
                    break;

                case 1: // Evolving (Hold or Follow)
                    cs.heldMag[(size_t) i] += (liveMag - cs.heldMag[(size_t) i]) * evolveRate;
                    outMag[(size_t) i] = cs.heldMag[(size_t) i];
                    cs.synthesisPhase[(size_t) i] += cs.holdIncrement[(size_t) i];
                    outPhase[(size_t) i] = cs.synthesisPhase[(size_t) i];
                    break;

                case 2: // Continuous (Hold or Follow)
                default:
                    outMag[(size_t) i] = liveMag; // never held, always fresh
                    {
                        const float jitter = (cs.rng.nextFloat() - 0.5f)
                            * juce::MathConstants<float>::twoPi * diffusion;
                        cs.synthesisPhase[(size_t) i] += cs.holdIncrement[(size_t) i] + jitter;
                    }
                    outPhase[(size_t) i] = cs.synthesisPhase[(size_t) i];
                    break;
            }
        }
    }

    cs.previousPhase = phase;
    cs.wasFrozen = freeze;

    // resynthesise
    for (int i = 0; i <= half; ++i)
        buf[(size_t) i] = std::polar(outMag[(size_t) i], outPhase[(size_t) i]);
    for (int i = half + 1; i < fftSize; ++i)
        buf[(size_t) i] = std::conj(buf[(size_t)(fftSize - i)]); // enforce real output

    fft->perform(buf.data(), tmp.data(), true);

    // Hann-Hann 75% overlap COLA normalisation constant
    static const float colaNorm = 1.0f / 1.5f; // sum of overlapped Hann^2 at 4x overlap == 1.5

    for (int i = 0; i < fftSize; ++i)
        cs.outAccum[(size_t) i] += tmp[(size_t) i].real() * window[(size_t) i] * colaNorm;

    for (int i = 0; i < hop; ++i)
        cs.outputQueue.push_back(cs.outAccum[(size_t) i]);

    std::rotate(cs.outAccum.begin(), cs.outAccum.begin() + hop, cs.outAccum.end());
    std::fill(cs.outAccum.end() - hop, cs.outAccum.end(), 0.0f);
}

//==============================================================================
void SpectralSmoothAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    // pick up an FFT size change safely, between blocks only
    static const int sizes[] = { 512, 1024, 2048, 4096, 8192, 16384 };
    const int wantedIndex = (int) apvts.getRawParameterValue(pid::fftSize)->load();
    if (sizes[juce::jlimit(0, 5, wantedIndex)] != currentFftSize)
        rebuildEngine();

    if (dryScratch.getNumSamples() < numSamples || dryScratch.getNumChannels() < numCh)
        dryScratch.setSize(juce::jmax(2, numCh), numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    for (int ch = 0; ch < juce::jmin(numCh, 2); ++ch)
    {
        auto& cs = channels[(size_t) ch];
        auto* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            cs.inputQueue.push_back(data[i]);
            if ((int) cs.inputQueue.size() >= currentHopSize)
                processHop(cs);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (!cs.outputQueue.empty())
            {
                data[i] = cs.outputQueue.front();
                cs.outputQueue.pop_front();
            }
            else
            {
                data[i] = 0.0f; // still filling the initial latency window
            }
        }
    }

    const bool freeze = apvts.getRawParameterValue(pid::freeze)->load() > 0.5f;
    const auto mode = (FreezeMode) (int) apvts.getRawParameterValue(pid::freezeMode)->load();
    const bool follow = freezeModeFollows(mode);

    // simple RMS gain-match between dry and wet, smoothed.
    // Hold modes: once frozen, the target must stop tracking the dry input.
    // The dry input keeps changing (or goes silent) independently of the
    // held wet output, so recomputing the match against it here was the
    // cause of the "goes quiet, then blasts on resume" bug -- the target
    // chased the dry input's RMS even though that input had nothing to do
    // with the frozen sound anymore. Holding the smoother's target steady
    // while frozen keeps the frozen level exactly where it was when freeze
    // engaged.
    // Follow modes: intentionally keep recomputing the target even while
    // frozen, so the frozen drone rises and falls with whatever else is in
    // the room -- this can dip quiet if the dry signal does, by design.
    const bool match = apvts.getRawParameterValue(pid::gainMatch)->load() > 0.5f;
    const bool shouldTrackGain = match && (!freeze || follow);
    if (shouldTrackGain)
    {
        double dryEnergy = 0.0, wetEnergy = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* d = dryScratch.getReadPointer(ch);
            const auto* w = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) { dryEnergy += (double) d[i] * d[i]; wetEnergy += (double) w[i] * w[i]; }
        }
        const float dryRms = (float) std::sqrt(dryEnergy / juce::jmax(1, numSamples * numCh));
        const float wetRms = (float) std::sqrt(wetEnergy / juce::jmax(1, numSamples * numCh));
        const float targetMatch = wetRms > 1.0e-6f ? juce::jlimit(0.1f, 8.0f, dryRms / wetRms) : 1.0f;
        matchGainSmoothed.setTargetValue(targetMatch);
    }
    // else: leave the target untouched -- the smoother holds its last value.

    mixSmoothed.setTargetValue(apvts.getRawParameterValue(pid::mix)->load());

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        const float mg = matchGainSmoothed.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = dryScratch.getReadPointer(ch)[i];
            const float wet = buffer.getReadPointer(ch)[i] * mg;
            buffer.getWritePointer(ch)[i] = dry * (1.0f - m) + wet * m;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* SpectralSmoothAudioProcessor::createEditor()
{
    return new SpectralSmoothAudioProcessorEditor(*this);
}

void SpectralSmoothAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SpectralSmoothAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralSmoothAudioProcessor();
}
