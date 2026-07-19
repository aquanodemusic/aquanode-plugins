#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // Show message if no audio is loaded
    if (!processor.isAudioLoaded())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load an audio file to begin", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& audio = processor.getLoadedAudio();
    int numSamples = audio.getNumSamples();

    if (numSamples == 0)
        return;

    // Draw waveform
    g.setColour(juce::Colours::lightblue);

    int width = getWidth();
    int height = getHeight();
    int centerY = height / 2;

    juce::Path waveformPath;
    bool firstPoint = true;

    const float* channelData = audio.getReadPointer(0);

    // Sample waveform across display width
    for (int x = 0; x < width; ++x)
    {
        float position = static_cast<float>(x) / width;
        int sampleIndex = static_cast<int>(position * numSamples);

        if (sampleIndex >= 0 && sampleIndex < numSamples)
        {
            float sample = channelData[sampleIndex];
            float y = centerY - (sample * centerY * 0.8f);

            if (firstPoint)
            {
                waveformPath.startNewSubPath(static_cast<float>(x), y);
                firstPoint = false;
            }
            else
            {
                waveformPath.lineTo(static_cast<float>(x), y);
            }
        }
    }

    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

    // Draw playhead line
    float playheadX = processor.getPlayheadPosition() * width;
    g.setColour(processor.isPlaying() ? juce::Colours::green : juce::Colours::yellow);
    g.drawLine(playheadX, 0, playheadX, static_cast<float>(height), 2.0f);

    // Draw playhead circle
    g.fillEllipse(playheadX - 4, centerY - 4, 8, 8);
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    // Jump playhead to clicked position
    updatePlayheadFromMouse(event);
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    // Scrub playhead while dragging
    updatePlayheadFromMouse(event);
    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent& event)
{
    // No action needed on release
}

void WaveformDisplay::updatePlayheadFromMouse(const juce::MouseEvent& event)
{
    // Convert mouse X to normalized position (0.0 to 1.0)
    float normalizedPosition = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(event.x) / getWidth());
    processor.setPlayheadPosition(normalizedPosition);
    repaint();
}

//==============================================================================
// GrainfreezeAudioProcessorEditor Implementation
//==============================================================================

GrainfreezeAudioProcessorEditor::GrainfreezeAudioProcessorEditor(GrainfreezeAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    waveformDisplay(p)
{
    // Set editor size (wider for two-column layout)
    setSize(900, 500);

    //==========================================================================
    // Control Buttons Setup
    //==========================================================================

    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Audio");
    loadButton.onClick = [this] { loadAudioFile(); };

    addAndMakeVisible(playButton);
    playButton.setButtonText("Play / Stop");
    playButton.onClick = [this]
        {
            audioProcessor.setPlaying(!audioProcessor.isPlaying());
        };

    addAndMakeVisible(freezeButton);
    freezeButton.setButtonText("Freeze");
    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this]
        {
            bool freezeMode = freezeButton.getToggleState();
            audioProcessor.freezeModeParam->beginChangeGesture();
            *audioProcessor.freezeModeParam = freezeMode;
            audioProcessor.freezeModeParam->endChangeGesture();

            // Enable/disable glide slider based on freeze mode
            glideSlider.setEnabled(freezeMode);
            glideLabel.setEnabled(freezeMode);
        };

    //==========================================================================
    // Status Labels Setup
    //==========================================================================

    addAndMakeVisible(statusLabel);
    statusLabel.setText("No audio loaded", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(recommendedLabel);
    recommendedLabel.setText("Grainfreeze by aquanode\nRecommended settings for freeze mode:\nStretch 1.5 | FFT 8192 | Hop 6.5 | Micro Move 100%",
        juce::dontSendNotification);
    recommendedLabel.setJustificationType(juce::Justification::centredRight);
    recommendedLabel.setFont(juce::Font(11.0f, juce::Font::italic));
    recommendedLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    //==========================================================================
    // Column Headers Setup
    //==========================================================================

    addAndMakeVisible(primaryControlsLabel);
    primaryControlsLabel.setText("Primary Controls", juce::dontSendNotification);
    primaryControlsLabel.setJustificationType(juce::Justification::centredLeft);
    primaryControlsLabel.setFont(juce::Font(14.0f, juce::Font::bold));

    addAndMakeVisible(advancedControlsLabel);
    advancedControlsLabel.setText("Advanced Controls", juce::dontSendNotification);
    advancedControlsLabel.setJustificationType(juce::Justification::centredLeft);
    advancedControlsLabel.setFont(juce::Font(14.0f, juce::Font::bold));

    //==========================================================================
    // PRIMARY CONTROLS Setup (Left Column)
    //==========================================================================

    // Time Stretch slider
    addAndMakeVisible(timeStretchSlider);
    timeStretchSlider.setRange(0.1, 4.0, 0.01);
    timeStretchSlider.setValue(1.0);
    timeStretchSlider.setSkewFactorFromMidPoint(1.0);
    timeStretchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timeStretchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    timeStretchSlider.onValueChange = [this]
        {
            *audioProcessor.timeStretch = timeStretchSlider.getValue();
        };

    addAndMakeVisible(timeStretchLabel);
    timeStretchLabel.setText("Time Stretch", juce::dontSendNotification);
    timeStretchLabel.setJustificationType(juce::Justification::centredLeft);

    // FFT Size slider
    addAndMakeVisible(fftSizeSlider);
    fftSizeSlider.setRange(0, 7, 1);
    fftSizeSlider.setValue(3);
    fftSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fftSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    fftSizeSlider.onValueChange = [this]
        {
            int index = static_cast<int>(fftSizeSlider.getValue());
            audioProcessor.fftSizeParam->beginChangeGesture();
            *audioProcessor.fftSizeParam = index;
            audioProcessor.fftSizeParam->endChangeGesture();

            // Update suffix to show actual FFT size
            int fftSizes[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
            fftSizeSlider.setTextValueSuffix(" (" + juce::String(fftSizes[index]) + ")");
        };
    fftSizeSlider.setTextValueSuffix(" (4096)");

    addAndMakeVisible(fftSizeLabel);
    fftSizeLabel.setText("FFT Size", juce::dontSendNotification);
    fftSizeLabel.setJustificationType(juce::Justification::centredLeft);

    // Hop Size slider
    addAndMakeVisible(hopSizeSlider);
    hopSizeSlider.setRange(2.0, 16.0, 0.5);
    hopSizeSlider.setValue(4.0);
    hopSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    hopSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    hopSizeSlider.onValueChange = [this]
        {
            *audioProcessor.hopSizeParam = hopSizeSlider.getValue();
        };

    addAndMakeVisible(hopSizeLabel);
    hopSizeLabel.setText("Hop Div", juce::dontSendNotification);
    hopSizeLabel.setJustificationType(juce::Justification::centredLeft);

    // Glide slider (only active in freeze mode)
    addAndMakeVisible(glideSlider);
    glideSlider.setRange(0.0, 1000.0, 1.0);
    glideSlider.setValue(100.0);
    glideSlider.setSkewFactorFromMidPoint(100.0);
    glideSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    glideSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    glideSlider.setTextValueSuffix(" ms");
    glideSlider.setEnabled(false);
    glideSlider.onValueChange = [this]
        {
            *audioProcessor.glideParam = glideSlider.getValue();
        };

    addAndMakeVisible(glideLabel);
    glideLabel.setText("Freeze Glide", juce::dontSendNotification);
    glideLabel.setJustificationType(juce::Justification::centredLeft);
    glideLabel.setEnabled(false);

    //==========================================================================
    // ADVANCED CONTROLS Setup (Right Column)
    //==========================================================================

    // HF Boost slider
    addAndMakeVisible(hfBoostSlider);
    hfBoostSlider.setRange(0.0, 100.0, 1.0);
    hfBoostSlider.setValue(10.0);
    hfBoostSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    hfBoostSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    hfBoostSlider.setTextValueSuffix(" %");
    hfBoostSlider.onValueChange = [this]
        {
            *audioProcessor.hfBoostParam = hfBoostSlider.getValue();
        };

    addAndMakeVisible(hfBoostLabel);
    hfBoostLabel.setText("HF Boost", juce::dontSendNotification);
    hfBoostLabel.setJustificationType(juce::Justification::centredLeft);

    // Micro Movement slider
    addAndMakeVisible(microMovementSlider);
    microMovementSlider.setRange(0.0, 100.0, 1.0);
    microMovementSlider.setValue(20.0);
    microMovementSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    microMovementSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    microMovementSlider.setTextValueSuffix(" %");
    microMovementSlider.onValueChange = [this]
        {
            *audioProcessor.microMovementParam = microMovementSlider.getValue();
        };

    addAndMakeVisible(microMovementLabel);
    microMovementLabel.setText("Micro Move", juce::dontSendNotification);
    microMovementLabel.setJustificationType(juce::Justification::centredLeft);

    // Window Type slider
    addAndMakeVisible(windowTypeSlider);
    windowTypeSlider.setRange(0, 1, 1);
    windowTypeSlider.setValue(1);
    windowTypeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    windowTypeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 20);
    windowTypeSlider.onValueChange = [this]
        {
            int index = static_cast<int>(windowTypeSlider.getValue());
            audioProcessor.windowTypeParam->beginChangeGesture();
            *audioProcessor.windowTypeParam = index;
            audioProcessor.windowTypeParam->endChangeGesture();

            // Update suffix to show window name
            const char* names[] = { "Hann", "Blackman-Harris" };
            windowTypeSlider.setTextValueSuffix(" (" + juce::String(names[index]) + ")");
        };
    windowTypeSlider.setTextValueSuffix(" (Blackman-Harris)");

    addAndMakeVisible(windowTypeLabel);
    windowTypeLabel.setText("Window", juce::dontSendNotification);
    windowTypeLabel.setJustificationType(juce::Justification::centredLeft);

    // Crossfade Length slider
    addAndMakeVisible(crossfadeLengthSlider);
    crossfadeLengthSlider.setRange(1.0, 8.0, 0.5);
    crossfadeLengthSlider.setValue(2.0);
    crossfadeLengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    crossfadeLengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    crossfadeLengthSlider.setTextValueSuffix(" hops");
    crossfadeLengthSlider.onValueChange = [this]
        {
            *audioProcessor.crossfadeLengthParam = crossfadeLengthSlider.getValue();
        };

    addAndMakeVisible(crossfadeLengthLabel);
    crossfadeLengthLabel.setText("X-Fade Len", juce::dontSendNotification);
    crossfadeLengthLabel.setJustificationType(juce::Justification::centredLeft);

    //==========================================================================
    // Waveform Display Setup
    //==========================================================================

    addAndMakeVisible(waveformDisplay);

    // Start timer for UI updates (30Hz)
    startTimerHz(30);
}

GrainfreezeAudioProcessorEditor::~GrainfreezeAudioProcessorEditor()
{
}

void GrainfreezeAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Fill background
    g.fillAll(juce::Colours::darkgrey);
}

void GrainfreezeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    //==========================================================================
    // Top Control Bar Layout
    //==========================================================================

    auto topBar = bounds.removeFromTop(170);
    topBar.reduce(10, 10);

    // Buttons area (left side)
    auto buttonArea = topBar.removeFromLeft(120);
    loadButton.setBounds(buttonArea.removeFromTop(30));
    buttonArea.removeFromTop(5);
    playButton.setBounds(buttonArea.removeFromTop(30));
    buttonArea.removeFromTop(5);
    freezeButton.setBounds(buttonArea.removeFromTop(30));

    topBar.removeFromLeft(15);

    // Parameters area (two columns)
    auto paramsArea = topBar;

    // Calculate column width
    int columnWidth = (paramsArea.getWidth() - 15) / 2;

    // Left column (Primary Controls)
    auto leftColumn = paramsArea.removeFromLeft(columnWidth);

    // Column header
    primaryControlsLabel.setBounds(leftColumn.removeFromTop(20));
    leftColumn.removeFromTop(3);

    // Time Stretch row
    auto stretchRow = leftColumn.removeFromTop(30);
    timeStretchLabel.setBounds(stretchRow.removeFromLeft(90));
    stretchRow.removeFromLeft(5);
    timeStretchSlider.setBounds(stretchRow);
    leftColumn.removeFromTop(2);

    // FFT Size row
    auto fftRow = leftColumn.removeFromTop(30);
    fftSizeLabel.setBounds(fftRow.removeFromLeft(90));
    fftRow.removeFromLeft(5);
    fftSizeSlider.setBounds(fftRow);
    leftColumn.removeFromTop(2);

    // Hop Size row
    auto hopRow = leftColumn.removeFromTop(30);
    hopSizeLabel.setBounds(hopRow.removeFromLeft(90));
    hopRow.removeFromLeft(5);
    hopSizeSlider.setBounds(hopRow);
    leftColumn.removeFromTop(2);

    // Glide row
    auto glideRow = leftColumn.removeFromTop(30);
    glideLabel.setBounds(glideRow.removeFromLeft(90));
    glideRow.removeFromLeft(5);
    glideSlider.setBounds(glideRow);

    // Gap between columns
    paramsArea.removeFromLeft(15);

    // Right column (Advanced Controls)
    auto rightColumn = paramsArea;

    // Column header
    advancedControlsLabel.setBounds(rightColumn.removeFromTop(20));
    rightColumn.removeFromTop(3);

    // HF Boost row
    auto hfRow = rightColumn.removeFromTop(30);
    hfBoostLabel.setBounds(hfRow.removeFromLeft(90));
    hfRow.removeFromLeft(5);
    hfBoostSlider.setBounds(hfRow);
    rightColumn.removeFromTop(2);

    // Micro Movement row
    auto microRow = rightColumn.removeFromTop(30);
    microMovementLabel.setBounds(microRow.removeFromLeft(90));
    microRow.removeFromLeft(5);
    microMovementSlider.setBounds(microRow);
    rightColumn.removeFromTop(2);

    // Window Type row
    auto windowRow = rightColumn.removeFromTop(30);
    windowTypeLabel.setBounds(windowRow.removeFromLeft(90));
    windowRow.removeFromLeft(5);
    windowTypeSlider.setBounds(windowRow);
    rightColumn.removeFromTop(2);

    // Crossfade Length row
    auto xfadeRow = rightColumn.removeFromTop(30);
    crossfadeLengthLabel.setBounds(xfadeRow.removeFromLeft(90));
    xfadeRow.removeFromLeft(5);
    crossfadeLengthSlider.setBounds(xfadeRow);

    //==========================================================================
    // Status Bar Layout (Bottom)
    //==========================================================================

    auto statusArea = bounds.removeFromBottom(40);
    auto recommendedArea = statusArea.removeFromRight(280);
    statusLabel.setBounds(statusArea);
    recommendedLabel.setBounds(recommendedArea);

    //==========================================================================
    // Waveform Display Layout (Center)
    //==========================================================================

    bounds.reduce(10, 10);
    waveformDisplay.setBounds(bounds);
}

void GrainfreezeAudioProcessorEditor::timerCallback()
{
    // Update waveform display
    waveformDisplay.repaint();

    // Update freeze button visual state
    bool isFreezeMode = audioProcessor.freezeModeParam->get();
    freezeButton.setToggleState(isFreezeMode, juce::dontSendNotification);
    freezeButton.setColour(juce::TextButton::buttonColourId,
        isFreezeMode ? juce::Colours::orange : juce::Colours::grey);

    // Update status text
    if (audioProcessor.isAudioLoaded())
    {
        float stretch = audioProcessor.timeStretch->get();
        juce::String status = "Audio loaded - ";

        if (isFreezeMode)
        {
            status += "FREEZE MODE";
        }
        else if (audioProcessor.isPlaying())
        {
            status += "PLAYING at ";
            if (stretch < 1.0f)
                status += juce::String(1.0f / stretch, 2) + "x speed";
            else if (stretch > 1.0f)
                status += juce::String(stretch, 2) + "x slower";
            else
                status += "normal speed";
        }
        else
        {
            status += "STOPPED";
        }

        statusLabel.setText(status, juce::dontSendNotification);

        // Update play button color
        playButton.setColour(juce::TextButton::buttonColourId,
            audioProcessor.isPlaying() ? juce::Colours::green : juce::Colours::grey);
    }
    else
    {
        statusLabel.setText("No audio loaded", juce::dontSendNotification);
    }
}

void GrainfreezeAudioProcessorEditor::loadAudioFile()
{
    // Create file chooser for audio files
    fileChooser = std::make_unique<juce::FileChooser>("Select an audio file to load...",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.mp3;*.aif;*.aiff;*.flac");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    // Open file chooser asynchronously
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                // Load selected file and stop playback
                audioProcessor.loadAudioFile(file);
                audioProcessor.setPlaying(false);
                waveformDisplay.repaint();
            }
        });
}