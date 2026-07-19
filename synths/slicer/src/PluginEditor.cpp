#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a6633));

    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawRect(getLocalBounds(), 2);

    if (!processor.hasSample())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(14.0f);
        g.drawText("Drag and drop an audio file here or click 'Load Sample'",
            getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Draw waveform with HIGHER RESOLUTION
    auto& buffer = processor.getSampleBuffer();
    auto bounds = getLocalBounds().reduced(4);

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();

    if (numSamples > 0)
    {
        juce::Path waveformPath;

        int displayWidth = bounds.getWidth();

        // IMPROVED: Sample every pixel for maximum detail
        // Previously was skipping samples, now we look at ALL samples in each pixel range

        // Build waveform outline (top half)
        for (int x = 0; x < displayWidth; ++x)
        {
            int startSample = static_cast<int>((static_cast<int64_t>(x) * numSamples) / displayWidth);
            int endSample = static_cast<int>((static_cast<int64_t>(x + 1) * numSamples) / displayWidth);

            float maxVal = 0.0f;

            // Look at EVERY sample in this pixel range (not skipping!)
            for (int s = startSample; s < endSample; ++s)
            {
                if (s >= numSamples) break;

                float sample = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sample += buffer.getReadPointer(ch)[s];
                sample /= (float)numChannels;

                maxVal = juce::jmax(maxVal, sample);
            }

            float centerY = bounds.getCentreY();
            float maxY = centerY - maxVal * bounds.getHeight() * 0.45f;

            if (x == 0)
                waveformPath.startNewSubPath(bounds.getX() + x, maxY);
            else
                waveformPath.lineTo(bounds.getX() + x, maxY);
        }

        // Bottom half
        for (int x = displayWidth - 1; x >= 0; --x)
        {
            int startSample = static_cast<int>((static_cast<int64_t>(x) * numSamples) / displayWidth);
            int endSample = static_cast<int>((static_cast<int64_t>(x + 1) * numSamples) / displayWidth);

            float minVal = 0.0f;

            // Look at EVERY sample in this pixel range
            for (int s = startSample; s < endSample; ++s)
            {
                if (s >= numSamples) break;

                float sample = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sample += buffer.getReadPointer(ch)[s];
                sample /= (float)numChannels;

                minVal = juce::jmin(minVal, sample);
            }

            float centerY = bounds.getCentreY();
            float minY = centerY - minVal * bounds.getHeight() * 0.45f;

            waveformPath.lineTo(bounds.getX() + x, minY);
        }

        waveformPath.closeSubPath();

        // Cyan waveform with enhanced visibility
        g.setColour(juce::Colours::cyan.withAlpha(0.6f));
        g.fillPath(waveformPath);

        g.setColour(juce::Colours::cyan.withAlpha(0.9f));
        g.strokePath(waveformPath, juce::PathStrokeType(1.0f));

        // Draw region selection
        int regionStart = processor.getRegionStart();
        int regionEnd = processor.getRegionEnd();

        int regionStartX = bounds.getX() + (regionStart * bounds.getWidth()) / numSamples;
        int regionEndX = bounds.getX() + (regionEnd * bounds.getWidth()) / numSamples;

        // Darken area outside region
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        if (regionStartX > bounds.getX())
            g.fillRect(bounds.getX(), bounds.getY(), regionStartX - bounds.getX(), bounds.getHeight());
        if (regionEndX < bounds.getRight())
            g.fillRect(regionEndX, bounds.getY(), bounds.getRight() - regionEndX, bounds.getHeight());

        // Draw region boundaries
        g.setColour(juce::Colours::yellow.withAlpha(0.8f));
        g.drawLine(regionStartX, bounds.getY(), regionStartX, bounds.getBottom(), 2.0f);
        g.drawLine(regionEndX, bounds.getY(), regionEndX, bounds.getBottom(), 2.0f);

        // Draw slice markers
        const auto& slicePoints = processor.getSlicePoints();

        for (size_t i = 0; i < slicePoints.size(); ++i)
        {
            const auto& slice = slicePoints[i];

            if (!slice.active)
                continue;

            float normalizedPos = (float)slice.samplePosition / numSamples;
            int sliceX = bounds.getX() + (int)(normalizedPos * bounds.getWidth());

            // Check if this slice is currently playing
            bool isPlaying = processor.isSlicePlaying((int)i);

            // Different colors: lime for first, cyan for playing, orange for others
            if (isPlaying)
                g.setColour(juce::Colours::cyan.brighter());
            else if (i == 0)
                g.setColour(juce::Colours::lime);
            else
                g.setColour(juce::Colours::orange);

            // Draw slice line (thicker if playing)
            float lineWidth = isPlaying ? 3.0f : 2.0f;
            g.drawLine(sliceX, bounds.getY(), sliceX, bounds.getBottom(), lineWidth);

            // Draw glow effect if playing
            if (isPlaying)
            {
                g.setColour(juce::Colours::cyan.withAlpha(0.3f));
                for (int offset = 1; offset <= 4; ++offset)
                {
                    g.drawLine(sliceX - offset, bounds.getY(), sliceX - offset, bounds.getBottom(), 1.0f);
                    g.drawLine(sliceX + offset, bounds.getY(), sliceX + offset, bounds.getBottom(), 1.0f);
                }
                // Reset color for text
                if (isPlaying)
                    g.setColour(juce::Colours::cyan.brighter());
                else if (i == 0)
                    g.setColour(juce::Colours::lime);
                else
                    g.setColour(juce::Colours::orange);
            }

            // Draw slice number
            g.setFont(10.0f);
            juce::String sliceLabel = juce::String(i + 1);

            // Background for better readability when playing
            if (isPlaying)
            {
                g.setColour(juce::Colours::cyan.withAlpha(0.5f));
                g.fillRect(sliceX - 12, bounds.getY() + 3, 24, 17);
                g.setColour(juce::Colours::white);
            }

            g.drawText(sliceLabel, sliceX - 10, bounds.getY() + 5, 20, 15,
                juce::Justification::centred);

            // Draw MIDI note assignment - FIXED FORMULA!
            int midiNote = 36 + i; // C2 = 36
            if (midiNote <= 108) // C9
            {
                static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                                   "F#", "G", "G#", "A", "A#", "B" };
                // FIXED: Changed from (midiNote / 12) - 2 to (midiNote / 12) - 1
                // MIDI note 36: 36/12 = 3, 3-1 = 2, so C2 ✓
                // MIDI note 48: 48/12 = 4, 4-1 = 3, so C3 ✓
                int octave = (midiNote / 12) - 1;
                int noteIndex = midiNote % 12;

                juce::String noteLabel = juce::String(noteNames[noteIndex]) + juce::String(octave);
                g.setFont(9.0f);
                g.drawText(noteLabel, sliceX - 15, bounds.getBottom() - 20, 30, 15,
                    juce::Justification::centred);
            }
        }
    }
}

bool WaveformDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& file : files)
    {
        if (file.endsWith(".wav") || file.endsWith(".mp3") ||
            file.endsWith(".aif") || file.endsWith(".aiff") ||
            file.endsWith(".flac") || file.endsWith(".ogg"))
            return true;
    }
    return false;
}

void WaveformDisplay::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.size() > 0)
    {
        juce::File file(files[0]);
        processor.loadSample(file);
        repaint();
    }
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    if (!processor.hasSample())
        return;

    auto& buffer = processor.getSampleBuffer();
    int numSamples = buffer.getNumSamples();
    auto bounds = getLocalBounds().reduced(4);

    // Convert mouse X to sample position
    float normalizedPos = (float)(event.x - bounds.getX()) / bounds.getWidth();
    int samplePos = (int)(normalizedPos * numSamples);
    samplePos = juce::jlimit(0, numSamples - 1, samplePos);

    // Check if clicking near region boundaries
    int regionStart = processor.getRegionStart();
    int regionEnd = processor.getRegionEnd();

    int regionStartX = bounds.getX() + static_cast<int>((static_cast<int64_t>(regionStart) * bounds.getWidth()) / numSamples);
    int regionEndX = bounds.getX() + static_cast<int>((static_cast<int64_t>(regionEnd) * bounds.getWidth()) / numSamples);

    const int clickTolerance = 10;

    if (std::abs(event.x - regionStartX) < clickTolerance)
    {
        isDraggingRegionStart = true;
        return;
    }
    if (std::abs(event.x - regionEndX) < clickTolerance)
    {
        isDraggingRegionEnd = true;
        return;
    }

    // Check if clicking near existing slice to drag it or delete it
    const auto& slicePoints = processor.getSlicePoints();
    for (size_t i = 0; i < slicePoints.size(); ++i)
    {
        float sliceNormPos = (float)slicePoints[i].samplePosition / numSamples;
        int sliceX = bounds.getX() + (int)(sliceNormPos * bounds.getWidth());

        if (std::abs(event.x - sliceX) < clickTolerance)
        {
            // RIGHT-CLICK: Delete slice
            if (event.mods.isPopupMenu()) // Right-click or Ctrl+click on Mac
            {
                processor.removeSlice((int)i);
                repaint();
                return;
            }
            // LEFT-CLICK: Drag slice
            else
            {
                draggedSliceIndex = (int)i;
                return;
            }
        }
    }

    // ---------------------------------------------------------------
    // If NOT in add-slice mode:
    //   single-click → play the slice under the cursor
    //                  (if loop is on AND that slice is already playing → stop it)
    //   double-click → toggle random stream on/off
    //
    // When the random stream is already active the first click of a
    // double-click sequence is swallowed so it doesn't interrupt playback;
    // only the confirmed double-click fires the stop/start toggle.
    // ---------------------------------------------------------------
    if (!processor.isAddSliceModeActive())
    {
        if (event.getNumberOfClicks() >= 2)
        {
            // Double-click toggles random stream
            if (processor.isRandomStreamActive())
                processor.stopRandomStream();
            else
                processor.startRandomStream();
            return;
        }

        // Single-click — swallow if the random stream is running so the
        // first tap of an intended double-click doesn't cut the stream.
        if (processor.isRandomStreamActive())
            return;

        // Find which slice region contains this sample position and
        // trigger or stop it.
        bool loopMode = processor.getValueTreeState()
            .getRawParameterValue("loopMode")->load() > 0.5f;

        const auto& slicePoints = processor.getSlicePoints();
        for (int i = (int)slicePoints.size() - 1; i >= 0; --i)
        {
            if (samplePos >= slicePoints[i].samplePosition)
            {
                // If loop is on and this slice is already playing, stop it
                if (loopMode && processor.isSlicePlaying(i))
                    processor.stopSlice(i);
                else
                    processor.triggerSlice(i, 1.0f);
                return;
            }
        }
        return;
    }

    // ---------------------------------------------------------------
    // Add-slice mode (original behaviour below)
    // ---------------------------------------------------------------

    // If double-click and not near any slice, add a new slice
    if (event.getNumberOfClicks() >= 2)
    {
        processor.addSliceAtPosition(samplePos);
        repaint();
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    if (!processor.hasSample())
        return;

    auto& buffer = processor.getSampleBuffer();
    int numSamples = buffer.getNumSamples();
    auto bounds = getLocalBounds().reduced(4);

    float normalizedPos = (float)(event.x - bounds.getX()) / bounds.getWidth();
    int samplePos = (int)(normalizedPos * numSamples);
    samplePos = juce::jlimit(0, numSamples - 1, samplePos);

    if (isDraggingRegionStart)
    {
        float newNormalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        auto* param = processor.getValueTreeState().getParameter("regionStart");
        if (param)
            param->setValueNotifyingHost(newNormalizedPos);
        repaint();
    }
    else if (isDraggingRegionEnd)
    {
        float newNormalizedPos = juce::jlimit(0.0f, 1.0f, normalizedPos);
        auto* param = processor.getValueTreeState().getParameter("regionEnd");
        if (param)
            param->setValueNotifyingHost(newNormalizedPos);
        repaint();
    }
    else if (draggedSliceIndex >= 0)
    {
        processor.setSlicePosition(draggedSliceIndex, samplePos);
        repaint();
    }
}

void WaveformDisplay::mouseUp(const juce::MouseEvent& event)
{
    draggedSliceIndex = -1;
    isDraggingRegionStart = false;
    isDraggingRegionEnd = false;
}

//==============================================================================
// SlicerAudioProcessorEditor Implementation
//==============================================================================

SlicerAudioProcessorEditor::SlicerAudioProcessorEditor(SlicerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), waveformDisplay(p)
{
    setSize(800, 450);

    // Waveform display
    addAndMakeVisible(waveformDisplay);

    // Load button
    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Sample");
    loadButton.onClick = [this]()
        {
            chooser = std::make_unique<juce::FileChooser>(
                "Select an audio file to load...",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");

            auto flags = juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles;

            chooser->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    auto results = fc.getResults();
                    if (results.size() > 0)
                    {
                        audioProcessor.loadSample(results[0]);
                        updateInfoLabel();
                    }
                });
        };

    // Analyze button
    addAndMakeVisible(analyzeButton);
    analyzeButton.setButtonText("Re-Analyze");
    analyzeButton.onClick = [this]()
        {
            audioProcessor.analyzeAndSlice();
            updateInfoLabel();
        };

    // Export button
    addAndMakeVisible(exportButton);
    exportButton.setButtonText("Export Slices");

    // Region sliders with custom look and feel
    addAndMakeVisible(regionStartSlider);
    regionStartSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    regionStartSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    regionStartSlider.setLookAndFeel(&customKnobLookAndFeel);
    regionStartAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "regionStart", regionStartSlider);

    addAndMakeVisible(regionEndSlider);
    regionEndSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    regionEndSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    regionEndSlider.setLookAndFeel(&customKnobLookAndFeel);
    regionEndAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "regionEnd", regionEndSlider);

    addAndMakeVisible(sliceStrengthSlider);
    sliceStrengthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sliceStrengthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    sliceStrengthSlider.setLookAndFeel(&customKnobLookAndFeel);
    sliceStrengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "sliceStrength", sliceStrengthSlider);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    volumeSlider.setLookAndFeel(&customKnobLookAndFeel);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "volume", volumeSlider);

    addAndMakeVisible(fadeoutMsSlider);
    fadeoutMsSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    fadeoutMsSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    fadeoutMsSlider.setLookAndFeel(&customKnobLookAndFeel);
    fadeoutMsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "fadeoutMs", fadeoutMsSlider);

    // Playback speed slider (horizontal, simple style)
    addAndMakeVisible(playbackSpeedSlider);
    playbackSpeedSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    playbackSpeedSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    playbackSpeedSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff66bb77));
    playbackSpeedSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff88dd99));
    playbackSpeedSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a2520));
    playbackSpeedSlider.setTextValueSuffix("x");
    playbackSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getValueTreeState(), "playbackSpeed", playbackSpeedSlider);

    // Labels
    addAndMakeVisible(regionStartLabel);
    regionStartLabel.setText("Region Start", juce::dontSendNotification);
    regionStartLabel.setJustificationType(juce::Justification::centred);
    regionStartLabel.attachToComponent(&regionStartSlider, false);

    addAndMakeVisible(regionEndLabel);
    regionEndLabel.setText("Region End", juce::dontSendNotification);
    regionEndLabel.setJustificationType(juce::Justification::centred);
    regionEndLabel.attachToComponent(&regionEndSlider, false);

    addAndMakeVisible(sliceStrengthLabel);
    sliceStrengthLabel.setText("Slice Strength", juce::dontSendNotification);
    sliceStrengthLabel.setJustificationType(juce::Justification::centred);
    sliceStrengthLabel.attachToComponent(&sliceStrengthSlider, false);

    addAndMakeVisible(volumeLabel);
    volumeLabel.setText("Volume", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centred);
    volumeLabel.attachToComponent(&volumeSlider, false);

    addAndMakeVisible(fadeoutMsLabel);
    fadeoutMsLabel.setText("Fadeout (ms)", juce::dontSendNotification);
    fadeoutMsLabel.setJustificationType(juce::Justification::centred);
    fadeoutMsLabel.attachToComponent(&fadeoutMsSlider, false);

    addAndMakeVisible(playbackSpeedLabel);
    playbackSpeedLabel.setText("Speed", juce::dontSendNotification);
    playbackSpeedLabel.setJustificationType(juce::Justification::centredLeft);
    playbackSpeedLabel.setFont(juce::Font(12.0f));

    // Slice End Mode combo box
    addAndMakeVisible(sliceEndModeCombo);
    sliceEndModeCombo.addItem("Full Length", 1);
    sliceEndModeCombo.addItem("Trim at -40dB", 2);
    sliceEndModeCombo.addItem("Smart Trim (Quietest)", 3);
    sliceEndModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "sliceEndMode", sliceEndModeCombo);
    sliceEndModeCombo.onChange = [this]()
        {
            // Re-apply slice end mode when changed
            audioProcessor.applySliceEndMode();
        };

    // Reverse mode toggle
    addAndMakeVisible(reverseModeButton);
    reverseModeButton.setButtonText("Rev");
    reverseModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "reverseMode", reverseModeButton);

    // Interaction mode combo — persisted via APVTS so it survives DAW session reload.
    // The ComboBoxAttachment keeps the combo and parameter in sync automatically;
    // isAddSliceModeActive() on the processor reads the parameter directly.
    addAndMakeVisible(interactionModeCombo);
    interactionModeCombo.addItem("Add Slice on Mouse Click", 1);
    interactionModeCombo.addItem("Play Slice on Mouse Click", 2);
    interactionModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getValueTreeState(), "interactionMode", interactionModeCombo);

    // Loop mode toggle (persisted parameter)
    addAndMakeVisible(loopModeButton);
    loopModeButton.setButtonText("Loop");
    loopModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "loopMode", loopModeButton);

    // Mono mode toggle (persisted parameter)
    // Stops all playing voices before triggering a new slice — essential with loop mode
    addAndMakeVisible(monoModeButton);
    monoModeButton.setButtonText("Mono");
    monoModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getValueTreeState(), "monoMode", monoModeButton);

    exportButton.onClick = [this]()
        {
            if (!audioProcessor.hasSample())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "No Sample Loaded",
                    "Please load a sample before exporting slices.",
                    "OK");
                return;
            }

            if (audioProcessor.getSlicePoints().empty())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "No Slices Detected",
                    "Please analyze the sample to detect slices before exporting.",
                    "OK");
                return;
            }

            exportChooser = std::make_unique<juce::FileChooser>(
                "Choose output folder for slices...",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                "");

            auto flags = juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectDirectories;

            exportChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    auto results = fc.getResults();
                    if (results.size() > 0)
                    {
                        auto outputFolder = results[0];

                        if (audioProcessor.exportSlicesToDisc(outputFolder))
                        {
                            int numSlices = (int)audioProcessor.getSlicePoints().size();
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::InfoIcon,
                                "Export Successful",
                                juce::String("Successfully exported ") + juce::String(numSlices) +
                                " slices to:\n" + outputFolder.getFullPathName(),
                                "OK");
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Export Failed",
                                "Failed to export slices. Please check the output folder permissions.",
                                "OK");
                        }
                    }
                });
        };

    // Set up slice strength slider to trigger re-analysis on mouse up
    sliceStrengthSlider.onValueChange = nullptr; // Don't re-analyze during drag
    sliceStrengthSlider.onDragEnd = [this]()
        {
            shouldReanalyze = true;
        };

    // Info label
    addAndMakeVisible(infoLabel);
    infoLabel.setText("No sample loaded", juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    infoLabel.setFont(juce::Font(12.0f));

    updateInfoLabel();
    startTimerHz(30);
}

SlicerAudioProcessorEditor::~SlicerAudioProcessorEditor()
{
    stopTimer();

    // Clean up custom look and feel
    regionStartSlider.setLookAndFeel(nullptr);
    regionEndSlider.setLookAndFeel(nullptr);
    sliceStrengthSlider.setLookAndFeel(nullptr);
    volumeSlider.setLookAndFeel(nullptr);
    fadeoutMsSlider.setLookAndFeel(nullptr);
}

void SlicerAudioProcessorEditor::timerCallback()
{
    waveformDisplay.repaint();
    repaint(); // Needed so the instructions text updates when interaction mode changes

    // Re-analyze when slice strength changes
    if (shouldReanalyze)
    {
        shouldReanalyze = false;
        audioProcessor.analyzeAndSlice();
        updateInfoLabel();
    }
}

void SlicerAudioProcessorEditor::updateInfoLabel()
{
    if (!audioProcessor.hasSample())
    {
        infoLabel.setText("No sample loaded", juce::dontSendNotification);
        return;
    }

    const auto& slicePoints = audioProcessor.getSlicePoints();
    int numSlices = (int)slicePoints.size();

    juce::String info = juce::String(numSlices) + " slices detected";

    if (numSlices > 0)
    {
        int firstNote = 36; // C2
        int lastNote = 36 + numSlices - 1;

        if (lastNote <= 108) // C9
        {
            info += " (C2-";

            static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B" };
            // FIXED: Changed from (lastNote / 12) - 2 to (lastNote / 12) - 1
            int octave = (lastNote / 12) - 1;
            int noteIndex = lastNote % 12;

            info += juce::String(noteNames[noteIndex]) + juce::String(octave) + ")";
        }
        else
        {
            info += " (exceeds C9 - some slices not mapped)";
        }
    }

    infoLabel.setText(info, juce::dontSendNotification);
}

void SlicerAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient background(
        juce::Colour(0xff2aaa3e),
        r.getTopLeft(),
        juce::Colour(0xff00bbbb),
        r.getBottomRight(),
        false
    );

    g.setGradientFill(background);
    g.fillAll();

    // Gloss overlay
    auto gloss = r.withHeight(r.getHeight() * 0.3f);

    juce::ColourGradient glossGradient(
        juce::Colours::white.withAlpha(0.15f),
        gloss.getTopLeft(),
        juce::Colours::white.withAlpha(0.02f),
        gloss.getBottomLeft(),
        false
    );

    g.setGradientFill(glossGradient);
    g.fillRect(gloss);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawText("Slicer Drum Slicing Plugin", 15, 10, 400, 30, juce::Justification::left);

    // Instructions — text changes depending on the active interaction mode
    g.setFont(11.0f);
    g.setColour(juce::Colours::lightgrey);
    juce::String instructions;
    if (audioProcessor.isAddSliceModeActive())
        instructions = "Click twice to add slice | Drag to move | Right-click to delete | Drag yellow bars for region | MIDI B1 = Random Stream, C2+ = Slices";
    else
        instructions = "Click to play slice | Click twice to toggle Random Stream | Drag to move | Right-click to delete | Drag yellow bars for region | MIDI B1 = Random, C2+ = Slices";
    g.drawText(instructions, 15, 40, 770, 20, juce::Justification::left);

    // Border
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f, 1.0f);
}

void SlicerAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Top section - waveform
    auto topSection = bounds.removeFromTop(280);
    topSection.removeFromTop(60);
    waveformDisplay.setBounds(topSection.reduced(10));

    // Buttons (restored to original layout)
    loadButton.setBounds(240, 13, 100, 28);
    analyzeButton.setBounds(350, 13, 100, 28);

    // Info label (restored to original position)
    infoLabel.setBounds(460, 13, 150, 28);

    // Speed slider and label in top right
    playbackSpeedLabel.setBounds(620, 13, 45, 28);
    playbackSpeedSlider.setBounds(670, 13, 115, 28);

    // Controls section
    const int controlsTop = 290;
    const int knobSize = 100;
    const int spacingX = 120;
    const int labelGap = 15;

    int x = 15;
    int y = controlsTop;

    regionStartSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    regionEndSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    sliceStrengthSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    volumeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    fadeoutMsSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20);

    // Export controls section (right side)
    // 4 equal-height rows, all 160 px wide.
    const int exportSectionX = 620;
    const int rowH = 28;
    const int rowGap = 4;
    const int rowStep = rowH + rowGap;

    // Row 0 — Interaction mode combo (full width)
    int ry = 296;
    interactionModeCombo.setBounds(exportSectionX, ry, 160, rowH);

    // Row 1 — Reverse | Loop | Mono (three equal columns, 2px gaps)
    // 52 + 2 + 52 + 2 + 52 = 160
    ry += rowStep;
    reverseModeButton.setBounds(exportSectionX, ry, 52, rowH);
    loopModeButton.setBounds(exportSectionX + 54, ry, 52, rowH);
    monoModeButton.setBounds(exportSectionX + 108, ry, 52, rowH);

    // Row 2 — Trim method combo (full width)
    ry += rowStep;
    sliceEndModeCombo.setBounds(exportSectionX, ry, 160, rowH);

    // Row 3 — Export button (full width, slightly taller for prominence)
    ry += rowStep;
    exportButton.setBounds(exportSectionX, ry, 160, rowH + 6);
}