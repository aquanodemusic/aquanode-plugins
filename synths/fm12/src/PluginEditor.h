#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CustomKnobLNF : public juce::LookAndFeel_V4
{
public:
    // DX7 Inspired Palette
    const juce::Colour dx7Brown = juce::Colour(0xFF6D4C41);
    const juce::Colour dx7Accent = juce::Colour(0xFF6D4C41);
    const juce::Colour dx7Green = juce::Colour(0xFF00FF41); // Classic fluorescent display green

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, const float rotaryStartAngle,
        const float rotaryEndAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = 3.5f;

        juce::Point<float> centre(bounds.getCentreX(), bounds.getCentreY());

        // 1. Draw "Chassis" well
        g.setColour(dx7Brown.darker());
        g.fillEllipse(bounds);

        // 2. Draw Value Arc (The "Display" Green)
        if (slider.isEnabled())
        {
            juce::Path valuePath;
            valuePath.addCentredArc(centre.x, centre.y, radius - 4, radius - 4, 0.0f, rotaryStartAngle, toAngle, true);
            g.setColour(dx7Green.withAlpha(0.6f));
            g.strokePath(valuePath, juce::PathStrokeType(lineW, juce::PathStrokeType::curved));
        }

        // 3. Glowing Dot
        juce::Point<float> dotPos = centre.getPointOnCircumference(radius - 4, toAngle);
        g.setColour(dx7Green);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(dotPos));
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
        int buttonX, int buttonY, int buttonW, int buttonH,
        juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

        // DX7 Panel look: Dark background, light border
        g.setColour(dx7Brown);
        g.fillRoundedRectangle(bounds, 2.0f);

        g.setColour(dx7Accent);
        g.drawRoundedRectangle(bounds, 2.0f, 1.5f);

        // Arrow
        juce::Path p;
        p.addTriangle(width - 15, height / 2 - 2, width - 10, height / 2 + 3, width - 5, height / 2 - 2);
        g.setColour(dx7Green);
        g.fillPath(p);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.fillAll(dx7Brown);
        g.setColour(dx7Accent);
        g.drawRect(0, 0, width, height, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
        bool isSeparator, bool isActive, bool isHighlighted, bool isChecked,
        bool isOpen, const juce::String& text, const juce::String& shortcutKeyText,
        const juce::Drawable* icon, const juce::Colour* textColourToUse) override
    {
        if (isHighlighted)
        {
            g.setColour(dx7Accent);
            g.fillRect(area);
        }

        g.setColour(isHighlighted ? dx7Green : juce::Colours::white);
        g.setFont(14.0f);
        g.drawFittedText(text, area.reduced(10, 0), juce::Justification::centredLeft, 1);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(shouldDrawButtonAsDown ? dx7Brown.brighter() : dx7Brown);
        g.fillRoundedRectangle(bounds, 2.0f);

        g.setColour(dx7Accent);
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }
};

//==============================================================================
class FM12SynthAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor&);
    ~FM12SynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // ── Timer: syncs OSC combos with APVTS values (e.g. after preset load) ───
    void timerCallback() override;

    // ── User wave loading helper ──────────────────────────────────────────────
    // Opens a file chooser and writes the chosen single-cycle WAV/AIFF into
    // processor.userWaveData[op] (guarded by processor.userWaveLock).
    void loadUserWave(int op, int numSamples);

    CustomKnobLNF myCustomLNF;
    FM12SynthAudioProcessor& processor;

    static constexpr int numOperators = 12;

    // ── Operator Knobs (12 Ops × 6 Parameters: ATT DEC SUS REL VOL RATIO) ───
    // Index 5 within each operator block is RATIO — uses RatioKnob.
    std::vector<std::unique_ptr<juce::Slider>> opKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> opKnobAttachments;

    // ── Cancel toggles (one per operator) ────────────────────────────────────
    std::vector<std::unique_ptr<juce::ToggleButton>> cancelToggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> cancelAttachments;

    // ── DELAY knobs (one per operator, 0 – 10 s) ─────────────────────────────
    // Delays the ADSR note-on by this many seconds after a MIDI note is played.
    std::vector<std::unique_ptr<juce::Slider>> delayKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> delayAttachments;

    // ── OSC waveform comboboxes (one per operator) ────────────────────────────
    // Items: Sine | Tri | Saw | Sqr | User (512) | User (1k) | User (2k)
    // Managed manually (no APVTS attachment) so we can intercept "User" picks
    // and open a file chooser to load the single-cycle wave.
    std::vector<std::unique_ptr<juce::ComboBox>> oscCombos;

    // ── Audio format manager (used by loadUserWave) ───────────────────────────
    juce::AudioFormatManager formatManager;

    // ── Routing Matrix (12 × 12 checkboxes, excluding diagonal) ─────────────
    std::vector<std::unique_ptr<juce::ToggleButton>> matrixButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> matrixAttachments;

    // ── Feedback knobs (diagonal of matrix — 12 knobs) ───────────────────────
    std::vector<std::unique_ptr<juce::Slider>> feedbackKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> feedbackAttachments;

    std::unique_ptr<juce::TextButton> randomizeButton;
    std::unique_ptr<juce::TextButton> stabButton;

    // ── Preset Save/Load ──────────────────────────────────────────────────────
    std::unique_ptr<juce::TextButton> saveButton;
    std::unique_ptr<juce::TextButton> loadButton;
    std::unique_ptr<juce::TextButton> halveModsButton;

    // ── EXP FB toggle ─────────────────────────────────────────────────────────
    std::unique_ptr<juce::ToggleButton> expFeedbackToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> expFeedbackAttachment;

    // ── FM Engine Mode combobox ───────────────────────────────────────────────
    std::unique_ptr<juce::ComboBox> fmEngineModeComboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fmEngineModeAttachment;

    std::unique_ptr<juce::Slider> chorusAmountKnob;
    std::unique_ptr<juce::Slider> chorusWidthKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusWidthAttachment;

    std::unique_ptr<juce::Slider> nyquistSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> nyquistAttachment;

    // ── View OP selector ──────────────────────────────────────────────────────
    //   0        = All (12 ops)
    //   1–12     = single operator
    //   13–18    = pairs:  1&2, 3&4, 5&6, 7&8, 9&10, 11&12
    //   19–21    = quads:  1–4, 5–8, 9–12
    //   22-23    = sixes:  1-6, 7-12
    std::unique_ptr<juce::ComboBox> viewOpComboBox;
    int currentViewOp = 0;

    // Returns {firstOp (0-indexed), opCount} for the current view selection.
    std::pair<int, int> getViewRange() const;

    void updateViewLayout();
    void randomizeMatrix();
    void randomizeStab();
    void halveModulators();
    void savePreset();
    void loadPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessorEditor)
};