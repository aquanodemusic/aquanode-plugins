#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

//==============================================================================
SpectrogramProcessor::SpectrogramProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      fft (FFT_ORDER)
{
    buildWindows();

    inputRing.assign (FFT_SIZE * 2, 0.0f);

    fftBufH .assign (2 * FFT_SIZE, 0.0f);
    fftBufDH.assign (2 * FFT_SIZE, 0.0f);
    fftBufTH.assign (2 * FFT_SIZE, 0.0f);

    for (auto& col : lookaheadBuf) col.fill (0.0f);
    for (auto& col : ringBuffer)   col.fill (0.0f);
}

//==============================================================================
void SpectrogramProcessor::buildWindows()
{
    winH .resize (FFT_SIZE);
    winDH.resize (FFT_SIZE);
    winTH.resize (FFT_SIZE);

    const float N    = static_cast<float> (FFT_SIZE);
    const float half = (N - 1.0f) * 0.5f;
    const float pi   = juce::MathConstants<float>::pi;

    for (int n = 0; n < FFT_SIZE; ++n)
    {
        const float phase = pi * 2.0f * n / (N - 1.0f);
        const float h     = 0.5f * (1.0f - std::cos (phase));

        winH  [n] = h;
        winDH [n] = pi / (N - 1.0f) * std::sin (phase);
        winTH [n] = (static_cast<float> (n) - half) * h;
    }
}

//==============================================================================
void SpectrogramProcessor::prepareToPlay (double sr, int)
{
    currentSampleRate.store (sr);

    // ------------------------------------------------------------------
    //  4-pole Butterworth lowpass at fc_norm = 0.225 (normalised to each
    //  stage's own input sample rate — self-similar across all 3 stages).
    //
    //  Factored into two 2nd-order sections via Butterworth pole pairing:
    //
    //    Section A  Q₁ = 1/(2·sin(π/8))  ≈ 1.3066
    //    Section B  Q₂ = 1/(2·sin(3π/8)) ≈ 0.5412
    //
    //  Bilinear-transform 2nd-order Butterworth with cutoff fc_norm, Q:
    //    Wc = tan(π · fc_norm)
    //    K  = 1 / (Wc² + Wc/Q + 1)
    //    b0 = b2 = Wc² · K
    //    b1      = 2 · Wc² · K
    //    a1      = 2 · (Wc² − 1) · K
    //    a2      = (Wc² − Wc/Q + 1) · K
    //
    //  Alias rejection at the fold frequency (≈ 2.22 × fc):
    //    2-pole (original): |H|² ≈ −13.8 dB  (phantom bands below C2)
    //    4-pole (new):      |H|² ≈ −30.6 dB  (aliases attenuated ~4× more)
    // ------------------------------------------------------------------
    static constexpr float kFcNorm = 0.225f;
    static constexpr float kQ1     = 1.3066f;   // 1/(2·sin(π/8))
    static constexpr float kQ2     = 0.5412f;   // 1/(2·sin(3π/8))

    const float pi  = juce::MathConstants<float>::pi;
    const float wc  = std::tan (pi * kFcNorm);
    const float wc2 = wc * wc;

    // --- Section A (high-Q) ---
    {
        const float K = 1.0f / (wc2 + wc / kQ1 + 1.0f);
        decB0a = wc2 * K;
        decB1a = 2.0f * decB0a;
        decB2a = decB0a;
        decA1a = 2.0f * (wc2 - 1.0f) * K;
        decA2a = (wc2 - wc / kQ1 + 1.0f) * K;
    }

    // --- Section B (low-Q) ---
    {
        const float K = 1.0f / (wc2 + wc / kQ2 + 1.0f);
        decB0b = wc2 * K;
        decB1b = 2.0f * decB0b;
        decB2b = decB0b;
        decA1b = 2.0f * (wc2 - 1.0f) * K;
        decA2b = (wc2 - wc / kQ2 + 1.0f) * K;
    }

    for (auto& s : decStageA) s.reset();
    for (auto& s : decStageB) s.reset();
    decCnt[0] = decCnt[1] = decCnt[2] = 0;

    inputWritePos   = 0;
    samplesSinceHop = 0;
    lookaheadPos    = 0;

    for (auto& col : lookaheadBuf) col.fill (0.0f);
    for (auto& col : ringBuffer)   col.fill (0.0f);

    writeHead.store (0);
    totalColumnsProduced.store (0);
}

//==============================================================================
inline float SpectrogramProcessor::runBiquadA (BiquadState& s, float x) const noexcept
{
    const float y = decB0a * x + decB1a * s.x1 + decB2a * s.x2
                               - decA1a * s.y1  - decA2a * s.y2;
    s.x2 = s.x1;  s.x1 = x;
    s.y2 = s.y1;  s.y1 = y;
    return y;
}

inline float SpectrogramProcessor::runBiquadB (BiquadState& s, float x) const noexcept
{
    const float y = decB0b * x + decB1b * s.x1 + decB2b * s.x2
                               - decA1b * s.y1  - decA2b * s.y2;
    s.x2 = s.x1;  s.x1 = x;
    s.y2 = s.y1;  s.y1 = y;
    return y;
}

//==============================================================================
bool SpectrogramProcessor::feedDecimator (float x, float& out) noexcept
{
    // Stage 0 — runs on every real sample, outputs 1 in 2
    // Section A → Section B → decimate
    const float y0 = runBiquadB (decStageB[0], runBiquadA (decStageA[0], x));
    if (++decCnt[0] < 2) return false;
    decCnt[0] = 0;

    // Stage 1 — runs at SR/2, outputs 1 in 2
    const float y1 = runBiquadB (decStageB[1], runBiquadA (decStageA[1], y0));
    if (++decCnt[1] < 2) return false;
    decCnt[1] = 0;

    // Stage 2 — runs at SR/4, outputs 1 in 2  → final rate = SR/8
    const float y2 = runBiquadB (decStageB[2], runBiquadA (decStageA[2], y1));
    if (++decCnt[2] < 2) return false;
    decCnt[2] = 0;

    out = y2;
    return true;
}

//==============================================================================
void SpectrogramProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int ringSize    = static_cast<int> (inputRing.size());

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer (ch)[i];
        mono /= static_cast<float> (numChannels);

        float decimated = 0.0f;
        if (! feedDecimator (mono, decimated))
            continue;

        inputRing[inputWritePos] = decimated;
        inputWritePos = (inputWritePos + 1) % ringSize;

        if (++samplesSinceHop >= HOP_SIZE)
        {
            samplesSinceHop = 0;
            processFrame();
        }
    }
}

//==============================================================================
int SpectrogramProcessor::freqToDisplayBin (float freqHz) noexcept
{
    if (freqHz <= FREQ_MIN) return 0;
    if (freqHz >= FREQ_MAX) return DISPLAY_BINS - 1;
    const float t = std::log (freqHz / FREQ_MIN)
                  / std::log (FREQ_MAX / FREQ_MIN);
    return static_cast<int> (t * (DISPLAY_BINS - 1) + 0.5f);
}

//==============================================================================
void SpectrogramProcessor::processFrame()
{
    const float effSRate = static_cast<float> (currentSampleRate.load())
                           / static_cast<float> (DECIMATION);
    const float twoPi    = juce::MathConstants<float>::twoPi;
    const int   ringSize = static_cast<int> (inputRing.size());

    // --- 1. Window and copy into FFT buffers ---
    for (int i = 0; i < FFT_SIZE; ++i)
    {
        const int   idx = (inputWritePos - FFT_SIZE + i + ringSize) % ringSize;
        const float x   = inputRing[idx];

        fftBufH  [2 * i]     = x * winH  [i];
        fftBufH  [2 * i + 1] = 0.0f;
        fftBufDH [2 * i]     = x * winDH [i];
        fftBufDH [2 * i + 1] = 0.0f;
        fftBufTH [2 * i]     = x * winTH [i];
        fftBufTH [2 * i + 1] = 0.0f;
    }

    // --- 2. Three forward FFTs ---
    fft.performRealOnlyForwardTransform (fftBufH .data(), false);
    fft.performRealOnlyForwardTransform (fftBufDH.data(), false);
    fft.performRealOnlyForwardTransform (fftBufTH.data(), false);

    // --- 3. Find frame peak for adaptive gate ---
    float peakMag2 = 0.0f;
    for (int k = 1; k < NUM_BINS - 1; ++k)
    {
        const float re = fftBufH[2 * k];
        const float im = fftBufH[2 * k + 1];
        const float m2 = re * re + im * im;
        if (m2 > peakMag2) peakMag2 = m2;
    }

    // Convert dB knob value to power ratio threshold
    const float gateDeltaDB = paramGateDeltaDB.load (std::memory_order_relaxed);
    const float kGateRel    = std::pow (10.0f, gateDeltaDB * 0.1f);
    static constexpr float kGateAbs = 1.0e-12f;
    const float threshold = std::max (kGateAbs, peakMag2 * kGateRel);

    // --- 4. Time-frequency reassignment ---
    for (int k = 1; k < NUM_BINS - 1; ++k)
    {
        const float H_re  = fftBufH  [2 * k],  H_im  = fftBufH  [2 * k + 1];
        const float DH_re = fftBufDH [2 * k],  DH_im = fftBufDH [2 * k + 1];
        const float TH_re = fftBufTH [2 * k],  TH_im = fftBufTH [2 * k + 1];

        const float mag2 = H_re * H_re + H_im * H_im;
        if (mag2 < threshold) continue;

        // ω̂(k) = 2πk/N + Im[X_DH·conj(X_H)] / |X_H|²
        const float dh_cH_im  = DH_im * H_re - DH_re * H_im;
        const float omega_hat = (twoPi * static_cast<float> (k) / FFT_SIZE)
                                 + dh_cH_im / mag2;
        const float freq_hat  = omega_hat * effSRate / twoPi;

        if (freq_hat < FREQ_MIN || freq_hat >= FREQ_MAX) continue;
        const int dispBin = freqToDisplayBin (freq_hat);

        // t̂(k) = Re[X_TH·conj(X_H)] / |X_H|²
        const float th_cH_re      = TH_re * H_re + TH_im * H_im;
        const float t_offset_samp = th_cH_re / mag2;
        int col_offset = static_cast<int> (std::round (t_offset_samp / HOP_SIZE));
        col_offset = juce::jlimit (-LOOKAHEAD_COLS, LOOKAHEAD_COLS, col_offset);

        const int target = (lookaheadPos + LOOKAHEAD_COLS + col_offset + LOOKAHEAD_BUF)
                           % LOOKAHEAD_BUF;

        lookaheadBuf[target][dispBin] += std::sqrt (mag2) / FFT_SIZE;
    }

    // --- 5. Commit oldest lookahead slot ---
    {
        const int head = writeHead.load (std::memory_order_relaxed);
        ringBuffer[head] = lookaheadBuf[lookaheadPos];
        lookaheadBuf[lookaheadPos].fill (0.0f);
        writeHead.store ((head + 1) % MAX_COLUMNS, std::memory_order_release);
        totalColumnsProduced.fetch_add (1, std::memory_order_relaxed);
    }

    lookaheadPos = (lookaheadPos + 1) % LOOKAHEAD_BUF;
}

//==============================================================================
juce::AudioProcessorEditor* SpectrogramProcessor::createEditor()
{
    return new SpectrogramEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrogramProcessor();
}
