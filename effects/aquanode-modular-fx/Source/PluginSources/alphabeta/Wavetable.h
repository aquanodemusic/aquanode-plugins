#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ==============================================================
//  Wavetable
//
//  Up to 256 single-cycle frames of 2048 samples. For every frame
//  a stack of 11 band-limited copies ("mip levels") is built with
//  an FFT: level 0 keeps all 1024 harmonics, each further level
//  halves them. The oscillator picks the level whose harmonics
//  stay below Nyquist, so high notes don't alias.
//
//  Import accepts any audio file JUCE can read. Frame size:
//    - a Serum "clm " chunk in the WAV declares it -> used as-is
//    - or the user picks one (256 ... 4096)
//    - otherwise (auto): length a multiple of 2048 -> 2048-sample
//      frames; up to 8192 samples -> one single cycle; longer ->
//      consecutive 2048-sample frames.
//  Every frame is resampled to 2048 samples internally.
// ==============================================================
class Wavetable : public juce::ReferenceCountedObject {
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Wavetable>;

    static constexpr int N = 2048;          // samples per frame
    static constexpr int LEVELS = 11;       // 1024, 512, ... 1 harmonics
    static constexpr int MAX_FRAMES = 256;

    juce::String name;
    int numFrames = 0;
    std::vector<float> raw;                 // original frames (saved with the patch)

    // pos 0..1 scans the frames, phase 0..1, dt = freq / sampleRate
    float sample(float pos, float phase, float dt) const {
        int level = 0;
        const float h = 2048.0f * dt;       // 1024 harmonics * 2 * dt
        if (h > 1.0f) level = juce::jmin(LEVELS - 1, (int)std::ceil(std::log2(h)));

        float fpos = juce::jlimit(0.0f, 1.0f, pos) * (float)(numFrames - 1);
        int f0 = (int)fpos;
        int f1 = juce::jmin(f0 + 1, numFrames - 1);
        float ft = fpos - (float)f0;

        float x = phase * (float)N;
        int i = juce::jlimit(0, N - 1, (int)x);
        float t = x - (float)i;

        const float* a = table(f0, level);
        float va = a[i] + (a[i + 1] - a[i]) * t;
        if (f1 == f0 || ft <= 0.0f) return va;
        const float* b = table(f1, level);
        float vb = b[i] + (b[i + 1] - b[i]) * t;
        return va + (vb - va) * ft;
    }

    // ----------------------------------------------------------
    //  Construction
    // ----------------------------------------------------------
    static Ptr fromFrames(const juce::String& name, std::vector<float> frames) {
        int nf = juce::jlimit(0, MAX_FRAMES, (int)(frames.size() / N));
        if (nf == 0) return nullptr;
        frames.resize((size_t)nf * N);

        Ptr wt = new Wavetable();
        wt->name = name;
        wt->numFrames = nf;
        wt->raw = std::move(frames);
        wt->data.assign((size_t)nf * LEVELS * (N + 1), 0.0f);

        juce::dsp::FFT fft(11);             // 2^11 = 2048
        std::vector<float> spec((size_t)N * 2), work((size_t)N * 2);

        for (int f = 0; f < nf; ++f) {
            std::fill(spec.begin(), spec.end(), 0.0f);
            std::copy_n(wt->raw.begin() + (ptrdiff_t)f * N, N, spec.begin());
            fft.performRealOnlyForwardTransform(spec.data());

            for (int lv = 0; lv < LEVELS; ++lv) {
                const int maxH = 1024 >> lv;
                work = spec;
                // interleaved complex bins 0..N-1; keep 1..maxH and the mirror
                for (int k = 0; k < N; ++k) {
                    bool keep = (k >= 1 && k <= maxH) || (k >= N - maxH && k <= N - 1);
                    if (!keep) { work[(size_t)k * 2] = 0.0f; work[(size_t)k * 2 + 1] = 0.0f; }
                }
                fft.performRealOnlyInverseTransform(work.data());
                float* dst = wt->tableW(f, lv);
                std::copy_n(work.begin(), N, dst);
                dst[N] = dst[0];            // guard sample for interpolation
            }
        }

        // Normalise so the loudest full-band frame peaks at 1
        float peak = 0.0f;
        for (int f = 0; f < nf; ++f) {
            const float* t = wt->table(f, 0);
            for (int i = 0; i < N; ++i) peak = juce::jmax(peak, std::abs(t[i]));
        }
        if (peak > 1e-9f)
            for (auto& v : wt->data) v /= peak;
        return wt;
    }

    // One frame from harmonic amplitudes: sinAmp[k] / cosAmp[k] for k = 1..1023
    static std::vector<float> frameFromHarmonics(const std::vector<float>& sinAmp,
                                                 const std::vector<float>& cosAmp = {}) {
        juce::dsp::FFT fft(11);
        std::vector<float> spec((size_t)N * 2, 0.0f);
        for (int k = 1; k < N / 2; ++k) {
            float a = k < (int)sinAmp.size() ? sinAmp[(size_t)k] : 0.0f;
            float b = k < (int)cosAmp.size() ? cosAmp[(size_t)k] : 0.0f;
            // x(t) = b cos + a sin  <->  X[k] = (b - i a) N/2, mirrored conjugate
            spec[(size_t)k * 2] = b;             spec[(size_t)k * 2 + 1] = -a;
            spec[(size_t)(N - k) * 2] = b;       spec[(size_t)(N - k) * 2 + 1] = a;
        }
        fft.performRealOnlyInverseTransform(spec.data());
        spec.resize(N);
        return spec;
    }

    // Frame size declared by a Serum-style "clm " chunk ("<!>2048 ..."), or 0
    static int serumFrameSize(const juce::File& file) {
        juce::MemoryBlock mb;
        if (!file.hasFileExtension("wav") || !file.loadFileAsData(mb) || mb.getSize() < 12) return 0;
        auto* d = (const char*)mb.getData();
        if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return 0;
        size_t p = 12;
        while (p + 8 <= mb.getSize()) {
            auto sz = (size_t)((uint8_t)d[p + 4] | ((uint8_t)d[p + 5] << 8) | ((uint8_t)d[p + 6] << 16) | ((uint32_t)(uint8_t)d[p + 7] << 24));
            if (memcmp(d + p, "clm ", 4) == 0 && p + 8 + sz <= mb.getSize()) {
                juce::String txt(d + p + 8, juce::jmin<size_t>(sz, 64));
                if (txt.startsWith("<!>")) {
                    int fs = txt.substring(3).getIntValue();
                    if (fs >= 16 && fs <= 65536) return fs;
                }
                return 0;
            }
            p += 8 + sz + (sz & 1);
        }
        return 0;
    }

    // Resample one cycle of `len` samples (cyclic) to N samples
    static void resampleCycle(const float* src, int len, float* dst) {
        for (int i = 0; i < N; ++i) {
            double x = (double)i * len / N;
            int i0 = (int)x;
            int i1 = (i0 + 1) % len;
            float t = (float)(x - i0);
            dst[i] = src[i0] + (src[i1] - src[i0]) * t;
        }
    }

    // Reads an audio file and slices it into frames (see header comment).
    // frameSize 0 = auto (Serum chunk, else the 2048 rules).
    static Ptr fromFile(const juce::File& file, juce::String& error, int frameSize = 0) {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr) { error = "Can't read " + file.getFileName(); return nullptr; }

        const auto len = (int)std::min<juce::int64>(reader->lengthInSamples, (juce::int64)N * MAX_FRAMES * 4);
        if (len < 16) { error = "File is too short"; return nullptr; }

        juce::AudioBuffer<float> buf((int)reader->numChannels, len);
        reader->read(&buf, 0, len, 0, true, true);

        std::vector<float> mono((size_t)len, 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < len; ++i)
                mono[(size_t)i] += buf.getSample(ch, i) / (float)buf.getNumChannels();

        if (frameSize <= 0) frameSize = serumFrameSize(file);
        if (frameSize <= 0) {
            if (len % N == 0 || len > 4 * N) frameSize = N;   // 2048-sample frames
            else frameSize = len;                              // one single cycle
        }
        frameSize = juce::jlimit(16, len, frameSize);

        int nf = juce::jmax(1, len / frameSize);
        std::vector<int> pick;                                 // source frames to use
        if (nf > MAX_FRAMES)
            for (int f = 0; f < MAX_FRAMES; ++f) pick.push_back((int)((double)f * (nf - 1) / (MAX_FRAMES - 1)));
        else
            for (int f = 0; f < nf; ++f) pick.push_back(f);

        std::vector<float> frames(pick.size() * (size_t)N);
        for (size_t f = 0; f < pick.size(); ++f) {
            const float* src = mono.data() + (ptrdiff_t)pick[f] * frameSize;
            if (frameSize == N) std::copy_n(src, N, frames.begin() + (ptrdiff_t)f * N);
            else resampleCycle(src, frameSize, frames.data() + f * N);
        }
        auto wt = fromFrames(file.getFileNameWithoutExtension(), std::move(frames));
        if (wt == nullptr) error = "No usable frames in " + file.getFileName();
        return wt;
    }

    // ----------------------------------------------------------
    //  Patch storage: the raw frames as base64 in a ValueTree
    // ----------------------------------------------------------
    juce::ValueTree toValueTree(int slot) const {
        juce::ValueTree v("WAVETABLE");
        v.setProperty("slot", slot, nullptr);
        v.setProperty("name", name, nullptr);
        v.setProperty("frames", numFrames, nullptr);
        juce::MemoryBlock mb(raw.data(), raw.size() * sizeof(float));
        v.setProperty("data", mb.toBase64Encoding(), nullptr);
        return v;
    }

    static Ptr fromValueTree(const juce::ValueTree& v) {
        juce::MemoryBlock mb;
        if (!mb.fromBase64Encoding(v.getProperty("data").toString())) return nullptr;
        std::vector<float> frames(mb.getSize() / sizeof(float));
        std::memcpy(frames.data(), mb.getData(), frames.size() * sizeof(float));
        return fromFrames(v.getProperty("name").toString(), std::move(frames));
    }

private:
    std::vector<float> data;                // [frame][level][N + 1]

    const float* table(int f, int lv) const { return data.data() + ((size_t)f * LEVELS + (size_t)lv) * (N + 1); }
    float* tableW(int f, int lv) { return data.data() + ((size_t)f * LEVELS + (size_t)lv) * (N + 1); }
};
