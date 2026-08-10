#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralFilterAudioProcessor::SpectralFilterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    filterCurveDB_write.fill(0.0f);
    filterCurveDB_read.fill(0.0f);
    linearGainCache.fill(1.0f);

    phaseCurve_write.fill(0.0f);
    phaseCurve_read.fill(0.0f);

    freqShiftCurve_write.fill(0.0f);
    freqShiftCurve_read.fill(0.0f);

    panCurve_write.fill(0.0f);
    panCurve_read.fill(0.0f);

    fftAnalysis = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    allocateBuffers();
}

SpectralFilterAudioProcessor::~SpectralFilterAudioProcessor() {}

//==============================================================================
const juce::String SpectralFilterAudioProcessor::getName() const { return JucePlugin_Name; }
bool SpectralFilterAudioProcessor::acceptsMidi()  const { return false; }
bool SpectralFilterAudioProcessor::producesMidi() const { return false; }
bool SpectralFilterAudioProcessor::isMidiEffect() const { return false; }
double SpectralFilterAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    SpectralFilterAudioProcessor::getNumPrograms() { return 1; }
int    SpectralFilterAudioProcessor::getCurrentProgram() { return 0; }
void   SpectralFilterAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralFilterAudioProcessor::getProgramName(int) { return {}; }
void   SpectralFilterAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void SpectralFilterAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (int ch = 0; ch < (int)inputFifoIndex.size(); ++ch)
    {
        inputFifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
        std::fill(inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(fftBuffer[ch].begin(), fftBuffer[ch].end(), 0.0f);
        std::fill(drySnapshot[ch].begin(), drySnapshot[ch].end(), 0.0f);
    }

    rebuildLinearGainCache();
}

void SpectralFilterAudioProcessor::releaseResources() {}

bool SpectralFilterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

//==============================================================================
void SpectralFilterAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void SpectralFilterAudioProcessor::allocateBuffers()
{
    const int numChannels = 2;

    inputFifo.resize(numChannels);
    analysisFrame.resize(numChannels);
    fftBuffer.resize(numChannels);
    outputAccum.resize(numChannels);
    drySnapshot.resize(numChannels);

    inputFifoIndex.resize(numChannels, 0);
    outputWritePos.resize(numChannels, 0);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        inputFifo[ch].resize(fftSize * 2, 0.0f);
        analysisFrame[ch].resize(fftSize, 0.0f);
        fftBuffer[ch].resize(fftSize * 2, 0.0f);
        outputAccum[ch].resize(fftSize * 4, 0.0f);
        drySnapshot[ch].resize(numBins * 2, 0.0f);
    }

    window.resize(fftSize);
    smoothedSpectrumMagnitudes.resize(numBins, 0.0f);

    createWindow();
}

//==============================================================================
void SpectralFilterAudioProcessor::rebuildLinearGainCache()
{
    for (int bin = 0; bin < numBins; ++bin)
    {
        float dB = filterCurveDB_read[bin];
        linearGainCache[bin] = (dB <= -144.0f) ? 0.0f : std::pow(10.0f, dB / 20.0f);
    }
}

//==============================================================================
// setFFTSize
//==============================================================================
void SpectralFilterAudioProcessor::setFFTSize(int newSize)
{
    if (newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;

    int newOrder = (newSize == 1024) ? 10 : (newSize == 2048) ? 11 : (newSize == 4096) ? 12 : 13;

    suspendProcessing(true);

    fftOrder = newOrder;
    fftSize = newSize;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;

    fftAnalysis = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    allocateBuffers();

    { juce::ScopedLock l(filterCurveLock);    filterCurveDB_write.fill(0.0f); }
    { juce::ScopedLock l(phaseCurveLock);     phaseCurve_write.fill(0.0f); }
    { juce::ScopedLock l(freqShiftCurveLock); freqShiftCurve_write.fill(0.0f); }
    { juce::ScopedLock l(panCurveLock);       panCurve_write.fill(0.0f); }
    curveIsDirty.store(true);   phaseIsDirty.store(true);
    freqShiftIsDirty.store(true); panIsDirty.store(true);

    for (int ch = 0; ch < (int)inputFifoIndex.size(); ++ch)
    {
        inputFifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
        std::fill(inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(fftBuffer[ch].begin(), fftBuffer[ch].end(), 0.0f);
        std::fill(drySnapshot[ch].begin(), drySnapshot[ch].end(), 0.0f);
    }

    rebuildLinearGainCache();

    autoShiftFilterAccum = 0.0f;
    autoShiftBinAccum = 0.0f;
    autoShiftBinPos = 0;
    autoShiftPhaseAccum = 0.0f;
    autoShiftFreqAccum = 0.0f;
    autoShiftPanAccum = 0.0f;

    suspendProcessing(false);
}

//==============================================================================
// Color setters
//==============================================================================
void SpectralFilterAudioProcessor::setBackgroundColor(juce::Colour c) { backgroundColor = c; }
void SpectralFilterAudioProcessor::setCurveColor(juce::Colour c) { curveColor = c; }
void SpectralFilterAudioProcessor::setGridColor(juce::Colour c) { gridColor = c; }
void SpectralFilterAudioProcessor::setSpectrumColor(juce::Colour c) { spectrumColor = c; }
void SpectralFilterAudioProcessor::setPhaseColor(juce::Colour c) { phaseColor = c; }
void SpectralFilterAudioProcessor::setFreqShiftColor(juce::Colour c) { freqShiftColor = c; }
void SpectralFilterAudioProcessor::setPanColor(juce::Colour c) { panColor = c; }

void SpectralFilterAudioProcessor::resetColors()
{
    backgroundColor = juce::Colour(0xffffffff);
    curveColor = juce::Colour(0xff55eedd);
    gridColor = juce::Colour(0xffaaaaaa);
    spectrumColor = juce::Colour(0xff336655);
    phaseColor = juce::Colour(0xffdd55bb);
    freqShiftColor = juce::Colour(0xff44ccff);
    panColor = juce::Colour(0xffffcc33);
}

//==============================================================================
// Filter curve API
//==============================================================================
void SpectralFilterAudioProcessor::setFilterCurveRange(int s, int e, float sv, float ev)
{
    if (s > e) { std::swap(s, e); std::swap(sv, ev); }
    s = juce::jlimit(0, numBins - 1, s);
    e = juce::jlimit(0, numBins - 1, e);
    {
        juce::ScopedLock l(filterCurveLock);
        int span = e - s;
        if (span == 0) { filterCurveDB_write[s] = sv; }
        else { for (int b = s; b <= e; ++b) { float t = float(b - s) / span; filterCurveDB_write[b] = sv + t * (ev - sv); } }
    }
    curveIsDirty.store(true);
}

void SpectralFilterAudioProcessor::resetFilterCurve()
{
    { juce::ScopedLock l(filterCurveLock); filterCurveDB_write.fill(0.0f); }
    curveIsDirty.store(true);
}

void SpectralFilterAudioProcessor::getFilterCurve(std::array<float, maxBins>& dest)
{
    juce::ScopedLock l(filterCurveLock);
    for (int i = 0; i < numBins; ++i) dest[i] = filterCurveDB_write[i];
}

void SpectralFilterAudioProcessor::setFilterCurveShifted(const std::array<float, maxBins>& base, int shift)
{
    {
        juce::ScopedLock l(filterCurveLock);
        int n = numBins; shift = ((shift % n) + n) % n;
        for (int i = 0; i < n; ++i) filterCurveDB_write[(i + shift) % n] = base[i];
    }
    curveIsDirty.store(true);
}

void SpectralFilterAudioProcessor::randomizeFilterCurve()
{
    juce::Random rng;
    {
        juce::ScopedLock l(filterCurveLock);
        for (int b = 0; b < numBins; b += 8)
        {
            float a = rng.nextFloat() * 48.f - 24.f, c = rng.nextFloat() * 48.f - 24.f;
            int nb = juce::jmin(b + 8, numBins - 1);
            for (int i = b; i < nb && i < numBins; ++i)
                filterCurveDB_write[i] = a + (float(i - b) / 8.f) * (c - a);
        }
    }
    curveIsDirty.store(true);
}

//==============================================================================
// Phase curve API
//==============================================================================
void SpectralFilterAudioProcessor::setPhaseCurveRange(int s, int e, float sv, float ev)
{
    if (s > e) { std::swap(s, e); std::swap(sv, ev); }
    s = juce::jlimit(0, numBins - 1, s); e = juce::jlimit(0, numBins - 1, e);
    {
        juce::ScopedLock l(phaseCurveLock);
        int span = e - s;
        if (span == 0) { phaseCurve_write[s] = sv; }
        else { for (int b = s; b <= e; ++b) { float t = float(b - s) / span; phaseCurve_write[b] = sv + t * (ev - sv); } }
    }
    phaseIsDirty.store(true);
}

void SpectralFilterAudioProcessor::resetPhaseCurve()
{
    { juce::ScopedLock l(phaseCurveLock); phaseCurve_write.fill(0.0f); }
    phaseIsDirty.store(true);
}

void SpectralFilterAudioProcessor::getPhaseCurve(std::array<float, maxBins>& dest)
{
    juce::ScopedLock l(phaseCurveLock);
    for (int i = 0; i < numBins; ++i) dest[i] = phaseCurve_write[i];
}

void SpectralFilterAudioProcessor::setPhaseCurveShifted(const std::array<float, maxBins>& base, int shift)
{
    {
        juce::ScopedLock l(phaseCurveLock);
        int n = numBins; shift = ((shift % n) + n) % n;
        for (int i = 0; i < n; ++i) phaseCurve_write[(i + shift) % n] = base[i];
    }
    phaseIsDirty.store(true);
}

void SpectralFilterAudioProcessor::randomizePhaseCurve()
{
    juce::Random rng;
    const float pi = juce::MathConstants<float>::pi;
    {
        juce::ScopedLock l(phaseCurveLock);
        for (int b = 0; b < numBins; b += 8)
        {
            float a = rng.nextFloat() * 2.f * pi - pi, c = rng.nextFloat() * 2.f * pi - pi;
            int nb = juce::jmin(b + 8, numBins - 1);
            for (int i = b; i < nb && i < numBins; ++i)
                phaseCurve_write[i] = a + (float(i - b) / 8.f) * (c - a);
        }
    }
    phaseIsDirty.store(true);
}

//==============================================================================
// Freq shift curve API
//==============================================================================
void SpectralFilterAudioProcessor::setFreqShiftCurveRange(int s, int e, float sv, float ev)
{
    if (s > e) { std::swap(s, e); std::swap(sv, ev); }
    s = juce::jlimit(0, numBins - 1, s); e = juce::jlimit(0, numBins - 1, e);
    {
        juce::ScopedLock l(freqShiftCurveLock);
        int span = e - s;
        if (span == 0) { freqShiftCurve_write[s] = sv; }
        else { for (int b = s; b <= e; ++b) { float t = float(b - s) / span; freqShiftCurve_write[b] = sv + t * (ev - sv); } }
    }
    freqShiftIsDirty.store(true);
}

void SpectralFilterAudioProcessor::resetFreqShiftCurve()
{
    { juce::ScopedLock l(freqShiftCurveLock); freqShiftCurve_write.fill(0.0f); }
    freqShiftIsDirty.store(true);
}

void SpectralFilterAudioProcessor::getFreqShiftCurve(std::array<float, maxBins>& dest)
{
    juce::ScopedLock l(freqShiftCurveLock);
    for (int i = 0; i < numBins; ++i) dest[i] = freqShiftCurve_write[i];
}

void SpectralFilterAudioProcessor::setFreqShiftCurveShifted(const std::array<float, maxBins>& base, int shift)
{
    {
        juce::ScopedLock l(freqShiftCurveLock);
        int n = numBins; shift = ((shift % n) + n) % n;
        for (int i = 0; i < n; ++i) freqShiftCurve_write[(i + shift) % n] = base[i];
    }
    freqShiftIsDirty.store(true);
}

void SpectralFilterAudioProcessor::randomizeFreqShiftCurve()
{
    juce::Random rng;
    const float range = static_cast<float>(numBins / 2);
    {
        juce::ScopedLock l(freqShiftCurveLock);
        for (int b = 0; b < numBins; b += 8)
        {
            float a = rng.nextFloat() * 2.f * range - range, c = rng.nextFloat() * 2.f * range - range;
            int nb = juce::jmin(b + 8, numBins - 1);
            for (int i = b; i < nb && i < numBins; ++i)
                freqShiftCurve_write[i] = a + (float(i - b) / 8.f) * (c - a);
        }
    }
    freqShiftIsDirty.store(true);
}

//==============================================================================
// Pan curve API
//==============================================================================
void SpectralFilterAudioProcessor::setPanCurveRange(int s, int e, float sv, float ev)
{
    if (s > e) { std::swap(s, e); std::swap(sv, ev); }
    s = juce::jlimit(0, numBins - 1, s); e = juce::jlimit(0, numBins - 1, e);
    {
        juce::ScopedLock l(panCurveLock);
        int span = e - s;
        if (span == 0) { panCurve_write[s] = sv; }
        else { for (int b = s; b <= e; ++b) { float t = float(b - s) / span; panCurve_write[b] = sv + t * (ev - sv); } }
    }
    panIsDirty.store(true);
}

void SpectralFilterAudioProcessor::resetPanCurve()
{
    { juce::ScopedLock l(panCurveLock); panCurve_write.fill(0.0f); }
    panIsDirty.store(true);
}

void SpectralFilterAudioProcessor::getPanCurve(std::array<float, maxBins>& dest)
{
    juce::ScopedLock l(panCurveLock);
    for (int i = 0; i < numBins; ++i) dest[i] = panCurve_write[i];
}

void SpectralFilterAudioProcessor::setPanCurveShifted(const std::array<float, maxBins>& base, int shift)
{
    {
        juce::ScopedLock l(panCurveLock);
        int n = numBins; shift = ((shift % n) + n) % n;
        for (int i = 0; i < n; ++i) panCurve_write[(i + shift) % n] = base[i];
    }
    panIsDirty.store(true);
}

void SpectralFilterAudioProcessor::randomizePanCurve()
{
    juce::Random rng;
    {
        juce::ScopedLock l(panCurveLock);
        for (int b = 0; b < numBins; b += 8)
        {
            float a = rng.nextFloat() * 2.f - 1.f, c = rng.nextFloat() * 2.f - 1.f;
            int nb = juce::jmin(b + 8, numBins - 1);
            for (int i = b; i < nb && i < numBins; ++i)
                panCurve_write[i] = a + (float(i - b) / 8.f) * (c - a);
        }
    }
    panIsDirty.store(true);
}

//==============================================================================
// getFFTData
//==============================================================================
void SpectralFilterAudioProcessor::getFFTData(float* out, int n)
{
    juce::ScopedLock l(fftDisplayLock);
    int use = juce::jmin(n, (int)smoothedSpectrumMagnitudes.size());
    for (int i = 0; i < use; ++i) out[i] = smoothedSpectrumMagnitudes[i];
}

//==============================================================================
// DSP pipeline
//==============================================================================

// Stage 1: refresh curve read-sides + forward FFT only
void SpectralFilterAudioProcessor::stageForwardProcess(int ch)
{
    if (ch == 0)
    {
        if (curveIsDirty.load(std::memory_order_acquire))
        {
            { juce::ScopedLock l(filterCurveLock); filterCurveDB_read = filterCurveDB_write; }
            curveIsDirty.store(false); rebuildLinearGainCache();
        }
        if (phaseIsDirty.load(std::memory_order_acquire))
        {
            { juce::ScopedLock l(phaseCurveLock); phaseCurve_read = phaseCurve_write; }
            phaseIsDirty.store(false);
        }
        if (freqShiftIsDirty.load(std::memory_order_acquire))
        {
            { juce::ScopedLock l(freqShiftCurveLock); freqShiftCurve_read = freqShiftCurve_write; }
            freqShiftIsDirty.store(false);
        }
        if (panIsDirty.load(std::memory_order_acquire))
        {
            { juce::ScopedLock l(panCurveLock); panCurve_read = panCurve_write; }
            panIsDirty.store(false);
        }
    }

    fftAnalysis->performRealOnlyForwardTransform(fftBuffer[ch].data(), true);
}

// Stage 2: per-bin freq shift only (no global shift, no pan — those come after the snapshot)
void SpectralFilterAudioProcessor::stageFreqShiftAndPan(int numActiveChannels)
{
    const int n = numBins;

    std::vector<std::vector<float>> shifted(numActiveChannels,
        std::vector<float>(n * 2, 0.0f));

    for (int ch = 0; ch < numActiveChannels; ++ch)
    {
        for (int bin = 0; bin < n; ++bin)
        {
            int perBinOff = static_cast<int>(std::round(freqShiftCurve_read[bin]));
            int dest = bin + perBinOff;
            dest = ((dest % n) + n) % n;
            shifted[ch][dest * 2] += fftBuffer[ch][bin * 2];
            shifted[ch][dest * 2 + 1] += fftBuffer[ch][bin * 2 + 1];
        }
    }

    for (int ch = 0; ch < numActiveChannels; ++ch)
        std::copy(shifted[ch].begin(), shifted[ch].end(), fftBuffer[ch].begin());
}

// Stage 3: update display from ch0 (call after stageFreqShiftAndPan)
void SpectralFilterAudioProcessor::stageUpdateDisplay()
{
    juce::ScopedLock l(fftDisplayLock);
    const float smooth = 0.7f;
    for (int bin = 0; bin < numBins; ++bin)
    {
        float re = fftBuffer[0][bin * 2], im = fftBuffer[0][bin * 2 + 1];
        float mag = std::sqrt(re * re + im * im);
        smoothedSpectrumMagnitudes[bin] =
            smoothedSpectrumMagnitudes[bin] * smooth + mag * (1.f - smooth);
    }
}

// Stage 4: IFFT + overlap-add
void SpectralFilterAudioProcessor::stageInverseAccum(int ch)
{
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer[ch].data());

    const float overlapFactor = static_cast<float>(fftSize) / static_cast<float>(hopSize);
    const float norm = 2.0f / overlapFactor;
    const int accumSize = static_cast<int>(outputAccum[ch].size());

    for (int i = 0; i < fftSize; ++i)
    {
        int idx = (outputWritePos[ch] + i) % accumSize;
        outputAccum[ch][idx] += fftBuffer[ch][i] * window[i] * norm;
    }
}

//==============================================================================
// processBlock
//==============================================================================
void SpectralFilterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalIn = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // We process per-sample, but FFT frames fire once every hopSize samples.
    // We need all channels' FFT buffers ready at the same time for pan processing,
    // so we track whether all channels have filled fftBuffer for this hop.

    const int accumSize0 = totalIn > 0 ? (int)outputAccum[0].size() : 0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        // Feed input samples into per-channel FIFOs
        for (int ch = 0; ch < totalIn; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            const int cap = (int)inputFifo[ch].size();
            if (inputFifoIndex[ch] < cap)
                inputFifo[ch][inputFifoIndex[ch]] = data[sample];
            inputFifoIndex[ch]++;
        }

        // When all channels have hopSize new samples, fire an FFT hop
        if (inputFifoIndex[0] >= fftSize)
        {
            // 1. Apply window + copy to fftBuffer per channel
            for (int ch = 0; ch < totalIn; ++ch)
            {
                for (int i = 0; i < fftSize; ++i)
                    fftBuffer[ch][i] = inputFifo[ch][i] * window[i];
                std::fill(fftBuffer[ch].begin() + fftSize, fftBuffer[ch].end(), 0.0f);
            }

            const bool doWetOnly = wetOnly.load(std::memory_order_relaxed);

            // 2. Forward FFT (also refreshes curve read-sides)
            for (int ch = 0; ch < totalIn; ++ch)
                stageForwardProcess(ch);

            // 3. Per-bin freq shift
            stageFreqShiftAndPan(totalIn);

            // 4. Snapshot post-freq-shift bins for wet-only subtraction
            if (doWetOnly)
            {
                for (int ch = 0; ch < totalIn; ++ch)
                    for (int b = 0; b < numBins; ++b)
                    {
                        drySnapshot[ch][b * 2] = fftBuffer[ch][b * 2];
                        drySnapshot[ch][b * 2 + 1] = fftBuffer[ch][b * 2 + 1];
                    }
            }

            // 5. Gain + phase per channel
            for (int ch = 0; ch < totalIn; ++ch)
            {
                for (int bin = 0; bin < numBins; ++bin)
                {
                    float g = linearGainCache[bin];
                    fftBuffer[ch][bin * 2] *= g;
                    fftBuffer[ch][bin * 2 + 1] *= g;
                }
                for (int bin = 0; bin < numBins; ++bin)
                {
                    float ph = phaseCurve_read[bin];
                    if (ph == 0.f) continue;
                    float cosP = std::cos(ph), sinP = std::sin(ph);
                    float re = fftBuffer[ch][bin * 2], im = fftBuffer[ch][bin * 2 + 1];
                    fftBuffer[ch][bin * 2] = re * cosP - im * sinP;
                    fftBuffer[ch][bin * 2 + 1] = re * sinP + im * cosP;
                }
            }

            // 6. Pan (cross-channel, constant-power)
            {
                const float invSqrt2 = 1.f / std::sqrt(2.f);
                if (totalIn >= 2)
                {
                    for (int bin = 0; bin < numBins; ++bin)
                    {
                        float pan = juce::jlimit(-1.f, 1.f, panCurve_read[bin]);
                        float angle = (pan + 1.f) * 0.25f * juce::MathConstants<float>::pi;
                        float gL = std::cos(angle) * invSqrt2;
                        float gR = std::sin(angle) * invSqrt2;
                        float midRe = fftBuffer[0][bin * 2] + fftBuffer[1][bin * 2];
                        float midIm = fftBuffer[0][bin * 2 + 1] + fftBuffer[1][bin * 2 + 1];
                        fftBuffer[0][bin * 2] = midRe * gL;
                        fftBuffer[0][bin * 2 + 1] = midIm * gL;
                        fftBuffer[1][bin * 2] = midRe * gR;
                        fftBuffer[1][bin * 2 + 1] = midIm * gR;
                    }
                }
            }

            // 7. Wet-only: subtract post-freq-shift snapshot so only filter/phase/pan effect remains
            if (doWetOnly)
            {
                for (int ch = 0; ch < totalIn; ++ch)
                    for (int b = 0; b < numBins; ++b)
                    {
                        fftBuffer[ch][b * 2] -= drySnapshot[ch][b * 2];
                        fftBuffer[ch][b * 2 + 1] -= drySnapshot[ch][b * 2 + 1];
                    }
            }

            // 8. Global bin shift — always applied last, unaffected by wet/dry
            {
                const int n = numBins;
                const int globalShift = binShiftAmount.load(std::memory_order_relaxed);
                if (globalShift != 0)
                {
                    std::vector<std::vector<float>> shifted(totalIn,
                        std::vector<float>(n * 2, 0.0f));
                    for (int ch = 0; ch < totalIn; ++ch)
                        for (int bin = 0; bin < n; ++bin)
                        {
                            int dest = (((bin + globalShift) % n) + n) % n;
                            shifted[ch][dest * 2] += fftBuffer[ch][bin * 2];
                            shifted[ch][dest * 2 + 1] += fftBuffer[ch][bin * 2 + 1];
                        }
                    for (int ch = 0; ch < totalIn; ++ch)
                        std::copy(shifted[ch].begin(), shifted[ch].end(), fftBuffer[ch].begin());
                }
            }

            // 6. Update display from ch0
            stageUpdateDisplay();

            // 7. IFFT + overlap-add per channel
            for (int ch = 0; ch < totalIn; ++ch)
                stageInverseAccum(ch);

            // 8. Auto-shift (driven once per hop)
            {
                const float sph = static_cast<float>(hopSize) / static_cast<float>(currentSampleRate);

                auto doAutoShiftCurve = [&](std::atomic<bool>& flag, std::atomic<float>& speed,
                    float& accum, juce::CriticalSection& lock,
                    std::array<float, maxBins>& writeBuf, std::atomic<bool>& dirty)
                    {
                        if (!flag.load(std::memory_order_relaxed)) return;
                        accum += speed.load(std::memory_order_relaxed) * sph;
                        int steps = static_cast<int>(accum);
                        if (steps != 0)
                        {
                            accum -= static_cast<float>(steps);
                            juce::ScopedLock l(lock);
                            int nb = numBins;
                            steps = ((steps % nb) + nb) % nb;
                            std::rotate(writeBuf.begin(), writeBuf.begin() + (nb - steps), writeBuf.begin() + nb);
                            dirty.store(true);
                        }
                    };

                doAutoShiftCurve(autoShiftFilter, autoShiftFilterSpeed, autoShiftFilterAccum,
                    filterCurveLock, filterCurveDB_write, curveIsDirty);
                doAutoShiftCurve(autoShiftPhase, autoShiftPhaseSpeed, autoShiftPhaseAccum,
                    phaseCurveLock, phaseCurve_write, phaseIsDirty);
                doAutoShiftCurve(autoShiftFreq, autoShiftFreqSpeed, autoShiftFreqAccum,
                    freqShiftCurveLock, freqShiftCurve_write, freqShiftIsDirty);
                doAutoShiftCurve(autoShiftPan, autoShiftPanSpeed, autoShiftPanAccum,
                    panCurveLock, panCurve_write, panIsDirty);

                if (autoShiftBin.load(std::memory_order_relaxed))
                {
                    autoShiftBinAccum += autoShiftBinSpeed.load(std::memory_order_relaxed) * sph;
                    int steps = static_cast<int>(autoShiftBinAccum);
                    if (steps != 0)
                    {
                        autoShiftBinAccum -= static_cast<float>(steps);
                        autoShiftBinPos = (autoShiftBinPos + steps) % numBins;
                        if (autoShiftBinPos < 0) autoShiftBinPos += numBins;
                        binShiftAmount.store(autoShiftBinPos, std::memory_order_relaxed);
                    }
                }
            }

            // 9. Shift FIFOs
            for (int ch = 0; ch < totalIn; ++ch)
            {
                std::copy(inputFifo[ch].begin() + hopSize,
                    inputFifo[ch].begin() + fftSize,
                    inputFifo[ch].begin());
                std::fill(inputFifo[ch].begin() + (fftSize - hopSize),
                    inputFifo[ch].begin() + fftSize, 0.0f);
                inputFifoIndex[ch] -= hopSize;
            }
        }

        // Read output samples
        for (int ch = 0; ch < totalIn; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            const int accumSize = (int)outputAccum[ch].size();
            data[sample] = outputAccum[ch][outputWritePos[ch]];
            outputAccum[ch][outputWritePos[ch]] = 0.0f;
            outputWritePos[ch] = (outputWritePos[ch] + 1) % accumSize;
        }
    }
}

//==============================================================================
// Export IR
//==============================================================================
void SpectralFilterAudioProcessor::exportImpulseResponse(const juce::File& outputFile)
{
    // Snapshot all four curves
    std::array<float, maxBins> snapFilter, snapPhase, snapFreqShift, snapPan;
    { juce::ScopedLock l(filterCurveLock);    snapFilter = filterCurveDB_write; }
    { juce::ScopedLock l(phaseCurveLock);     snapPhase = phaseCurve_write; }
    { juce::ScopedLock l(freqShiftCurveLock); snapFreqShift = freqShiftCurve_write; }
    { juce::ScopedLock l(panCurveLock);       snapPan = panCurve_write; }
    const int snapGlobalShift = binShiftAmount.load(std::memory_order_relaxed);
    const bool doWrap = binShiftWrap.load(std::memory_order_relaxed);

    const int irLen = fftSize;
    const int n = numBins;

    // Step 1: apply gain + phase to build phasor per bin
    std::vector<float> preBins(n * 2, 0.f);
    for (int bin = 0; bin < n; ++bin)
    {
        float dB = snapFilter[bin];
        float mag = (dB <= -144.f) ? 0.f : std::pow(10.f, dB / 20.f);
        float ph = snapPhase[bin];
        preBins[bin * 2] = mag * std::cos(ph);
        preBins[bin * 2 + 1] = mag * std::sin(ph);
    }

    // Step 2: apply per-bin freq shift + global bin shift
    std::vector<float> shiftedL(irLen * 2, 0.f);
    std::vector<float> shiftedR(irLen * 2, 0.f);

    for (int bin = 0; bin < n; ++bin)
    {
        int perBinOff = static_cast<int>(std::round(snapFreqShift[bin]));
        int dest = bin + perBinOff + snapGlobalShift;
        if (dest < 0 || dest >= n)
        {
            if (!doWrap) continue;
            dest = ((dest % n) + n) % n;
        }

        // Step 3: apply pan at destination (same law as DSP: gL=cos(a)/sqrt2, gR=sin(a)/sqrt2)
        float pan = juce::jlimit(-1.f, 1.f, snapPan[dest]);
        float angle = (pan + 1.f) * 0.25f * juce::MathConstants<float>::pi;
        static const float iSq2 = 1.f / std::sqrt(2.f);
        float gL = std::cos(angle) * iSq2, gR = std::sin(angle) * iSq2;

        // IR is mono (preBins): treat as L=R=preBins, mid=2*preBins
        // Output = mid * gL or gR. mid = 2*mono, so factor=2*gL/gR but we use impulse as unity input.
        // For an impulse IR the source is 1.0 mono => gL/gR directly.
        shiftedL[dest * 2] += preBins[bin * 2] * gL * 2.f;
        shiftedL[dest * 2 + 1] += preBins[bin * 2 + 1] * gL * 2.f;
        shiftedR[dest * 2] += preBins[bin * 2] * gR * 2.f;
        shiftedR[dest * 2 + 1] += preBins[bin * 2 + 1] * gR * 2.f;
    }

    // Force DC and Nyquist imaginary = 0 for both channels
    shiftedL[1] = shiftedR[1] = 0.f;
    shiftedL[(n - 1) * 2 + 1] = shiftedR[(n - 1) * 2 + 1] = 0.f;

    // Mirror negative frequencies
    auto mirrorNeg = [&](std::vector<float>& buf)
        {
            for (int bin = n; bin < irLen; ++bin)
            {
                int m = irLen - bin;
                if (m > 0 && m < n)
                {
                    buf[bin * 2] = buf[m * 2];
                    buf[bin * 2 + 1] = -buf[m * 2 + 1];
                }
            }
        };
    mirrorNeg(shiftedL);
    mirrorNeg(shiftedR);

    // IFFT
    auto irFFT = std::make_unique<juce::dsp::FFT>(fftOrder);
    irFFT->performRealOnlyInverseTransform(shiftedL.data());
    irFFT->performRealOnlyInverseTransform(shiftedR.data());

    // Normalise both together
    float maxVal = 0.f;
    for (int i = 0; i < irLen; ++i)
    {
        maxVal = juce::jmax(maxVal, std::abs(shiftedL[i]));
        maxVal = juce::jmax(maxVal, std::abs(shiftedR[i]));
    }
    if (maxVal > 0.f)
    {
        float sc = 0.95f / maxVal;
        for (int i = 0; i < irLen; ++i) { shiftedL[i] *= sc; shiftedR[i] *= sc; }
    }

    // Write stereo WAV
    if (outputFile.hasWriteAccess() || outputFile.create())
    {
        juce::WavAudioFormat fmt;
        auto* os = new juce::FileOutputStream(outputFile);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            fmt.createWriterFor(os, currentSampleRate, 2, 24, {}, 0));
        if (writer)
        {
            const float* chans[2] = { shiftedL.data(), shiftedR.data() };
            writer->writeFromFloatArrays(chans, 2, irLen);
        }
    }
}

//==============================================================================
bool SpectralFilterAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SpectralFilterAudioProcessor::createEditor()
{
    return new SpectralFilterAudioProcessorEditor(*this);
}

//==============================================================================
// State
//==============================================================================
void SpectralFilterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ScopedLock l1(filterCurveLock), l2(phaseCurveLock),
        l3(freqShiftCurveLock), l4(panCurveLock);

    // V4: [fftSize] [7 colors] [numBins*4 curves] [5 speeds]
    size_t sz = sizeof(int)
        + 7 * sizeof(juce::uint32)
        + numBins * sizeof(float) * 4
        + 5 * sizeof(float);
    destData.ensureSize(sz);
    char* p = static_cast<char*>(destData.getData());

    auto writeInt = [&](int v) { std::memcpy(p, &v, sizeof(int));         p += sizeof(int); };
    auto writeUInt = [&](juce::uint32 v) { std::memcpy(p, &v, sizeof(juce::uint32)); p += sizeof(juce::uint32); };
    auto writeFloat = [&](float v) { std::memcpy(p, &v, sizeof(float));       p += sizeof(float); };
    auto writeArr = [&](const std::array<float, maxBins>& a, int nb)
        { std::memcpy(p, a.data(), nb * sizeof(float)); p += nb * sizeof(float); };

    writeInt(fftSize);
    writeUInt(backgroundColor.getARGB());
    writeUInt(curveColor.getARGB());
    writeUInt(gridColor.getARGB());
    writeUInt(spectrumColor.getARGB());
    writeUInt(phaseColor.getARGB());
    writeUInt(freqShiftColor.getARGB());
    writeUInt(panColor.getARGB());

    writeArr(filterCurveDB_write, numBins);
    writeArr(phaseCurve_write, numBins);
    writeArr(freqShiftCurve_write, numBins);
    writeArr(panCurve_write, numBins);

    writeFloat(autoShiftFilterSpeed.load());
    writeFloat(autoShiftBinSpeed.load());
    writeFloat(autoShiftPhaseSpeed.load());
    writeFloat(autoShiftFreqSpeed.load());
    writeFloat(autoShiftPanSpeed.load());
}

void SpectralFilterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (sizeInBytes < (int)sizeof(int)) return;
    const char* p = static_cast<const char*>(data);

    int savedFFTSize;
    std::memcpy(&savedFFTSize, p, sizeof(int)); p += sizeof(int);
    int rem = sizeInBytes - sizeof(int);
    int nb = savedFFTSize / 2 + 1;

    // Detect format versions by size
    int v4 = 7 * (int)sizeof(juce::uint32) + nb * (int)sizeof(float) * 4 + 5 * (int)sizeof(float);
    int v3 = 5 * (int)sizeof(juce::uint32) + nb * (int)sizeof(float) * 2 + 3 * (int)sizeof(float);
    int v2 = 5 * (int)sizeof(juce::uint32) + nb * (int)sizeof(float) * 2;
    int v1 = 4 * (int)sizeof(juce::uint32) + nb * (int)sizeof(float);
    int v0 = nb * (int)sizeof(float);

    auto readColor = [&]() -> juce::Colour {
        juce::uint32 v; std::memcpy(&v, p, sizeof(juce::uint32)); p += sizeof(juce::uint32);
        return juce::Colour(v);
        };
    auto readFloat = [&]() -> float {
        float v; std::memcpy(&v, p, sizeof(float)); p += sizeof(float); return v;
        };

    if (rem == v4) {
        backgroundColor = readColor(); curveColor = readColor(); gridColor = readColor();
        spectrumColor = readColor(); phaseColor = readColor(); freqShiftColor = readColor();
        panColor = readColor();
    }
    else if (rem == v3 || rem == v2) {
        backgroundColor = readColor(); curveColor = readColor(); gridColor = readColor();
        spectrumColor = readColor(); phaseColor = readColor();
    }
    else if (rem == v1) {
        backgroundColor = readColor(); curveColor = readColor(); gridColor = readColor(); spectrumColor = readColor();
    }
    else if (rem != v0) {
        return; // unrecognised
    }

    setFFTSize(savedFFTSize);

    auto readCurve = [&](juce::CriticalSection& lock, std::array<float, maxBins>& buf, std::atomic<bool>& dirty)
        {
            juce::ScopedLock l(lock);
            std::memcpy(buf.data(), p, nb * sizeof(float)); p += nb * sizeof(float);
            dirty.store(true);
        };

    { juce::ScopedLock l(filterCurveLock); std::memcpy(filterCurveDB_write.data(), p, nb * sizeof(float)); p += nb * sizeof(float); }
    curveIsDirty.store(true);

    if (rem >= v2) { readCurve(phaseCurveLock, phaseCurve_write, phaseIsDirty); }
    if (rem == v4) {
        readCurve(freqShiftCurveLock, freqShiftCurve_write, freqShiftIsDirty);
        readCurve(panCurveLock, panCurve_write, panIsDirty);
    }

    if (rem == v4) {
        float s0 = readFloat(), s1 = readFloat(), s2 = readFloat(), s3 = readFloat(), s4 = readFloat();
        if (s0 != 0.f) autoShiftFilterSpeed.store(s0);
        if (s1 != 0.f) autoShiftBinSpeed.store(s1);
        if (s2 != 0.f) autoShiftPhaseSpeed.store(s2);
        if (s3 != 0.f) autoShiftFreqSpeed.store(s3);
        if (s4 != 0.f) autoShiftPanSpeed.store(s4);
    }
    else if (rem == v3) {
        float s0 = readFloat(), s1 = readFloat(), s2 = readFloat();
        if (s0 != 0.f) autoShiftFilterSpeed.store(s0);
        if (s1 != 0.f) autoShiftBinSpeed.store(s1);
        if (s2 != 0.f) autoShiftPhaseSpeed.store(s2);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralFilterAudioProcessor();
}