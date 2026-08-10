#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
// Parameter layout
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SpectralCompareAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Morph amount  [0 … 2]
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morph", "Morph",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));

    // Clarity / envelope width  [0.05 … 0.5]
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "clarity", "Clarity",
        juce::NormalisableRange<float>(0.05f, 0.5f, 0.005f),
        0.25f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 3); })));

    // FFT Size — choice
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fftSize", "FFT Size",
        juce::StringArray { "1024", "2048", "4096", "8192" },
        1 /* default = 2048 */));

    // Smoothing sliders  [0 … 0.99]
    auto makeSmooth = [](const juce::String& id, const juce::String& name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat>(
            id, name,
            juce::NormalisableRange<float>(0.0f, 0.99f, 0.01f),
            def);
    };
    params.push_back(makeSmooth("smoothMain",      "Smooth Main",  0.7f));
    params.push_back(makeSmooth("smoothSidechain", "Smooth Side",  0.7f));
    params.push_back(makeSmooth("smoothMorph",     "Smooth Morph", 0.7f));
    params.push_back(makeSmooth("smoothDelta",     "Smooth Delta", 0.7f));

    // Frequency view range knobs — log-scaled
    {
        juce::NormalisableRange<float> fromRange(20.0f, 19800.0f, 1.0f);
        fromRange.setSkewForCentre(1000.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "freqFrom", "Freq From",
            fromRange, 20.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("Hz")
                .withStringFromValueFunction([](float v, int) {
                    return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }
    {
        juce::NormalisableRange<float> toRange(220.0f, 22000.0f, 1.0f);
        toRange.setSkewForCentre(4000.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "freqTo", "Freq To",
            toRange, 20000.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("Hz")
                .withStringFromValueFunction([](float v, int) {
                    return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }

    return { params.begin(), params.end() };
}

//==============================================================================
SpectralCompareAudioProcessor::SpectralCompareAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Main",     juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain",juce::AudioChannelSet::stereo(), false)
        .withOutput("Output",   juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SpectralCompare", createParameterLayout())
{
    // Cache raw parameter pointers — these are valid for the lifetime of the processor.
    morphParam           = apvts.getRawParameterValue("morph");
    envelopeWidthParam   = apvts.getRawParameterValue("clarity");
    smoothMainParam      = apvts.getRawParameterValue("smoothMain");
    smoothSidechainParam = apvts.getRawParameterValue("smoothSidechain");
    smoothMorphParam     = apvts.getRawParameterValue("smoothMorph");

    // Listen for FFT size changes so we can call setFFTSize on the message thread.
    apvts.addParameterListener("fftSize", this);

    fftMain      = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSidechain = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

SpectralCompareAudioProcessor::~SpectralCompareAudioProcessor()
{
    apvts.removeParameterListener("fftSize", this);
    cancelPendingUpdate();
}

// ============================================================================
// FFT size parameter change — triggered on any thread, executed on message thread
// ============================================================================
void SpectralCompareAudioProcessor::parameterChanged(const juce::String& paramID, float newValue)
{
    if (paramID == "fftSize")
    {
        const int idx = juce::roundToInt(newValue);   // AudioParameterChoice stores index
        const int sizes[] = { 1024, 2048, 4096, 8192 };
        if (idx >= 0 && idx < 4)
        {
            pendingFFTSize.store(sizes[idx], std::memory_order_relaxed);
            triggerAsyncUpdate();
        }
    }
}

void SpectralCompareAudioProcessor::handleAsyncUpdate()
{
    // Called on the message thread — safe to call suspendProcessing here.
    const int sz = pendingFFTSize.exchange(0, std::memory_order_relaxed);
    if (sz > 0)
        setFFTSize(sz);
}

//==============================================================================
const juce::String SpectralCompareAudioProcessor::getName() const { return JucePlugin_Name; }
bool SpectralCompareAudioProcessor::acceptsMidi()  const { return false; }
bool SpectralCompareAudioProcessor::producesMidi() const { return false; }
bool SpectralCompareAudioProcessor::isMidiEffect() const { return false; }
double SpectralCompareAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    SpectralCompareAudioProcessor::getNumPrograms() { return 1; }
int    SpectralCompareAudioProcessor::getCurrentProgram() { return 0; }
void   SpectralCompareAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralCompareAudioProcessor::getProgramName(int) { return {}; }
void   SpectralCompareAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
bool SpectralCompareAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    auto sideIn = layouts.getChannelSet(true, 1);
    if (!sideIn.isDisabled() &&
        sideIn != juce::AudioChannelSet::mono() &&
        sideIn != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void SpectralCompareAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void SpectralCompareAudioProcessor::allocateBuffers()
{
    // Build the window first — olaNorm computation below depends on it.
    window.resize(fftSize);
    createWindow();

    mainFifo     .assign(fftSize,     0.0f);
    sidechainFifo.assign(fftSize,     0.0f);
    analysisFrame.assign(fftSize * 2, 0.0f);
    preFftFrame  .assign(fftSize,     0.0f);
    morphFrame   .assign(fftSize * 2, 0.0f);

    smoothedMain    .assign(numBins, 0.0f);
    smoothedSidechain.assign(numBins, 0.0f);
    smoothedMorphed .assign(numBins, 0.0f);
    instantaneousSidechain.assign(numBins, 0.0f);
    morphEnvelopeSidechain.assign(numBins, 0.0f);

    // ------------------------------------------------------------------
    // OLA circular accumulator (size = fftSize is sufficient because each
    // synthesis frame spans exactly fftSize samples and at 75% overlap
    // only 4 frames ever coexist — fitting comfortably in one fftSize buffer).
    // ------------------------------------------------------------------
    outputOLA.assign(fftSize, 0.0f);
    outputReadPos = 0;

    // ------------------------------------------------------------------
    // Precompute per-position OLA normalisation factors.
    //
    // At 75% overlap (hopSize = fftSize/4), exactly 4 Hann frames overlap
    // at every sample position n.  Their synthesis-window² contributions sum
    // to a value that is constant within each period of hopSize samples:
    //
    //   olaNorm[n % hopSize] = Σ_{k=0}^{3}  window[n%hopSize + k*hopSize]²
    //
    // Dividing the OLA output by this factor gives perfect reconstruction.
    // ------------------------------------------------------------------
    const int numFrames = fftSize / hopSize;   // = 4
    olaNorm.assign(hopSize, 0.0f);
    for (int n = 0; n < hopSize; ++n)
    {
        float s = 0.0f;
        for (int k = 0; k < numFrames; ++k)
        {
            int idx = n + k * hopSize;
            if (idx < fftSize)
                s += window[idx] * window[idx];
        }
        olaNorm[n] = (s > 1e-8f) ? s : 1.0f;
    }

    mainFifoIndex      = 0;
    sidechainFifoIndex = 0;
}

//==============================================================================
void SpectralCompareAudioProcessor::setFFTSize(int newSize)
{
    if (newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;

    int newOrder = (newSize == 1024) ? 10
                 : (newSize == 2048) ? 11
                 : (newSize == 4096) ? 12 : 13;

    suspendProcessing(true);

    fftOrder = newOrder;
    fftSize  = newSize;
    hopSize  = fftSize / 4;
    numBins  = fftSize / 2 + 1;

    fftMain      = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSidechain = std::make_unique<juce::dsp::FFT>(fftOrder);

    allocateBuffers();
    setLatencySamples(fftSize);

    suspendProcessing(false);
}

//==============================================================================
void SpectralCompareAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    allocateBuffers();
    setLatencySamples(fftSize);
}

void SpectralCompareAudioProcessor::releaseResources() {}

// Spectral envelope via log-scaled box filter using a prefix-sum table.
//
// The half-width grows linearly with bin index so that the smoothing bandwidth
// is constant in octaves (~±1/4 octave) rather than constant in Hz.  This
// matches how we perceive timbral character and prevents the old fixed-width
// filter from smearing low-frequency harmonics or under-smoothing high ones.
//
// Complexity: O(N) — one prefix-sum pass, then O(1) per bin.
//   halfW(bin) = max(kMinHalfW, int(bin * kLogFraction))
//   kLogFraction = 0.25  →  2^0.25 - 1 ≈ 0.19, so the window stays just
//   above ±1/4-octave half-bandwidth at all frequencies above ~bin 12.
//
// Double-precision prefix sum avoids float accumulation drift over 4 K bins.

static constexpr int   kMinHalfW    = 3;

static void computeLogEnvelope(const float* mags, float* env, int n, float logFraction)
{
    // prefixSum[i+1] = Σ mags[0..i]
    double prefixSum[SpectralCompareAudioProcessor::maxBins + 1];
    prefixSum[0] = 0.0;
    for (int i = 0; i < n; ++i)
        prefixSum[i + 1] = prefixSum[i] + (double)mags[i];

    for (int i = 0; i < n; ++i)
    {
        const int hw  = juce::jmax(kMinHalfW, (int)(i * logFraction));
        const int lo  = juce::jmax(0, i - hw);
        const int hi  = juce::jmin(n - 1, i + hw);
        const int cnt = hi - lo + 1;
        env[i] = (float)((prefixSum[hi + 1] - prefixSum[lo]) / (double)cnt);
    }
}

//==============================================================================
// processBlock
//
// Per-sample order (critical — must not be rearranged):
//   A. Capture dry mono input BEFORE writing output to buffer.
//   B. Read next OLA sample, normalise, clear its slot, write to buffer.
//   C. Push input samples into analysis FIFOs.
//   D. When main FIFO is full: forward FFT → display update → morph/OLA.
//   E. When sidechain FIFO is full: forward FFT → display update (analysis only).
//
// Morph = 0 shortcut (saves one full IFFT per hop):
//   Instead of FFT → identity → IFFT, we save the windowed time-domain frame
//   in preFftFrame before the forward FFT and OLA directly from it.
//   The OLA result is then window²·x / Σwindow² = x exactly.
//
// Morph > 0:
//   After the forward FFT, per-bin magnitudes are blended toward the smoothed
//   sidechain spectrum while preserving the main signal's phase.  The modified
//   spectrum is IFFT'd and overlap-added.
//==============================================================================
void SpectralCompareAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int mainNumCh  = getMainBusNumInputChannels();
    const int numSamples = buffer.getNumSamples();
    const int olaBufSize = (int)outputOLA.size();  // == fftSize

    auto sidechainBuffer = getBusBuffer(buffer, true, 1);
    const int sideNumCh  = sidechainBuffer.getNumChannels();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // ------------------------------------------------------------------
        // A. Capture dry input BEFORE we overwrite the buffer below.
        // ------------------------------------------------------------------
        float mainMono = 0.0f;
        for (int ch = 0; ch < mainNumCh; ++ch)
            mainMono += buffer.getSample(ch, sample);
        if (mainNumCh > 0) mainMono /= (float)mainNumCh;

        float sideMono = 0.0f;
        for (int ch = 0; ch < sideNumCh; ++ch)
            sideMono += sidechainBuffer.getSample(ch, sample);
        if (sideNumCh > 0) sideMono /= (float)sideNumCh;

        // ------------------------------------------------------------------
        // B. Read the next synthesised sample from the OLA circular buffer,
        //    apply per-position normalisation, clear the slot, then write
        //    to every output channel.
        //
        //    The first fftSize output samples are silence (the OLA buffer
        //    starts empty).  This is the inherent latency, pre-declared via
        //    setLatencySamples so the DAW can compensate automatically.
        // ------------------------------------------------------------------
        const int readIdx     = outputReadPos % olaBufSize;
        const int normIdx     = outputReadPos % hopSize;
        const float outSample = outputOLA[readIdx] / olaNorm[normIdx];
        outputOLA[readIdx]    = 0.0f;
        ++outputReadPos;

        for (int ch = 0; ch < mainNumCh; ++ch)
            buffer.setSample(ch, sample, outSample);

        // ------------------------------------------------------------------
        // C. Push samples into FIFOs.
        // ------------------------------------------------------------------
        if (mainFifoIndex < fftSize)
            mainFifo[mainFifoIndex] = mainMono;
        ++mainFifoIndex;

        if (sidechainFifoIndex < fftSize)
            sidechainFifo[sidechainFifoIndex] = sideMono;
        ++sidechainFifoIndex;

        // ------------------------------------------------------------------
        // D. Main FFT hop.
        // ------------------------------------------------------------------
        if (mainFifoIndex >= fftSize)
        {
            // --- D1. Window the input frame and save the time-domain copy
            //        BEFORE the FFT overwrites analysisFrame.  This is used
            //        by the morph=0 shortcut below to skip the IFFT entirely.
            for (int i = 0; i < fftSize; ++i)
            {
                const float w = window[i];
                preFftFrame[i]   = mainFifo[i] * w;   // windowed time-domain sample
                analysisFrame[i] = preFftFrame[i];     // FFT input
            }
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);

            // --- D2. Forward FFT (normalise=true → divides by N internally).
            fftMain->performRealOnlyForwardTransform(analysisFrame.data(), true);

            // --- D3. Update main display buffer and prepare morph data.
            //         We hold the lock for one tight pass: update smoothedMain,
            //         read smoothedSidechain into locals, then release.
            //         The per-bin morph arithmetic happens outside the lock.
            const float morph  = morphParam->load(std::memory_order_relaxed);
            const float normF  = 2.0f / (float)fftSize;
            const float smooth = smoothMainParam->load(std::memory_order_relaxed);

            // Temporary storage — stack alloc fine for maxBins = 4097 floats ≈ 16 kB.
            float sideMags[maxBins];   // sidechain frequency envelope for morph target

            {
                juce::ScopedLock l(displayLock);

                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re  = analysisFrame[bin * 2];
                    const float im  = analysisFrame[bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) * normF;
                    smoothedMain[bin] = smoothedMain[bin] * smooth + mag * (1.0f - smooth);
                }

                if (morph > 0.0f)
                    std::copy(morphEnvelopeSidechain.begin(),
                              morphEnvelopeSidechain.begin() + numBins,
                              sideMags);
            }

            // --- D4. OLA path — branches on morph to avoid unnecessary work.

            // OLA destination: write fftSize samples starting at the current
            // read position (the next unread output sample).
            const int writeStart = outputReadPos;

            if (morph == 0.0f)
            {
                // ---- Shortcut: skip IFFT entirely. -------------------------
                for (int i = 0; i < fftSize; ++i)
                    outputOLA[(writeStart + i) % olaBufSize] += preFftFrame[i] * window[i];

                {
                    juce::ScopedLock l(displayLock);
                    std::fill(smoothedMorphed.begin(),
                              smoothedMorphed.begin() + numBins, 0.0f);
                }
            }
            else
            {
                // ---- Envelope-transfer morph: IFFT, OLA. ----
                //
                // Classic spectral imprint technique:
                //   1. Frequency-smooth the main frame to get its spectral envelope.
                //   2. Divide each main bin by its local envelope  →  whitened main.
                //   3. Multiply by sidechain envelope              →  re-coloured output.
                //
                // This preserves the main signal's fine structure (harmonics,
                // transients) while reshaping its broad timbral character to match
                // the sidechain — controlled continuously by morphAmount.
                //
                // At morph=0 : scale = 1    (unchanged main)
                // At morph=1 : scale = sideEnv / mainEnv  (full sidechain envelope)
                //
                // Both envelopes use computeLogEnvelope() — a prefix-sum filter whose
                // half-width grows proportionally with bin index (~±1/4 octave constant-Q).
                // This matches perceptual timbral resolution far better than the old
                // fixed linear-Hz window, especially at low frequencies.

                // Extract main spectral envelope on the stack
                float mainMags[maxBins];
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2];
                    const float im = analysisFrame[bin * 2 + 1];
                    mainMags[bin]  = std::sqrt(re * re + im * im) * normF;
                }

                float mainEnv[maxBins];
                const float envWidth = envelopeWidthParam->load(std::memory_order_relaxed);
                computeLogEnvelope(mainMags, mainEnv, numBins, envWidth);
                // sideMags already holds morphEnvelopeSidechain (frequency-smoothed)

                std::copy(analysisFrame.begin(), analysisFrame.end(), morphFrame.begin());

                {
                    juce::ScopedLock l(displayLock);
                    const float smoothM = smoothMorphParam->load(std::memory_order_relaxed);

                    for (int bin = 0; bin < numBins; ++bin)
                    {
                        const float re      = morphFrame[bin * 2];
                        const float im      = morphFrame[bin * 2 + 1];
                        const float mainMag = std::sqrt(re * re + im * im) * normF;

                        float scale = 1.0f;

                        if (mainEnv[bin] > 1e-10f)
                        {
                            const float sideEnv = sideMags[bin];

                            // Dynamic pre-cap: maxRatio = maxScale^(1/morph) so that
                            // pow(capped_ratio, morph) ≤ kMaxScale always holds,
                            // regardless of the morph exponent (including morph > 1.0).
                            //
                            // At morph = 1.0 : maxRatio = 50   → +34 dB ceiling
                            // At morph = 2.0 : maxRatio ≈ 7.07 → still 7.07²=50 ceiling
                            //
                            // This lets morph beyond 1.0 exaggerate spectral contrast
                            // (peaks boosted further, troughs cut deeper) without
                            // risk of unbounded gain at any morph value.
                            static constexpr float kMaxScale = 50.0f;
                            const float maxRatio = std::pow(kMaxScale,
                                                            1.0f / juce::jmax(morph, 0.01f));
                            const float envRatio = (sideEnv > 1e-10f)
                                ? juce::jmin(sideEnv / mainEnv[bin], maxRatio)
                                : 0.0f;   // sidechain silent → silence this bin
                            scale = std::pow(envRatio, morph);
                        }
                        else
                        {
                            // Main bin is below noise floor: no envelope to normalise against.
                            // Pass it through unchanged — the near-zero magnitude means it's
                            // inaudible regardless of scale.
                            scale = 1.0f;
                        }

                        morphFrame[bin * 2]     = re * scale;
                        morphFrame[bin * 2 + 1] = im * scale;

                        const float morphedMag = mainMag * scale;
                        smoothedMorphed[bin] = smoothedMorphed[bin] * smoothM
                                             + morphedMag * (1.0f - smoothM);
                    }
                }

                // IFFT (no normalisation in JUCE's inverse — with the forward
                // normalise=true the round-trip FFT→IFFT recovers the original
                // signal, so morphFrame[i] ≈ windowed input when morph=0).
                fftMain->performRealOnlyInverseTransform(morphFrame.data());

                // Overlap-add with synthesis Hann window.
                // Accumulated: Σ window²·morphed, divided by Σwindow² on read.
                for (int i = 0; i < fftSize; ++i)
                    outputOLA[(writeStart + i) % olaBufSize] += morphFrame[i] * window[i];
            }

            // Slide main FIFO forward by one hop.
            std::copy(mainFifo.begin() + hopSize,
                      mainFifo.begin() + fftSize,
                      mainFifo.begin());
            std::fill(mainFifo.begin() + (fftSize - hopSize),
                      mainFifo.begin() + fftSize, 0.0f);
            mainFifoIndex -= hopSize;
        }

        // ------------------------------------------------------------------
        // E. Sidechain FFT hop — analysis only.
        // ------------------------------------------------------------------
        if (sidechainFifoIndex >= fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                analysisFrame[i] = sidechainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftSidechain->performRealOnlyForwardTransform(analysisFrame.data(), true);

            {
                juce::ScopedLock l(displayLock);
                const float normF  = 2.0f / (float)fftSize;
                const float smooth = smoothSidechainParam->load(std::memory_order_relaxed);
                const float smoothM = smoothMorphParam->load(std::memory_order_relaxed);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re  = analysisFrame[bin * 2];
                    const float im  = analysisFrame[bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) * normF;
                    smoothedSidechain[bin] = smoothedSidechain[bin] * smooth
                                          + mag * (1.0f - smooth);
                    instantaneousSidechain[bin] = mag;
                }

                // Build frequency-smoothed sidechain envelope for morphing.
                // Uses the same log-scaled window as the main envelope so the
                // ratio sideEnv/mainEnv is computed at a consistent resolution.
                float envTmp[maxBins];   // stack — no heap alloc in audio thread
                computeLogEnvelope(instantaneousSidechain.data(), envTmp, numBins,
                                   envelopeWidthParam->load(std::memory_order_relaxed));
                for (int bin = 0; bin < numBins; ++bin)
                    morphEnvelopeSidechain[bin] = morphEnvelopeSidechain[bin] * smoothM
                                               + envTmp[bin] * (1.0f - smoothM);
            }

            std::copy(sidechainFifo.begin() + hopSize,
                      sidechainFifo.begin() + fftSize,
                      sidechainFifo.begin());
            std::fill(sidechainFifo.begin() + (fftSize - hopSize),
                      sidechainFifo.begin() + fftSize, 0.0f);
            sidechainFifoIndex -= hopSize;
        }
    }
}

//==============================================================================
// Display data accessors  (call from message thread only)
//==============================================================================
void SpectralCompareAudioProcessor::getMainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMain.begin(), smoothedMain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getSidechainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedSidechain.begin(), smoothedSidechain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getMorphedFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMorphed.begin(), smoothedMorphed.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

//==============================================================================
// Colors
//==============================================================================
void SpectralCompareAudioProcessor::setBackgroundColor       (juce::Colour c) { backgroundColor        = c; }
void SpectralCompareAudioProcessor::setGridColor             (juce::Colour c) { gridColor              = c; }
void SpectralCompareAudioProcessor::setMainSpectrumColor     (juce::Colour c) { mainSpectrumColor      = c; }
void SpectralCompareAudioProcessor::setSidechainSpectrumColor(juce::Colour c) { sidechainSpectrumColor = c; }
void SpectralCompareAudioProcessor::setDeltaColor            (juce::Colour c) { deltaColor             = c; }
void SpectralCompareAudioProcessor::setMorphColor            (juce::Colour c) { morphColor             = c; }
void SpectralCompareAudioProcessor::setSidebarColor          (juce::Colour c) { sidebarColor           = c; }

void SpectralCompareAudioProcessor::resetColors()
{
    backgroundColor        = juce::Colour(0xffffffff);
    gridColor              = juce::Colour(0xff444444);
    mainSpectrumColor      = juce::Colour(0xff00ff00);
    sidechainSpectrumColor = juce::Colour(0xffff00ff);
    deltaColor             = juce::Colour(0xffffff00);
    morphColor             = juce::Colour(0xff00e5ff);
    sidebarColor           = juce::Colour(0xff1a1a1a);
}

//==============================================================================
// State persistence
//
// Strategy: serialise the APVTS ValueTree as-is, then bolt on color attributes
// as extra child elements (or properties) so legacy format is replaced cleanly.
//==============================================================================
void SpectralCompareAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Store colors as properties on the root ValueTree.
    state.setProperty("bgColor",        (int)backgroundColor.getARGB(),        nullptr);
    state.setProperty("gridColor",      (int)gridColor.getARGB(),              nullptr);
    state.setProperty("mainColor",      (int)mainSpectrumColor.getARGB(),      nullptr);
    state.setProperty("sidechainColor", (int)sidechainSpectrumColor.getARGB(), nullptr);
    state.setProperty("deltaColor",     (int)deltaColor.getARGB(),             nullptr);
    state.setProperty("morphColor",     (int)morphColor.getARGB(),             nullptr);
    state.setProperty("sidebarColor",   (int)sidebarColor.getARGB(),           nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpectralCompareAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml(*xml);

    // Restore APVTS parameters (handles morph, clarity, fftSize, smoothing, freq range).
    if (state.isValid())
        apvts.replaceState(state);

    // Restore colors.
    auto getCol = [&](const juce::Identifier& id, juce::uint32 def) -> juce::Colour {
        if (state.hasProperty(id))
            return juce::Colour((juce::uint32)(int)state.getProperty(id));
        return juce::Colour(def);
    };
    backgroundColor        = getCol("bgColor",        0xffffffff);
    gridColor              = getCol("gridColor",       0xff444444);
    mainSpectrumColor      = getCol("mainColor",       0xff00ff00);
    sidechainSpectrumColor = getCol("sidechainColor",  0xffff00ff);
    deltaColor             = getCol("deltaColor",      0xffffff00);
    morphColor             = getCol("morphColor",      0xff00e5ff);
    sidebarColor           = getCol("sidebarColor",    0xff1a1a1a);

    // Apply the FFT size that was just restored from state.
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("fftSize")))
    {
        const int sizes[] = { 1024, 2048, 4096, 8192 };
        const int idx = p->getIndex();
        if (idx >= 0 && idx < 4)
            setFFTSize(sizes[idx]);
    }
}

//==============================================================================
bool SpectralCompareAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SpectralCompareAudioProcessor::createEditor()
{
    return new SpectralCompareAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralCompareAudioProcessor();
}
