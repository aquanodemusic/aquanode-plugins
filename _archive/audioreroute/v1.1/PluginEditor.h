#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// ── Custom LookAndFeel ────────────────────────────────────────────────────────
// Draws toggle-button checkboxes with a visible white outline so the hitbox
// is always clearly visible, and paints rotary knobs in the plugin's accent
// colour for the dry / wet controls.
class AudioRerouterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AudioRerouterLookAndFeel()
    {
        // Inherit dark defaults then override per-component below
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1e1e2e));
    }

    // ── Checkbox (ToggleButton tick box) ─────────────────────────────────────
    // Draws a square with a white outline; tick mark is solid accent blue when on.
    void drawTickBox(juce::Graphics& g,
        juce::Component& /*component*/,
        float x, float y, float w, float h,
        bool ticked,
        bool isEnabled,
        bool /*isMouseOverButton*/,
        bool /*isButtonDown*/) override
    {
        const float boxSize = juce::jmin(w, h) - 2.0f;
        const float bx = x + (w - boxSize) * 0.5f;
        const float by = y + (h - boxSize) * 0.5f;

        // Background fill
        g.setColour(juce::Colour(0xff313244));
        g.fillRoundedRectangle(bx, by, boxSize, boxSize, 3.0f);

        // White outline — always visible so the clickable area is obvious
        g.setColour(isEnabled ? juce::Colours::white.withAlpha(0.85f)
            : juce::Colours::white.withAlpha(0.35f));
        g.drawRoundedRectangle(bx, by, boxSize, boxSize, 3.0f, 1.5f);

        // Tick mark
        if (ticked)
        {
            g.setColour(juce::Colour(0xff89b4fa));
            const float pad = 3.5f;
            juce::Path tick;
            tick.startNewSubPath(bx + pad, by + boxSize * 0.5f);
            tick.lineTo(bx + boxSize * 0.4f, by + boxSize - pad);
            tick.lineTo(bx + boxSize - pad, by + pad);
            g.strokePath(tick, juce::PathStrokeType(2.2f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }
    }

    // ── Rotary knob ───────────────────────────────────────────────────────────
    // Simple flat knob: dark filled circle, accent arc, white dot indicator.
    void drawRotarySlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPosProportional,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider& /*slider*/) override
    {
        const float radius = juce::jmin((float)width, (float)height) * 0.5f - 4.0f;
        const float centreX = (float)x + (float)width * 0.5f;
        const float centreY = (float)y + (float)height * 0.5f;
        const float angle = rotaryStartAngle
            + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Body
        g.setColour(juce::Colour(0xff313244));
        g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

        // Outline
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.5f);

        // Arc track (background)
        {
            juce::Path track;
            track.addArc(centreX - radius + 4.0f, centreY - radius + 4.0f,
                (radius - 4.0f) * 2.0f, (radius - 4.0f) * 2.0f,
                rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(juce::Colour(0xff1e1e2e));
            g.strokePath(track, juce::PathStrokeType(3.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Arc fill (value)
        if (sliderPosProportional > 0.0f)
        {
            juce::Path fill;
            fill.addArc(centreX - radius + 4.0f, centreY - radius + 4.0f,
                (radius - 4.0f) * 2.0f, (radius - 4.0f) * 2.0f,
                rotaryStartAngle, angle, true);
            g.setColour(juce::Colour(0xff89b4fa));
            g.strokePath(fill, juce::PathStrokeType(3.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Pointer dot
        const float dotR = 3.5f;
        const float dotDist = radius - 7.0f;
        g.setColour(juce::Colours::white);
        g.fillEllipse(centreX + dotDist * std::sin(angle) - dotR,
            centreY - dotDist * std::cos(angle) - dotR,
            dotR * 2.0f, dotR * 2.0f);
    }
};

// ── Editor ────────────────────────────────────────────────────────────────────
class AudioRerouterPluginAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::Slider::Listener,
    public juce::Button::Listener
{
public:
    AudioRerouterPluginAudioProcessorEditor(AudioRerouterPluginAudioProcessor&);
    ~AudioRerouterPluginAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void sliderValueChanged(juce::Slider* slider) override;
    void buttonClicked(juce::Button* button) override;

private:
    AudioRerouterPluginAudioProcessor& audioProcessor;

    AudioRerouterLookAndFeel laf;

    // ── PDC toggle (top-right corner of header bar) ───────────────────────────
    juce::ToggleButton pdcToggle;
    juce::Label        pdcLabel;

    // ── Mode row: two exclusive checkboxes ────────────────────────────────────
    juce::Label        modeLabel;
    juce::ToggleButton sendToggle;     // false = transmit  (mode param == 0)
    juce::ToggleButton receiveToggle;  // true  = receive   (mode param == 1)

    // ── Channel row ───────────────────────────────────────────────────────────
    juce::Label        channelLabel;
    juce::Slider       channelSlider;

    // ── Limiter + Threshold row (combined) ────────────────────────────────────
    juce::ToggleButton limiterToggle;
    juce::Label        limiterLabel;
    juce::Label        thresholdLabel;
    juce::Slider       thresholdSlider;

    // ── Dry / Wet row: two rotary knobs side by side ──────────────────────────
    juce::Label        dryLabel;
    juce::Slider       drySlider;
    juce::Label        wetLabel;
    juce::Slider       wetSlider;

    // ── APVTS attachments ─────────────────────────────────────────────────────
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> channelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRerouterPluginAudioProcessorEditor)
};