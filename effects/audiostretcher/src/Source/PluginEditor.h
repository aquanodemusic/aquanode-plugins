#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Waveform display component with region selection
class WaveformDisplay : public juce::Component, public juce::Timer
{
public:
    WaveformDisplay(AudioStretcherAudioProcessor& proc) : processor(proc)
    {
        startTimer(100); // Update waveform periodically
    }
    
    ~WaveformDisplay() override
    {
        stopTimer();
    }
    
    void paint(juce::Graphics& g) override
    {
        // Draw waveform background
        g.setColour(juce::Colour(0xff240046));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        
        if (!processor.hasAudioLoaded())
        {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawText("No audio loaded", getLocalBounds(), juce::Justification::centred);
            return;
        }
        
        // Get actual audio data
        int numSamples = processor.getLoadedSampleCount();
        int numChannels = processor.getLoadedNumChannels();
        float width = (float)getWidth();
        float height = (float)getHeight();
        float midY = height * 0.5f;
        
        // Calculate samples per pixel
        int samplesPerPixel = juce::jmax(1, numSamples / (int)width);
        
        // Create waveform path from actual audio data
        juce::Path waveformPath;
        waveformPath.startNewSubPath(0, midY);
        
        // Draw top half of waveform
        for (int x = 0; x < (int)width; ++x)
        {
            int startSample = x * samplesPerPixel;
            int endSample = juce::jmin(startSample + samplesPerPixel, numSamples);
            
            // Find peak amplitude in this pixel's range across all channels
            float maxVal = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* channelData = processor.getLoadedBufferReadPointer(ch);
                if (channelData != nullptr)
                {
                    for (int i = startSample; i < endSample; ++i)
                    {
                        maxVal = juce::jmax(maxVal, std::abs(channelData[i]));
                    }
                }
            }
            
            float yTop = midY - maxVal * midY * 0.9f;
            waveformPath.lineTo((float)x, yTop);
        }
        
        // Draw bottom half of waveform (mirror)
        for (int x = (int)width - 1; x >= 0; --x)
        {
            int startSample = x * samplesPerPixel;
            int endSample = juce::jmin(startSample + samplesPerPixel, numSamples);
            
            float maxVal = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* channelData = processor.getLoadedBufferReadPointer(ch);
                if (channelData != nullptr)
                {
                    for (int i = startSample; i < endSample; ++i)
                    {
                        maxVal = juce::jmax(maxVal, std::abs(channelData[i]));
                    }
                }
            }
            
            float yBottom = midY + maxVal * midY * 0.9f;
            waveformPath.lineTo((float)x, yBottom);
        }
        
        waveformPath.closeSubPath();
        
        // Draw unselected regions darker
        g.setColour(juce::Colour(0xff7b2cbf).withAlpha(0.3f));
        g.fillPath(waveformPath);
        
        // Draw selected region brighter
        float startX = startPos * width;
        float endX = endPos * width;
        
        g.saveState();
        g.reduceClipRegion(juce::Rectangle<int>((int)startX, 0, (int)(endX - startX), getHeight()));
        g.setColour(juce::Colour(0xffc77dff));
        g.fillPath(waveformPath);
        g.restoreState();
        
        // Draw vertical bar markers (not sliders)
        g.setColour(juce::Colours::white);
        g.fillRect(startX - 1, 0.0f, 3.0f, height); // Start marker
        g.fillRect(endX - 1, 0.0f, 3.0f, height);   // End marker
        
        // Draw draggable handles at top
        g.setColour(juce::Colour(0xffc77dff));
        g.fillRoundedRectangle(startX - 6, 2, 12, 16, 2.0f);
        g.fillRoundedRectangle(endX - 6, 2, 12, 16, 2.0f);
    }
    
    void resized() override
    {
        // No child components needed
    }
    
    void updateRegion()
    {
        if (!processor.hasAudioLoaded())
            return;
        
        int numSamples = processor.getLoadedSampleCount();
        int startSample = (int)(startPos * numSamples);
        int endSample = (int)(endPos * numSamples);
        
        processor.setRegion(startSample, endSample);
        repaint();
    }
    
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!processor.hasAudioLoaded())
            return;
        
        float normalizedX = (float)e.x / getWidth();
        normalizedX = juce::jlimit(0.0f, 1.0f, normalizedX);
        
        // Check if clicking near start or end handle
        float startX = startPos * getWidth();
        float endX = endPos * getWidth();
        
        if (std::abs(e.x - startX) < 10)
            draggingStart = true;
        else if (std::abs(e.x - endX) < 10)
            draggingEnd = true;
        else
        {
            // Click on waveform - move nearest marker
            if (std::abs(normalizedX - startPos) < std::abs(normalizedX - endPos))
                draggingStart = true;
            else
                draggingEnd = true;
        }
        
        mouseDrag(e);
    }
    
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!processor.hasAudioLoaded())
            return;
        
        float normalizedX = (float)e.x / getWidth();
        normalizedX = juce::jlimit(0.0f, 1.0f, normalizedX);
        
        if (draggingStart)
        {
            startPos = juce::jlimit(0.0f, endPos - 0.01f, normalizedX);
        }
        else if (draggingEnd)
        {
            endPos = juce::jlimit(startPos + 0.01f, 1.0f, normalizedX);
        }
        
        updateRegion();
    }
    
    void mouseUp(const juce::MouseEvent& e) override
    {
        draggingStart = false;
        draggingEnd = false;
    }
    
    void timerCallback() override
    {
        // Only repaint if dragging or audio just loaded
        if (draggingStart || draggingEnd)
            repaint();
    }

private:
    AudioStretcherAudioProcessor& processor;
    float startPos = 0.0f;
    float endPos = 1.0f;
    bool draggingStart = false;
    bool draggingEnd = false;
};

// Custom LookAndFeel for purple gradient UI
class PurpleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PurpleLookAndFeel()
    {
        // Set base colors
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff1a0a2e));
        setColour(juce::Slider::thumbColourId, juce::Colours::white);
        setColour(juce::Slider::trackColourId, juce::Colour(0xff9d4edd));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff3c096c));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7b2cbf));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9d4edd));
        setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::Label::textColourId, juce::Colours::white);
        setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffc77dff));
    }
    
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        // Custom rotary slider drawing with purple glow
        auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
        auto centerX = x + width * 0.5f;
        auto centerY = y + height * 0.5f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        
        // Draw outer glow
        g.setColour(juce::Colour(0xff9d4edd).withAlpha(0.3f));
        g.fillEllipse(centerX - radius - 4, centerY - radius - 4, radius * 2 + 8, radius * 2 + 8);
        
        // Draw slider background
        g.setColour(juce::Colour(0xff3c096c));
        g.fillEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2);
        
        // Draw pointer
        juce::Path p;
        auto pointerLength = radius * 0.7f;
        auto pointerThickness = 3.0f;
        p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(centerX, centerY));
        g.setColour(juce::Colours::white);
        g.fillPath(p);
    }
    
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float minSliderPos, float maxSliderPos,
                         const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (slider.isHorizontal())
        {
            // Horizontal slider
            auto trackHeight = juce::jmin(6.0f, (float)height * 0.25f);
            auto trackY = y + height * 0.5f - trackHeight * 0.5f;
            
            // Glow effect
            g.setColour(juce::Colour(0xff9d4edd).withAlpha(0.3f));
            g.fillRoundedRectangle((float)x, trackY - 2, (float)width, trackHeight + 4, 4.0f);
            
            // Track background
            g.setColour(juce::Colour(0xff3c096c));
            g.fillRoundedRectangle((float)x, trackY, (float)width, trackHeight, 3.0f);
            
            // Track fill (from start to thumb)
            if (sliderPos > x)
            {
                g.setColour(juce::Colour(0xff9d4edd));
                g.fillRoundedRectangle((float)x, trackY, sliderPos - x, trackHeight, 3.0f);
            }
            
            // Thumb with glow
            auto thumbRadius = 10.0f;
            g.setColour(juce::Colour(0xffc77dff).withAlpha(0.5f));
            g.fillEllipse(sliderPos - thumbRadius - 2, y + height * 0.5f - thumbRadius - 2, 
                         thumbRadius * 2 + 4, thumbRadius * 2 + 4);
            g.setColour(juce::Colours::white);
            g.fillEllipse(sliderPos - thumbRadius, y + height * 0.5f - thumbRadius, 
                         thumbRadius * 2, thumbRadius * 2);
        }
        else
        {
            // Vertical slider
            auto trackWidth = juce::jmin(6.0f, (float)height * 0.25f);
            auto trackX = x + width * 0.5f - trackWidth * 0.5f;
            
            // Glow effect
            g.setColour(juce::Colour(0xff9d4edd).withAlpha(0.3f));
            g.fillRoundedRectangle(trackX - 2, (float)y, trackWidth + 4, (float)height, 4.0f);
            
            // Track background
            g.setColour(juce::Colour(0xff3c096c));
            g.fillRoundedRectangle(trackX, (float)y, trackWidth, (float)height, 3.0f);
            
            // Track fill (from thumb to bottom)
            if (sliderPos < y + height)
            {
                g.setColour(juce::Colour(0xff9d4edd));
                g.fillRoundedRectangle(trackX, sliderPos, trackWidth, y + height - sliderPos, 3.0f);
            }
            
            // Thumb with glow
            auto thumbRadius = 10.0f;
            g.setColour(juce::Colour(0xffc77dff).withAlpha(0.5f));
            g.fillEllipse(x + width * 0.5f - thumbRadius - 2, sliderPos - thumbRadius - 2, 
                         thumbRadius * 2 + 4, thumbRadius * 2 + 4);
            g.setColour(juce::Colours::white);
            g.fillEllipse(x + width * 0.5f - thumbRadius, sliderPos - thumbRadius, 
                         thumbRadius * 2, thumbRadius * 2);
        }
    }
    
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
        auto baseColour = backgroundColour;
        
        if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.2f);
        
        // Glow effect
        g.setColour(juce::Colour(0xffc77dff).withAlpha(0.3f));
        g.fillRoundedRectangle(bounds.expanded(3.0f), 8.0f);
        
        // Button fill
        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 6.0f);
        
        // Highlight
        if (button.isEnabled())
        {
            g.setColour(juce::Colours::white.withAlpha(0.1f));
            g.fillRoundedRectangle(bounds.removeFromTop(bounds.getHeight() * 0.5f), 6.0f);
        }
    }
};

class AudioStretcherAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           public juce::FileDragAndDropTarget,
                                           public juce::Timer
{
public:
    AudioStretcherAudioProcessorEditor (AudioStretcherAudioProcessor&);
    ~AudioStretcherAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    // Drag and drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    
    // Timer for UI updates
    void timerCallback() override;
    
    // Export helper
    void exportAudio(AudioStretcherAudioProcessor::ExportFormat format);

private:
    AudioStretcherAudioProcessor& audioProcessor;
    
    // Custom look and feel
    PurpleLookAndFeel purpleLookAndFeel;
    
    // Progress value must be declared before progressBar
    double progressValue = 0.0;
    bool isProcessing = false;
    juce::String progressStage = "Ready";
    
    // UI Components
    WaveformDisplay waveformDisplay;
    juce::Label titleLabel;
    juce::Label fileLabel;
    juce::TextButton loadButton;
    juce::TextButton previewButton;
    juce::TextButton exportFlacButton;
    juce::TextButton exportMp3Button;
    
    juce::Label pitchLabel;
    juce::Slider pitchSlider;
    
    juce::Label timeStretchLabel;
    juce::Slider timeStretchSlider;
    
    juce::ToggleButton useNaiveMethodToggle;
    
    juce::Label statusLabel;
    juce::ProgressBar progressBar;
    
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioStretcherAudioProcessorEditor)
};
