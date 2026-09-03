/*
  ==============================================================================

    PluginProcessor.cpp
    IRForge — implementation.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace irforge;

//==============================================================================
// IRBuilder
//==============================================================================

void IRBuilder::minimumPhaseFromMagnitude(const std::vector<float>& logMag,
    std::vector<float>& out,
    int fftSize, int lifterCutoff,
    bool linearPhase)
{
    const int order = (int)std::log2((double)fftSize);
    juce::dsp::FFT fft(order);
    const int half = fftSize / 2;

    // --- real cepstrum: IFFT of the log magnitude spectrum ----------------
    std::vector<juce::dsp::Complex<float>> buf((size_t)fftSize), tmp((size_t)fftSize);
    for (int i = 0; i <= half; ++i)
        buf[(size_t)i] = { logMag[(size_t)i], 0.0f };
    for (int i = half + 1; i < fftSize; ++i)                 // mirror
        buf[(size_t)i] = { logMag[(size_t)(fftSize - i)], 0.0f };

    fft.perform(buf.data(), tmp.data(), true);              // inverse -> cepstrum

    std::vector<float> cep((size_t)fftSize);
    for (int i = 0; i < fftSize; ++i)
        cep[(size_t)i] = tmp[(size_t)i].real();

    // --- lifter -----------------------------------------------------------
    //
    // This is the whole CHARACTER control. Keeping few coefficients leaves the
    // smooth spectral envelope (room / system capture). Keeping all of them
    // leaves the full magnitude including the harmonic comb (puriFIR).
    //
    // Doubling the positive quefrencies and zeroing the negative ones is the
    // standard construction that makes the result minimum phase: it applies
    // the Hilbert transform relationship between log magnitude and phase.
    std::vector<float> lift((size_t)fftSize, 0.0f);
    const int k = juce::jlimit(2, half, lifterCutoff);

    lift[0] = cep[0];
    if (linearPhase)
    {
        // symmetric lifter -> zero phase, no Hilbert relationship
        for (int i = 1; i < k; ++i)
        {
            lift[(size_t)i] = cep[(size_t)i];
            lift[(size_t)(fftSize - i)] = cep[(size_t)(fftSize - i)];
        }
        if (k >= half) lift[(size_t)half] = cep[(size_t)half];
    }
    else
    {
        for (int i = 1; i < k; ++i)
            lift[(size_t)i] = cep[(size_t)i] * 2.0f;
        if (k >= half) lift[(size_t)half] = cep[(size_t)half];
    }

    // --- back to a spectrum, exponentiate, inverse transform --------------
    for (int i = 0; i < fftSize; ++i)
        buf[(size_t)i] = { lift[(size_t)i], 0.0f };
    fft.perform(buf.data(), tmp.data(), false);             // forward -> log spectrum

    for (int i = 0; i < fftSize; ++i)
    {
        // H(w) = exp(log|H| + j*phase)
        const float re = tmp[(size_t)i].real();
        const float im = tmp[(size_t)i].imag();
        const float mag = std::exp(juce::jlimit(-60.0f, 60.0f, re));
        buf[(size_t)i] = { mag * std::cos(im), mag * std::sin(im) };
    }
    fft.perform(buf.data(), tmp.data(), true);              // inverse -> impulse response

    out.resize((size_t)fftSize);
    for (int i = 0; i < fftSize; ++i)
        out[(size_t)i] = tmp[(size_t)i].real();

    if (linearPhase)
    {
        // centre the symmetric impulse
        std::rotate(out.begin(), out.begin() + half, out.end());
    }
}

void IRBuilder::resampleInPlace(juce::AudioBuffer<float>& buf, float ratio)
{
    if (std::abs(ratio - 1.0f) < 1.0e-4f) return;
    const int inLen = buf.getNumSamples();
    const int outLen = juce::jlimit(16, 4 * 1024 * 1024, (int)(inLen * ratio));
    juce::AudioBuffer<float> dst(buf.getNumChannels(), outLen);
    dst.clear();

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const auto* in = buf.getReadPointer(ch);
        auto* o = dst.getWritePointer(ch);
        for (int i = 0; i < outLen; ++i)
        {
            const float pos = (float)i / ratio;
            const int i0 = (int)pos;
            const float fr = pos - (float)i0;
            const float a = (i0 < inLen) ? in[i0] : 0.0f;
            const float b = (i0 + 1 < inLen) ? in[i0 + 1] : 0.0f;
            o[i] = a + (b - a) * fr;
        }
    }
    buf = std::move(dst);
}

void IRBuilder::applyPostFilters(juce::AudioBuffer<float>& ir, const BuildSettings& s)
{
    const int n = ir.getNumSamples();
    const double sr = s.sampleRate;

    // --- low cut ----------------------------------------------------------
    //
    // This matters more than it looks. Minimum-phase IRs made from musical
    // samples carry enormous low-frequency energy, which eats headroom and
    // makes everything sound woolly rather than characterful. A low cut on
    // the IR is close to mandatory in practice.
    if (s.lowCutHz > 21.0f)
    {
        auto coeffs = juce::IIRCoefficients::makeHighPass(sr, s.lowCutHz, 0.707f);
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            juce::IIRFilter f; f.setCoefficients(coeffs);
            f.processSamples(ir.getWritePointer(ch), n);
            juce::IIRFilter f2; f2.setCoefficients(coeffs);
            f2.processSamples(ir.getWritePointer(ch), n);   // 24 dB/oct
        }
    }

    if (s.highCutHz < 19500.0f)
    {
        auto coeffs = juce::IIRCoefficients::makeLowPass(sr, s.highCutHz, 0.707f);
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            juce::IIRFilter f; f.setCoefficients(coeffs);
            f.processSamples(ir.getWritePointer(ch), n);
        }
    }

    // --- spectral tilt ----------------------------------------------------
    if (std::abs(s.tiltDbOct) > 0.01f)
    {
        // low shelf and high shelf pulling in opposite directions around 1 kHz
        const float g = s.tiltDbOct;
        auto lowShelf = juce::IIRCoefficients::makeLowShelf(sr, 250.0, 0.707,
            juce::Decibels::decibelsToGain(-g * 1.5f));
        auto highShelf = juce::IIRCoefficients::makeHighShelf(sr, 4000.0, 0.707,
            juce::Decibels::decibelsToGain(g * 1.5f));
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            juce::IIRFilter a; a.setCoefficients(lowShelf);
            a.processSamples(ir.getWritePointer(ch), n);
            juce::IIRFilter b; b.setCoefficients(highShelf);
            b.processSamples(ir.getWritePointer(ch), n);
        }
    }
}

juce::AudioBuffer<float> IRBuilder::build(const juce::AudioBuffer<float>& source,
    double sourceRateIn,
    const BuildSettings& s)
{
    juce::AudioBuffer<float> result;
    const int srcLen = source.getNumSamples();
    if (srcLen < 16) return result;

    // --- crop -------------------------------------------------------------
    const int c0 = juce::jlimit(0, srcLen - 2, (int)(s.cropStart * srcLen));
    const int c1 = juce::jlimit(c0 + 8, srcLen, (int)(s.cropEnd * srcLen));
    const int cropLen = c1 - c0;

    // --- FFT size: must comfortably exceed the cropped region -------------
    int order = juce::jlimit(kMinFFTOrder, kMaxFFTOrder, s.fftOrder);
    while ((1 << order) < cropLen && order < kMaxFFTOrder) ++order;
    const int fftSize = 1 << order;
    const int half = fftSize / 2;

    // If the crop still doesn't fit (order pinned at kMaxFFTOrder), only the
    // first usedLen samples of it are analysed — everything past that is
    // discarded. Window over usedLen (not cropLen) so that cut lands on a
    // proper taper rather than an abrupt, unwindowed edge.
    const int usedLen = juce::jmin(cropLen, fftSize);

    // --- windowed magnitude spectrum of the source ------------------------
    const int numSrcCh = source.getNumChannels();
    const int outCh = juce::jmin(2, juce::jmax(1, numSrcCh));

    juce::AudioBuffer<float> ir(outCh, fftSize);
    ir.clear();

    juce::dsp::FFT fft(order);

    for (int ch = 0; ch < outCh; ++ch)
    {
        const auto* src = source.getReadPointer(juce::jmin(ch, numSrcCh - 1));

        std::vector<juce::dsp::Complex<float>> buf((size_t)fftSize), tmp((size_t)fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            float v = 0.0f;
            if (i < usedLen)
            {
                // Hann window over the cropped region, as puriFIR does — it
                // removes the edge discontinuity that would otherwise smear
                // broadband junk across the whole magnitude spectrum.
                const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                    * (float)i / (float)juce::jmax(1, usedLen - 1));
                v = src[c0 + i] * w;
            }
            buf[(size_t)i] = { v, 0.0f };
        }
        fft.perform(buf.data(), tmp.data(), false);

        std::vector<float> logMag((size_t)(half + 1));
        for (int i = 0; i <= half; ++i)
        {
            const float mag = std::abs(tmp[(size_t)i]);
            logMag[(size_t)i] = std::log(juce::jmax(mag, 1.0e-7f));
        }

        // --- CHARACTER: where the lifter sits -----------------------------
        //
        //   0%  -> 4 coefficients: pure spectral envelope, the old CepstralIR
        //          room-capture behaviour
        // 100%  -> the full half-spectrum: every harmonic peak preserved,
        //          i.e. puriFIR
        //
        // The mapping is GEOMETRIC, not linear or power-curve. All the useful
        // action lives at small lifter values and a linear map would waste
        // 97% of the knob. Measured on a guitar sample at a 32768-point FFT,
        // with this mapping:
        //
        //   char 0.00  lifter     4   envelope dev 14.11 dB   comb 0.00 dB
        //   char 0.30  lifter    49   envelope dev 14.75 dB   comb 0.00 dB
        //   char 0.50  lifter   256   envelope dev 15.36 dB   comb 3.76 dB
        //   char 0.60  lifter   588   envelope dev 15.43 dB   comb 4.03 dB
        //   char 1.00  lifter 16384   envelope dev 15.45 dB   comb 4.09 dB
        //
        // So the lower half varies envelope smoothness (room character) and
        // the harmonic comb emerges between 0.35 and 0.6. Everything on the
        // knob does something.
        const float ch01 = juce::jlimit(0.0f, 1.0f, s.character);
        const int lifterCutoff = juce::jlimit(4, half,
            (int)std::round(4.0f * std::pow((float)half / 4.0f, ch01)));

        std::vector<float> imp;
        minimumPhaseFromMagnitude(logMag, imp, fftSize, lifterCutoff, s.linearPhase);

        ir.copyFrom(ch, 0, imp.data(), fftSize);
    }

    // --- length and decay shaping ----------------------------------------
    int irLen = juce::jlimit(16, fftSize,
        (int)(s.irLengthMs * 0.001 * s.sampleRate));
    juce::AudioBuffer<float> trimmed(outCh, irLen);

    if (s.linearPhase)
    {
        // minimumPhaseFromMagnitude centres a linear-phase impulse at
        // fftSize/2 (see its trailing std::rotate). Trimming from sample 0,
        // as the minimum-phase path does, would just grab the quiet pre-ring
        // before the wavelet and miss the wavelet itself — so take a window
        // centred on the peak instead.
        const int centre = fftSize / 2;
        const int start = juce::jlimit(0, fftSize - irLen, centre - irLen / 2);
        for (int ch = 0; ch < outCh; ++ch)
            trimmed.copyFrom(ch, 0, ir, ch, start, irLen);
    }
    else
    {
        for (int ch = 0; ch < outCh; ++ch)
            trimmed.copyFrom(ch, 0, ir, ch, 0, irLen);
    }

    // extra exponential decay, and edge fades so truncation never clicks
    const int fade = juce::jmin(irLen / 4, (int)(0.010 * s.sampleRate));
    for (int ch = 0; ch < outCh; ++ch)
    {
        auto* d = trimmed.getWritePointer(ch);

        if (s.linearPhase)
        {
            // decay and fade radiate outward from the centre so the wavelet
            // stays symmetric
            const int centre = irLen / 2;
            if (s.decayShape > 0.001f)
            {
                const float tau = (float)irLen / (1.0f + 24.0f * s.decayShape);
                for (int i = 0; i < irLen; ++i)
                    d[i] *= std::exp(-std::abs((float)(i - centre)) / tau);
            }
            for (int i = 0; i < fade; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi
                    * (float)i / (float)fade);
                d[i] *= w;
                d[irLen - 1 - i] *= w;
            }
        }
        else
        {
            if (s.decayShape > 0.001f)
            {
                const float tau = (float)irLen / (0.5f + 12.0f * s.decayShape);
                for (int i = 0; i < irLen; ++i)
                    d[i] *= std::exp(-(float)i / tau);
            }
            for (int i = 0; i < fade; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi
                    * (float)i / (float)fade);
                d[irLen - 1 - i] *= w;
            }
        }
    }

    // --- reverse ----------------------------------------------------------
    if (s.reverse)
        for (int ch = 0; ch < outCh; ++ch)
        {
            auto* d = trimmed.getWritePointer(ch);
            std::reverse(d, d + irLen);
        }

    // --- stretch ----------------------------------------------------------
    resampleInPlace(trimmed, s.stretch);

    // --- filters and tilt -------------------------------------------------
    applyPostFilters(trimmed, s);

    // --- predelay ---------------------------------------------------------
    const int predelaySamples = juce::jlimit(0, (int)(0.5 * s.sampleRate),
        (int)(s.predelayMs * 0.001 * s.sampleRate));
    const int finalLen = trimmed.getNumSamples() + predelaySamples;

    result.setSize(2, finalLen);
    result.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        const int srcCh = juce::jmin(ch, outCh - 1);
        result.copyFrom(ch, predelaySamples, trimmed, srcCh, 0, trimmed.getNumSamples());
    }

    // --- stereo width on the IR ------------------------------------------
    if (std::abs(s.width - 1.0f) > 0.001f && result.getNumChannels() > 1)
    {
        auto* L = result.getWritePointer(0);
        auto* R = result.getWritePointer(1);
        for (int i = 0; i < finalLen; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float sd = 0.5f * (L[i] - R[i]) * s.width;
            L[i] = m + sd;
            R[i] = m - sd;
        }
    }

    // --- normalise to unit peak so the display and file are predictable ---
    const float peak = juce::jmax(result.getMagnitude(0, finalLen), 1.0e-9f);
    result.applyGain(0.99f / peak);

    return result;
}

//==============================================================================
// Parameters
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
IRForgeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::AudioParameterFloat;
    using PB = juce::AudioParameterBool;
    using PI = juce::AudioParameterInt;

    // The headline control. Defaults HIGH — the old plugin defaulted to a
    // lifter of 128, which discarded roughly half the harmonic structure.
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::character, 1 }, "Character",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));
    layout.add(std::make_unique<PI>(juce::ParameterID{ pid::fftOrder, 1 }, "FFT Size (2^n)",
        kMinFFTOrder, kMaxFFTOrder, 15));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::irLength, 1 }, "IR Length",
        juce::NormalisableRange<float>(5.0f, 4000.0f, 1.0f, 0.4f), 400.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::decayShape, 1 }, "Decay",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<PB>(juce::ParameterID{ pid::linearPhase, 1 }, "Linear Phase", false));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::stretch, 1 }, "Stretch",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.5f), 1.0f));
    layout.add(std::make_unique<PB>(juce::ParameterID{ pid::reverse, 1 }, "Reverse", false));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::predelay, 1 }, "Predelay",
        juce::NormalisableRange<float>(0.0f, 250.0f, 0.1f, 0.5f), 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::lowCut, 1 }, "IR Low Cut",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.3f), 30.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::highCut, 1 }, "IR High Cut",
        juce::NormalisableRange<float>(500.0f, 20000.0f, 1.0f, 0.3f), 20000.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::tilt, 1 }, "Tilt",
        juce::NormalisableRange<float>(-6.0f, 6.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::width, 1 }, "IR Width",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::cropStart, 1 }, "Crop Start",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::cropEnd, 1 }, "Crop End",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 1.0f));

    layout.add(std::make_unique<P>(juce::ParameterID{ pid::mix, 1 }, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::gain, 1 }, "Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<PB>(juce::ParameterID{ pid::gainMatch, 1 }, "Gain Match", true));
    layout.add(std::make_unique<PB>(juce::ParameterID{ pid::bypass, 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
IRForgeAudioProcessor::IRForgeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    Thread("IRForge Builder"),
    apvts(*this, nullptr, "IRFORGE", createParameterLayout())
{
    formatManager.registerBasicFormats();
    startThread(juce::Thread::Priority::low);
}

IRForgeAudioProcessor::~IRForgeAudioProcessor()
{
    signalThreadShouldExit();
    notify();
    stopThread(2000);
}

double IRForgeAudioProcessor::getTailLengthSeconds() const
{
    return currentTailSeconds;
}

void IRForgeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = 2;

    convolution.prepare(spec);
    convolution.reset();

    dryBuffer.setSize(2, samplesPerBlock);
    mixSmoothed.reset(sampleRate, 0.02);
    gainSmoothed.reset(sampleRate, 0.02);

    requestRebuild();
}

void IRForgeAudioProcessor::releaseResources()
{
    convolution.reset();
}

bool IRForgeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
// A fixed-resolution min/max overview, independent of pixel width, computed
// once at load time so the editor can always draw the whole timeline even
// though only a bounded window of it lives in sourceBuffer. Streamed through
// in chunks so memory use is bounded to the overview array, not the file.
namespace { constexpr int kOverviewPoints = 3000; }

void IRForgeAudioProcessor::buildOverviewFromReader(juce::AudioFormatReader& reader,
    std::vector<float>& mn, std::vector<float>& mx)
{
    const juce::int64 total = reader.lengthInSamples;
    mn.assign((size_t)kOverviewPoints, 0.0f);
    mx.assign((size_t)kOverviewPoints, 0.0f);
    if (total < 2) return;

    constexpr int kChunkSamples = 1 << 16;
    juce::AudioBuffer<float> chunk((int)juce::jmax(1u, juce::jmin(2u, reader.numChannels)), kChunkSamples);

    for (juce::int64 pos = 0; pos < total; pos += kChunkSamples)
    {
        const int len = (int)juce::jmin((juce::int64)kChunkSamples, total - pos);
        reader.read(&chunk, 0, len, pos, true, true);

        for (int i = 0; i < len; ++i)
        {
            int bin = (int)((pos + i) * kOverviewPoints / total);
            bin = juce::jlimit(0, kOverviewPoints - 1, bin);
            for (int ch = 0; ch < chunk.getNumChannels(); ++ch)
            {
                const float v = chunk.getReadPointer(ch)[i];
                mn[(size_t)bin] = juce::jmin(mn[(size_t)bin], v);
                mx[(size_t)bin] = juce::jmax(mx[(size_t)bin], v);
            }
        }
    }
}

void IRForgeAudioProcessor::buildOverviewFromBuffer(const juce::AudioBuffer<float>& buf,
    std::vector<float>& mn, std::vector<float>& mx)
{
    const int total = buf.getNumSamples();
    mn.assign((size_t)kOverviewPoints, 0.0f);
    mx.assign((size_t)kOverviewPoints, 0.0f);
    if (total < 2) return;

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const auto* d = buf.getReadPointer(ch);
        for (int i = 0; i < total; ++i)
        {
            int bin = juce::jlimit(0, kOverviewPoints - 1, (int)((juce::int64)i * kOverviewPoints / total));
            mn[(size_t)bin] = juce::jmin(mn[(size_t)bin], d[i]);
            mx[(size_t)bin] = juce::jmax(mx[(size_t)bin], d[i]);
        }
    }
}

bool IRForgeAudioProcessor::loadSourceFile(const juce::File& file, double windowStartSeconds)
{
    return loadSourceFileInternal(file, windowStartSeconds, true);
}

bool IRForgeAudioProcessor::loadSourceFileInternal(const juce::File& file, double windowStartSeconds, bool isNewFile)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return false;

    const double totalSeconds = (double)reader->lengthInSamples / juce::jmax(1.0, reader->sampleRate);
    // Leave room for at least a small window at the very end of the file.
    const double maxStart = juce::jmax(0.0, totalSeconds - 1.0);
    const double start = juce::jlimit(0.0, maxStart, windowStartSeconds);
    const juce::int64 startSample = (juce::int64)(start * reader->sampleRate);
    const juce::int64 remaining = juce::jmax((juce::int64)0, reader->lengthInSamples - startSample);

    const int len = (int)juce::jmin((juce::int64)(reader->sampleRate * kMaxLoadWindowSeconds), remaining);
    if (len < 16) return false;

    juce::AudioBuffer<float> temp((int)juce::jmin(2u, reader->numChannels), len);
    reader->read(&temp, 0, len, startSample, true, true);

    // Only a genuinely new load pays for a full pass over the file to build
    // the overview — shifting the resident window (ensureWindowCovers) just
    // moves the read pointer, it doesn't touch what's already on screen.
    std::vector<float> newOverviewMin, newOverviewMax;
    if (isNewFile)
        buildOverviewFromReader(*reader, newOverviewMin, newOverviewMax);

    {
        const juce::ScopedLock sl(sourceLock);
        sourceBuffer = std::move(temp);
        sourceRate = reader->sampleRate;
        sourceName = file.getFileNameWithoutExtension();
        if (isNewFile)
        {
            overviewMin = std::move(newOverviewMin);
            overviewMax = std::move(newOverviewMax);
            overviewTotalSeconds = totalSeconds;
        }
    }
    sourceGeneration.fetch_add(1, std::memory_order_relaxed);

    currentSourceFile = file;
    sourceFileTotalSeconds = totalSeconds;
    sourceWindowStartSeconds = start;

    fileBacked.store(true, std::memory_order_relaxed);
    windowStartSecondsAtomic.store(start, std::memory_order_relaxed);
    windowLenSecondsAtomic.store((double)len / juce::jmax(1.0, reader->sampleRate), std::memory_order_relaxed);
    fileTotalSecondsAtomic.store(totalSeconds, std::memory_order_relaxed);

    if (isNewFile)
    {
        // Reset the crop to the front of the file, sized to whatever's
        // guaranteed to be resident at once, so the initial selection never
        // implies more than what's actually loaded.
        const float capFrac = totalSeconds > 0.0
            ? (float)juce::jlimit(0.0, 1.0, kMaxLoadWindowSeconds / totalSeconds)
            : 1.0f;
        if (auto* p = apvts.getParameter(pid::cropStart)) p->setValueNotifyingHost(0.0f);
        if (auto* p = apvts.getParameter(pid::cropEnd)) p->setValueNotifyingHost(capFrac);
    }

    requestRebuild();
    return true;
}

bool IRForgeAudioProcessor::ensureWindowCovers(double startSeconds, double endSeconds)
{
    if (currentSourceFile == juce::File{}) return false; // fully resident already (recording, or nothing loaded)

    const double windowLenSec = sourceRate > 0.0 ? (double)sourceBuffer.getNumSamples() / sourceRate : 0.0;
    const double windowEndSec = sourceWindowStartSeconds + windowLenSec;

    // small slack so float error right at an edge doesn't trigger a reload
    if (startSeconds >= sourceWindowStartSeconds - 0.01 && endSeconds <= windowEndSec + 0.01)
        return false;

    return loadSourceFileInternal(currentSourceFile, startSeconds, false);
}

void IRForgeAudioProcessor::clearSource()
{
    {
        const juce::ScopedLock sl(sourceLock);
        sourceBuffer.setSize(0, 0);
        sourceName = {};
        overviewMin.clear();
        overviewMax.clear();
        overviewTotalSeconds = 0.0;
    }
    sourceGeneration.fetch_add(1, std::memory_order_relaxed);
    currentSourceFile = juce::File{};
    sourceFileTotalSeconds = 0.0;
    sourceWindowStartSeconds = 0.0;
    fileBacked.store(false, std::memory_order_relaxed);
    windowStartSecondsAtomic.store(0.0, std::memory_order_relaxed);
    windowLenSecondsAtomic.store(0.0, std::memory_order_relaxed);
    fileTotalSecondsAtomic.store(0.0, std::memory_order_relaxed);
    requestRebuild();
}

bool IRForgeAudioProcessor::saveIRToFile(const juce::File& file)
{
    const juce::ScopedLock sl(irLock);
    if (displayIR.getNumSamples() < 8) return false;

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr) return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), spec.sampleRate,
            (unsigned int)displayIR.getNumChannels(), 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release();

    writer->writeFromAudioSampleBuffer(displayIR, 0, displayIR.getNumSamples());
    return true;
}

//==============================================================================
// Recording
//==============================================================================
bool IRForgeAudioProcessor::startRecording()
{
    if (recording.load()) return false;

    const int capacity = (int)(spec.sampleRate * kMaxRecordSeconds);
    recordBuffer.setSize(2, juce::jmax(16, capacity), false, false, true);
    recordBuffer.clear();
    recordWritePos.store(0);
    recordSampleRate = spec.sampleRate;
    recording.store(true);
    return true;
}

void IRForgeAudioProcessor::stopRecording()
{
    recording.store(false);
}

void IRForgeAudioProcessor::finalizeRecording(const juce::File& fileToSaveTo)
{
    // in case stopRecording() wasn't called first
    recording.store(false);

    const int len = recordWritePos.load();
    if (len < 16)
    {
        recordBuffer.setSize(0, 0);
        return;
    }

    juce::AudioBuffer<float> captured(recordBuffer.getNumChannels(), len);
    for (int ch = 0; ch < captured.getNumChannels(); ++ch)
        captured.copyFrom(ch, 0, recordBuffer, ch, 0, len);
    recordBuffer.setSize(0, 0);

    if (fileToSaveTo != juce::File{})
    {
        fileToSaveTo.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream(fileToSaveTo.createOutputStream());
        if (stream != nullptr)
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(stream.get(), recordSampleRate,
                    (unsigned int)captured.getNumChannels(), 24, {}, 0));
            if (writer != nullptr)
            {
                stream.release();
                writer->writeFromAudioSampleBuffer(captured, 0, captured.getNumSamples());
            }
        }
    }

    // load the take straight in as the source — no need to re-read it back
    // off disk, the file on disk (if any) is just a copy for keeping.
    const double recordedSeconds = (double)captured.getNumSamples() / juce::jmax(1.0, recordSampleRate);
    std::vector<float> newOverviewMin, newOverviewMax;
    buildOverviewFromBuffer(captured, newOverviewMin, newOverviewMax);
    {
        const juce::ScopedLock sl(sourceLock);
        sourceBuffer = std::move(captured);
        sourceRate = recordSampleRate;
        sourceName = (fileToSaveTo != juce::File{})
            ? fileToSaveTo.getFileNameWithoutExtension()
            : juce::String("Recording");
        overviewMin = std::move(newOverviewMin);
        overviewMax = std::move(newOverviewMax);
        overviewTotalSeconds = recordedSeconds;
    }
    sourceGeneration.fetch_add(1, std::memory_order_relaxed);
    // A recording is loaded in full (not windowed) — there's no file on disk
    // to re-read a different window from, so it's never treated as file-backed.
    currentSourceFile = juce::File{};
    sourceFileTotalSeconds = 0.0;
    sourceWindowStartSeconds = 0.0;
    fileBacked.store(false, std::memory_order_relaxed);
    windowStartSecondsAtomic.store(0.0, std::memory_order_relaxed);
    windowLenSecondsAtomic.store(0.0, std::memory_order_relaxed);
    fileTotalSecondsAtomic.store(0.0, std::memory_order_relaxed);
    if (auto* p = apvts.getParameter(pid::cropStart)) p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter(pid::cropEnd)) p->setValueNotifyingHost(1.0f);
    requestRebuild();
}

//==============================================================================
BuildSettings IRForgeAudioProcessor::gatherSettings() const
{
    auto get = [this](const juce::String& id) -> float
        {
            if (auto* p = apvts.getRawParameterValue(id)) return p->load();
            return 0.0f;
        };

    BuildSettings s;
    s.character = get(pid::character);
    s.fftOrder = (int)get(pid::fftOrder);
    s.irLengthMs = get(pid::irLength);
    s.decayShape = get(pid::decayShape);
    s.linearPhase = get(pid::linearPhase) > 0.5f;
    s.stretch = get(pid::stretch);
    s.reverse = get(pid::reverse) > 0.5f;
    s.predelayMs = get(pid::predelay);
    s.lowCutHz = get(pid::lowCut);
    s.highCutHz = get(pid::highCut);
    s.tiltDbOct = get(pid::tilt);
    s.width = get(pid::width);
    s.cropStart = get(pid::cropStart);
    s.cropEnd = get(pid::cropEnd);
    s.sampleRate = spec.sampleRate;

    // cropStart/cropEnd are fractions of the *whole* source timeline, but
    // IRBuilder::build() receives only whatever window is resident in
    // sourceBuffer — translate accordingly. Reads the atomic mirrors rather
    // than the plain currentSourceFile/sourceWindowStartSeconds members,
    // since this runs on the build thread and those are only ever written on
    // the message thread.
    if (fileBacked.load(std::memory_order_relaxed))
    {
        const double total = fileTotalSecondsAtomic.load(std::memory_order_relaxed);
        const double winStart = windowStartSecondsAtomic.load(std::memory_order_relaxed);
        const double winLen = windowLenSecondsAtomic.load(std::memory_order_relaxed);
        if (total > 0.0 && winLen > 0.0)
        {
            const double cropStartSec = (double)s.cropStart * total;
            const double cropEndSec = (double)s.cropEnd * total;
            s.cropStart = (float)juce::jlimit(0.0, 1.0, (cropStartSec - winStart) / winLen);
            s.cropEnd = (float)juce::jlimit(0.0, 1.0, (cropEndSec - winStart) / winLen);
        }
    }

    if (s.cropEnd <= s.cropStart + 0.001f)
        s.cropEnd = juce::jmin(1.0f, s.cropStart + 0.001f);
    return s;
}

void IRForgeAudioProcessor::run()
{
    // Cached locally to the thread: only re-copied from sourceBuffer when
    // sourceGeneration changes (a new file/recording/clear), not on every
    // rebuild. Previously this copied the *entire* loaded buffer on every
    // parameter tweak, which made per-knob-turn cost scale with how much
    // audio was loaded — the real reason the load window used to be kept
    // small. Now a 3-minute load costs the same per-tweak as a 3-second one;
    // the copy only happens once, right after a new source is loaded.
    juce::AudioBuffer<float> cachedSource;
    double cachedRate = 44100.0;
    int lastSourceGen = -1;

    while (!threadShouldExit())
    {
        // poll for parameter changes as well as explicit requests, so moving a
        // knob rebuilds without the editor having to push anything
        auto s = gatherSettings();
        const bool paramsChanged = (s != lastBuilt);
        const bool explicitRequest = rebuildFlag.exchange(false);
        const int curGen = sourceGeneration.load(std::memory_order_relaxed);
        const bool sourceChanged = (curGen != lastSourceGen);

        if (paramsChanged || explicitRequest || sourceChanged)
        {
            lastBuilt = s;

            if (sourceChanged)
            {
                const juce::ScopedLock sl(sourceLock);
                cachedSource.makeCopyOf(sourceBuffer);
                cachedRate = sourceRate;
                lastSourceGen = curGen;
            }

            if (cachedSource.getNumSamples() >= 16)
            {
                building.store(true);
                auto built = IRBuilder::build(cachedSource, cachedRate, s);
                building.store(false);

                if (built.getNumSamples() > 8 && !threadShouldExit())
                {
                    {
                        const juce::ScopedLock pl(pendingLock);
                        pendingIR = std::move(built);
                    }
                    pendingReady.store(true);
                    triggerAsyncUpdate();
                }
            }
        }

        wait(60);
    }
}

void IRForgeAudioProcessor::handleAsyncUpdate()
{
    if (!pendingReady.exchange(false)) return;

    juce::AudioBuffer<float> ir;
    {
        const juce::ScopedLock pl(pendingLock);
        ir.makeCopyOf(pendingIR);
    }
    if (ir.getNumSamples() < 8) return;

    // energy of the IR, for gain-matched A/B: convolving with a dense musical
    // IR otherwise changes level enormously and every comparison is biased
    double energy = 0.0;
    for (int ch = 0; ch < ir.getNumChannels(); ++ch)
    {
        const auto* d = ir.getReadPointer(ch);
        for (int i = 0; i < ir.getNumSamples(); ++i) energy += (double)d[i] * d[i];
    }
    energy /= juce::jmax(1, ir.getNumChannels());
    irEnergyGain = (energy > 1.0e-12) ? (float)(1.0 / std::sqrt(energy)) : 1.0f;

    currentTailSeconds = ir.getNumSamples() / juce::jmax(1.0, spec.sampleRate);

    {
        const juce::ScopedLock sl(irLock);
        displayIR.makeCopyOf(ir);
    }
    irGeneration.fetch_add(1, std::memory_order_relaxed);

    // Normalise::no: the IR is already peak-normalised in IRBuilder::build()
    // and gain-matched by irEnergyGain just above. Leaving JUCE's own
    // Normalise::yes on top of that stacked a second, opaque attenuation onto
    // the first, which is why the wet signal used to come out so quiet that
    // Output had to be cranked all the way up to compensate.
    convolution.loadImpulseResponse(std::move(ir), spec.sampleRate,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::no);
    convolutionReady = true;

    if (onIRUpdated) onIRUpdated();
}

//==============================================================================
void IRForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    if (recording.load())
    {
        const int pos = recordWritePos.load();
        const int capacity = recordBuffer.getNumSamples();
        const int room = juce::jmax(0, capacity - pos);
        const int toCopy = juce::jmin(numSamples, room);

        if (toCopy > 0 && numCh > 0)
        {
            for (int ch = 0; ch < recordBuffer.getNumChannels(); ++ch)
                recordBuffer.copyFrom(ch, pos, buffer, juce::jmin(ch, numCh - 1), 0, toCopy);
            recordWritePos.store(pos + toCopy);
        }

        // IR output is deactivated while recording — pass the input straight
        // through dry so what you hear is what's being captured.
        return;
    }

    const bool bypassed = apvts.getRawParameterValue(pid::bypass)->load() > 0.5f;
    if (bypassed || !convolutionReady)
        return;

    if (dryBuffer.getNumSamples() < numSamples || dryBuffer.getNumChannels() < numCh)
        dryBuffer.setSize(juce::jmax(2, numCh), numSamples, false, false, true);

    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);

    const bool match = apvts.getRawParameterValue(pid::gainMatch)->load() > 0.5f;
    mixSmoothed.setTargetValue(apvts.getRawParameterValue(pid::mix)->load());
    gainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue(pid::gain)->load()));

    // Target RMS for the gain-matched wet signal. Now that the IR isn't also
    // being silently re-normalised by JUCE (see handleAsyncUpdate), 0.3
    // (~-10.5 dBFS RMS) gives a usable working level without needing Output
    // cranked up, while still leaving headroom for peaky, comb-y IRs.
    constexpr float kGainMatchTargetRMS = 0.3f;
    const float matchGain = match ? juce::jlimit(0.05f, 20.0f, irEnergyGain * kGainMatchTargetRMS) : 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        const float g = gainSmoothed.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer(ch);
            const float dry = dryBuffer.getReadPointer(ch)[i];
            // Output (g) scales only the wet/convolved signal, not the dry
            // pass-through. Previously g scaled the whole mixed result, so
            // cranking Output to compensate for a quiet IR meant that turning
            // Mix down (more dry) made the dry signal blow up by the same
            // amount — the "turning mix down gets extremely loud" symptom.
            w[i] = dry * (1.0f - m) + (w[i] * matchGain * g) * m;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* IRForgeAudioProcessor::createEditor()
{
    return new IRForgeAudioProcessorEditor(*this);
}

void IRForgeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // remember which sample was loaded, so a session reopens where it left off
    state.setProperty("sourcePath", sourceName, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void IRForgeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            requestRebuild();
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IRForgeAudioProcessor();
}