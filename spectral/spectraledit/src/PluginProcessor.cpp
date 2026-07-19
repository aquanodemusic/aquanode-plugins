#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

//==============================================================================
SpectralEditProcessor::SpectralEditProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    buildWindow();
    keyboardState.addListener(this);
}

SpectralEditProcessor::~SpectralEditProcessor()
{
    keyboardState.removeListener(this);
}

//==============================================================================
void SpectralEditProcessor::buildWindow()
{
    hannWin.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        hannWin[i] = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi
            * (float)i / (float)(fftSize - 1)));
}

//==============================================================================
void SpectralEditProcessor::setFftOrder(int order)
{
    order = juce::jlimit(9, 14, order);
    fftOrder = order;
    fftSize = 1 << order;
    hopSize = fftSize / 2;
    numBins = fftSize / 2 + 1;
    buildWindow();

    if (fileLoaded.load() && lastLoadedFile.existsAsFile())
        loadFile(lastLoadedFile, lastLoadStartSec.load());
}

//==============================================================================
void SpectralEditProcessor::prepareToPlay(double sampleRate, int)
{
    currentSR = sampleRate;

    // 80 ms glide so scrubbing feels smooth but still responsive
    scrubSmoothed.reset(sampleRate, 0.08);
    scrubSmoothed.setCurrentAndTargetValue(0.0);
}

void SpectralEditProcessor::releaseResources() {}

//==============================================================================
void SpectralEditProcessor::processBlock(juce::AudioBuffer<float>& buf,
    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buf.clear();

    keyboardState.processNextMidiBuffer(midi, 0, buf.getNumSamples(), true);

    // ── DAW transport: stop voices when playback transitions to stopped ───────
    {
        bool isPlaying = false;
        if (auto* ph = getPlayHead())
        {
            juce::AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition(pos))
                isPlaying = pos.isPlaying;
        }
        const bool wasPlaying = transportWasPlaying.exchange(isPlaying);
        if (wasPlaying && !isPlaying)
        {
            std::lock_guard<std::mutex> lk(voicesMutex);
            for (auto& v : voices) v.on = false;
            displayPlayhead.store(-1.0f);
        }
    }

    if (compiling.load()) return;

    std::unique_lock<std::mutex> sl(synthMutex, std::try_to_lock);
    if (!sl.owns_lock()) return;

    const int numOut = buf.getNumSamples();
    const int synthLen = synthBuf.getNumSamples();
    if (synthLen == 0) return;

    const float* srcL = synthBuf.getReadPointer(0);
    const float* srcR = synthBuf.getNumChannels() > 1
        ? synthBuf.getReadPointer(1) : srcL;
    float* outL = buf.getWritePointer(0);
    float* outR = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;

    std::lock_guard<std::mutex> vl(voicesMutex);

    for (auto& v : voices)
    {
        if (!v.on) continue;

        for (int i = 0; i < numOut; ++i)
        {
            const int p0 = (int)v.pos;
            if (p0 >= synthLen) { v.on = false; break; }
            const int   p1 = std::min(p0 + 1, synthLen - 1);
            const float frac = (float)(v.pos - p0);

            const float sL = srcL[p0] + frac * (srcL[p1] - srcL[p0]);
            const float sR = srcR[p0] + frac * (srcR[p1] - srcR[p0]);

            outL[i] += sL;
            if (outR) outR[i] += sR;

            v.pos += v.rate;
        }
    }

    // ── Update GUI playhead ───────────────────────────────────────────────────
    {
        const int    cLen = synthContentLen.load();
        const double refLen = (cLen > 0) ? (double)cLen : (double)synthLen;

        float ph = -1.0f;
        for (const auto& v : voices)
        {
            if (v.on && synthLen > 0)
            {
                ph = (float)(v.pos / refLen);
                break;
            }
        }
        displayPlayhead.store(ph);
    }

    // ── Scrub playback ────────────────────────────────────────────────────────
    // Two paths:
    //   scrubMaxSpeed == 0  → original SmoothedValue (80 ms) glide – no speed cap
    //   scrubMaxSpeed  > 0  → per-sample advance clamped to
    //                         lerp(200×, 1×, scrubMaxSpeed) samples/sample
    //
    // Both paths use synthContentLen (= nF×hopSize) as the position reference so
    // scrubTargetFraction 0..1 always spans the real audio, not the zero tail.
    if (scrubActive.load() && synthLen > 0)
    {
        const int    cLen = synthContentLen.load();
        const double refLen = (cLen > 0) ? (double)cLen : (double)synthLen;

        const double targetPos = (double)scrubTargetFraction.load() * refLen;
        const float  sm = scrubMaxSpeed.load();

        // Snap scrubCurrentPos on the first audio block after a mouse-down
        const double snapPos = scrubSnapPos.load();
        if (snapPos >= 0.0)
        {
            scrubCurrentPos = snapPos;
            scrubSnapPos.store(-1.0);
        }

        if (sm <= 0.0f)
        {
            // ── Original smooth-glide behaviour ────────────────────────────
            scrubSmoothed.setTargetValue(targetPos);

            for (int i = 0; i < numOut; ++i)
            {
                const double pos = scrubSmoothed.getNextValue();
                const int p0 = juce::jlimit(0, synthLen - 1, (int)pos);
                const int p1 = std::min(p0 + 1, synthLen - 1);
                const float frac = (float)(pos - (double)p0);

                outL[i] += srcL[p0] + frac * (srcL[p1] - srcL[p0]);
                if (outR) outR[i] += srcR[p0] + frac * (srcR[p1] - srcR[p0]);
            }

            scrubCurrentPos = scrubSmoothed.getCurrentValue(); // keep in sync
            displayPlayhead.store((float)(scrubCurrentPos / refLen));
        }
        else
        {
            // ── Speed-limited scrub ────────────────────────────────────────
            // maxAdv: samples advanced per output sample
            //   sm = 0 → ~200 (near-instant, matching the glide feel)
            //   sm = 1 → 1.0  (1× real-time)
            //   in-between: linear
            const double maxAdv = 1.0 + (1.0 - (double)sm) * 199.0;

            for (int i = 0; i < numOut; ++i)
            {
                const double delta = targetPos - scrubCurrentPos;
                if (delta > maxAdv) scrubCurrentPos += maxAdv;
                else if (delta < -maxAdv) scrubCurrentPos -= maxAdv;
                else                     scrubCurrentPos = targetPos;

                scrubCurrentPos = juce::jlimit(0.0, (double)(synthLen - 1),
                    scrubCurrentPos);

                const int p0 = (int)scrubCurrentPos;
                const int p1 = std::min(p0 + 1, synthLen - 1);
                const float frac = (float)(scrubCurrentPos - (double)p0);

                outL[i] += srcL[p0] + frac * (srcL[p1] - srcL[p0]);
                if (outR) outR[i] += srcR[p0] + frac * (srcR[p1] - srcR[p0]);
            }

            // Keep SmoothedValue in sync so switching back to sm==0 is seamless
            scrubSmoothed.setCurrentAndTargetValue(scrubCurrentPos);
            displayPlayhead.store((float)(scrubCurrentPos / refLen));
        }
    }
}

//==============================================================================
void SpectralEditProcessor::stopAllVoices()
{
    std::lock_guard<std::mutex> lk(voicesMutex);
    for (auto& v : voices) v.on = false;
    displayPlayhead.store(-1.0f);
}

//==============================================================================
void SpectralEditProcessor::loadFile(const juce::File& file, double startSec)
{
    lastLoadedFile = file;
    lastLoadStartSec.store(startSec);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> rdr(fm.createReaderFor(file));
    if (!rdr) return;

    sourceSampleRate = rdr->sampleRate;
    fileName = file.getFileName();

    const juce::int64 startSample = (juce::int64)(startSec * sourceSampleRate);
    const juce::int64 available = rdr->lengthInSamples - startSample;
    if (available <= 0) return;

    const int maxSamples = (int)(sourceSampleRate * 60.0);
    const int numSamples = (int)std::min((juce::int64)maxSamples, available);
    if (numSamples < fftSize) return;

    juce::AudioBuffer<float> raw(1, numSamples);
    rdr->read(&raw, 0, numSamples, startSample, true, false);

    analyzeAudio(raw, sourceSampleRate);
    fileLoaded.store(true);
    resynthesize();
}

//==============================================================================
void SpectralEditProcessor::analyzeAudio(const juce::AudioBuffer<float>& mono,
    double sr)
{
    sourceSampleRate = sr;
    const int   N = mono.getNumSamples();
    const float* src = mono.getReadPointer(0);

    const int nFrames = std::max(0, (N - fftSize) / hopSize + 1);

    juce::dsp::FFT localFft(fftOrder);
    std::vector<float> fftBuf(fftSize * 2, 0.0f);

    std::vector<std::vector<std::complex<float>>> newData(
        nFrames, std::vector<std::complex<float>>(numBins, 0.0f));

    for (int f = 0; f < nFrames; ++f)
    {
        const int start = f * hopSize;
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);

        for (int i = 0; i < fftSize && (start + i) < N; ++i)
            fftBuf[i] = src[start + i] * hannWin[i];

        localFft.performRealOnlyForwardTransform(fftBuf.data(), false);

        auto* cPtr = reinterpret_cast<std::complex<float>*>(fftBuf.data());
        for (int b = 0; b < numBins; ++b)
            newData[f][b] = cPtr[b];
    }

    std::lock_guard<std::mutex> lk(spectralMutex);
    spectralData = std::move(newData);
    numFrames = nFrames;
}

//==============================================================================
void SpectralEditProcessor::resynthesize()
{
    compiling.store(true);

    const int capOrder = fftOrder;
    const int capFftSize = fftSize;
    const int capHop = hopSize;
    const int capBins = numBins;

    std::thread([this, capOrder, capFftSize, capHop, capBins]()
        {
            std::vector<std::vector<std::complex<float>>> local;
            int nF;
            {
                std::lock_guard<std::mutex> lk(spectralMutex);
                local = spectralData;
                nF = numFrames;
            }

            if (nF == 0) { compiling.store(false); return; }

            const int outLen = nF * capHop + capFftSize;
            std::vector<float> out(outLen, 0.0f);
            std::vector<float> winSum(outLen, 0.0f);
            std::vector<float> fftBuf(capFftSize * 2, 0.0f);

            juce::dsp::FFT localFft(capOrder);

            std::vector<float> win(capFftSize);
            for (int i = 0; i < capFftSize; ++i)
                win[i] = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi
                    * (float)i / (float)(capFftSize - 1)));

            for (int f = 0; f < nF; ++f)
            {
                std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
                auto* cPtr = reinterpret_cast<std::complex<float>*>(fftBuf.data());

                for (int b = 0; b < capBins; ++b)
                    cPtr[b] = local[f][b];

                for (int b = 1; b < capFftSize / 2; ++b)
                    cPtr[capFftSize - b] = std::conj(local[f][b]);

                localFft.performRealOnlyInverseTransform(fftBuf.data());

                const int start = f * capHop;
                for (int i = 0; i < capFftSize && (start + i) < outLen; ++i)
                {
                    const float s = fftBuf[i];
                    const float w = win[i];
                    out[start + i] += s * w;
                    winSum[start + i] += w * w;    // WOLA: window applied at analysis AND synthesis, so normalise by ∑w²
                }
            }

            for (int i = 0; i < outLen; ++i)
                if (winSum[i] > 1e-6f)
                    out[i] /= winSum[i];

            {
                std::lock_guard<std::mutex> lk(synthMutex);
                synthBuf.setSize(2, outLen, false, true, false);
                auto* L = synthBuf.getWritePointer(0);
                auto* R = synthBuf.getWritePointer(1);
                std::copy(out.begin(), out.end(), L);
                std::copy(out.begin(), out.end(), R);
            }

            synthBufLen.store(outLen);
            synthContentLen.store(nF * capHop);   // meaningful audio samples (no zero-tail)
            compiling.store(false);
        }).detach();
}

//==============================================================================
juce::AudioBuffer<float> SpectralEditProcessor::copySynthBuffer()
{
    std::lock_guard<std::mutex> lk(synthMutex);
    juce::AudioBuffer<float> copy;
    const int nSamples = synthBuf.getNumSamples();
    const int nChans = synthBuf.getNumChannels();
    if (nSamples > 0 && nChans > 0)
    {
        copy.setSize(nChans, nSamples);
        for (int ch = 0; ch < nChans; ++ch)
            copy.copyFrom(ch, 0, synthBuf, ch, 0, nSamples);
    }
    return copy;
}

//==============================================================================
// cF: clamp frame index;  cB: clamp bin index
static inline int cF(int v, int n) { return juce::jlimit(0, std::max(0, n - 1), v); }
static inline int cB(int v, int nb) { return juce::jlimit(0, nb - 1, v); }

//==============================================================================
void SpectralEditProcessor::applyMirrorLR(const SelRect& s)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);

        for (int a = x0, b = x1; a < b; ++a, --b)
            for (int y = y0; y <= y1; ++y)
                std::swap(spectralData[a][y], spectralData[b][y]);
    }
    resynthesize();
}

void SpectralEditProcessor::applyMirrorUD(const SelRect& s)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);

        for (int a = y0, b = y1; a < b; ++a, --b)
            for (int x = x0; x <= x1; ++x)
                std::swap(spectralData[x][a], spectralData[x][b]);
    }
    resynthesize();
}

void SpectralEditProcessor::applyDelete(const SelRect& s)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);

        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                spectralData[x][y] = 0.0f;
    }
    resynthesize();
}

void SpectralEditProcessor::applyRotateBinsUp(const SelRect& s, int bins)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);
        const int range = y1 - y0 + 1;
        if (range <= 1) return;

        bins = ((bins % range) + range) % range;
        if (bins == 0) return;

        for (int x = x0; x <= x1; ++x)
        {
            std::vector<std::complex<float>> tmp(range);
            for (int i = 0; i < range; ++i) tmp[i] = spectralData[x][y0 + i];
            for (int i = 0; i < range; ++i) spectralData[x][y0 + i] = tmp[(i + bins) % range];
        }
    }
    resynthesize();
}

void SpectralEditProcessor::applyRotateRight(const SelRect& s, int frames)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);
        const int range = x1 - x0 + 1;
        if (range <= 1) return;

        frames = ((frames % range) + range) % range;
        if (frames == 0) return;

        for (int y = y0; y <= y1; ++y)
        {
            std::vector<std::complex<float>> tmp(range);
            for (int i = 0; i < range; ++i) tmp[i] = spectralData[x0 + i][y];
            for (int i = 0; i < range; ++i)
                spectralData[x0 + (i + frames) % range][y] = tmp[i];
        }
    }
    resynthesize();
}

void SpectralEditProcessor::applyChangeVolume(const SelRect& s, float pct)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);

        const float scale = juce::jlimit(0.0f, 2.0f, (100.0f + pct) / 100.0f);

        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                spectralData[x][y] *= scale;
    }
    resynthesize();
}

//==============================================================================
// ── New drawing tools ─────────────────────────────────────────────────────────

float SpectralEditProcessor::getMaxMagnitude() const noexcept
{
    std::lock_guard<std::mutex> lk(spectralMutex);
    float maxM = 1e-10f;
    for (const auto& frame : spectralData)
        for (const auto& c : frame)
        {
            const float m = std::abs(c);
            if (m > maxM) maxM = m;
        }
    return maxM;
}

void SpectralEditProcessor::applyPaintStroke(int frame, int binCenter,
    int halfThick, float amplitude)
{
    std::lock_guard<std::mutex> lk(spectralMutex);
    if (numFrames == 0 || frame < 0 || frame >= numFrames) return;

    const int b0 = std::max(0, binCenter - halfThick);
    const int b1 = std::min(numBins - 1, binCenter + halfThick);

    for (int b = b0; b <= b1; ++b)
    {
        // Cosine taper across the brush width for smooth spectral edges
        const float dx = (halfThick > 0)
            ? (float)(b - binCenter) / (float)halfThick
            : 0.0f;
        const float taper = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * dx));
        const float mag = amplitude * taper;

        // Preserve existing phase; replace magnitude
        const float phase = std::arg(spectralData[frame][b]);
        spectralData[frame][b] = std::polar(mag, phase);
    }
}

void SpectralEditProcessor::applySmearStroke(int frame, int binCenter, int radius)
{
    // Spectral smear = moving-average in the TIME direction for each affected bin.
    // We average magnitudes (not complex values) and keep the original phase so
    // phase cancellation can never reduce the level.
    if (radius < 1) radius = 1;
    std::lock_guard<std::mutex> lk(spectralMutex);
    if (numFrames == 0) return;

    const int f0 = std::max(0, frame - radius);
    const int f1 = std::min(numFrames - 1, frame + radius);
    const int b0 = std::max(0, binCenter - radius);
    const int b1 = std::min(numBins - 1, binCenter + radius);

    // Snapshot so we read from unmodified data
    const int pW = f1 - f0 + 1;
    const int pH = b1 - b0 + 1;
    std::vector<std::complex<float>> patch(pW * pH);
    for (int f = f0; f <= f1; ++f)
        for (int b = b0; b <= b1; ++b)
            patch[(f - f0) * pH + (b - b0)] = spectralData[f][b];

    // For each bin in the brush area, apply a boxcar average of magnitudes
    // across all frames in the brush window, then write back with original phase.
    for (int b = b0; b <= b1; ++b)
    {
        for (int f = f0; f <= f1; ++f)
        {
            // Sliding window half-width capped to what's available in the patch
            const int wf0 = f0;
            const int wf1 = f1;

            float sumMag = 0.0f;
            int   count = 0;
            for (int wf = wf0; wf <= wf1; ++wf)
            {
                sumMag += std::abs(patch[(wf - f0) * pH + (b - b0)]);
                ++count;
            }
            const float avgMag = (count > 0) ? sumMag / (float)count : 0.0f;
            const float origPhase = std::arg(patch[(f - f0) * pH + (b - b0)]);
            spectralData[f][b] = std::polar(avgMag, origPhase);
        }
    }
}

//==============================================================================
void SpectralEditProcessor::applySpectralCompress(const SelRect& s, float strength)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);

        // 1) Compute average magnitude across the selection
        double sumMag = 0.0;
        int    count = 0;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
            {
                sumMag += std::abs(spectralData[x][y]);
                ++count;
            }
        if (count == 0) return;
        const float avg = (float)(sumMag / count);
        if (avg < 1e-20f) return;

        // 2) Apply:  mag_new = mag_old * (mag_old / avg)^strength
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
            {
                const float mag = std::abs(spectralData[x][y]);
                if (mag < 1e-30f) continue;
                const float ratio = mag / avg;
                const float newMag = mag * std::pow(ratio, strength);
                const float phase = std::arg(spectralData[x][y]);
                spectralData[x][y] = std::polar(newMag, phase);
            }
    }
    resynthesize();
}

//==============================================================================
void SpectralEditProcessor::applyFreeze(const SelRect& s)
{
    {
        std::lock_guard<std::mutex> lk(spectralMutex);
        if (numFrames == 0 || !s.valid) return;
        const int nb = numBins;
        const int x0 = cF(s.left(), numFrames), x1 = cF(s.right(), numFrames);
        const int y0 = cB(s.bottom(), nb), y1 = cB(s.top(), nb);

        // Copy the first frame in the selection to all subsequent frames
        for (int x = x0 + 1; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                spectralData[x][y] = spectralData[x0][y];
    }
    resynthesize();
}

//==============================================================================
void SpectralEditProcessor::handleNoteOn(juce::MidiKeyboardState*, int,
    int note, float /*vel*/)
{
    // Use synthContentLen (= nF*hopSize) so that startFraction 0..1 spans only
    // the meaningful audio, not the fftSize-sized zero-padded tail.
    const int cLen = synthContentLen.load();
    const int len = (cLen > 0) ? cLen : synthBufLen.load();
    const double startPos = (double)startFraction.load() * (double)len;

    std::lock_guard<std::mutex> lk(voicesMutex);
    for (auto& v : voices)
    {
        if (!v.on)
        {
            v.on = true;
            v.note = note;
            v.pos = startPos;
            v.rate = std::pow(2.0, (note - 60) / 12.0)
                * (sourceSampleRate / currentSR);
            return;
        }
    }
    // Voice steal
    voices[0].on = true;
    voices[0].note = note;
    voices[0].pos = startPos;
    voices[0].rate = std::pow(2.0, (note - 60) / 12.0)
        * (sourceSampleRate / currentSR);
}

void SpectralEditProcessor::handleNoteOff(juce::MidiKeyboardState*, int,
    int note, float)
{
    // One-shot: voices play to end regardless of note-off.
    (void)note;
}

//==============================================================================
juce::AudioProcessorEditor* SpectralEditProcessor::createEditor()
{
    return new SpectralEditEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralEditProcessor();
}