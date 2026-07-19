#pragma once
#include <JuceHeader.h>
#include <vector>
#include <mutex>
#include <memory>
#include <array>

//==============================================================================
struct SpectralFrame
{
    std::vector<float> freqHz;
    std::vector<float> magDB;
    // Populated only when time-reassignment (interpolation) is active.
    // Each value is the fractional hop offset for the corresponding bin:
    //   0.0  = bin is exactly at this frame's nominal time
    //  -0.5  = bin belongs half a hop earlier (render to the left)
    //  +0.5  = bin belongs half a hop later  (render to the right)
    std::vector<float> timeOffsetHops;
};

//==============================================================================
class SpectralResolverProcessor : public juce::AudioProcessor
{
public:
    SpectralResolverProcessor();
    ~SpectralResolverProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SpectralResolve"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool   popFrame(SpectralFrame& out);
    bool   popSCFrame(SpectralFrame& out);
    double getEffectiveSampleRate() const { return effectiveSampleRate; }
    // Width of one FFT bin in Hz at the current settings — used by the
    // editor to compute frequency-dependent pixel height for each reassigned bin.
    double getBinHz() const { return effectiveSampleRate / double(currentFFTSize); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

private:
    std::unique_ptr<juce::dsp::FFT> fft;

    int currentFFTOrder{ 12 };
    int currentFFTSize{ 4096 };
    int currentHopSize{ 1024 };

    // Window functions:
    //   winH  = analysis window  h[n]
    //   winDH = time-derivative  h'[n]         → frequency reassignment operator
    //   winTH = time-ramp        (n-N/2)*h[n]  → time reassignment operator
    std::vector<float> winH, winDH, winTH;

    // Main signal ring + work buffers
    std::vector<float> ringBuf;
    std::vector<float> workFrame, workBufH, workBufDH, workBufTH;
    int ringWrite{ 0 };
    int hopCounter{ 0 };
    int ringMask{ 0 };

    // Sidechain ring + work buffers
    std::vector<float> scRingBuf;
    std::vector<float> scWorkFrame, scWorkBufH, scWorkBufDH, scWorkBufTH;
    int scRingWrite{ 0 };
    int scHopCounter{ 0 };

    double currentSampleRate{ 44100.0 };
    double effectiveSampleRate{ 44100.0 };

    static constexpr int   DECIM_STAGES = 4;
    static constexpr float BW8_Q[DECIM_STAGES]{ 0.5098f, 0.6013f, 0.8999f, 2.5629f };

    struct Biquad
    {
        float b0{ 1 }, b1{ 0 }, b2{ 0 }, a1{ 0 }, a2{ 0 };
        float x1{ 0 }, x2{ 0 }, y1{ 0 }, y2{ 0 };

        void reset() { x1 = x2 = y1 = y2 = 0.f; }

        void design(double fc, double fs, double Q)
        {
            const double w0 = juce::MathConstants<double>::twoPi * fc / fs;
            const double cosw = std::cos(w0);
            const double sinw = std::sin(w0);
            const double alpha = sinw / (2.0 * Q);
            const double inv = 1.0 / (1.0 + alpha);
            b0 = float(((1.0 - cosw) * 0.5) * inv);
            b1 = float((1.0 - cosw) * inv);
            b2 = b0;
            a1 = float((-2.0 * cosw) * inv);
            a2 = float((1.0 - alpha) * inv);
        }

        float process(float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    std::array<Biquad, DECIM_STAGES> decimFilter;
    std::array<Biquad, DECIM_STAGES> scDecimFilter;
    int currentDecimFactor{ 1 };
    int decimPhase{ 0 };
    int scDecimPhase{ 0 };

    int lastFFTChoice{ -1 };
    int lastHopChoice{ -1 };
    int lastWindowChoice{ -1 };
    int lastDecimChoice{ -1 };

    static constexpr int QUEUE_CAPACITY = 512;

    std::mutex queueMutex;
    std::vector<SpectralFrame> frameQueue;
    int qHead{ 0 };
    int qTail{ 0 };

    std::mutex scQueueMutex;
    std::vector<SpectralFrame> scFrameQueue;
    int scQHead{ 0 };
    int scQTail{ 0 };

    static constexpr int FFT_ORDERS[] = { 10, 11, 12,  13,  14 };
    static constexpr int HOP_DENOMS[] = { 2,  4,  8,  16,  32,  64, 128, 256 };
    static constexpr int DECIM_FACTORS[] = { 1,  2,  4,   8,  16 };

    void rebuildFFT(int order, int hopDenom, int windowChoice);
    void rebuildDecimator(int factor);
    void processWindow(const float* data);
    void processWindowSC(const float* data);
    void pushFrame(SpectralFrame&&);
    void pushSCFrame(SpectralFrame&&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralResolverProcessor)
};