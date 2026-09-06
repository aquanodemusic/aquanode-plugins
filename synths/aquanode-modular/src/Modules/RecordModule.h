#pragma once

#include "ModuleCore.h"

// Record - captures whatever passes through it (audio flows through
// unaffected, this is a tap not an insert) and saves it to disk as WAV or
// FLAC. The sample rate is never a knob here: it is always whatever this
// instance is actually running at - the host's rate in a DAW, the audio
// device's rate in the standalone app, or 44.1 kHz if neither is available
// yet. Recording auto-stops after 2 minutes so it can't run away and eat all
// your memory. Input: 0 = Audio In. Output: 0 = Audio Out.
class RecordModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pFormat = 0, pBitDepth };
    static constexpr double maxRecordSeconds = 120.0;

    void prepare (double sr) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    void uiButtonClicked (const juce::String& paramId) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 22; }

    // polled by the status display (message thread) - safe, atomics only
    bool isRecording() const { return recording.load (std::memory_order_relaxed); }
    double recordedSeconds() const
    {
        return sampleRate > 0.0 ? (double) recordedCount.load (std::memory_order_relaxed) / sampleRate : 0.0;
    }
    double currentSampleRate() const { return sampleRate; }

private:
    void saveToFile();
    void writeToFile (const juce::File& file, int numFrames, double sr, bool useFlac, int bitDepth);

    std::vector<float> bufL, bufR;
    std::atomic<bool> recording { false };
    std::atomic<int> recordedCount { 0 };

    std::unique_ptr<juce::FileChooser> activeSaveChooser;
};
