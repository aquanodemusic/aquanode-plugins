#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Sakura palette — kept in one place so the whole app stays consistent.
namespace Sakura
{
    static const juce::Colour bgTop(0xff3a0f26);
    static const juce::Colour bgBottom(0xff180810);
    static const juce::Colour panel(0xff4a1830);
    static const juce::Colour panelLight(0xff5c2038);
    static const juce::Colour blossom(0xffffb7c5);
    static const juce::Colour blossomHi(0xffffd9e3);
    static const juce::Colour reddish(0xffe8557d);
    static const juce::Colour reddishGlow(0xffff8fab);
    static const juce::Colour textMain(0xfffdf1f5);
    static const juce::Colour textDim(0xffcfa3b3);
}

//==============================================================================
// Waveform display with a draggable start/end selection region.
class WaveformDisplay : public juce::Component, public juce::Timer
{
public:
    WaveformDisplay(AudioStretcherAudioProcessor& proc) : processor(proc)
    {
        startTimer(30); // ~30Hz, smooth enough for the playhead without being wasteful
    }

    ~WaveformDisplay() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(Sakura::panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);

        if (!processor.hasAudioLoaded())
        {
            g.setColour(Sakura::textDim);
            g.setFont(15.0f);
            g.drawText("Load a track to start", getLocalBounds(), juce::Justification::centred);
            return;
        }

        int numSamples = processor.getLoadedSampleCount();
        int numChannels = processor.getLoadedNumChannels();
        float width = (float)getWidth();
        float height = (float)getHeight();
        float midY = height * 0.5f;

        int samplesPerPixel = juce::jmax(1, numSamples / juce::jmax(1, (int)width));

        juce::Path waveformPath;
        waveformPath.startNewSubPath(0, midY);

        for (int x = 0; x < (int)width; ++x)
        {
            int startSample = x * samplesPerPixel;
            int endSample = juce::jmin(startSample + samplesPerPixel, numSamples);

            float maxVal = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* channelData = processor.getLoadedBufferReadPointer(ch);
                if (channelData != nullptr)
                    for (int i = startSample; i < endSample; ++i)
                        maxVal = juce::jmax(maxVal, std::abs(channelData[i]));
            }

            waveformPath.lineTo((float)x, midY - maxVal * midY * 0.9f);
        }

        for (int x = (int)width - 1; x >= 0; --x)
        {
            int startSample = x * samplesPerPixel;
            int endSample = juce::jmin(startSample + samplesPerPixel, numSamples);

            float maxVal = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* channelData = processor.getLoadedBufferReadPointer(ch);
                if (channelData != nullptr)
                    for (int i = startSample; i < endSample; ++i)
                        maxVal = juce::jmax(maxVal, std::abs(channelData[i]));
            }

            waveformPath.lineTo((float)x, midY + maxVal * midY * 0.9f);
        }

        waveformPath.closeSubPath();

        // Unselected part of the waveform, dim
        g.setColour(Sakura::reddish.withAlpha(0.28f));
        g.fillPath(waveformPath);

        // Selected region, bright
        float startX = startPos * width;
        float endX = endPos * width;

        g.saveState();
        g.reduceClipRegion(juce::Rectangle<int>((int)startX, 0, (int)(endX - startX), getHeight()));
        g.setColour(Sakura::blossom);
        g.fillPath(waveformPath);
        g.restoreState();

        // Selection edge lines
        g.setColour(Sakura::blossomHi);
        g.fillRect(startX - 1.0f, 0.0f, 2.0f, height);
        g.fillRect(endX - 1.0f, 0.0f, 2.0f, height);

        // Drag handles
        g.setColour(Sakura::blossomHi);
        g.fillRoundedRectangle(startX - 7, 2.0f, 14.0f, 18.0f, 3.0f);
        g.fillRoundedRectangle(endX - 7, 2.0f, 14.0f, 18.0f, 3.0f);
        g.setColour(Sakura::reddish);
        g.fillRoundedRectangle(startX - 7, height - 20.0f, 14.0f, 18.0f, 3.0f);
        g.fillRoundedRectangle(endX - 7, height - 20.0f, 14.0f, 18.0f, 3.0f);

        // Playhead - only shown while preview is actually playing. The
        // preview only ever covers the selected region, and its internal
        // position is normalised 0..1 across that render, so it maps
        // linearly onto the region's on-screen span regardless of the
        // current speed/pitch settings.
        float progress = processor.getPreviewProgress();
        if (progress >= 0.0f)
        {
            float playheadX = startX + progress * (endX - startX);
            g.setColour(juce::Colours::yellow);
            g.fillRect(playheadX - 1.5f, 0.0f, 3.0f, height);
        }
    }

    void resized() override {}

    void updateRegion()
    {
        if (!processor.hasAudioLoaded())
            return;

        int numSamples = processor.getLoadedSampleCount();
        processor.setRegion((int)(startPos * numSamples), (int)(endPos * numSamples));
        repaint();
    }

    // Called after a new file is loaded so the selection resets to the full track.
    void resetSelection()
    {
        startPos = 0.0f;
        endPos = 1.0f;
        updateRegion();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!processor.hasAudioLoaded())
            return;

        float normalizedX = juce::jlimit(0.0f, 1.0f, (float)e.x / (float)getWidth());
        float startX = startPos * getWidth();
        float endX = endPos * getWidth();

        if (std::abs(e.x - startX) < 16)
            draggingStart = true;
        else if (std::abs(e.x - endX) < 16)
            draggingEnd = true;
        else if (std::abs(normalizedX - startPos) < std::abs(normalizedX - endPos))
            draggingStart = true;
        else
            draggingEnd = true;

        mouseDrag(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!processor.hasAudioLoaded())
            return;

        float normalizedX = juce::jlimit(0.0f, 1.0f, (float)e.x / (float)getWidth());

        if (draggingStart)
            startPos = juce::jlimit(0.0f, endPos - 0.01f, normalizedX);
        else if (draggingEnd)
            endPos = juce::jlimit(startPos + 0.01f, 1.0f, normalizedX);

        updateRegion();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingStart = false;
        draggingEnd = false;
    }

    void timerCallback() override
    {
        bool playing = processor.isPreviewPlaying();
        if (draggingStart || draggingEnd || playing || wasPlaying)
            repaint();
        wasPlaying = playing;
    }

private:
    AudioStretcherAudioProcessor& processor;
    float startPos = 0.0f;
    float endPos = 1.0f;
    bool draggingStart = false;
    bool draggingEnd = false;
    bool wasPlaying = false; // detects the play->stop edge so we repaint one more time and clear the playhead
};

//==============================================================================
// Custom LookAndFeel — sakura pink knobs and buttons.
class SakuraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SakuraLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Sakura::bgBottom);
        setColour(juce::Slider::thumbColourId, Sakura::blossomHi);
        setColour(juce::Slider::trackColourId, Sakura::reddish);
        setColour(juce::Slider::backgroundColourId, Sakura::panel);
        setColour(juce::Slider::textBoxTextColourId, Sakura::textMain);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextButton::buttonColourId, Sakura::panelLight);
        setColour(juce::TextButton::buttonOnColourId, Sakura::reddish);
        setColour(juce::TextButton::textColourOffId, Sakura::textMain);
        setColour(juce::TextButton::textColourOnId, Sakura::textMain);
        setColour(juce::Label::textColourId, Sakura::textMain);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override
    {
        auto radius = juce::jmin(width, height) * 0.5f - 6.0f;
        auto centerX = x + width * 0.5f;
        auto centerY = y + height * 0.5f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Outer glow
        g.setColour(Sakura::reddishGlow.withAlpha(0.28f));
        g.fillEllipse(centerX - radius - 5, centerY - radius - 5, radius * 2 + 10, radius * 2 + 10);

        // Track (unfilled)
        juce::Path track;
        track.addCentredArc(centerX, centerY, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(Sakura::panelLight);
        g.strokePath(track, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Track (filled)
        juce::Path fill;
        fill.addCentredArc(centerX, centerY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(Sakura::reddish);
        g.strokePath(fill, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob body
        auto knobRadius = radius - 8.0f;
        g.setColour(Sakura::panel);
        g.fillEllipse(centerX - knobRadius, centerY - knobRadius, knobRadius * 2, knobRadius * 2);
        g.setColour(Sakura::blossom.withAlpha(0.4f));
        g.drawEllipse(centerX - knobRadius, centerY - knobRadius, knobRadius * 2, knobRadius * 2, 1.5f);

        // Pointer
        juce::Path p;
        auto pointerLength = knobRadius * 0.75f;
        p.addRoundedRectangle(-2.0f, -knobRadius + 4.0f, 4.0f, pointerLength, 2.0f);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(centerX, centerY));
        g.setColour(Sakura::blossomHi);
        g.fillPath(p);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto baseColour = backgroundColour;

        if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.15f);

        g.setColour(Sakura::reddishGlow.withAlpha(0.22f));
        g.fillRoundedRectangle(bounds.expanded(2.5f), 10.0f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 8.0f);

        if (button.isEnabled())
        {
            g.setColour(Sakura::blossomHi.withAlpha(0.08f));
            g.fillRoundedRectangle(bounds.removeFromTop(bounds.getHeight() * 0.5f), 8.0f);
        }
    }
};

//==============================================================================
class AudioStretcherAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget,
    public juce::Timer
{
public:
    AudioStretcherAudioProcessorEditor(AudioStretcherAudioProcessor&);
    ~AudioStretcherAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void timerCallback() override;

    void saveSelection();
    void togglePlay();

    // Resets Speed/Pitch/Fine-Tune to default for a newly loaded track.
    // Uses sendNotification so each knob's onValueChange fires and pushes
    // the reset value into the processor parameter too - setting the knob
    // alone (e.g. with dontSendNotification) would reset the UI while the
    // engine kept using whatever was dialled in for the previous track.
    void resetStretchControlsToDefault();

private:
    AudioStretcherAudioProcessor& audioProcessor;

    SakuraLookAndFeel sakuraLookAndFeel;

    double progressValue = 0.0;
    bool isProcessing = false;
    juce::String progressStage;

    juce::Label titleLabel;
    WaveformDisplay waveformDisplay;
    juce::Label fileLabel;

    juce::TextButton loadButton;
    juce::TextButton saveButton;
    juce::TextButton playButton;
    juce::TextButton pauseButton;
    juce::TextButton loopButton;
    juce::TextButton resetButton;

    juce::Label speedLabel;
    juce::Slider speedKnob;

    juce::Label pitchLabel;
    juce::Slider pitchKnob;

    juce::Label fineTuneLabel;
    juce::Slider fineTuneKnob;

    juce::Label fadeInLabel;
    juce::Slider fadeInSlider;

    juce::Label fadeOutLabel;
    juce::Slider fadeOutSlider;

    juce::Label statusLabel;
    juce::ProgressBar progressBar;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioStretcherAudioProcessorEditor)
};