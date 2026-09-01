#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Waveform Display Component
// Displays audio waveform and playhead position, allows scrubbing
//==============================================================================

class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay(GrainfreezeAudioProcessor& p) : processor(p) {}

    // Draws the waveform and playhead
    void paint(juce::Graphics& g) override;

    // Handles mouse click to jump playhead
    void mouseDown(const juce::MouseEvent& event) override;

    // Handles mouse drag for scrubbing
    void mouseDrag(const juce::MouseEvent& event) override;

    // Handles mouse release (currently no action)
    void mouseUp(const juce::MouseEvent& event) override;

private:
    GrainfreezeAudioProcessor& processor;

    // Updates playhead position based on mouse X coordinate
    void updatePlayheadFromMouse(const juce::MouseEvent& event);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
// Spectrum Visualizer Component
// Displays FFT spectrum as vertical bars for musical notes
//==============================================================================

class SpectrumVisualizer : public juce::Component
{
public:
    SpectrumVisualizer(GrainfreezeAudioProcessor& p) : processor(p) {}

    // Draws the spectrum visualization
    void paint(juce::Graphics& g) override;

    // Updates the spectrum data from the processor
    void updateSpectrum(const std::vector<float>& magnitudes, int fftSize, double sampleRate);

private:
    GrainfreezeAudioProcessor& processor;

    // Spectrum data storage
    std::vector<float> noteMagnitudes;  // Magnitude for each note
    static const int numNotes = 88;     // Piano range: A0 to C8
    static const int lowestNote = 21;   // MIDI note A0

    // Converts frequency to MIDI note number
    int frequencyToMidiNote(float frequency);

    // Converts MIDI note to note name string
    juce::String midiNoteToName(int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumVisualizer)
};

//==============================================================================
// Main Plugin Editor
// Two-column layout with primary and advanced controls
//==============================================================================

class GrainfreezeAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::Timer
{
public:
    GrainfreezeAudioProcessorEditor(GrainfreezeAudioProcessor&);
    ~GrainfreezeAudioProcessorEditor() override;

    // Draws the background
    void paint(juce::Graphics&) override;

    // Layouts all UI components
    void resized() override;

    // Updates UI state periodically (30Hz)
    void timerCallback() override;

private:
    GrainfreezeAudioProcessor& audioProcessor;

    // Waveform Display
    WaveformDisplay waveformDisplay;

    // Spectrum Visualizer
    SpectrumVisualizer spectrumVisualizer;

    // Control Buttons
    juce::TextButton loadButton;    // Loads audio file
    juce::TextButton playButton;    // Toggles playback
    juce::TextButton freezeButton;  // Toggles freeze mode

    // Status Display
    juce::Label statusLabel;        // Shows playback status
    juce::Label recommendedLabel;   // Shows recommended settings

    // Primary Controls (Left Column)
    juce::Slider timeStretchSlider;  // Time stretch factor control
    juce::Label timeStretchLabel;

    juce::Slider fftSizeSlider;      // FFT size selection
    juce::Label fftSizeLabel;

    juce::Slider hopSizeSlider;      // Hop size divisor control
    juce::Label hopSizeLabel;

    juce::Slider glideSlider;        // Freeze mode glide time control
    juce::Label glideLabel;

    // Advanced Controls (Right Column)
    juce::Slider hfBoostSlider;      // High-frequency boost control
    juce::Label hfBoostLabel;

    juce::Slider microMovementSlider;  // Freeze micro-movement control
    juce::Label microMovementLabel;

    juce::Slider windowTypeSlider;   // Window function type selector
    juce::Label windowTypeLabel;

    juce::Slider crossfadeLengthSlider;  // Crossfade length control
    juce::Label crossfadeLengthLabel;

    // Column Headers
    juce::Label primaryControlsLabel;   // "Primary Controls" header
    juce::Label advancedControlsLabel;  // "Advanced Controls" header

    std::unique_ptr<juce::FileChooser> fileChooser;  // File chooser dialog

    // Opens file chooser and loads selected audio file
    void loadAudioFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrainfreezeAudioProcessorEditor)
};