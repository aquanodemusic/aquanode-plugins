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
            int startSample = (x * numSamples) / displayWidth;
            int endSample = ((x + 1) * numSamples) / displayWidth;

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
            int startSample = (x * numSamples) / displayWidth;
            int endSample = ((x + 1) * numSamples) / displayWidth;

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
            int midiNote = 36 + i; // C1 = 36
            if (midiNote <= 108) // C9
            {
                static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                                   "F#", "G", "G#", "A", "A#", "B" };
                // FIXED: Changed from (midiNote / 12) - 1 to (midiNote / 12) - 2
                // MIDI note 36: 36/12 = 3, 3-2 = 1, so C1 ✓
                // MIDI note 48: 48/12 = 4, 4-2 = 2, so C2 ✓
                int octave = (midiNote / 12) - 2;
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

    auto bounds = getLocalBounds().reduced(4);
    int numSamples = processor.getSampleBuffer().getNumSamples();

    // Check if clicking near region boundaries
    int regionStart = processor.getRegionStart();
    int regionEnd = processor.getRegionEnd();

    int regionStartX = bounds.getX() + (regionStart * bounds.getWidth()) / numSamples;
    int regionEndX = bounds.getX() + (regionEnd * bounds.getWidth()) / numSamples;

    const int grabThreshold = 8;

    if (std::abs(event.x - regionStartX) < grabThreshold)
    {
        isDraggingRegionStart = true;
        return;
    }

    if (std::abs(event.x - regionEndX) < grabThreshold)
    {
        isDraggingRegionEnd = true;
        return;
    }

    // Check if clicking on a slice marker
    const auto& slicePoints = processor.getSlicePoints();

    for (size_t i = 0; i < slicePoints.size(); ++i)
    {
        float normalizedPos = (float)slicePoints[i].samplePosition / numSamples;
        int sliceX = bounds.getX() + (int)(normalizedPos * bounds.getWidth());

        if (std::abs(event.x - sliceX) < grabThreshold)
        {
            draggedSliceIndex = (int)i;
            return;
        }
    }

    // Double-click to add slice
    if (event.getNumberOfClicks() == 2)
    {
        float clickPos = (float)(event.x - bounds.getX()) / bounds.getWidth();
        clickPos = juce::jlimit(0.0f, 1.0f, clickPos);
        int samplePos = (int)(clickPos * numSamples);
        processor.addSliceAtPosition(samplePos);
        repaint();
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    if (!processor.hasSample())
        return;

    auto bounds = getLocalBounds().reduced(4);
    int numSamples = processor.getSampleBuffer().getNumSamples();

    float dragPos = (float)(event.x - bounds.getX()) / bounds.getWidth();
    dragPos = juce::jlimit(0.0f, 1.0f, dragPos);

    if (isDraggingRegionStart)
    {
        auto* param = processor.getValueTreeState().getParameter("regionStart");
        param->setValueNotifyingHost(dragPos);
        repaint();
    }
    else if (isDraggingRegionEnd)
    {
        auto* param = processor.getValueTreeState().getParameter("regionEnd");
        param->setValueNotifyingHost(dragPos);
        repaint();
    }
    else if (draggedSliceIndex >= 0)
    {
        int newSamplePos = (int)(dragPos * numSamples);
        processor.setSlicePosition(draggedSliceIndex, newSamplePos);
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
    setSize(800, 440);

    addAndMakeVisible(waveformDisplay);

    // Load button
    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Sample");
    loadButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser>("Select an audio file...",
                juce::File{},
                "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");

            auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

            chooser->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    if (fc.getURLResults().size() > 0)
                    {
                        auto result = fc.getURLResults()[0];
                        if (result.isLocalFile())
                        {
                            audioProcessor.loadSample(result.getLocalFile());
                            waveformDisplay.repaint();
                            updateInfoLabel();
                        }
                    }
                });
        };

    // Analyze button
    addAndMakeVisible(analyzeButton);
    analyzeButton.setButtonText("Re-Analyze");
    analyzeButton.onClick = [this]
        {
            audioProcessor.analyzeAndSlice();
            waveformDisplay.repaint();
            updateInfoLabel();
        };

    // Setup sliders
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label,
        const juce::String& labelText, const juce::String& paramID,
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
        {
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);

            addAndMakeVisible(label);
            label.setText(labelText, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.attachToComponent(&slider, false);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                audioProcessor.getValueTreeState(), paramID, slider);
        };

    setupSlider(regionStartSlider, regionStartLabel, "Region Start", "regionStart", regionStartAttachment);
    setupSlider(regionEndSlider, regionEndLabel, "Region End", "regionEnd", regionEndAttachment);
    setupSlider(sliceStrengthSlider, sliceStrengthLabel, "Slice Strength", "sliceStrength", sliceStrengthAttachment);
    setupSlider(volumeSlider, volumeLabel, "Volume", "volume", volumeAttachment);

    // Export button (bottom right section)
    addAndMakeVisible(exportButton);
    exportButton.setButtonText("Export Slices to 24 bit 48 kHz linear FLAC");
    exportButton.onClick = [this]
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

    // Dump MIDI button (above export button)
    addAndMakeVisible(dumpMidiButton);
    dumpMidiButton.setButtonText("Dump to MIDI");
    dumpMidiButton.onClick = [this]
        {
            if (!audioProcessor.hasSample())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "No Sample Loaded",
                    "Please load a sample before generating MIDI.",
                    "OK");
                return;
            }

            if (audioProcessor.getSlicePoints().empty())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "No Slices Detected",
                    "Please analyze the sample to detect slices before generating MIDI.",
                    "OK");
                return;
            }

            auto chooser = std::make_unique<juce::FileChooser>(
                "Save MIDI file...",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                "*.mid");

            auto flags = juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles;

            chooser->launchAsync(flags, [this, chooser_ptr = chooser.get()](const juce::FileChooser& fc)
                {
                    auto results = fc.getResults();
                    if (results.size() > 0)
                    {
                        auto outputFile = results[0];

                        // Ensure .mid extension
                        if (!outputFile.hasFileExtension(".mid"))
                            outputFile = outputFile.withFileExtension(".mid");

                        if (audioProcessor.generateMidiFile(outputFile))
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::InfoIcon,
                                "MIDI Export Successful",
                                "MIDI file created with " + juce::String(audioProcessor.getSlicePoints().size()) +
                                " notes.\nYou can drag this into your DAW's piano roll!\n\nFile: " +
                                outputFile.getFullPathName(),
                                "OK");
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "MIDI Export Failed",
                                "Failed to generate MIDI file.",
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
}

void SlicerAudioProcessorEditor::timerCallback()
{
    waveformDisplay.repaint();

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
        int firstNote = 36; // C1
        int lastNote = 36 + numSlices - 1;

        if (lastNote <= 108) // C9
        {
            info += " (C1-";

            static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B" };
            // FIXED: Changed from (lastNote / 12) - 1 to (lastNote / 12) - 2
            int octave = (lastNote / 12) - 2;
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
        juce::Colour(0xff11ffaa),
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

    // Instructions
    g.setFont(11.0f);
    g.setColour(juce::Colours::lightgrey);
    g.drawText("Click waveform twice to add slice | Drag slices to move | Drag yellow bars for region",
        15, 35, 770, 20, juce::Justification::left);

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
    loadButton.setBounds(420, 13, 100, 28);
    analyzeButton.setBounds(530, 13, 100, 28);

    // Info label (restored to original position)
    infoLabel.setBounds(640, 13, 150, 28);

    // Controls section
    const int controlsTop = 290;
    const int knobSize = 100;
    const int spacingX = 120;
    const int labelGap = 15;

    int x = 120;
    int y = controlsTop;

    regionStartSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    regionEndSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    sliceStrengthSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX + 50;
    volumeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20);

    // MIDI dump button - 10px above export button
    dumpMidiButton.setBounds(650, 345, 130, 35);
    
    // Export button - bottom right corner
    exportButton.setBounds(650, 390, 130, 35);
}