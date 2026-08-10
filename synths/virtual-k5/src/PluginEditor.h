#pragma once
#include "PluginProcessor.h"

//==============================================================================
/*  One parameter, one control.

    Floats get a rotary slider, choices get a combo box. Which one is decided
    by asking the APVTS what the parameter actually is, so adding a parameter
    in createLayout() is enough to make it appear in the UI.

    Hovering anywhere over the control reports its description upward, which
    the editor shows in the info panel at the top.
*/
class ParamControl : public juce::Component
{
public:
    ParamControl(juce::AudioProcessorValueTreeState& state,
        const juce::String& parameterID,
        const juce::String& displayName,
        juce::String descriptionText,
        std::function<void(const juce::String&)> hoverCallback);

    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    juce::Label caption;
    juce::String description;
    std::function<void(const juce::String&)> onHover;

    std::unique_ptr<juce::Slider>   slider;
    std::unique_ptr<juce::ComboBox> combo;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

//==============================================================================
/** A titled block of controls that reflows into as many columns as it fits. */
class SectionPanel : public juce::Component
{
public:
    static constexpr int cellWidth = 88;
    static constexpr int cellHeight = 100;
    static constexpr int headerHeight = 26;
    static constexpr int padding = 8;

    SectionPanel(const juce::String& sectionTitle, juce::Colour accentColour);

    void addControl(juce::AudioProcessorValueTreeState& state,
        const juce::String& parameterID,
        const juce::String& displayName,
        const juce::String& description,
        std::function<void(const juce::String&)> hoverCallback);

    int getPreferredHeight(int availableWidth) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int columnsFor(int availableWidth) const;

    juce::String title;
    juce::Colour accent;
    juce::OwnedArray<ParamControl> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionPanel)
};

//==============================================================================
class K5AudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit K5AudioProcessorEditor(K5AudioProcessor& p);
    ~K5AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Plain-language explanation of a parameter, including what the
        three-letter Kawai section names actually stand for. */
    static juce::String describeParameter(const juce::String& parameterID,
        const juce::String& displayName);

private:
    void buildSections();
    void refreshPresetList(const juce::String& nameToSelect);
    void presetSelected();
    void saveButtonClicked();
    void finishSave(int modalResult);
    void deleteButtonClicked();
    void showDescription(const juce::String& text);

    static juce::String shortenName(const juce::String& fullName,
        const juce::String& prefixToDrop);

    K5AudioProcessor& processor;

    juce::Label      titleLabel;
    juce::Label      infoLabel;
    juce::ComboBox   presetBox;
    juce::TextButton saveButton{ "Save As..." };
    juce::TextButton deleteButton{ "Delete" };
    juce::TextButton folderButton{ "Folder" };

    juce::Viewport   viewport;
    juce::Component  content;
    juce::OwnedArray<SectionPanel> sections;

    std::unique_ptr<juce::AlertWindow> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K5AudioProcessorEditor)
};