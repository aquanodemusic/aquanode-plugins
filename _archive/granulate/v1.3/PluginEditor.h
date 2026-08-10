#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class WaveformDisplay : public juce::Component,
    public juce::FileDragAndDropTarget
{
public:
    WaveformDisplay(GranulateAudioProcessor& p) : processor(p) {}

    void paint(juce::Graphics& g) override;
    void resized() override {}

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    GranulateAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
// Collapsible side panel: colour theming + preset save/load
//==============================================================================
class ColorSidePanel : public juce::Component
{
public:
    ColorSidePanel(GranulateAudioProcessor& p, std::function<void()> repaintCb);
    ~ColorSidePanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Re-read all colour values from the processor state into the hex editors.
    // Call this whenever the panel is opened or a preset is loaded.
    void refreshEditors();

private:
    GranulateAudioProcessor& processor;
    std::function<void()>    repaintCallback;

    struct ColorRow
    {
        juce::String                    propertyName;
        juce::String                    displayName;
        juce::Colour                    defaultColour;
        std::unique_ptr<juce::TextEditor> hexEditor;
        juce::Rectangle<int>            swatchBounds;
    };

    std::vector<ColorRow> colorRows;

    juce::TextButton randomizeButton    { "Randomize" };
    juce::TextButton savePresetButton   { "Save Preset" };
    juce::TextButton loadPresetButton   { "Load Preset" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    void applyHexFromEditor(ColorRow& row);
    void savePreset();
    void loadPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColorSidePanel)
};

//==============================================================================
class GranulateAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    GranulateAudioProcessorEditor(GranulateAudioProcessor&);
    ~GranulateAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    GranulateAudioProcessor& audioProcessor;

    WaveformDisplay  waveformDisplay;
    juce::TextButton loadButton;
    juce::ToggleButton modeToggle;
    juce::TextButton colorPanelButton;
    bool             sidebarOpen = false;
    juce::Colour     lastTextColour { 0xffffffff }; // cache to detect changes

    juce::Slider numGrainsSlider;
    juce::Slider grainSizeSlider;
    juce::Slider grainPositionSlider;
    juce::Slider spraySlider;
    juce::Slider windowSizeSlider;

    // Grain ADSR
    juce::Slider grainAttackSlider;
    juce::Slider grainDecaySlider;
    juce::Slider grainSustainSlider;
    juce::Slider grainReleaseSlider;

    // Note ADSR
    juce::Slider noteAttackSlider;
    juce::Slider noteDecaySlider;
    juce::Slider noteSustainSlider;
    juce::Slider noteReleaseSlider;

    juce::Slider amplitudeModSlider;
    juce::Slider amDispersionSlider;
    juce::Slider pitchDispersionSlider;
    juce::Slider pitchSlider;
    juce::Slider stereoSpreadSlider;
    juce::Slider volumeSlider;
    juce::Slider reversedGrainsSlider;

    juce::Label numGrainsLabel;
    juce::Label grainSizeLabel;
    juce::Label grainPositionLabel;
    juce::Label sprayLabel;
    juce::Label windowSizeLabel;

    // Grain ADSR
    juce::Label grainAttackLabel;
    juce::Label grainDecayLabel;
    juce::Label grainSustainLabel;
    juce::Label grainReleaseLabel;

    // Note ADSR
    juce::Label noteAttackLabel;
    juce::Label noteDecayLabel;
    juce::Label noteSustainLabel;
    juce::Label noteReleaseLabel;

    juce::Label amplitudeModLabel;
    juce::Label amDispersionLabel;
    juce::Label pitchDispersionLabel;
    juce::Label pitchLabel;
    juce::Label stereoSpreadLabel;
    juce::Label volumeLabel;
    juce::Label reversedGrainsLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    // Color panel — created in constructor, lives alongside the editor
    std::unique_ptr<ColorSidePanel> colorSidePanel;

    // Attachments MUST be declared last so they are destroyed first.
    // C++ destroys members in reverse declaration order.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numGrainsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainPositionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sprayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowSizeAttachment;

    // Grain ADSR
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainReleaseAttachment;

    // Note ADSR
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteReleaseAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amplitudeModAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amDispersionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchDispersionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoSpreadAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reversedGrainsAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranulateAudioProcessorEditor)
};
