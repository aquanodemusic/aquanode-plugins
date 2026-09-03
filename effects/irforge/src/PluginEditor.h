/*
  ==============================================================================

    PluginEditor.h
    IRForge — interface.

    Layout is built around the thing the old two-plugin workflow lacked: a
    feedback loop. The source waveform and the resulting IR spectrum sit one
    above the other, so every control shows its effect immediately.

      +------------------------------------------------------------+
      |  IRFORGE            [ sample name ]      Load  Save  Clear  |
      +------------------------------------------------------------+
      |  SOURCE WAVEFORM  (drag to crop)                           |
      +------------------------------------------------------------+
      |  RESULTING IR — spectrum + impulse                         |
      +------------------------------------------------------------+
      |  CHARACTER (big)  |  FORGE knobs   |  SHAPE     |  OUTPUT  |
      +------------------------------------------------------------+

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace irforge
{
    struct Palette
    {
        static juce::Colour bg() { return juce::Colour(0xff141a1d); }
        static juce::Colour bgLift() { return juce::Colour(0xff1d262a); }
        static juce::Colour panel() { return juce::Colours::white.withAlpha(0.055f); }
        static juce::Colour edge() { return juce::Colours::white.withAlpha(0.13f); }
        static juce::Colour hot() { return juce::Colour(0xffe8a33d); }   // the forge
        static juce::Colour cool() { return juce::Colour(0xff49b8c4); }
        static juce::Colour source() { return juce::Colour(0xff7f8f96); }
        static juce::Colour outputGreen() { return juce::Colour(0xff5bc47d); }
        static juce::Colour text() { return juce::Colours::white.withAlpha(0.90f); }
        static juce::Colour textDim() { return juce::Colours::white.withAlpha(0.55f); }
    };

    class ForgeLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        ForgeLookAndFeel();
        void drawRotarySlider(juce::Graphics&, int, int, int, int,
            float, float, float, juce::Slider&) override;
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
            bool, bool) override;
        juce::Font getLabelFont(juce::Label&) override;
    };

    class ForgeKnob : public juce::Component
    {
    public:
        explicit ForgeKnob(juce::String caption = {}, bool large = false);
        void resized() override;
        void paint(juce::Graphics&) override;
        juce::Slider slider;
        void setCaption(const juce::String& c) { text = c; repaint(); }
        void setAccent(juce::Colour c) { accent = c; slider.setColour(juce::Slider::rotarySliderFillColourId, c); }

    private:
        juce::String text;
        juce::Colour accent{ Palette::hot() };
        bool isLarge;
    };

    //==========================================================================
    // Source waveform with draggable crop handles.
    //
    // Which part of the sample you convert is the entire musical decision —
    // one chord rather than the next — so this is the most important control
    // on the panel even though it is not a knob.
    //==========================================================================
    class SourceDisplay : public juce::Component,
        public juce::FileDragAndDropTarget,
        private juce::Timer
    {
    public:
        explicit SourceDisplay(IRForgeAudioProcessor&);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

        void mouseUp(const juce::MouseEvent&) override;

        bool isInterestedInFileDrag(const juce::StringArray&) override;
        void filesDropped(const juce::StringArray&, int, int) override;
        void fileDragEnter(const juce::StringArray&, int, int) override;
        void fileDragExit(const juce::StringArray&) override;

        std::function<void(const juce::File&)> onFileDropped;

    private:
        void timerCallback() override;
        void refreshOverview();
        float xToNorm(float x) const;
        // Largest fraction of the whole timeline the crop region is allowed
        // to span — 1.0 (unrestricted) unless the source is file-backed and
        // longer than the processor's load window, in which case the handles
        // can never be pulled further apart than that window covers.
        float maxSpanFrac() const;

        IRForgeAudioProcessor& processor;
        std::vector<float> ovMin, ovMax; // whole-timeline overview, local copy
        double totalSeconds = 0.0;
        int lastGeneration = -1;
        bool draggingStart = false, draggingEnd = false, dragOver = false;
    };

    //==========================================================================
    // The resulting IR: magnitude spectrum above, impulse below.
    //
    // The spectrum view is what makes CHARACTER legible — you can watch the
    // harmonic comb emerge from the smooth envelope as you turn it up.
    //==========================================================================
    class IRDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit IRDisplay(IRForgeAudioProcessor&);
        void paint(juce::Graphics&) override;

    private:
        void timerCallback() override;
        void recompute();
        IRForgeAudioProcessor& processor;
        std::vector<float> magDb, impulse;
        int lastLen = -1;
        int lastGeneration = -1;
        bool dirty = true;
    };
}

//==============================================================================
class IRForgeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit IRForgeAudioProcessorEditor(IRForgeAudioProcessor&);
    ~IRForgeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void openLoadDialog();
    void openSaveDialog();
    void toggleRecording();
    void updateRecordButtonAppearance();

    IRForgeAudioProcessor& processor;
    irforge::ForgeLookAndFeel lnf;

    juce::TextButton loadButton{ "Load" }, saveButton{ "Save IR" }, clearButton{ "Clear" };
    juce::TextButton recordButton{ "Record" };
    juce::Label titleLabel, sourceLabel, statusLabel;

    std::unique_ptr<irforge::SourceDisplay> sourceDisplay;
    std::unique_ptr<irforge::IRDisplay> irDisplay;

    // forge section
    irforge::ForgeKnob characterKnob{ "CHARACTER", true };
    irforge::ForgeKnob fftKnob{ "FFT 2^n" }, lengthKnob{ "Length" }, decayKnob{ "Decay" };
    juce::ToggleButton linearPhaseButton{ "Linear Phase" };

    // shape section
    irforge::ForgeKnob stretchKnob{ "Stretch" }, predelayKnob{ "Predelay" };
    irforge::ForgeKnob lowCutKnob{ "Low Cut" }, highCutKnob{ "High Cut" };
    irforge::ForgeKnob tiltKnob{ "Tilt" }, widthKnob{ "Width" };
    juce::ToggleButton reverseButton{ "Reverse" };

    // output
    irforge::ForgeKnob mixKnob{ "Mix" }, gainKnob{ "Output" };

    std::unique_ptr<juce::FileChooser> chooser;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SA>> sliderAttachments;
    std::vector<std::unique_ptr<BA>> buttonAttachments;
    void attach(irforge::ForgeKnob&, const juce::String& id, juce::Colour accent);
    void attach(juce::ToggleButton&, const juce::String& id);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRForgeAudioProcessorEditor)
};