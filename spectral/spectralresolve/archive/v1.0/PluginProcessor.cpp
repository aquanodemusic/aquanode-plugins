#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <complex>

constexpr int   SpectralResolverProcessor::FFT_ORDERS[];
constexpr int   SpectralResolverProcessor::HOP_DENOMS[];
constexpr int   SpectralResolverProcessor::DECIM_FACTORS[];
constexpr float SpectralResolverProcessor::BW8_Q[];

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SpectralResolverProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "FREQ_LOW",  "Freq Low",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.35f), 20.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "FREQ_HIGH", "Freq High",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.35f), 500.f));

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        "FFT_SIZE", "FFT Size",
        juce::StringArray { "1024","2048","4096","8192","16384" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        "HOP_RATIO", "Hop Ratio",
        juce::StringArray { "1/2","1/4","1/8","1/16","1/32","1/64","1/128","1/256" }, 5));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        "WINDOW", "Window",
        juce::StringArray { "Hann","Blackman-Harris","Nuttall","Kaiser" }, 3));

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        "DECIM", "Decimation",
        juce::StringArray { "Off","/2","/4","/8","/16" }, 2));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "THRESHOLD", "Threshold",
        juce::NormalisableRange<float>(-60.f, -5.f, 0.5f), -60.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "MAX_REASSIGN", "Max Reassign",
        juce::NormalisableRange<float>(0.1f, 8.f, 0.05f), 1.0f));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "SCROLL_SPEED", "Scroll Speed",
        juce::NormalisableRange<float>(1.f, 16.f, 1.f, 0.5f), 1.f));

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        "INTERPOLATE", "Interpolation",
        juce::StringArray { "Off","On" }, 1));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "BG_R", "BG Red",   juce::NormalisableRange<float>(0.f,255.f,1.f), 30.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "BG_G", "BG Green", juce::NormalisableRange<float>(0.f,255.f,1.f), 30.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "BG_B", "BG Blue",  juce::NormalisableRange<float>(0.f,255.f,1.f), 30.f));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "GRAD_LOW_R", "Grad Low Red",   juce::NormalisableRange<float>(0.f,255.f,1.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "GRAD_LOW_G", "Grad Low Green", juce::NormalisableRange<float>(0.f,255.f,1.f), 90.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "GRAD_LOW_B", "Grad Low Blue",  juce::NormalisableRange<float>(0.f,255.f,1.f), 120.f));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "GRAD_HIGH_R", "Grad High Red",   juce::NormalisableRange<float>(0.f,255.f,1.f), 0.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "GRAD_HIGH_G", "Grad High Green", juce::NormalisableRange<float>(0.f,255.f,1.f), 255.f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "GRAD_HIGH_B", "Grad High Blue",  juce::NormalisableRange<float>(0.f,255.f,1.f), 90.f));

    return { p.begin(), p.end() };
}

//==============================================================================
SpectralResolverProcessor::SpectralResolverProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", createParams())
{
    frameQueue.resize   (QUEUE_CAPACITY);
    scFrameQueue.resize (QUEUE_CAPACITY);
}

//==============================================================================
bool SpectralResolverProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::stereo() &&
        mainOut != juce::AudioChannelSet::mono())
        return false;

    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    auto sc = layouts.getChannelSet (true, 1);
    if (!sc.isDisabled() &&
        sc != juce::AudioChannelSet::stereo() &&
        sc != juce::AudioChannelSet::mono())
        return false;

    return true;
}

//==============================================================================
void SpectralResolverProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SpectralResolverProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
void SpectralResolverProcessor::rebuildDecimator (int factor)
{
    currentDecimFactor  = factor;
    effectiveSampleRate = currentSampleRate / (double)factor;
    decimPhase          = 0;
    scDecimPhase        = 0;

    for (auto& b : decimFilter)   b.reset();
    for (auto& b : scDecimFilter) b.reset();

    if (factor > 1)
    {
        const double fc = (currentSampleRate / (2.0 * factor)) * 0.90;
        for (int i = 0; i < DECIM_STAGES; ++i)
        {
            decimFilter  [i].design (fc, currentSampleRate, (double)BW8_Q[i]);
            scDecimFilter[i].design (fc, currentSampleRate, (double)BW8_Q[i]);
        }
    }
}

//==============================================================================
// Modified Bessel function I0(x) — Abramowitz & Stegun approximation, error < 1.6e-7
// Used for Kaiser window computation.
static float besselI0 (float x) noexcept
{
    if (x < 3.75f)
    {
        const float t = x / 3.75f, t2 = t * t;
        return 1.f + t2 * (3.5156229f + t2 * (3.0899424f + t2 * (1.2067492f
             + t2 * (0.2659732f + t2 * (0.0360768f + t2 * 0.0045813f)))));
    }
    const float t = 3.75f / x;
    return (std::exp (x) / std::sqrt (x))
         * (0.39894228f + t * (0.01328592f + t * (0.00225319f
         + t * (-0.00157565f + t * (0.00916281f + t * (-0.02057706f
         + t * (0.02635537f + t * (-0.01647633f + t * 0.00392377f))))))));;
}

void SpectralResolverProcessor::rebuildFFT (int order, int hopDenom, int windowChoice)
{
    currentFFTOrder = order;
    currentFFTSize  = 1 << order;
    currentHopSize  = currentFFTSize / hopDenom;

    fft = std::make_unique<juce::dsp::FFT> (order);

    const int ringSize = currentFFTSize * 2;
    ringMask = ringSize - 1;

    // Main signal buffers
    ringBuf.assign    (ringSize, 0.f);
    ringWrite  = 0;
    hopCounter = 0;
    workFrame.resize  (currentFFTSize);
    workBufH.assign   (currentFFTSize * 2, 0.f);
    workBufDH.assign  (currentFFTSize * 2, 0.f);
    workBufTH.assign  (currentFFTSize * 2, 0.f);

    // Sidechain signal buffers
    scRingBuf.assign    (ringSize, 0.f);
    scRingWrite  = 0;
    scHopCounter = 0;
    scWorkFrame.resize  (currentFFTSize);
    scWorkBufH.assign   (currentFFTSize * 2, 0.f);
    scWorkBufDH.assign  (currentFFTSize * 2, 0.f);
    scWorkBufTH.assign  (currentFFTSize * 2, 0.f);

    // Window coefficients
    winH .resize (currentFFTSize);
    winDH.resize (currentFFTSize);
    winTH.resize (currentFFTSize);

    const int    half   = currentFFTSize / 2;
    const int    N      = currentFFTSize;
    const double twoPi  = juce::MathConstants<double>::twoPi;
    const double pi     = juce::MathConstants<double>::pi;

    // ── Constants for cosine-sum windows ─────────────────────────────────────
    // Blackman-Harris 4-term (Harris 1978, ~92 dB sidelobes)
    constexpr double BH_a0 = 0.35875, BH_a1 = 0.48829,
                     BH_a2 = 0.14128, BH_a3 = 0.01168;
    // Nuttall 4-term minimum sidelobe (Nuttall 1981, ~98 dB sidelobes)
    constexpr double NT_a0 = 0.3635819, NT_a1 = 0.4891775,
                     NT_a2 = 0.1365995, NT_a3 = 0.0106411;
    // Kaiser β — controls sidelobe level vs. main-lobe width trade-off.
    // β = 9 ≈ 120 dB sidelobe attenuation, excellent for high-resolution analysis.
    constexpr float KAISER_BETA = 9.0f;
    const float     kaiserInvI0 = (windowChoice == 3) ? 1.f / besselI0 (KAISER_BETA) : 0.f;

    // ── Pass 1: compute winH ──────────────────────────────────────────────────
    for (int n = 0; n < N; ++n)
    {
        const double ph = twoPi * n / N;
        switch (windowChoice)
        {
            case 0:  // Hann
                winH[n] = float (0.5 * (1.0 - std::cos (ph)));
                break;
            case 1:  // Blackman-Harris
                winH[n] = float (BH_a0 - BH_a1 * std::cos (ph)
                               + BH_a2 * std::cos (2.0 * ph)
                               - BH_a3 * std::cos (3.0 * ph));
                break;
            case 2:  // Nuttall
                winH[n] = float (NT_a0 - NT_a1 * std::cos (ph)
                               + NT_a2 * std::cos (2.0 * ph)
                               - NT_a3 * std::cos (3.0 * ph));
                break;
            default: // Kaiser
            {
                const float x = 2.f * float (n) / float (N - 1) - 1.f;  // ∈ [-1, 1]
                winH[n] = besselI0 (KAISER_BETA * std::sqrt (1.f - x * x)) * kaiserInvI0;
                break;
            }
        }
    }

    // ── Pass 2: compute winDH ──────────────────────────────────────────────────
    if (windowChoice == 3)
    {
        // Kaiser has no simple closed-form derivative — use central differences.
        // Error is O(h²) which is negligible for N ≥ 1024.
        winDH[0]     = winH[1]     - winH[0];
        winDH[N - 1] = winH[N - 1] - winH[N - 2];
        for (int n = 1; n < N - 1; ++n)
            winDH[n] = (winH[n + 1] - winH[n - 1]) * 0.5f;
    }
    else
    {
        // Analytical derivatives for cosine-sum windows (exact).
        for (int n = 0; n < N; ++n)
        {
            const double ph = twoPi * n / N;
            switch (windowChoice)
            {
                case 0:  // Hann  dh/dn = (π/N) sin(2πn/N)
                    winDH[n] = float ((pi / N) * std::sin (ph));
                    break;
                case 1:  // Blackman-Harris
                    winDH[n] = float ((twoPi / N)
                             * ( BH_a1 * std::sin (ph)
                               - 2.0 * BH_a2 * std::sin (2.0 * ph)
                               + 3.0 * BH_a3 * std::sin (3.0 * ph)));
                    break;
                case 2:  // Nuttall
                    winDH[n] = float ((twoPi / N)
                             * ( NT_a1 * std::sin (ph)
                               - 2.0 * NT_a2 * std::sin (2.0 * ph)
                               + 3.0 * NT_a3 * std::sin (3.0 * ph)));
                    break;
            }
        }
    }

    // ── Pass 3: time-ramp window  winTH[n] = (n − N/2) · h[n] ───────────────
    // Re(cTH/cH) = 0 means the bin's energy sits exactly at the frame midpoint.
    for (int n = 0; n < N; ++n)
        winTH[n] = float (n - half) * winH[n];
}

//==============================================================================
void SpectralResolverProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    const int fftChoice    = (int)*apvts.getRawParameterValue ("FFT_SIZE");
    const int hopChoice    = (int)*apvts.getRawParameterValue ("HOP_RATIO");
    const int windowChoice = (int)*apvts.getRawParameterValue ("WINDOW");
    const int decimChoice  = (int)*apvts.getRawParameterValue ("DECIM");

    lastFFTChoice    = fftChoice;
    lastHopChoice    = hopChoice;
    lastWindowChoice = windowChoice;
    lastDecimChoice  = decimChoice;

    rebuildDecimator (DECIM_FACTORS[decimChoice]);
    rebuildFFT (FFT_ORDERS[fftChoice], HOP_DENOMS[hopChoice], windowChoice);
}

//==============================================================================
void SpectralResolverProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    const int fftChoice    = (int)*apvts.getRawParameterValue ("FFT_SIZE");
    const int hopChoice    = (int)*apvts.getRawParameterValue ("HOP_RATIO");
    const int windowChoice = (int)*apvts.getRawParameterValue ("WINDOW");
    const int decimChoice  = (int)*apvts.getRawParameterValue ("DECIM");

    const bool decimChanged = (decimChoice  != lastDecimChoice);
    const bool fftChanged   = (fftChoice    != lastFFTChoice    ||
                                hopChoice    != lastHopChoice    ||
                                windowChoice != lastWindowChoice);

    if (decimChanged)
    {
        lastDecimChoice = decimChoice;
        rebuildDecimator (DECIM_FACTORS[decimChoice]);
    }
    if (fftChanged || decimChanged)
    {
        lastFFTChoice    = fftChoice;
        lastHopChoice    = hopChoice;
        lastWindowChoice = windowChoice;
        rebuildFFT (FFT_ORDERS[fftChoice], HOP_DENOMS[hopChoice], windowChoice);
    }

    // ── Main signal ────────────────────────────────────────────────────────
    auto mainBuf   = getBusBuffer (buffer, true, 0);
    const int nCh  = mainBuf.getNumChannels();
    const int nSmp = mainBuf.getNumSamples();

    for (int s = 0; s < nSmp; ++s)
    {
        float mono = 0.f;
        for (int ch = 0; ch < nCh; ++ch)
            mono += mainBuf.getSample (ch, s);
        mono /= float(nCh);

        if (currentDecimFactor > 1)
        {
            for (int i = 0; i < DECIM_STAGES; ++i)
                mono = decimFilter[i].process (mono);
            if (++decimPhase < currentDecimFactor) continue;
            decimPhase = 0;
        }

        ringBuf[ringWrite] = mono;
        ringWrite = (ringWrite + 1) & ringMask;

        if (++hopCounter >= currentHopSize)
        {
            hopCounter = 0;
            for (int i = 0; i < currentFFTSize; ++i)
                workFrame[i] = ringBuf[(ringWrite - currentFFTSize + i) & ringMask];
            processWindow (workFrame.data());
        }
    }

    // ── Sidechain signal ───────────────────────────────────────────────────
    auto* scBus = getBus (true, 1);
    if (scBus != nullptr && scBus->isEnabled())
    {
        auto scBuf  = getBusBuffer (buffer, true, 1);
        const int scCh = scBuf.getNumChannels();

        // Skip if sidechain is silent (not connected / no signal)
        float scMag = 0.f;
        for (int ch = 0; ch < scCh; ++ch)
            scMag += scBuf.getMagnitude (ch, 0, scBuf.getNumSamples());

        if (scMag > 1e-6f)
        {
            for (int s = 0; s < nSmp; ++s)
            {
                float scMono = 0.f;
                for (int ch = 0; ch < scCh; ++ch)
                    scMono += scBuf.getSample (ch, s);
                scMono /= float(scCh);

                if (currentDecimFactor > 1)
                {
                    for (int i = 0; i < DECIM_STAGES; ++i)
                        scMono = scDecimFilter[i].process (scMono);
                    if (++scDecimPhase < currentDecimFactor) continue;
                    scDecimPhase = 0;
                }

                scRingBuf[scRingWrite] = scMono;
                scRingWrite = (scRingWrite + 1) & ringMask;

                if (++scHopCounter >= currentHopSize)
                {
                    scHopCounter = 0;
                    for (int i = 0; i < currentFFTSize; ++i)
                        scWorkFrame[i] = scRingBuf[(scRingWrite - currentFFTSize + i) & ringMask];
                    processWindowSC (scWorkFrame.data());
                }
            }
        }
    }
}

//==============================================================================
// Core reassignment analysis.
//
// Frequency reassignment (always applied):
//   ω̂_k  = ω_k − Im(cDH_k / cH_k) * fs/(2π)
//
// Time reassignment (applied when computeTimeReassign = true):
//   t̂_k  = t_frame + Re(cTH_k / cH_k)          [in samples from frame centre]
//   hopOffset = t̂_k / hopSize                    [fractional hops]
//
// The time-ramp window winTH[n] = (n − N/2) · h[n] is already centred, so
// Re(cTH/cH) = 0 means the bin's energy sits exactly at the frame midpoint.
//==============================================================================
static void runReassignment (const float*               data,
                             int                        N,
                             int                        hopSize,
                             bool                       computeTimeReassign,
                             const std::vector<float>&  winH,
                             const std::vector<float>&  winDH,
                             const std::vector<float>&  winTH,
                             std::vector<float>&        workBufH,
                             std::vector<float>&        workBufDH,
                             std::vector<float>&        workBufTH,
                             juce::dsp::FFT&            fft,
                             float fs,
                             float threshOff,
                             float maxReassignBins,
                             SpectralFrame&             frame)
{
    // ── Apply windows and forward-transform ───────────────────────────────
    std::fill (workBufH .begin(), workBufH .end(), 0.f);
    std::fill (workBufDH.begin(), workBufDH.end(), 0.f);

    for (int n = 0; n < N; ++n)
    {
        workBufH [n] = data[n] * winH [n];
        workBufDH[n] = data[n] * winDH[n];
    }

    fft.performRealOnlyForwardTransform (workBufH .data(), false);
    fft.performRealOnlyForwardTransform (workBufDH.data(), false);

    std::complex<float>* cTH = nullptr;

    if (computeTimeReassign)
    {
        std::fill (workBufTH.begin(), workBufTH.end(), 0.f);
        for (int n = 0; n < N; ++n)
            workBufTH[n] = data[n] * winTH[n];
        fft.performRealOnlyForwardTransform (workBufTH.data(), false);
        cTH = reinterpret_cast<std::complex<float>*> (workBufTH.data());
    }

    auto* cH  = reinterpret_cast<std::complex<float>*> (workBufH .data());
    auto* cDH = reinterpret_cast<std::complex<float>*> (workBufDH.data());

    const float binW          = fs / float(N);
    const int   nBins         = N / 2;
    const float maxReassignHz = maxReassignBins * binW;
    // Clamp time reassignment to ±2 hops so outliers don't scatter energy wildly
    const float maxTimeHops   = 2.f;

    // ── Adaptive RMS threshold ────────────────────────────────────────────
    float sumSq = 0.f;
    for (int k = 1; k < nBins; ++k)
        sumSq += std::norm (cH[k]);
    const float rmsDB = 10.f * std::log10 (sumSq / float(nBins) + 1e-12f);
    const float thresh = std::max (-70.f, rmsDB + threshOff);

    frame.freqHz.clear();
    frame.magDB .clear();
    frame.timeOffsetHops.clear();

    frame.freqHz.reserve (nBins / 4);
    frame.magDB .reserve (nBins / 4);
    if (computeTimeReassign)
        frame.timeOffsetHops.reserve (nBins / 4);

    // ── Per-bin reassignment ──────────────────────────────────────────────
    for (int k = 1; k < nBins - 1; ++k)
    {
        const float mag   = std::abs (cH[k]);
        const float magDB = 20.f * std::log10 (mag + 1e-9f);
        if (magDB < thresh) continue;

        const auto  denom  = cH[k] + std::complex<float> (1e-9f, 0.f);

        // ── Frequency reassignment ──────────────────────────────────────
        const auto  ratioF = cDH[k] / denom;
        const float fShift = -(fs / juce::MathConstants<float>::twoPi) * ratioF.imag();
        const float fCorr  = float(k) * binW + fShift;

        if (std::abs (fShift) > maxReassignHz)  continue;
        if (fCorr < 1.f || fCorr > fs * 0.5f)  continue;

        // ── Time reassignment ────────────────────────────────────────────
        float hopOffset = 0.f;
        if (computeTimeReassign)
        {
            // Re(cTH / cH) gives the time displacement in samples
            // relative to the window's centre.  Dividing by hopSize
            // converts to fractional hops.
            const float tShiftSamples = (cTH[k] / denom).real();
            hopOffset = tShiftSamples / float(hopSize);

            if (std::abs (hopOffset) > maxTimeHops) continue;
        }

        frame.freqHz.push_back (fCorr);
        frame.magDB .push_back (magDB);
        if (computeTimeReassign)
            frame.timeOffsetHops.push_back (hopOffset);
    }
}

//==============================================================================
void SpectralResolverProcessor::processWindow (const float* data)
{
    const float fs          = float (effectiveSampleRate);
    const float threshOff   = *apvts.getRawParameterValue ("THRESHOLD");
    const float maxReassign = *apvts.getRawParameterValue ("MAX_REASSIGN");
    const bool  interp      = (int)*apvts.getRawParameterValue ("INTERPOLATE") > 0;

    SpectralFrame frame;
    runReassignment (data, currentFFTSize, currentHopSize,
                     interp,
                     winH, winDH, winTH,
                     workBufH, workBufDH, workBufTH,
                     *fft, fs, threshOff, maxReassign, frame);
    pushFrame (std::move (frame));
}

void SpectralResolverProcessor::processWindowSC (const float* data)
{
    const float fs          = float (effectiveSampleRate);
    const float threshOff   = *apvts.getRawParameterValue ("THRESHOLD");
    const float maxReassign = *apvts.getRawParameterValue ("MAX_REASSIGN");
    const bool  interp      = (int)*apvts.getRawParameterValue ("INTERPOLATE") > 0;

    SpectralFrame frame;
    runReassignment (data, currentFFTSize, currentHopSize,
                     interp,
                     winH, winDH, winTH,
                     scWorkBufH, scWorkBufDH, scWorkBufTH,
                     *fft, fs, threshOff, maxReassign, frame);
    pushSCFrame (std::move (frame));
}

//==============================================================================
void SpectralResolverProcessor::pushFrame (SpectralFrame&& f)
{
    std::lock_guard<std::mutex> lock (queueMutex);
    const int next = (qTail + 1) % QUEUE_CAPACITY;
    if (next == qHead) return;
    frameQueue[qTail] = std::move (f);
    qTail = next;
}

bool SpectralResolverProcessor::popFrame (SpectralFrame& out)
{
    std::lock_guard<std::mutex> lock (queueMutex);
    if (qHead == qTail) return false;
    out   = std::move (frameQueue[qHead]);
    qHead = (qHead + 1) % QUEUE_CAPACITY;
    return true;
}

void SpectralResolverProcessor::pushSCFrame (SpectralFrame&& f)
{
    std::lock_guard<std::mutex> lock (scQueueMutex);
    const int next = (scQTail + 1) % QUEUE_CAPACITY;
    if (next == scQHead) return;
    scFrameQueue[scQTail] = std::move (f);
    scQTail = next;
}

bool SpectralResolverProcessor::popSCFrame (SpectralFrame& out)
{
    std::lock_guard<std::mutex> lock (scQueueMutex);
    if (scQHead == scQTail) return false;
    out    = std::move (scFrameQueue[scQHead]);
    scQHead = (scQHead + 1) % QUEUE_CAPACITY;
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* SpectralResolverProcessor::createEditor()
{
    return new SpectralResolverEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralResolverProcessor();
}
