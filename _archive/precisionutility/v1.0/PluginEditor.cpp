/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PrecisionUtilityAudioProcessorEditor::PrecisionUtilityAudioProcessorEditor (PrecisionUtilityAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (400, 500);
    
    // === Delay Section ===
    delayLabel.setText("Delay (ms)", juce::dontSendNotification);
    delayLabel.setJustificationType(juce::Justification::centred);
    delayLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(delayLabel);
    
    delaySlider.setSliderStyle(juce::Slider::LinearVertical);
    delaySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    delaySlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    delaySlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00ffff));
    delaySlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff004444));
    addAndMakeVisible(delaySlider);
    delayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "delay", delaySlider);
    
    delayValueBox.setMultiLine(false);
    delayValueBox.setReturnKeyStartsNewLine(false);
    delayValueBox.setReadOnly(false);
    delayValueBox.setCaretVisible(true);
    delayValueBox.setPopupMenuEnabled(true);
    delayValueBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff003333));
    delayValueBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    delayValueBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff00ffff));
    delayValueBox.setJustification(juce::Justification::centred);
    delayValueBox.setText(juce::String(delaySlider.getValue(), 1));
    delayValueBox.onReturnKey = [this]() {
        float value = delayValueBox.getText().getFloatValue();
        value = juce::jlimit(0.0f, 10000.0f, value);
        delaySlider.setValue(value);
        delayValueBox.setText(juce::String(value, 1));
    };
    addAndMakeVisible(delayValueBox);
    
    delaySlider.onValueChange = [this]() {
        delayValueBox.setText(juce::String(delaySlider.getValue(), 1), false);
    };
    
    // === Phase Invert Section ===
    phaseLabel.setText("Phase Invert", juce::dontSendNotification);
    phaseLabel.setJustificationType(juce::Justification::centred);
    phaseLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(phaseLabel);
    
    phaseSlider.setSliderStyle(juce::Slider::LinearVertical);
    phaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    phaseSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    phaseSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00ffff));
    phaseSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff004444));
    phaseSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    phaseSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff003333));
    phaseSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(phaseSlider);
    phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "phase", phaseSlider);
    
    // === Pan Left Section ===
    panLeftLabel.setText("Pan Left", juce::dontSendNotification);
    panLeftLabel.setJustificationType(juce::Justification::centred);
    panLeftLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(panLeftLabel);
    
    panLeftSlider.setSliderStyle(juce::Slider::LinearVertical);
    panLeftSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    panLeftSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    panLeftSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00ffff));
    panLeftSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff004444));
    panLeftSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    panLeftSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff003333));
    panLeftSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(panLeftSlider);
    panLeftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "panLeft", panLeftSlider);
    
    // === Pan Right Section ===
    panRightLabel.setText("Pan Right", juce::dontSendNotification);
    panRightLabel.setJustificationType(juce::Justification::centred);
    panRightLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(panRightLabel);
    
    panRightSlider.setSliderStyle(juce::Slider::LinearVertical);
    panRightSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    panRightSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    panRightSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00ffff));
    panRightSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff004444));
    panRightSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    panRightSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff003333));
    panRightSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(panRightSlider);
    panRightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "panRight", panRightSlider);
}

PrecisionUtilityAudioProcessorEditor::~PrecisionUtilityAudioProcessorEditor()
{
}

//==============================================================================
void PrecisionUtilityAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Create turquoise gradient background
    juce::ColourGradient gradient(
        juce::Colour(0xff00cccc), 0.0f, 0.0f,
        juce::Colour(0xff007777), 0.0f, static_cast<float>(getHeight()),
        false);
    
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawFittedText("Precision Utility", getLocalBounds().removeFromTop(50), juce::Justification::centred, 1);
}

void PrecisionUtilityAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(60); // Title area
    bounds.reduce(20, 10);
    
    int sliderWidth = 80;
    int spacing = 10;
    int totalWidth = (sliderWidth * 4) + (spacing * 3);
    int startX = (getWidth() - totalWidth) / 2;
    
    // Delay section
    auto delayBounds = bounds.removeFromLeft(sliderWidth);
    delayLabel.setBounds(delayBounds.removeFromTop(25));
    delayValueBox.setBounds(delayBounds.removeFromTop(30).reduced(5, 0));
    delayBounds.removeFromTop(5);
    delaySlider.setBounds(delayBounds.removeFromTop(280));
    
    bounds.removeFromLeft(spacing);
    
    // Phase section
    auto phaseBounds = bounds.removeFromLeft(sliderWidth);
    phaseLabel.setBounds(phaseBounds.removeFromTop(25));
    phaseBounds.removeFromTop(5);
    phaseSlider.setBounds(phaseBounds.removeFromTop(330));
    
    bounds.removeFromLeft(spacing);
    
    // Pan Left section
    auto panLeftBounds = bounds.removeFromLeft(sliderWidth);
    panLeftLabel.setBounds(panLeftBounds.removeFromTop(25));
    panLeftBounds.removeFromTop(5);
    panLeftSlider.setBounds(panLeftBounds.removeFromTop(330));
    
    bounds.removeFromLeft(spacing);
    
    // Pan Right section
    auto panRightBounds = bounds.removeFromLeft(sliderWidth);
    panRightLabel.setBounds(panRightBounds.removeFromTop(25));
    panRightBounds.removeFromTop(5);
    panRightSlider.setBounds(panRightBounds.removeFromTop(330));
}
