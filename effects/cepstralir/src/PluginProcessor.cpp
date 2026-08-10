#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <algorithm>
#include <numeric>

//==============================================================================
CepstralIRProcessor::CepstralIRProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();

    addParameter(irLengthParam = new juce::AudioParameterFloat("irlength", "IR Length",
        juce::NormalisableRange<float>(0.01f, 1.0f, 0.01f), 0.2f));
    addParameter(smoothingParam = new juce::AudioParameterFloat("smoothing", "Quefrency Cutoff",
        juce::NormalisableRange<float>(4.0f, 2048.0f, 1.0f), 128.0f));
    addParameter(applyWindowParam = new juce::AudioParameterBool("window", "Apply Window", true));
    addParameter(fftSizeParam = new juce::AudioParameterInt("fftsize", "FFT Size (2^n)", 12, 20, 15));
    addParameter(linearPhaseParam = new juce::AudioParameterBool("linearphase", "Linear Phase", false));
    addParameter(useHardCapParam = new juce::AudioParameterBool("usehardcap", "Use Hard Cap", false));
    addParameter(hardCapLengthParam = new juce::AudioParameterFloat("hardcaplength", "Hard Cap Length (ms)",
        juce::NormalisableRange<float>(10.0f, 5000.0f, 1.0f), 500.0f));
}

CepstralIRProcessor::~CepstralIRProcessor() {}

//==============================================================================
const juce::String CepstralIRProcessor::getName() const { return "CepstralIR"; }
bool CepstralIRProcessor::acceptsMidi() const { return false; }
bool CepstralIRProcessor::producesMidi() const { return false; }
bool CepstralIRProcessor::isMidiEffect() const { return false; }
double CepstralIRProcessor::getTailLengthSeconds() const { return 0.0; }
int CepstralIRProcessor::getNumPrograms() { return 1; }
int CepstralIRProcessor::getCurrentProgram() { return 0; }
void CepstralIRProcessor::setCurrentProgram(int) {}
const juce::String CepstralIRProcessor::getProgramName(int) { return {}; }
void CepstralIRProcessor::changeProgramName(int, const juce::String&) {}
bool CepstralIRProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* CepstralIRProcessor::createEditor()
{
    return new CepstralIREditor(*this);
}

void CepstralIRProcessor::prepareToPlay(double, int) {}
void CepstralIRProcessor::releaseResources() {}

bool CepstralIRProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void CepstralIRProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // This plugin is an offline tool; pass audio through
    // (could add convolution preview here in future)
}

//==============================================================================
// Cooley-Tukey FFT (in-place, radix-2, DIF)
void CepstralIRProcessor::fftForward(std::vector<std::complex<float>>& data)
{
    const int N = (int)data.size();
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; ++i)
    {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    // Butterfly stages
    for (int len = 2; len <= N; len <<= 1)
    {
        float ang = -2.0f * juce::MathConstants<float>::pi / (float)len;
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < N; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j)
            {
                auto u = data[i + j];
                auto v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void CepstralIRProcessor::fftInverse(std::vector<std::complex<float>>& data)
{
    for (auto& c : data) c = std::conj(c);
    fftForward(data);
    float scale = 1.0f / (float)data.size();
    for (auto& c : data) c = std::conj(c) * scale;
}

int CepstralIRProcessor::nextPowerOfTwo(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

//==============================================================================
// CEPSTRAL ANALYSIS PIPELINE
//
// The cepstrum C(t) = IFFT{ log|FFT{x(t)}| }
// By liftering (low-pass in quefrency), we extract the spectral envelope.
// Converting back via exp() and minimum-phase reconstruction yields an IR
// whose frequency response matches the spectral shape of the source signal.
//==============================================================================

void CepstralIRProcessor::computeCepstrum(const std::vector<float>& signal,
    std::vector<float>& cepstrum,
    int fftSize)
{
    // Zero-pad signal to fftSize
    std::vector<std::complex<float>> X(fftSize, { 0.0f, 0.0f });
    int copyLen = std::min((int)signal.size(), fftSize);
    for (int i = 0; i < copyLen; ++i) X[i] = { signal[i], 0.0f };

    // Apply Hann window to reduce spectral leakage
    for (int i = 0; i < copyLen; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (float)(copyLen - 1)));
        X[i] *= w;
    }

    // FFT
    fftForward(X);

    // Log magnitude spectrum (power cepstrum uses log|X|)
    const float epsilon = 1e-10f;
    std::vector<std::complex<float>> logX(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        float mag = std::abs(X[i]);
        logX[i] = { std::log(mag + epsilon), 0.0f };
    }

    // IFFT of log spectrum = real cepstrum
    fftInverse(logX);

    cepstrum.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        cepstrum[i] = logX[i].real();
}

void CepstralIRProcessor::lifterCepstrum(const std::vector<float>& cepstrum,
    std::vector<float>& lifteredCepstrum,
    int numCoeffs)
{
    // Low-pass lifter: keep only the first numCoeffs quefrency bins
    // and mirror them to preserve real-valued spectrum after FFT.
    // This removes pitch (periodicity) and retains spectral envelope.
    int N = (int)cepstrum.size();
    lifteredCepstrum.assign(N, 0.0f);

    int cutoff = std::min(numCoeffs, N / 2);

    // DC term
    lifteredCepstrum[0] = cepstrum[0];

    // Positive quefrencies (scale by 2 to compensate for mirroring)
    for (int i = 1; i < cutoff; ++i)
        lifteredCepstrum[i] = cepstrum[i] * 2.0f;

    // Nyquist (if N even)
    if (cutoff < N / 2)
        lifteredCepstrum[N / 2] = cepstrum[N / 2];

    // Negative quefrencies are mirror (set to zero for minimum-phase)
    // The minimum-phase reconstruction handles the anti-causal part
}

void CepstralIRProcessor::cepstrumToLinearPhaseIR(const std::vector<float>& lifteredCepstrum,
    std::vector<float>& ir,
    int fftSize)
{
    // Convert liftered cepstrum → spectral envelope → linear-phase (zero-phase) IR
    //
    // For linear-phase, we use only the magnitude and apply linear phase (delay)
    // to center the IR properly in the middle of the buffer without wrap-around.

    // 1. FFT of liftered cepstrum → log spectrum (real part)
    std::vector<std::complex<float>> C(fftSize, { 0.0f, 0.0f });
    for (int i = 0; i < fftSize; ++i)
        C[i] = { lifteredCepstrum[i], 0.0f };

    fftForward(C);

    // 2. Exp to recover magnitude spectrum and apply linear phase for centering
    //    Linear phase = -w * delay, where delay = fftSize/2 centers the impulse
    std::vector<std::complex<float>> H(fftSize);
    int half = fftSize / 2;
    
    for (int i = 0; i < fftSize; ++i)
    {
        float logMag = C[i].real();
        float mag = std::exp(logMag);
        
        // Apply linear phase shift to center the IR at fftSize/2
        // Phase = -2*pi*f*delay, where f = i/fftSize and delay = fftSize/2
        float phase = -juce::MathConstants<float>::pi * (float)i;  // Simplified: -2*pi*(i/fftSize)*(fftSize/2)
        
        H[i] = std::polar(mag, phase);
    }

    // 3. IFFT to get the time-domain IR (now properly centered at fftSize/2)
    fftInverse(H);

    ir.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        ir[i] = H[i].real();
}

void CepstralIRProcessor::cepstrumToIR(const std::vector<float>& lifteredCepstrum,
    std::vector<float>& ir,
    int fftSize)
{
    // Convert liftered cepstrum → spectral envelope → minimum-phase IR
    //
    // 1. FFT of liftered cepstrum → log spectrum (real part)
    std::vector<std::complex<float>> C(fftSize, { 0.0f, 0.0f });
    for (int i = 0; i < fftSize; ++i)
        C[i] = { lifteredCepstrum[i], 0.0f };

    fftForward(C);

    // 2. Exp to recover magnitude spectrum
    //    For minimum-phase: H(w) = exp(log|H(w)| + j * hilbert(log|H(w)|))
    //    The imaginary part of C after FFT gives us the Hilbert transform
    //    of the log-magnitude, which is exactly what we need for min-phase.

    std::vector<std::complex<float>> H(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        // C[i].real() = log|H(w)|,  C[i].imag() = phase (from Hilbert)
        float logMag = C[i].real();
        float phase = C[i].imag();
        float mag = std::exp(logMag);
        H[i] = std::polar(mag, phase);
    }

    // 3. IFFT to get the time-domain IR
    fftInverse(H);

    ir.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        ir[i] = H[i].real();
}

void CepstralIRProcessor::applyTukeyWindow(std::vector<float>& ir, float taperRatio)
{
    int N = (int)ir.size();
    int taperLen = (int)(taperRatio * N / 2.0f);

    // Attack taper (fade in at start)
    for (int i = 0; i < taperLen; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * i / (float)taperLen));
        ir[i] *= w;
    }

    // Decay taper (fade out at end)
    for (int i = 0; i < taperLen; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * i / (float)taperLen));
        ir[N - 1 - i] *= w;
    }
}

void CepstralIRProcessor::applyLinearPhaseWindow(std::vector<float>& ir)
{
    // For linear-phase (centered) IRs, apply a symmetric window that only tapers the edges
    // This preserves the main impulse while preventing edge discontinuities
    int N = (int)ir.size();
    int center = N / 2;
    
    // Find the extent of the main impulse (±2000 samples from center as default)
    int mainImpulseRadius = std::min(center, 2000);
    
    // Only taper the outer 10% on each end
    int taperLen = N / 10;
    
    // Fade in at start
    for (int i = 0; i < taperLen; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * i / (float)taperLen));
        ir[i] *= w;
    }
    
    // Fade out at end
    for (int i = 0; i < taperLen; ++i)
    {
        float w = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * i / (float)taperLen));
        ir[N - 1 - i] *= w;
    }
}

int CepstralIRProcessor::findIRTailCutoff(const std::vector<float>& ir, float noiseFloorDb)
{
    // Find where the IR energy drops below the noise floor and stays there
    // Use RMS in sliding windows to be robust to single spikes

    int N = (int)ir.size();
    if (N < 100) return N;

    // Find peak energy for reference
    float peakEnergy = 0.0f;
    for (int i = 0; i < N; ++i)
        peakEnergy = std::max(peakEnergy, std::abs(ir[i]));

    if (peakEnergy < 1e-8f) return N;  // Avoid divide by zero

    float noiseFloorLinear = peakEnergy * std::pow(10.0f, noiseFloorDb / 20.0f);

    // Scan from end backwards looking for where signal rises above noise floor
    int windowSize = 512;  // ~11ms at 44.1kHz
    int lastSignificantSample = N - 1;

    for (int i = N - windowSize; i >= 0; i -= windowSize / 4)
    {
        // Calculate RMS of this window
        float rms = 0.0f;
        for (int j = 0; j < windowSize && (i + j) < N; ++j)
        {
            float val = ir[i + j];
            rms += val * val;
        }
        rms = std::sqrt(rms / (float)windowSize);

        if (rms > noiseFloorLinear)
        {
            lastSignificantSample = i + windowSize;
            break;
        }
    }

    // Add a small tail buffer (5% of detected length or 1024 samples, whichever is smaller)
    int tailBuffer = std::min(1024, (int)(lastSignificantSample * 0.05f));
    lastSignificantSample = std::min(N, lastSignificantSample + tailBuffer);

    // Ensure minimum length
    return std::max(lastSignificantSample, 2048);
}

void CepstralIRProcessor::normalizeBuffer(std::vector<float>& buf)
{
    float peak = 0.0f;
    for (auto v : buf) peak = std::max(peak, std::abs(v));
    if (peak > 1e-8f)
    {
        float invPeak = 1.0f / peak;
        for (auto& v : buf) v *= invPeak;
    }
}

//==============================================================================
bool CepstralIRProcessor::loadSampleFile(const juce::File& file)
{
    if (file.existsAsFile())
    {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(file));

        if (reader == nullptr)
        {
            statusMessage = "Error: Could not read file: " + file.getFileName();
            return false;
        }

        sourceSampleRate = reader->sampleRate;
        sourceNumChannels = (int)reader->numChannels;
        auto numSamples = (int)reader->lengthInSamples;

        sourceBuffer.setSize(sourceNumChannels, numSamples);
        reader->read(&sourceBuffer, 0, numSamples, 0, true, true);
        sourceLoaded = true;
        irExtracted = false;
        statusMessage = "Loaded: " + file.getFileName()
            + "\n" + juce::String(numSamples) + " samples @ "
            + juce::String((int)sourceSampleRate) + " Hz";
    }

    if (!sourceLoaded)
    {
        statusMessage = "No source loaded.";
        return false;
    }

    return reprocessIR();
}

bool CepstralIRProcessor::reprocessIR()
{
    if (!sourceLoaded) return false;

    int numSamples = sourceBuffer.getNumSamples();
    // -----------------------------------------------------------------------
    // CEPSTRAL IR EXTRACTION
    // -----------------------------------------------------------------------
    int fftSizeExp = fftSizeParam->get();          // e.g. 13 → 8192
    int fftSize = 1 << fftSizeExp;
    int quefrCutoff = (int)smoothingParam->get();   // liftering cutoff
    float irLenFactor = irLengthParam->get();       // fraction of fftSize to keep

    // Build mono mix
    std::vector<float> monoSignal(numSamples, 0.0f);
    for (int ch = 0; ch < sourceNumChannels; ++ch)
    {
        const float* rd = sourceBuffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            monoSignal[i] += rd[i];
    }
    float chScale = 1.0f / (float)sourceNumChannels;
    for (auto& v : monoSignal) v *= chScale;

    // We process in overlapping FFT frames and average the cepstra
    // to get a more stable estimate of the spectral envelope
    int hopSize = fftSize / 4;
    int numFrames = 0;
    std::vector<float> avgCepstrum(fftSize, 0.0f);
    std::vector<float> frameCepstrum;

    for (int start = 0; start + fftSize <= numSamples; start += hopSize)
    {
        std::vector<float> frame(monoSignal.begin() + start,
            monoSignal.begin() + start + fftSize);
        computeCepstrum(frame, frameCepstrum, fftSize);
        for (int i = 0; i < fftSize; ++i)
            avgCepstrum[i] += frameCepstrum[i];
        ++numFrames;
    }

    // If signal is shorter than one FFT frame, process the whole thing
    if (numFrames == 0)
    {
        computeCepstrum(monoSignal, avgCepstrum, fftSize);
        numFrames = 1;
    }
    else
    {
        float inv = 1.0f / (float)numFrames;
        for (auto& v : avgCepstrum) v *= inv;
    }

    // Lifter (low-pass in quefrency)
    std::vector<float> lifteredCepstrum;
    lifterCepstrum(avgCepstrum, lifteredCepstrum, quefrCutoff);

    // Convert to minimum-phase or linear-phase IR
    std::vector<float> rawIR;
    if (linearPhaseParam->get())
        cepstrumToLinearPhaseIR(lifteredCepstrum, rawIR, fftSize);
    else
        cepstrumToIR(lifteredCepstrum, rawIR, fftSize);

    // Intelligently detect the useful IR length
    int intelligentLength;
    
    if (linearPhaseParam->get())
    {
        // For linear-phase, find the radius around center where energy is significant
        int center = (int)rawIR.size() / 2;
        float peakEnergy = 0.0f;
        for (int i = 0; i < (int)rawIR.size(); ++i)
            peakEnergy = std::max(peakEnergy, std::abs(rawIR[i]));
        
        // Much more aggressive cutoff for compact IRs
        float noiseFloorLinear = peakEnergy * std::pow(10.0f, -40.0f / 20.0f);  // -40dB instead of -60dB
        int windowSize = 256;  // Smaller window for tighter detection
        int foundRadius = 1024;  // Default minimum radius
        
        // Search outward from center to find where energy drops below noise floor
        for (int radius = 512; radius < center; radius += windowSize / 2)
        {
            // Check both sides at this radius
            float rmsLeft = 0.0f, rmsRight = 0.0f;
            int leftStart = std::max(0, center - radius - windowSize);
            int rightStart = std::min((int)rawIR.size() - windowSize, center + radius);
            
            if (leftStart >= 0 && leftStart + windowSize < (int)rawIR.size())
            {
                for (int j = 0; j < windowSize; ++j)
                {
                    float valL = rawIR[leftStart + j];
                    rmsLeft += valL * valL;
                }
                rmsLeft = std::sqrt(rmsLeft / (float)windowSize);
            }
            
            if (rightStart >= 0 && rightStart + windowSize < (int)rawIR.size())
            {
                for (int j = 0; j < windowSize; ++j)
                {
                    float valR = rawIR[rightStart + j];
                    rmsRight += valR * valR;
                }
                rmsRight = std::sqrt(rmsRight / (float)windowSize);
            }
            
            // If both sides are below threshold, we found our cutoff
            if (rmsLeft < noiseFloorLinear && rmsRight < noiseFloorLinear)
            {
                foundRadius = radius;
                break;
            }
        }
        
        // intelligentLength is the full width (diameter) needed
        intelligentLength = foundRadius * 2 + 512;  // Add small buffer
        
        // Apply user-defined hard cap if enabled
        if (useHardCapParam->get())
        {
            float hardCapMs = hardCapLengthParam->get();
            int hardCapSamples = (int)(hardCapMs * sourceSampleRate / 1000.0f);
            intelligentLength = hardCapSamples;  // Use hard cap exactly
        }
        else
        {
            intelligentLength = std::max(intelligentLength, 1024);  // Minimum size only if no hard cap
        }
    }
    else
    {
        // For minimum-phase, use the existing tail cutoff method
        intelligentLength = findIRTailCutoff(rawIR, -60.0f);
        
        // Apply hard cap for minimum-phase too
        if (useHardCapParam->get())
        {
            float hardCapMs = hardCapLengthParam->get();
            int hardCapSamples = (int)(hardCapMs * sourceSampleRate / 1000.0f);
            intelligentLength = std::min(intelligentLength, hardCapSamples);
        }
    }

    // Then apply user's desired length factor on top of that
    int irLen = (int)((float)intelligentLength * irLenFactor);
    
    // Only apply minimum if hard cap is disabled
    if (!useHardCapParam->get())
        irLen = std::max(irLen, 64);
        
    irLen = std::min(irLen, (int)rawIR.size());  // Don't exceed buffer

    // For linear-phase, extract centered portion; for min-phase, keep from start
    if (linearPhaseParam->get())
    {
        int center = (int)rawIR.size() / 2;
        int halfLen = irLen / 2;
        int startIdx = std::max(0, center - halfLen);
        int endIdx = std::min((int)rawIR.size(), startIdx + irLen);
        
        std::vector<float> centeredIR(rawIR.begin() + startIdx, rawIR.begin() + endIdx);
        rawIR = std::move(centeredIR);
        irLen = (int)rawIR.size();
    }
    else
    {
        rawIR.resize(irLen);
    }

    // Apply windowing (different approach for linear vs minimum phase)
    if (applyWindowParam->get())
    {
        if (linearPhaseParam->get())
            applyLinearPhaseWindow(rawIR);  // Symmetric window for centered IR
        else
            applyTukeyWindow(rawIR, 0.05f);  // Fade in/out for causal IR
    }

    // Normalize
    normalizeBuffer(rawIR);

    // Store into stereo buffer (both channels identical; could do L/R separately)
    int outCh = std::max(sourceNumChannels, 1);
    irBuffer.setSize(outCh, irLen);
    for (int ch = 0; ch < outCh; ++ch)
    {
        float* wr = irBuffer.getWritePointer(ch);
        for (int i = 0; i < irLen; ++i)
            wr[i] = rawIR[i];
    }

    irExtracted = true;
    statusMessage = "IR extracted! " + juce::String(irLen) + " samples ("
        + juce::String(irLen / sourceSampleRate * 1000.0, 1) + " ms)";

    return true;
}

//==============================================================================
bool CepstralIRProcessor::saveImpulseResponse(const juce::File& outputFile)
{
    if (!irExtracted)
    {
        statusMessage = "Error: No IR to save. Load a sample first.";
        return false;
    }

    outputFile.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> fos(outputFile.createOutputStream());
    if (fos == nullptr)
    {
        statusMessage = "Error: Could not create output file.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(fos.get(),
            sourceSampleRate,
            (unsigned int)irBuffer.getNumChannels(),
            24,     // 24-bit
            {},
            0));

    if (writer == nullptr)
    {
        statusMessage = "Error: Could not create WAV writer.";
        return false;
    }

    fos.release(); // writer owns the stream now

    writer->writeFromAudioSampleBuffer(irBuffer, 0, irBuffer.getNumSamples());
    writer.reset();

    statusMessage = "Saved IR: " + outputFile.getFileName();
    return true;
}

//==============================================================================
void CepstralIRProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos(destData, true);
    mos.writeFloat(*irLengthParam);
    mos.writeFloat(*smoothingParam);
    mos.writeBool(*applyWindowParam);
    mos.writeInt(*fftSizeParam);
    mos.writeBool(*linearPhaseParam);
}

void CepstralIRProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream mis(data, (size_t)sizeInBytes, false);
    if (mis.getDataSize() < 16) return;
    *irLengthParam = mis.readFloat();
    *smoothingParam = mis.readFloat();
    *applyWindowParam = mis.readBool();
    *fftSizeParam = mis.readInt();
    if (mis.getDataSize() >= 17)  // Check if we have the new parameter
        *linearPhaseParam = mis.readBool();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CepstralIRProcessor();
}