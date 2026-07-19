#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class CustomKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomKnobLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff88dd99));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff66bb77));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a3f2f));
    }
    
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centreX = bounds.getCentreX();
        auto centreY = bounds.getCentreY();
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        
        // Outer glow
        g.setColour(juce::Colour(0xff66bb77).withAlpha(0.3f));
        g.fillEllipse(centreX - radius - 3, centreY - radius - 3, radius * 2 + 6, radius * 2 + 6);
        
        // Track background
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(centreX, centreY, radius, radius,
                                   0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff1a2520));
        g.strokePath(backgroundArc, juce::PathStrokeType(4.0f));
        
        // Value arc
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius, radius,
                              0.0f, rotaryStartAngle, angle, true);
        
        juce::ColourGradient gradient(juce::Colour(0xff88dd99), centreX - radius, centreY,
                                     juce::Colour(0xff44aa66), centreX + radius, centreY, false);
        g.setGradientFill(gradient);
        g.strokePath(valueArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
        
        // Center knob with gradient
        auto knobRadius = radius * 0.6f;
        juce::ColourGradient knobGradient(juce::Colour(0xff9eff00), centreX, centreY - knobRadius,
                                         juce::Colour(0xff66bb77), centreX, centreY + knobRadius, false);
        g.setGradientFill(knobGradient);
        g.fillEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2, knobRadius * 2);
        
        // Inner shadow
        g.setColour(juce::Colour(0xff000000).withAlpha(0.3f));
        g.fillEllipse(centreX - knobRadius + 2, centreY - knobRadius + 2, knobRadius * 2 - 4, knobRadius * 2 - 4);
        
        // Shiny highlight
        juce::ColourGradient highlight(juce::Colour(0xffffffff).withAlpha(0.4f), 
                                       centreX - knobRadius * 0.3f, centreY - knobRadius * 0.7f,
                                       juce::Colour(0x00ffffff), 
                                       centreX, centreY, true);
        g.setGradientFill(highlight);
        g.fillEllipse(centreX - knobRadius * 0.9f, centreY - knobRadius * 0.9f, 
                     knobRadius * 1.2f, knobRadius * 1.2f);
        
        // Pointer
        juce::Path pointer;
        auto pointerLength = radius * 0.4f;
        auto pointerThickness = 3.0f;
        pointer.addRectangle(-pointerThickness * 0.5f, -radius * 0.55f, pointerThickness, pointerLength);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        
        g.setColour(juce::Colour(0xff2a3f2f));
        g.fillPath(pointer);
    }
};

//==============================================================================
class WaveformDisplay : public juce::Component,
    public juce::FileDragAndDropTarget
{
public:
    WaveformDisplay(SlicerAudioProcessor& p) : processor(p) {}

    void paint(juce::Graphics& g) override;
    void resized() override {}

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    SlicerAudioProcessor& processor;

    int draggedSliceIndex = -1;
    bool isDraggingRegionStart = false;
    bool isDraggingRegionEnd = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
class SlicerAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    SlicerAudioProcessorEditor(SlicerAudioProcessor&);
    ~SlicerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SlicerAudioProcessor& audioProcessor;

    WaveformDisplay waveformDisplay;
    juce::TextButton loadButton;
    juce::TextButton analyzeButton;
    juce::TextButton exportButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> regionStartAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> regionEndAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliceStrengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fadeoutMsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sliceEndModeAttachment;

    juce::Slider regionStartSlider;
    juce::Slider regionEndSlider;
    juce::Slider sliceStrengthSlider;
    juce::Slider volumeSlider;
    juce::Slider fadeoutMsSlider;
    
    juce::ComboBox sliceEndModeCombo;

    juce::Label regionStartLabel;
    juce::Label regionEndLabel;
    juce::Label sliceStrengthLabel;
    juce::Label volumeLabel;
    juce::Label fadeoutMsLabel;
    juce::Label infoLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::FileChooser> exportChooser;

    bool shouldReanalyze = false;

    void updateInfoLabel();
    
    CustomKnobLookAndFeel customKnobLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicerAudioProcessorEditor)
};