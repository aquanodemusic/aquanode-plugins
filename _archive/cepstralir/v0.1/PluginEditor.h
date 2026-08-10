#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Waveform display component for source and IR preview
class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay(juce::Colour colour) : waveColour(colour) {}

    void setBuffer(const juce::AudioBuffer<float>* buf)
    {
        buffer = buf;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Background - white with slight cyan tint
        g.setColour(juce::Colour(0xffffffff));
        g.fillRoundedRectangle(bounds, 4.0f);

        // Grid lines - light cyan
        g.setColour(juce::Colour(0xffc0e0e8));
        float midY = bounds.getCentreY();
        g.drawHorizontalLine((int)midY, bounds.getX(), bounds.getRight());
        g.drawHorizontalLine((int)(midY - bounds.getHeight() * 0.25f), bounds.getX(), bounds.getRight());
        g.drawHorizontalLine((int)(midY + bounds.getHeight() * 0.25f), bounds.getX(), bounds.getRight());

        if (buffer == nullptr || buffer->getNumSamples() == 0)
        {
            g.setColour(juce::Colour(0xff88aaaa));
            g.setFont(juce::Font(13.0f));
            g.drawText("No data", bounds, juce::Justification::centred);
            return;
        }

        // Draw waveform
        int numSamples = buffer->getNumSamples();
        float w = bounds.getWidth();
        float h = bounds.getHeight();
        float halfH = h * 0.5f;
        float yOff = bounds.getY() + halfH;

        juce::Path wavePath;
        bool started = false;

        int samplesPerPixel = std::max(1, (int)(numSamples / w));
        int numPixels = (int)w;

        // Find peak for better scaling (excluding first/last 1% to avoid edge artifacts)
        float globalPeak = 0.0f;
        int skipSamples = numSamples / 100;  // Skip 1% on each end
        for (int ch = 0; ch < buffer->getNumChannels(); ++ch)
        {
            const float* rd = buffer->getReadPointer(ch);
            for (int s = skipSamples; s < numSamples - skipSamples; ++s)
                globalPeak = std::max(globalPeak, std::abs(rd[s]));
        }
        float scale = globalPeak > 0.0f ? 0.95f / globalPeak : 0.95f;

        for (int px = 0; px < numPixels; ++px)
        {
            int sampleStart = (int)((float)px / w * numSamples);
            int sampleEnd = std::min(sampleStart + samplesPerPixel, numSamples);

            float maxVal = 0.0f, minVal = 0.0f;
            for (int ch = 0; ch < buffer->getNumChannels(); ++ch)
            {
                const float* rd = buffer->getReadPointer(ch);
                for (int s = sampleStart; s < sampleEnd; ++s)
                {
                    maxVal = std::max(maxVal, rd[s]);
                    minVal = std::min(minVal, rd[s]);
                }
            }

            float x = bounds.getX() + (float)px;
            float yTop = yOff - maxVal * halfH * scale;
            float yBottom = yOff - minVal * halfH * scale;

            if (!started)
            {
                wavePath.startNewSubPath(x, yTop);
                started = true;
            }
            else
                wavePath.lineTo(x, yTop);
        }

        // Draw filled area
        juce::Path fillPath(wavePath);
        fillPath.lineTo(bounds.getRight(), yOff);
        fillPath.lineTo(bounds.getX(), yOff);
        fillPath.closeSubPath();

        g.setColour(waveColour.withAlpha(0.15f));
        g.fillPath(fillPath);

        g.setColour(waveColour);
        juce::PathStrokeType stroke(1.2f);
        g.strokePath(wavePath, stroke);

        // Border
        g.setColour(waveColour.withAlpha(0.4f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

private:
    const juce::AudioBuffer<float>* buffer = nullptr;
    juce::Colour waveColour;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
// Custom rotary knob look and feel
class IRLookAndFeel : public juce::LookAndFeel_V4
{
public:
    IRLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff00aa99));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff003355));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xffb0d8e8));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xffffffff));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider&) override
    {
        float radius = std::min(width, height) * 0.4f;
        float cx = x + width * 0.5f;
        float cy = y + height * 0.5f;

        // Background track
        juce::Path track;
        track.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xffc0e0e8));
        g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Fill arc
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        juce::Path fill;
        fill.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xff00cccc));
        g.strokePath(fill, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Thumb dot
        float thumbX = cx + radius * std::sin(angle);
        float thumbY = cy - radius * std::cos(angle);
        g.setColour(juce::Colour(0xff006688));
        g.fillEllipse(thumbX - 4, thumbY - 4, 8, 8);

        // Inner circle
        g.setColour(juce::Colour(0xfff0f8ff));
        g.fillEllipse(cx - radius * 0.55f, cy - radius * 0.55f,
            radius * 1.1f, radius * 1.1f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        bool isToggled = button.getToggleState();

        juce::Colour base = isToggled ? juce::Colour(0xff00cccc) : juce::Colour(0xffe0f0f8);
        if (isHighlighted) base = base.brighter(0.1f);
        if (isDown)        base = base.darker(0.15f);

        g.setColour(base);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colour(0xff00aa99).withAlpha(0.8f));
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
        bool, bool) override
    {
        g.setColour(button.getToggleState() ? juce::Colour(0xff003355) : juce::Colour(0xff006688));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(button.getButtonText(), button.getLocalBounds(),
            juce::Justification::centred, true);
    }
};

//==============================================================================
class CepstralIREditor : public juce::AudioProcessorEditor,
    public juce::Timer,
    public juce::DragAndDropTarget
{
public:
    CepstralIREditor(CepstralIRProcessor&);
    ~CepstralIREditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // Drag & Drop for audio files
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails&) override { isDragOver = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { isDragOver = false; repaint(); }

private:
    void loadFile(const juce::File& f);
    void triggerReprocess();

    CepstralIRProcessor& processorRef;
    IRLookAndFeel laf;

    // Waveform displays
    WaveformDisplay sourceDisplay{ juce::Colour(0xff00aa99) };
    WaveformDisplay irDisplay{ juce::Colour(0xff0088cc) };

    // Buttons
    juce::TextButton loadButton{ "LOAD SAMPLE" };
    juce::TextButton saveIRButton{ "SAVE IR to WAV" };
    juce::TextButton reprocessButton{ "RE-EXTRACT IR" };

    // Sliders
    juce::Slider irLengthSlider;
    juce::Slider smoothingSlider;
    juce::Slider fftSizeSlider;

    juce::ToggleButton windowToggle{ "Window" };
    juce::ToggleButton linearPhaseToggle{ "Linear Phase (Centered)" };

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> irLenAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothAttach;

    // Labels
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label sourceLabel, irLabel;
    juce::Label irLenLabel, smoothLabel, fftLabel;
    juce::Label hintLabel;

    bool isDragOver = false;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CepstralIREditor)
};