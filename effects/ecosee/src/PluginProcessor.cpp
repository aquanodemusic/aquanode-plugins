#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EcoSeeAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // ---- Analysis ----------------------------------------------------------
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "AN_FFT_SIZE", "FFT Size",
        juce::StringArray{ "512", "1024", "2048", "4096", "8192" }, 2));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "AN_WINDOW", "Window",
        juce::StringArray{ "Hann", "Hamming", "Blackman", "Blackman-Harris", "Flat Top" }, 0));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "AN_OVERLAP", "Overlap",
        juce::StringArray{ "1x (none)", "2x", "4x", "8x" }, 1));

    // ---- Range / scaling -----------------------------------------------------
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "RG_FREQ_MIN", "Freq Range Low",
        juce::NormalisableRange<float>(0.0f, 4000.0f, 1.0f, 0.4f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "RG_FREQ_MAX", "Freq Range High",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.4f), 10000.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "RG_SPREAD_MAX", "Spread Range",
        juce::NormalisableRange<float>(200.0f, 8000.0f, 1.0f, 0.4f), 4000.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "RG_AMP_GAIN", "Amplitude Gain",
        juce::NormalisableRange<float>(0.5f, 8.0f, 0.01f), 4.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "RG_AMMOD_SCALE", "AM Mod Scale",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 6.0f));

    // ---- Motion ----------------------------------------------------------
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "MO_AUTO_ROTATE", "Auto Rotate", true));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MO_ROT_A", "Rotation Speed A",
        juce::NormalisableRange<float>(-0.02f, 0.02f, 0.0001f), 0.0035f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MO_ROT_B", "Rotation Speed B",
        juce::NormalisableRange<float>(-0.02f, 0.02f, 0.0001f), -0.0022f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MO_ANGLE_A", "Angle A",
        juce::NormalisableRange<float>(0.0f, 360.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MO_ANGLE_B", "Angle B",
        juce::NormalisableRange<float>(0.0f, 360.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MO_TRAIL", "Trail Fade",
        juce::NormalisableRange<float>(0.02f, 0.6f, 0.001f), 0.16f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "MO_FPS", "Target FPS",
        juce::StringArray{ "30", "45", "60" }, 2));

    // ---- Visual ----------------------------------------------------------
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_GLOW_SIZE", "Glow Size",
        juce::NormalisableRange<float>(0.4f, 4.0f, 0.01f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_GLOW_INTENSITY", "Glow Intensity",
        juce::NormalisableRange<float>(0.2f, 2.0f, 0.01f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_POINT_SIZE", "Point Size",
        juce::NormalisableRange<float>(0.4f, 3.0f, 0.01f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_LINE_THICK", "Line Thickness",
        juce::NormalisableRange<float>(0.2f, 3.0f, 0.01f), 1.2f));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "VS_SHOW_AXES", "Show Axes", true));
    // How many of the kHistorySize (96) stored frames are actually drawn,
    // newest-first: e.g. 40 here means only the newest 40 datapoints show
    // (and connect to each other) on every datapoint-edge-datapoint panel
    // (Spread/Entropy, Tone Map, FM/AM Cube, Custom 3D Grid) -- the full 96
    // frames keep recording underneath regardless, this only hides the
    // oldest (96 - n) of them. At 1, a single (newest) point shows with no
    // connecting lines at all.
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        "VS_POINT_COUNT", "Visible Points", 1, kHistorySize, kHistorySize));
    // When on, the datapoint-edge-datapoint panels replace their straight
    // point-to-point connections with a single continuously smooth curve
    // (Catmull-Rom spline) through the currently-visible points, faded from
    // dim tail to bright head -- reads as a moving, glowing "snake" rather
    // than a polyline that snaps from vertex to vertex.
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "VS_SNAKE_MODE", "Spline Trail Mode", false));
    // Alpha (0..1) of the spline trail's oldest end and newest end. Default
    // gives a dim tail fading up to a fully bright head; both are free to
    // move independently, e.g. push both to 1.0 for a uniformly bright
    // ribbon, or drop the head below the tail for an inverted fade.
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_SNAKE_TAIL_BRIGHT", "Trail Tail Brightness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.12f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_SNAKE_HEAD_BRIGHT", "Trail Head Brightness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_CEPSTRUM_SMOOTH", "Cepstrum Smoothing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "VS_PANEL_D_MODE", "Panel D View",
        juce::StringArray{ "Cepstrogram", "Vocal Signature (Radar)" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "VS_VIEW_MODE", "View Mode",
        juce::StringArray{ "All 4 Panels", "Spread / Entropy", "Tone Map", "FM / AM Cube", "Bottom-Right Panel", "Custom 3D Grid" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_MACRO_ZOOM", "Single-Panel Macro Zoom",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f), 1.6f));

    // ---- Custom 3D Grid: user picks which computed field drives each axis ----
    static const juce::StringArray gridFieldChoices {
        "Spectral Centroid (Hz)", "Spectral Spread (Hz)", "Spectral Entropy", "Spectral Flatness",
        "Amplitude", "Spectral Skewness", "Spectral Crest", "Spectral Slope", "Time"
    };
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "VS_GRID_X", "Grid X Field", gridFieldChoices, 0));  // Spectral Centroid
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "VS_GRID_Y", "Grid Y Field", gridFieldChoices, 4));  // Amplitude
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "VS_GRID_Z", "Grid Z Field", gridFieldChoices, 8));  // Time
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "VS_WHITE_BG", "White Background", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "VS_LABEL_POINTS", "Label Data Points", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VS_SCALE", "Graph Scale",
        juce::NormalisableRange<float>(0.6f, 1.6f, 0.01f), 1.15f));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "VS_HIDE_QUIET", "Hide Dots If Quiet", true));
    // When on, the 3D panels (Spread/Entropy, FM/AM Cube, Custom 3D Grid)
    // scale each axis to the actual observed min/max of that field across
    // the current history buffer, instead of a fixed nominal range -- so a
    // field that happens to sit nearly flat still fills its axis instead of
    // collapsing everything onto a thin sliver/plane. Off by default since
    // it changes what the fixed axis numbers mean (they now track the data).
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "VS_NORMALIZE", "Normalize Axes To Data", false));

    // ---- Colour scheme: low/mid/high stops, applied as a gradient across
    // every diagram's data-colour mapping. Stored as packed 0xRRGGBB ints.
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        "CS_LOW_RGB", "Colour Low", 0, 0xFFFFFF, 0x1E6FFF));
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        "CS_MID_RGB", "Colour Mid", 0, 0xFFFFFF, 0x2CFF6B));
    p.push_back(std::make_unique<juce::AudioParameterInt>(
        "CS_HIGH_RGB", "Colour High", 0, 0xFFFFFF, 0xFF3B30));

    return { p.begin(), p.end() };
}

//==============================================================================
EcoSeeAudioProcessor::EcoSeeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "STATE", createParams())
{
    history.fill(SpectralFrame());
}

EcoSeeAudioProcessor::~EcoSeeAudioProcessor() {}

void EcoSeeAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    currentFftOrderIndex = -1; // force a rebuild on the first block
    currentWindowIndex = -1;
}

void EcoSeeAudioProcessor::releaseResources() {}

bool EcoSeeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void EcoSeeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void EcoSeeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

void EcoSeeAudioProcessor::rebuildAnalysisIfNeeded()
{
    const int fftIndex = (int)*apvts.getRawParameterValue("AN_FFT_SIZE");
    const int windowIndex = (int)*apvts.getRawParameterValue("AN_WINDOW");
    const int overlapIdx = (int)*apvts.getRawParameterValue("AN_OVERLAP");

    const bool needsRebuild = (fftIndex != currentFftOrderIndex) || (windowIndex != currentWindowIndex)
        || fft == nullptr;

    if (!needsRebuild)
    {
        // Overlap can change hop size without a full FFT rebuild.
        const int overlapFactor = 1 << overlapIdx; // 1,2,4,8
        hopSize = juce::jmax(1, currentFftSize / overlapFactor);
        return;
    }

    currentFftOrderIndex = fftIndex;
    currentWindowIndex = windowIndex;

    const int order = FFT_ORDERS[juce::jlimit(0, 4, fftIndex)];
    currentFftSize = 1 << order;

    fft = std::make_unique<juce::dsp::FFT>(order);

    using WM = juce::dsp::WindowingFunction<float>::WindowingMethod;
    WM method = WM::hann;
    switch (windowIndex)
    {
    case 0: method = WM::hann;           break;
    case 1: method = WM::hamming;        break;
    case 2: method = WM::blackman;       break;
    case 3: method = WM::blackmanHarris; break;
    case 4: method = WM::flatTop;        break;
    default: break;
    }
    window = std::make_unique<juce::dsp::WindowingFunction<float>>((size_t)currentFftSize, method);

    fifo.assign((size_t)currentFftSize, 0.0f);
    fifoIndex = 0;
    fftData.assign((size_t)currentFftSize * 2, 0.0f);
    cepBufIn.assign((size_t)currentFftSize, {});
    cepBufOut.assign((size_t)currentFftSize, {});

    const int overlapFactor = 1 << overlapIdx;
    hopSize = juce::jmax(1, currentFftSize / overlapFactor);
    hopCounter = 0;
}

void EcoSeeAudioProcessor::pushSample(float sample)
{
    // Shift-in via a simple rolling fifo; cheap enough at UI-rate hop sizes.
    if (fifoIndex < (int)fifo.size())
        fifo[(size_t)fifoIndex++] = sample;

    if (fifoIndex >= currentFftSize)
    {
        // Slide the buffer left by hopSize, keeping the most recent currentFftSize samples,
        // so we can analyse overlapping windows without a full re-fill each time.
        computeSpectralFrame();

        const int keep = currentFftSize - hopSize;
        if (keep > 0)
            std::move(fifo.begin() + hopSize, fifo.begin() + currentFftSize, fifo.begin());
        fifoIndex = juce::jmax(0, keep);
    }
}

void EcoSeeAudioProcessor::computeSpectralFrame()
{
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < currentFftSize; ++i)
        fftData[(size_t)i] = fifo[(size_t)i];

    float sumSq = 0.0f;
    for (int i = 0; i < currentFftSize; ++i)
        sumSq += fifo[(size_t)i] * fifo[(size_t)i];
    const float rms = std::sqrt(sumSq / (float)currentFftSize);
    const float levelDb = 20.0f * std::log10(juce::jmax(rms, 1.0e-6f));

    window->multiplyWithWindowingTable(fftData.data(), (size_t)currentFftSize);
    fft->performFrequencyOnlyForwardTransform(fftData.data());

    const int numBins = currentFftSize / 2;
    const float binHz = (float)(currentSampleRate / (double)currentFftSize);

    double magSum = 0.0;
    double weightedFreqSum = 0.0;
    double logMagSum = 0.0;
    const float eps = 1.0e-9f;

    for (int i = 0; i < numBins; ++i)
    {
        const float mag = fftData[(size_t)i];
        const float freq = (float)i * binHz;
        magSum += mag;
        weightedFreqSum += (double)mag * (double)freq;
        logMagSum += std::log((double)mag + eps);
    }

    const float centroid = magSum > 0.0 ? (float)(weightedFreqSum / magSum) : 0.0f;

    double spreadAcc = 0.0;
    double entropyAcc = 0.0;
    const double invMagSum = magSum > 0.0 ? 1.0 / magSum : 0.0;

    for (int i = 0; i < numBins; ++i)
    {
        const float mag = fftData[(size_t)i];
        const float freq = (float)i * binHz;
        const double diff = (double)freq - (double)centroid;
        spreadAcc += (double)mag * diff * diff;

        const double pr = (double)mag * invMagSum;
        if (pr > 0.0)
            entropyAcc += -pr * std::log2(pr);
    }

    const float spread = magSum > 0.0 ? (float)std::sqrt(spreadAcc * invMagSum) : 0.0f;

    const float maxEntropy = std::log2((float)numBins);
    const float entropyNorm = maxEntropy > 0.0f ? juce::jlimit(0.0f, 1.0f, (float)entropyAcc / maxEntropy) : 0.0f;

    const float geoMean = (float)std::exp(logMagSum / (double)numBins);
    const float arithMean = (float)(magSum / (double)numBins);
    const float flatnessNorm = arithMean > eps ? juce::jlimit(0.0f, 1.0f, geoMean / arithMean) : 0.0f;

    const float ampGain = *apvts.getRawParameterValue("RG_AMP_GAIN");

    // ---- Extra descriptors for the Vocal Signature radar ------------------
    float maxMag = 0.0f;
    for (int i = 0; i < numBins; ++i)
        maxMag = juce::jmax(maxMag, fftData[(size_t)i]);
    const float crestRatio = arithMean > eps ? maxMag / arithMean : 0.0f;
    const float crestNorm = juce::jlimit(0.0f, 1.0f, (crestRatio - 1.0f) / 19.0f);

    double skewAcc = 0.0;
    for (int i = 0; i < numBins; ++i)
    {
        const double freq = (double)i * (double)binHz;
        const double diff = freq - (double)centroid;
        skewAcc += (double)fftData[(size_t)i] * diff * diff * diff;
    }
    const double skewRaw = (magSum > 0.0 && spread > 0.0f)
        ? (skewAcc * invMagSum) / ((double)spread * (double)spread * (double)spread)
        : 0.0;
    const float skewNorm = juce::jlimit(0.0f, 1.0f, (float)(0.5 + std::tanh(skewRaw * 0.001) * 0.5));

    // Spectral slope/tilt: linear regression of log-magnitude against frequency.
    // This MUST be weighted by magnitude (as below), not a plain unweighted fit --
    // an unweighted fit lets the many near-silent bins (log of ~0, i.e. large
    // negative outliers) dominate the line and swamps the actual spectral
    // shape, which is what made this always saturate to one end before.
    double sumW = 0.0, sumWX = 0.0, sumWY = 0.0, sumWXY = 0.0, sumWXX = 0.0;
    for (int i = 0; i < numBins; ++i)
    {
        const double freq = (double)i * (double)binHz;
        const double magD = (double)fftData[(size_t)i];
        const double logMag = std::log(magD + (double)eps);
        const double w = magD; // weight the fit by how much energy is actually at this bin

        sumW += w; sumWX += w * freq; sumWY += w * logMag;
        sumWXY += w * freq * logMag; sumWXX += w * freq * freq;
    }
    const double slopeDenom = sumW * sumWXX - sumWX * sumWX;
    const double slopeRaw = (sumW > 0.0 && slopeDenom != 0.0)
        ? (sumW * sumWXY - sumWX * sumWY) / slopeDenom
        : 0.0;
    // tanh instead of a hard linear clamp: it still saturates at the extremes
    // (very tilted spectra) but responds smoothly in between rather than
    // pinning to 0 or 1 for almost every real signal.
    const float slopeNorm = juce::jlimit(0.0f, 1.0f, (float)(0.5 + std::tanh(slopeRaw * 4000.0) * 0.5));

    SpectralFrame frame;
    frame.centroidHz = centroid;
    frame.spreadHz = spread;
    frame.entropy01 = entropyNorm;
    frame.flatness01 = flatnessNorm;
    frame.amplitude01 = juce::jlimit(0.0f, 1.0f, rms * ampGain);
    frame.levelDb = levelDb;
    frame.skewness01 = skewNorm;
    frame.crest01 = crestNorm;
    frame.slope01 = slopeNorm;

    // ---- Cepstral analysis --------------------------------------------------
    // Take the log-magnitude spectrum, mirror it out to a full
    // (conjugate-symmetric) spectrum, and inverse-FFT it to get the real
    // cepstrum. Quefrency bin index along X, magnitude along Y -- straight
    // per-frame values, no averaging or persistence weighting.
    {
        const int fftSize = currentFftSize;
        const int half = juce::jmin(numBins, fftSize / 2);
        const float epsC = 1.0e-8f;

        for (int i = 0; i <= half; ++i)
        {
            const float lm = std::log(std::max(epsC, fftData[(size_t)i]));
            cepBufIn[(size_t)i] = { lm, 0.0f };
            const int mirror = fftSize - i;
            if (mirror > half && mirror < fftSize)
                cepBufIn[(size_t)mirror] = { lm, 0.0f };
        }

        fft->perform(cepBufIn.data(), cepBufOut.data(), true); // inverse -> real cepstrum

        // Skip coefficient 0 (overall log-energy) and take the next
        // coefficients up to fftSize/2 (dynamic: grows/shrinks with the
        // FFT Size analysis parameter, capped at kMaxCepstrumSize).
        //
        // Normalise relative to THIS FRAME'S OWN PEAK rather than a fixed
        // scale. The lowest few quefrency bins (spectral envelope/formant
        // shape) are almost always much larger in magnitude than the
        // higher-quefrency bins (pitch/harmonic structure), so a fixed
        // tanh(mag * 4) compression saturated the low bins to ~1 and
        // crushed everything else toward 0 for virtually any real signal --
        // that's why the display always looked pinned to the lowest bins
        // no matter what was playing. Dividing by the frame's own peak
        // keeps the full 0..1 range meaningful for whatever shape is
        // actually present, so real differences between sounds show up.
        const int cepstrumCount = juce::jmin(kMaxCepstrumSize, half);
        frame.cepstrumCount = cepstrumCount;

        std::array<float, kMaxCepstrumSize> rawCepstrum {};
        float peakMag = 1.0e-6f;
        for (int i = 0; i < cepstrumCount; ++i)
        {
            const int idx = juce::jmin(fftSize - 1, i + 1);
            const float mag = std::abs(cepBufOut[(size_t)idx].real());
            rawCepstrum[(size_t)i] = mag;
            peakMag = juce::jmax(peakMag, mag);
        }
        for (int i = 0; i < cepstrumCount; ++i)
            frame.cepstrum[(size_t)i] = juce::jlimit(0.0f, 1.0f, rawCepstrum[(size_t)i] / peakMag);
    }

    const juce::ScopedLock sl(historyLock);
    history[(size_t)historyWritePos] = frame;
    historyWritePos = (historyWritePos + 1) % kHistorySize;
}

void EcoSeeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    rebuildAnalysisIfNeeded();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        if (numChannels > 0)
            mono /= (float)numChannels;

        pushSample(mono);
    }

    // Pure analyser: audio passes through unmodified.
}

void EcoSeeAudioProcessor::getHistorySnapshot (std::array<SpectralFrame, kHistorySize>& out)
{
    const juce::ScopedLock sl(historyLock);

    for (int i = 0; i < kHistorySize; ++i)
    {
        const int idx = (historyWritePos + i) % kHistorySize;
        out[(size_t)i] = history[(size_t)idx];
    }
}

juce::AudioProcessorEditor* EcoSeeAudioProcessor::createEditor()
{
    return new EcoSeeAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EcoSeeAudioProcessor();
}