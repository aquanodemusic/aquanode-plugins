/*
  ==============================================================================
    PluginEditor.h
    Top-down view of the two drums (click to play) with rows of knobs above.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** The clickable top-down drum surface. Clicking a drum computes the radial
    strike position (centre = syahi, edge = rim) and fires a strike. Vertical
    click height sets velocity (top = hard, bottom = soft), so you can perform
    dynamics with the mouse.                                                  */
class TablaPad : public juce::Component,
    private juce::Timer
{
public:
    explicit TablaPad(TablaAudioProcessor& p) : proc(p) { startTimerHz(30); }

    void setSlideMode(bool on) { slideMode = on; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void hit(const juce::MouseEvent& e);
    void timerCallback() override { repaint(); }

    juce::Rectangle<float> bayanCircle() const;
    juce::Rectangle<float> dayanCircle() const;

    TablaAudioProcessor& proc;
    bool slideMode = false;
    bool dragging = false;
    int  dragDrum = -1;
    juce::Point<float> dragStart;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TablaPad)
};

//==============================================================================
class TablaAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit TablaAudioProcessorEditor(TablaAudioProcessor&);
    ~TablaAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct KnobBlock
    {
        juce::Slider slider;
        juce::Label  label;
        juce::Label  valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void addKnob(const juce::String& paramID, const juce::String& text);

    TablaAudioProcessor& processor;
    TablaPad pad;
    juce::OwnedArray<KnobBlock> knobs;
    juce::Label title;
    juce::ToggleButton slideModeToggle{ "Slide Mode" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TablaAudioProcessorEditor)
};