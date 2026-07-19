#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    // Setup plugin formats
    pluginFormatManager.addFormat(new juce::VST3PluginFormat());
#if JUCE_MAC
    pluginFormatManager.addFormat(new juce::AudioUnitPluginFormat());
#endif

    // Setup track buttons
    juce::Colour trackColors[5] = { juce::Colours::red, juce::Colours::green, juce::Colours::blue,
                                     juce::Colours::yellow, juce::Colours::cyan };
    for (int i = 0; i < 5; ++i)
    {
        trackButtons[i].setButtonText("Track " + juce::String(i + 1));
        trackButtons[i].setColour(juce::TextButton::buttonColourId, trackColors[i].darker(0.5f));
        trackButtons[i].onClick = [this, i] { setCurrentTrack(i); };
        addAndMakeVisible(trackButtons[i]);

        // Instrument load buttons
        loadInstrumentButtons[i].setButtonText("Load Synth");
        loadInstrumentButtons[i].onClick = [this, i] {
            currentTrack = i;
            loadVSTInstrument();
            };
        addAndMakeVisible(loadInstrumentButtons[i]);

        // Instrument GUI open buttons
        openInstrumentGUIButtons[i].setButtonText("Synth GUI");
        openInstrumentGUIButtons[i].onClick = [this, i] {
            openInstrumentGUI(i);
            };
        openInstrumentGUIButtons[i].setEnabled(false); // Disabled until plugin loaded
        addAndMakeVisible(openInstrumentGUIButtons[i]);

        // MIDI import button
        importMIDIButtons[i].setButtonText("Import MIDI");
        importMIDIButtons[i].onClick = [this, i] {
            importMIDIToTrack(i);
            };
        addAndMakeVisible(importMIDIButtons[i]);

        // MIDI export button
        exportMIDIButtons[i].setButtonText("Export MIDI");
        exportMIDIButtons[i].onClick = [this, i] {
            exportMIDIFromTrack(i);
            };
        addAndMakeVisible(exportMIDIButtons[i]);

        // Effect load buttons
        for (int fx = 0; fx < 3; ++fx)
        {
            loadEffectButtons[i][fx].setButtonText("Load FX" + juce::String(fx + 1));
            loadEffectButtons[i][fx].onClick = [this, i, fx] {
                currentTrack = i;
                loadVSTEffect(fx);
                };
            addAndMakeVisible(loadEffectButtons[i][fx]);

            // Effect GUI open buttons
            openEffectGUIButtons[i][fx].setButtonText("FX GUI");
            openEffectGUIButtons[i][fx].onClick = [this, i, fx] {
                openEffectGUI(i, fx);
                };
            openEffectGUIButtons[i][fx].setEnabled(false); // Disabled until plugin loaded
            addAndMakeVisible(openEffectGUIButtons[i][fx]);
        }
    }

    // Highlight first track
    trackButtons[0].setColour(juce::TextButton::buttonColourId, trackColors[0]);

    // Setup control buttons
    startStopButton.setButtonText("Start");
    startStopButton.onClick = [this] { startStop(); };
    addAndMakeVisible(startStopButton);

    showPluginsButton.setButtonText("Show Plugins");
    showPluginsButton.onClick = [this] { showLoadedPlugins(); };
    addAndMakeVisible(showPluginsButton);

    saveButton.setButtonText("Save Project");
    saveButton.onClick = [this] { saveProject(); };
    addAndMakeVisible(saveButton);

    loadButton.setButtonText("Load Project");
    loadButton.onClick = [this] { loadProject(); };
    addAndMakeVisible(loadButton);

    exportButton.setButtonText("Export FLAC");
    exportButton.onClick = [this] { exportToFLAC(); };
    addAndMakeVisible(exportButton);

    // Automation lane setup
    for (int i = 0; i < 5; ++i)
    {
        automationLaneButtons[i].setButtonText("Automation Lane " + juce::String(i + 1));
        automationLaneButtons[i].setClickingTogglesState(true);
        automationLaneButtons[i].onClick = [this, i] {
            pianoRoll.setSelectedAutomationLane(i);
            // Unselect other lanes
            for (int j = 0; j < 5; ++j)
                automationLaneButtons[j].setToggleState(j == i, juce::dontSendNotification);
            pianoRoll.repaint();
            };
        addAndMakeVisible(automationLaneButtons[i]);

        assignAutomationButtons[i].setButtonText("Assign Parameter");
        assignAutomationButtons[i].onClick = [this, i] {
            if (lastTouchedPlugin != nullptr && lastTouchedParameterIndex >= 0)
            {
                pianoRoll.getAutomationLane(i).assignToParameter(
                    lastTouchedPlugin, lastTouchedParameterIndex, lastTouchedParameterName);
                automationLaneLabels[i].setText(lastTouchedParameterName, juce::dontSendNotification);
                pianoRoll.repaint();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                    "Automation", "Touch a VST knob first, then click Assign!");
            }
            };
        addAndMakeVisible(assignAutomationButtons[i]);

        automationLaneLabels[i].setText("(Not Assigned)", juce::dontSendNotification);
        automationLaneLabels[i].setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(automationLaneLabels[i]);
    }

    // Select first automation lane by default
    automationLaneButtons[0].setToggleState(true, juce::dontSendNotification);
    pianoRoll.setSelectedAutomationLane(0);

    // Setup BPM slider
    bpmLabel.setText("BPM:", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmLabel);

    bpmSlider.setRange(60.0, 200.0, 1.0);
    bpmSlider.setValue(120.0);
    bpmSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    bpmSlider.onValueChange = [this] {
        audioProcessor.setBPM(bpmSlider.getValue());
        };
    addAndMakeVisible(bpmSlider);

    // Setup Sidechain slider (Track 3 -> Track 2)
    sidechainLabel.setText("Sidechain Track 3 to 2:", juce::dontSendNotification);
    sidechainLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(sidechainLabel);

    sidechainSlider.setRange(0.0, 1.0, 0.01);
    sidechainSlider.setValue(0.0);
    sidechainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sidechainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    sidechainSlider.onValueChange = [this] {
        audioProcessor.setSidechainAmount(static_cast<float>(sidechainSlider.getValue()));
        };
    addAndMakeVisible(sidechainSlider);

    // Setup Track 3 Volume slider
    track3VolumeLabel.setText("T3 Vol:", juce::dontSendNotification);
    track3VolumeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(track3VolumeLabel);

    track3VolumeSlider.setRange(0.0, 1.0, 0.01);
    track3VolumeSlider.setValue(1.0);
    track3VolumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    track3VolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    track3VolumeSlider.onValueChange = [this] {
        audioProcessor.setTrack3Volume(static_cast<float>(track3VolumeSlider.getValue()));
        };
    addAndMakeVisible(track3VolumeSlider);

    // Setup Sidechain slider (Track 5 -> Track 4)
    sidechain5Label.setText("SC 5 to 4:", juce::dontSendNotification);
    sidechain5Label.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(sidechain5Label);

    sidechain5Slider.setRange(0.0, 1.0, 0.01);
    sidechain5Slider.setValue(0.0);
    sidechain5Slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sidechain5Slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    sidechain5Slider.onValueChange = [this] {
        audioProcessor.setSidechain5Amount(static_cast<float>(sidechain5Slider.getValue()));
        };
    addAndMakeVisible(sidechain5Slider);

    // Setup Track 5 Volume slider
    track5VolumeLabel.setText("T5 Vol:", juce::dontSendNotification);
    track5VolumeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(track5VolumeLabel);

    track5VolumeSlider.setRange(0.0, 1.0, 0.01);
    track5VolumeSlider.setValue(1.0);
    track5VolumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    track5VolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    track5VolumeSlider.onValueChange = [this] {
        audioProcessor.setTrack5Volume(static_cast<float>(track5VolumeSlider.getValue()));
        };
    addAndMakeVisible(track5VolumeSlider);

    // Setup color customization controls
    backgroundColorLabel.setText("Background Color:", juce::dontSendNotification);
    backgroundColorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(backgroundColorLabel);

    backgroundColorEditor.setText("505050"); // Default darker grey
    backgroundColorEditor.setFont(juce::Font(14.0f, juce::Font::bold));
    backgroundColorEditor.setJustification(juce::Justification::centred);
    backgroundColorEditor.onReturnKey = [this] { updateBackgroundColor(); };
    backgroundColorEditor.onFocusLost = [this] { updateBackgroundColor(); };
    addAndMakeVisible(backgroundColorEditor);

    pianoRollColorLabel.setText("Piano Roll:", juce::dontSendNotification);
    pianoRollColorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pianoRollColorLabel);

    pianoRollColorEditor.setText("000000"); // Default black
    pianoRollColorEditor.setFont(juce::Font(14.0f, juce::Font::bold));
    pianoRollColorEditor.setJustification(juce::Justification::centred);
    pianoRollColorEditor.onReturnKey = [this] { updatePianoRollColor(); };
    pianoRollColorEditor.onFocusLost = [this] { updatePianoRollColor(); };
    addAndMakeVisible(pianoRollColorEditor);

    // Apply initial colors from text editors
    updateBackgroundColor();
    updatePianoRollColor();

    // Setup piano roll
    addAndMakeVisible(pianoRoll);
    audioProcessor.setPianoRoll(&pianoRoll);

    // Setup audio device
    juce::String error = audioDeviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Audio Device Error",
            error);
    }

    audioDeviceManager.addAudioCallback(&audioProcessor);

    // Enable keyboard focus and add as key listener
    setWantsKeyboardFocus(true);
    addKeyListener(this);

    setSize(1130, 700);
}

MainComponent::~MainComponent()
{
    // STEP 1: Close ALL desktop components except the main window
    // This handles AlertWindows, file choosers, and any other modal dialogs
    auto& desktop = juce::Desktop::getInstance();

    // Iterate backwards to avoid index issues when deleting
    for (int i = desktop.getNumComponents() - 1; i >= 0; --i)
    {
        auto* comp = desktop.getComponent(i);

        // Skip if this is the main window (we don't want to delete that here)
        if (auto* docWindow = dynamic_cast<juce::DocumentWindow*>(comp))
        {
            // Check if this is our main application window by checking class name
            if (juce::String(typeid(*docWindow).name()).contains("MainWindow"))
                continue;
        }

        // Close any other components (AlertWindows, FileChoosers, PluginWindows, etc.)
        if (auto* alertWindow = dynamic_cast<juce::AlertWindow*>(comp))
        {
            alertWindow->exitModalState(0);
            alertWindow->setVisible(false);
        }
        else if (comp != nullptr)
        {
            comp->setVisible(false);
            if (comp->isOnDesktop())
                comp->removeFromDesktop();
        }
    }

    // Clear any file choosers
    activeFileChoosers.clear();

    // STEP 2: Stop playback
    if (isPlaying)
    {
        isPlaying = false;
        audioProcessor.setPlaying(false);
        pianoRoll.setPlaying(false);
    }

    // STEP 3: Remove audio callback
    audioDeviceManager.removeAudioCallback(&audioProcessor);

    // STEP 4: Remove as key listener
    removeKeyListener(this);

    // STEP 5: Delete all plugin windows (these should already be gone from Step 1, but just in case)
    for (int track = 0; track < 5; ++track)
    {
        if (instrumentWindows[track] != nullptr)
        {
            PluginWindow* window = dynamic_cast<PluginWindow*>(instrumentWindows[track].getComponent());
            instrumentWindows[track] = nullptr;
            delete window;
        }

        for (int fx = 0; fx < 3; ++fx)
        {
            if (effectWindows[track][fx] != nullptr)
            {
                PluginWindow* window = dynamic_cast<PluginWindow*>(effectWindows[track][fx].getComponent());
                effectWindows[track][fx] = nullptr;
                delete window;
            }
        }
    }

    // Release all plugin resources
    for (int track = 0; track < 5; ++track)
    {
        if (vstInstruments[track] != nullptr)
        {
            vstInstruments[track]->releaseResources();
        }

        for (int fx = 0; fx < 3; ++fx)
        {
            if (vstEffects[track][fx] != nullptr)
            {
                vstEffects[track][fx]->releaseResources();
            }
        }
    }

    // Clear plugin references from processor
    for (int track = 0; track < 5; ++track)
    {
        audioProcessor.getTrack(track).setVSTInstrument(nullptr);
        for (int fx = 0; fx < 3; ++fx)
        {
            audioProcessor.getTrack(track).setVSTEffect(fx, nullptr);
        }
    }

    // Now safely destroy plugins
    for (int track = 0; track < 5; ++track)
    {
        vstInstruments[track].reset();
        for (int fx = 0; fx < 3; ++fx)
        {
            vstEffects[track][fx].reset();
        }
    }

    // Close audio device last
    audioDeviceManager.closeAudioDevice();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColor);

    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("SimpleDAW (5-Track DAW) by aquanode", getLocalBounds().removeFromTop(35), juce::Justification::centred, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(35); // Title area

    // ===== ROW 1: Main Control buttons + Color controls on the right =====
    auto controlRow1 = area.removeFromTop(40);
    controlRow1.reduce(10, 5);

    // Color controls on the right
    auto colorArea = controlRow1.removeFromRight(360);
    pianoRollColorEditor.setBounds(colorArea.removeFromRight(70));
    colorArea.removeFromRight(5);
    pianoRollColorLabel.setBounds(colorArea.removeFromRight(75));
    colorArea.removeFromRight(10);
    backgroundColorEditor.setBounds(colorArea.removeFromRight(70));
    colorArea.removeFromRight(5);
    backgroundColorLabel.setBounds(colorArea.removeFromRight(120));

    // Main control buttons on the left
    startStopButton.setBounds(controlRow1.removeFromLeft(90));
    controlRow1.removeFromLeft(8);
    showPluginsButton.setBounds(controlRow1.removeFromLeft(130));
    controlRow1.removeFromLeft(8);
    saveButton.setBounds(controlRow1.removeFromLeft(110));
    controlRow1.removeFromLeft(8);
    loadButton.setBounds(controlRow1.removeFromLeft(110));
    controlRow1.removeFromLeft(8);
    exportButton.setBounds(controlRow1.removeFromLeft(110));

    area.removeFromTop(5);

    // ===== ROW 2: All knobs + Automation controls in ONE ROW =====
    auto controlRow2 = area.removeFromTop(75);
    controlRow2.reduce(10, 5);

    // Knobs section (left side) - more compact layout
    int knobSize = 60;
    int labelHeight = 12;
    int knobWidth = 70; // Uniform width for all knob sections

    auto bpmSection = controlRow2.removeFromLeft(knobWidth);
    bpmLabel.setBounds(bpmSection.removeFromTop(labelHeight));
    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmSlider.setBounds(bpmSection.withTrimmedTop(0).withSizeKeepingCentre(knobSize, knobSize));
    controlRow2.removeFromLeft(3);

    auto scSection = controlRow2.removeFromLeft(knobWidth + 50);
    sidechainLabel.setBounds(scSection.removeFromTop(labelHeight));
    sidechainLabel.setJustificationType(juce::Justification::centred);
    sidechainSlider.setBounds(scSection.withTrimmedTop(0).withSizeKeepingCentre(knobSize, knobSize));
    controlRow2.removeFromLeft(3);

    auto t3Section = controlRow2.removeFromLeft(knobWidth);
    track3VolumeLabel.setBounds(t3Section.removeFromTop(labelHeight));
    track3VolumeLabel.setJustificationType(juce::Justification::centred);
    track3VolumeSlider.setBounds(t3Section.withTrimmedTop(0).withSizeKeepingCentre(knobSize, knobSize));
    controlRow2.removeFromLeft(3);

    auto sc5Section = controlRow2.removeFromLeft(knobWidth);
    sidechain5Label.setBounds(sc5Section.removeFromTop(labelHeight));
    sidechain5Label.setJustificationType(juce::Justification::centred);
    sidechain5Slider.setBounds(sc5Section.withTrimmedTop(0).withSizeKeepingCentre(knobSize, knobSize));
    controlRow2.removeFromLeft(3);

    auto t5Section = controlRow2.removeFromLeft(knobWidth);
    track5VolumeLabel.setBounds(t5Section.removeFromTop(labelHeight));
    track5VolumeLabel.setJustificationType(juce::Justification::centred);
    track5VolumeSlider.setBounds(t5Section.withTrimmedTop(0).withSizeKeepingCentre(knobSize, knobSize));
    controlRow2.removeFromLeft(15);

    // Automation lanes section (right side) - 5 columns, each with 3 rows
    for (int i = 0; i < 5; ++i)
    {
        auto laneColumn = controlRow2.removeFromLeft(135);

        // Row 1: Lane button
        automationLaneButtons[i].setBounds(laneColumn.removeFromTop(22).reduced(1));
        laneColumn.removeFromTop(2);

        // Row 2: Assign button
        assignAutomationButtons[i].setBounds(laneColumn.removeFromTop(22).reduced(1));
        laneColumn.removeFromTop(2);

        // Row 3: Label
        automationLaneLabels[i].setBounds(laneColumn.removeFromTop(22).reduced(1));

        controlRow2.removeFromLeft(3);
    }

    area.removeFromTop(8);

    // ===== TRACK CONTROLS: 5 rows (one horizontal line per track) =====
    for (int track = 0; track < 5; ++track)
    {
        auto trackRow = area.removeFromTop(45);
        trackRow.reduce(10, 5);

        // Track selector button
        trackButtons[track].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(8);

        // Load Synth
        loadInstrumentButtons[track].setBounds(trackRow.removeFromLeft(100));
        trackRow.removeFromLeft(6);

        // Synth GUI
        openInstrumentGUIButtons[track].setBounds(trackRow.removeFromLeft(100));
        trackRow.removeFromLeft(12);

        // FX1 + FX1 GUI
        loadEffectButtons[track][0].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(6);
        openEffectGUIButtons[track][0].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(8);

        // FX2 + FX2 GUI
        loadEffectButtons[track][1].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(6);
        openEffectGUIButtons[track][1].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(8);

        // FX3 + FX3 GUI
        loadEffectButtons[track][2].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(6);
        openEffectGUIButtons[track][2].setBounds(trackRow.removeFromLeft(90));
        trackRow.removeFromLeft(12);

        // MIDI Import button
        importMIDIButtons[track].setBounds(trackRow.removeFromLeft(100));
        trackRow.removeFromLeft(6);

        // MIDI Export button
        exportMIDIButtons[track].setBounds(trackRow.removeFromLeft(100));

        area.removeFromTop(2); // Space between tracks
    }

    area.removeFromTop(8);

    // ===== PIANO ROLL: Use all remaining space =====
    pianoRoll.setBounds(area.reduced(10));
}

void MainComponent::setCurrentTrack(int track)
{
    if (track < 0 || track >= 5) return;

    currentTrack = track;
    pianoRoll.setCurrentTrack(track);
    updateTrackButtons();
}

void MainComponent::updateTrackButtons()
{
    juce::Colour trackColors[5] = { juce::Colours::red, juce::Colours::green, juce::Colours::blue,
                                     juce::Colours::yellow, juce::Colours::cyan };

    for (int i = 0; i < 5; ++i)
    {
        if (i == currentTrack)
            trackButtons[i].setColour(juce::TextButton::buttonColourId, trackColors[i]);
        else
            trackButtons[i].setColour(juce::TextButton::buttonColourId, trackColors[i].darker(0.5f));
    }
}

void MainComponent::loadPluginFromDescription(const juce::PluginDescription& desc, bool isInstrument,
    int trackOrSlot, const juce::String& pluginStateBase64, bool openGUI)
{
    juce::String error;

    auto* currentDevice = audioDeviceManager.getCurrentAudioDevice();
    double sampleRate = currentDevice ? currentDevice->getCurrentSampleRate() : 44100.0;
    int bufferSize = currentDevice ? currentDevice->getCurrentBufferSizeSamples() : 512;

    auto instance = pluginFormatManager.createPluginInstance(desc, sampleRate, bufferSize, error);

    if (instance != nullptr)
    {
        if (isInstrument)
        {
            int track = trackOrSlot;

            // Clean up old instrument
            if (vstInstruments[track] != nullptr)
            {
                audioProcessor.getTrack(track).setVSTInstrument(nullptr);
                vstInstruments[track]->releaseResources();

                // Remove from parameter listener tracking
                parameterListenerManager.removePluginListener(vstInstruments[track].get());

                vstInstruments[track].reset();
            }

            // Close old window
            if (instrumentWindows[track] != nullptr)
            {
                auto* window = instrumentWindows[track].getComponent();
                if (window != nullptr)
                {
                    window->setVisible(false);
                    delete window;
                }
                instrumentWindows[track] = nullptr;
            }

            vstInstruments[track].reset(instance.release());
            vstInstruments[track]->prepareToPlay(sampleRate, bufferSize);

            // Restore plugin state if provided - MUST be AFTER prepareToPlay
            // Use a small delay to ensure the plugin is fully initialized
            if (pluginStateBase64.isNotEmpty())
            {
                juce::MemoryOutputStream memStream;
                if (juce::Base64::convertFromBase64(memStream, pluginStateBase64))
                {
                    juce::MemoryBlock stateData(memStream.getData(), memStream.getDataSize());

                    // Set state immediately
                    vstInstruments[track]->setStateInformation(stateData.getData(), (int)stateData.getSize());

                    // Force parameter update after a short delay to combat plugins that reset on initialization
                    juce::Timer::callAfterDelay(50, [this, track, stateData]() mutable
                        {
                            if (vstInstruments[track] != nullptr)
                            {
                                vstInstruments[track]->setStateInformation(stateData.getData(), (int)stateData.getSize());

                                // Force all parameters to notify their current values
                                for (auto* param : vstInstruments[track]->getParameters())
                                {
                                    param->sendValueChangedMessageToListeners(param->getValue());
                                }
                            }
                        });
                }
            }

            audioProcessor.getTrack(track).setVSTInstrument(vstInstruments[track].get());
            savedInstrumentDesc[track] = desc;
            loadedInstrumentNames[track] = desc.name;

            // Setup parameter listeners for automation
            setupParameterListeners(vstInstruments[track].get());

            loadInstrumentButtons[track].setButtonText(desc.name.substring(0, 12));

            // Enable GUI open button
            openInstrumentGUIButtons[track].setEnabled(true);

            // Open plugin GUI only if requested (not during project load)
            if (openGUI)
            {
                instrumentWindows[track] = new PluginWindow("Track " + juce::String(track + 1) + " - " + desc.name,
                    vstInstruments[track].get());
            }
        }
        else // Effect
        {
            int track = currentTrack;
            int effectSlot = trackOrSlot;

            // Clean up old effect
            if (vstEffects[track][effectSlot] != nullptr)
            {
                audioProcessor.getTrack(track).setVSTEffect(effectSlot, nullptr);
                vstEffects[track][effectSlot]->releaseResources();

                // Remove from parameter listener tracking
                parameterListenerManager.removePluginListener(vstEffects[track][effectSlot].get());

                vstEffects[track][effectSlot].reset();
            }

            // Close old window
            if (effectWindows[track][effectSlot] != nullptr)
            {
                auto* window = effectWindows[track][effectSlot].getComponent();
                if (window != nullptr)
                {
                    window->setVisible(false);
                    delete window;
                }
                effectWindows[track][effectSlot] = nullptr;
            }

            vstEffects[track][effectSlot].reset(instance.release());
            vstEffects[track][effectSlot]->prepareToPlay(sampleRate, bufferSize);

            // Restore plugin state if provided - MUST be AFTER prepareToPlay
            // Use a small delay to ensure the plugin is fully initialized
            if (pluginStateBase64.isNotEmpty())
            {
                juce::MemoryOutputStream memStream;
                if (juce::Base64::convertFromBase64(memStream, pluginStateBase64))
                {
                    juce::MemoryBlock stateData(memStream.getData(), memStream.getDataSize());

                    // Set state immediately
                    vstEffects[track][effectSlot]->setStateInformation(stateData.getData(), (int)stateData.getSize());

                    // Force parameter update after a short delay to combat plugins that reset on initialization
                    juce::Timer::callAfterDelay(50, [this, track, effectSlot, stateData]() mutable
                        {
                            if (vstEffects[track][effectSlot] != nullptr)
                            {
                                vstEffects[track][effectSlot]->setStateInformation(stateData.getData(), (int)stateData.getSize());

                                // Force all parameters to notify their current values
                                for (auto* param : vstEffects[track][effectSlot]->getParameters())
                                {
                                    param->sendValueChangedMessageToListeners(param->getValue());
                                }
                            }
                        });
                }
            }

            audioProcessor.getTrack(track).setVSTEffect(effectSlot, vstEffects[track][effectSlot].get());
            savedEffectDesc[track][effectSlot] = desc;
            loadedEffectNames[track][effectSlot] = desc.name;

            // Setup parameter listeners for automation
            setupParameterListeners(vstEffects[track][effectSlot].get());

            loadEffectButtons[track][effectSlot].setButtonText(desc.name.substring(0, 8));

            // Enable GUI open button
            openEffectGUIButtons[track][effectSlot].setEnabled(true);

            // Open plugin GUI only if requested (not during project load)
            if (openGUI)
            {
                effectWindows[track][effectSlot] = new PluginWindow(
                    "Track " + juce::String(track + 1) + " FX" + juce::String(effectSlot + 1) + " - " + desc.name,
                    vstEffects[track][effectSlot].get());
            }
        }
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error",
            "Failed to load plugin: " + error);
    }
}

void MainComponent::loadVSTInstrument()
{
    auto* chooser = new juce::FileChooser("Select a VST3 instrument...",
        juce::File(), "*.vst3");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
            {
                juce::OwnedArray<juce::PluginDescription> typesFound;
                juce::VST3PluginFormat format;

                format.findAllTypesForFile(typesFound, file.getFullPathName());

                if (typesFound.size() > 0)
                {
                    loadPluginFromDescription(*typesFound[0], true, currentTrack);
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Error",
                        "No VST3 plugins found in the selected file!");
                }
            }

            delete chooser;
        });
}

void MainComponent::loadVSTEffect(int effectSlot)
{
    auto* chooser = new juce::FileChooser("Select a VST3 effect...",
        juce::File(), "*.vst3");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser, effectSlot](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
            {
                juce::OwnedArray<juce::PluginDescription> typesFound;
                juce::VST3PluginFormat format;

                format.findAllTypesForFile(typesFound, file.getFullPathName());

                if (typesFound.size() > 0)
                {
                    loadPluginFromDescription(*typesFound[0], false, effectSlot);
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Error",
                        "No VST3 plugins found in the selected file!");
                }
            }

            delete chooser;
        });
}

void MainComponent::startStop()
{
    isPlaying = !isPlaying;
    audioProcessor.setPlaying(isPlaying);
    pianoRoll.setPlaying(isPlaying);
    startStopButton.setButtonText(isPlaying ? "Stop" : "Start");
}

void MainComponent::showLoadedPlugins()
{
    juce::String message = "Loaded Plugins:\n\n";

    for (int track = 0; track < 5; ++track)
    {
        message += "Track " + juce::String(track + 1) + ":\n";

        if (loadedInstrumentNames[track].isNotEmpty())
            message += "  Instrument: " + loadedInstrumentNames[track] + "\n";

        for (int fx = 0; fx < 3; ++fx)
        {
            if (loadedEffectNames[track][fx].isNotEmpty())
                message += "  FX" + juce::String(fx + 1) + ": " + loadedEffectNames[track][fx] + "\n";
        }

        message += "\n";
    }

    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
        "Loaded Plugins",
        message);
}

void MainComponent::saveProject()
{
    auto* chooser = new juce::FileChooser("Save Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
            {
                juce::ValueTree projectState("Project");

                // Save BPM
                projectState.setProperty("BPM", audioProcessor.getBPM(), nullptr);

                // Save sidechain amount and track 3 volume
                projectState.setProperty("SidechainAmount", audioProcessor.getSidechainAmount(), nullptr);
                projectState.setProperty("Track3Volume", audioProcessor.getTrack3Volume(), nullptr);

                // Save sidechain5 amount and track 5 volume
                projectState.setProperty("Sidechain5Amount", audioProcessor.getSidechain5Amount(), nullptr);
                projectState.setProperty("Track5Volume", audioProcessor.getTrack5Volume(), nullptr);

                // Save color settings
                projectState.setProperty("BackgroundColor", backgroundColor.toString(), nullptr);
                projectState.setProperty("PianoRollColor", pianoRollColor.toString(), nullptr);

                // Save all tracks
                for (int track = 0; track < 5; ++track)
                {
                    juce::ValueTree trackNode("Track");
                    trackNode.setProperty("index", track, nullptr);

                    // Save instrument
                    if (vstInstruments[track] != nullptr)
                    {
                        juce::ValueTree instrumentNode("Instrument");
                        instrumentNode.setProperty("name", savedInstrumentDesc[track].name, nullptr);
                        instrumentNode.setProperty("pluginFormatName", savedInstrumentDesc[track].pluginFormatName, nullptr);
                        instrumentNode.setProperty("category", savedInstrumentDesc[track].category, nullptr);
                        instrumentNode.setProperty("manufacturerName", savedInstrumentDesc[track].manufacturerName, nullptr);
                        instrumentNode.setProperty("version", savedInstrumentDesc[track].version, nullptr);
                        instrumentNode.setProperty("fileOrIdentifier", savedInstrumentDesc[track].fileOrIdentifier, nullptr);
                        instrumentNode.setProperty("uid", savedInstrumentDesc[track].uniqueId, nullptr);
                        instrumentNode.setProperty("isInstrument", savedInstrumentDesc[track].isInstrument, nullptr);
                        instrumentNode.setProperty("numInputChannels", savedInstrumentDesc[track].numInputChannels, nullptr);
                        instrumentNode.setProperty("numOutputChannels", savedInstrumentDesc[track].numOutputChannels, nullptr);

                        // Save plugin state
                        juce::MemoryBlock stateData;
                        vstInstruments[track]->getStateInformation(stateData);
                        juce::String stateBase64 = stateData.toBase64Encoding();
                        instrumentNode.setProperty("state", stateBase64, nullptr);

                        // NEW: Save explicitly changed parameters
                        auto changedParams = parameterListenerManager.getChangedParamsForPlugin(vstInstruments[track].get());
                        for (const auto& pair : changedParams)
                        {
                            juce::ValueTree paramNode("ParamChange");
                            paramNode.setProperty("index", pair.first, nullptr);
                            paramNode.setProperty("value", pair.second.value, nullptr);
                            paramNode.setProperty("name", pair.second.name, nullptr);
                            instrumentNode.appendChild(paramNode, nullptr);
                        }

                        trackNode.appendChild(instrumentNode, nullptr);
                    }

                    // Save effects
                    for (int fx = 0; fx < 3; ++fx)
                    {
                        if (vstEffects[track][fx] != nullptr)
                        {
                            juce::ValueTree effectNode("Effect");
                            effectNode.setProperty("slot", fx, nullptr);
                            effectNode.setProperty("name", savedEffectDesc[track][fx].name, nullptr);
                            effectNode.setProperty("pluginFormatName", savedEffectDesc[track][fx].pluginFormatName, nullptr);
                            effectNode.setProperty("category", savedEffectDesc[track][fx].category, nullptr);
                            effectNode.setProperty("manufacturerName", savedEffectDesc[track][fx].manufacturerName, nullptr);
                            effectNode.setProperty("version", savedEffectDesc[track][fx].version, nullptr);
                            effectNode.setProperty("fileOrIdentifier", savedEffectDesc[track][fx].fileOrIdentifier, nullptr);
                            effectNode.setProperty("uid", savedEffectDesc[track][fx].uniqueId, nullptr);
                            effectNode.setProperty("isInstrument", savedEffectDesc[track][fx].isInstrument, nullptr);
                            effectNode.setProperty("numInputChannels", savedEffectDesc[track][fx].numInputChannels, nullptr);
                            effectNode.setProperty("numOutputChannels", savedEffectDesc[track][fx].numOutputChannels, nullptr);

                            // Save plugin state
                            juce::MemoryBlock stateData;
                            vstEffects[track][fx]->getStateInformation(stateData);
                            juce::String stateBase64 = stateData.toBase64Encoding();
                            effectNode.setProperty("state", stateBase64, nullptr);

                            // NEW: Save explicitly changed parameters
                            auto changedParams = parameterListenerManager.getChangedParamsForPlugin(vstEffects[track][fx].get());
                            for (const auto& pair : changedParams)
                            {
                                juce::ValueTree paramNode("ParamChange");
                                paramNode.setProperty("index", pair.first, nullptr);
                                paramNode.setProperty("value", pair.second.value, nullptr);
                                paramNode.setProperty("name", pair.second.name, nullptr);
                                effectNode.appendChild(paramNode, nullptr);
                            }

                            trackNode.appendChild(effectNode, nullptr);
                        }
                    }

                    projectState.appendChild(trackNode, nullptr);
                }

                // Save piano roll data (notes and automation points)
                projectState.appendChild(pianoRoll.getState(), nullptr);

                // Save automation lane assignments with precise track/slot info
                // This is separate from piano roll state to include track/slot identification
                for (int laneIdx = 0; laneIdx < 5; ++laneIdx)
                {
                    const auto& lane = pianoRoll.getAutomationLane(laneIdx);
                    if (lane.isAssigned())
                    {
                        auto* plugin = lane.getPlugin();
                        if (plugin != nullptr)
                        {
                            juce::ValueTree assignmentNode("AutomationAssignment");
                            assignmentNode.setProperty("laneIndex", laneIdx, nullptr);
                            assignmentNode.setProperty("parameterIndex", lane.getParameterIndex(), nullptr);
                            assignmentNode.setProperty("assignmentName", lane.getAssignmentName(), nullptr);
                            assignmentNode.setProperty("pluginName", plugin->getName(), nullptr);

                            // Find which track and slot this plugin is in
                            bool found = false;

                            // Check instruments
                            for (int track = 0; track < 5 && !found; ++track)
                            {
                                if (vstInstruments[track].get() == plugin)
                                {
                                    assignmentNode.setProperty("isInstrument", true, nullptr);
                                    assignmentNode.setProperty("track", track, nullptr);
                                    found = true;
                                }
                            }

                            // Check effects
                            if (!found)
                            {
                                for (int track = 0; track < 5 && !found; ++track)
                                {
                                    for (int fx = 0; fx < 3 && !found; ++fx)
                                    {
                                        if (vstEffects[track][fx].get() == plugin)
                                        {
                                            assignmentNode.setProperty("isInstrument", false, nullptr);
                                            assignmentNode.setProperty("track", track, nullptr);
                                            assignmentNode.setProperty("effectSlot", fx, nullptr);
                                            found = true;
                                        }
                                    }
                                }
                            }

                            if (found)
                            {
                                projectState.appendChild(assignmentNode, nullptr);
                            }
                        }
                    }
                }

                // Write to file
                std::unique_ptr<juce::XmlElement> xml(projectState.createXml());
                if (xml != nullptr)
                {
                    xml->writeTo(file);
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                        "Success",
                        "Project saved!");
                }
            }

            delete chooser;
        });
}

void MainComponent::loadProject()
{
    auto* chooser = new juce::FileChooser("Load Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File() && file.existsAsFile())
            {
                std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));

                if (xml != nullptr)
                {
                    juce::ValueTree projectState = juce::ValueTree::fromXml(*xml);

                    if (projectState.isValid() && projectState.hasType("Project"))
                    {
                        // Load BPM
                        if (projectState.hasProperty("BPM"))
                        {
                            double bpm = projectState.getProperty("BPM");
                            bpmSlider.setValue(bpm);
                            audioProcessor.setBPM(bpm);
                        }

                        // Load sidechain amount
                        if (projectState.hasProperty("SidechainAmount"))
                        {
                            float sidechainAmount = projectState.getProperty("SidechainAmount");
                            sidechainSlider.setValue(sidechainAmount);
                            audioProcessor.setSidechainAmount(sidechainAmount);
                        }

                        // Load track 3 volume
                        if (projectState.hasProperty("Track3Volume"))
                        {
                            float track3Vol = projectState.getProperty("Track3Volume");
                            track3VolumeSlider.setValue(track3Vol);
                            audioProcessor.setTrack3Volume(track3Vol);
                        }

                        // Load sidechain5 amount
                        if (projectState.hasProperty("Sidechain5Amount"))
                        {
                            float sidechain5Amount = projectState.getProperty("Sidechain5Amount");
                            sidechain5Slider.setValue(sidechain5Amount);
                            audioProcessor.setSidechain5Amount(sidechain5Amount);
                        }

                        // Load track 5 volume
                        if (projectState.hasProperty("Track5Volume"))
                        {
                            float track5Vol = projectState.getProperty("Track5Volume");
                            track5VolumeSlider.setValue(track5Vol);
                            audioProcessor.setTrack5Volume(track5Vol);
                        }

                        // Load color settings
                        if (projectState.hasProperty("BackgroundColor"))
                        {
                            juce::String colorStr = projectState.getProperty("BackgroundColor").toString();
                            backgroundColor = juce::Colour::fromString(colorStr);
                            backgroundColorEditor.setText(backgroundColor.toDisplayString(false).substring(2)); // Remove "ff" prefix
                            pianoRoll.setSidebarColor(backgroundColor);
                            repaint();
                        }

                        if (projectState.hasProperty("PianoRollColor"))
                        {
                            juce::String colorStr = projectState.getProperty("PianoRollColor").toString();
                            pianoRollColor = juce::Colour::fromString(colorStr);
                            pianoRollColorEditor.setText(pianoRollColor.toDisplayString(false).substring(2)); // Remove "ff" prefix
                            pianoRoll.setBackgroundColor(pianoRollColor);
                        }

                        // Load tracks
                        for (auto trackNode : projectState)
                        {
                            if (trackNode.hasType("Track"))
                            {
                                int trackIndex = trackNode.getProperty("index");

                                // Load instrument
                                auto instrumentNode = trackNode.getChildWithName("Instrument");
                                if (instrumentNode.isValid())
                                {
                                    juce::PluginDescription desc;
                                    desc.name = instrumentNode.getProperty("name").toString();
                                    desc.pluginFormatName = instrumentNode.getProperty("pluginFormatName").toString();
                                    desc.category = instrumentNode.getProperty("category").toString();
                                    desc.manufacturerName = instrumentNode.getProperty("manufacturerName").toString();
                                    desc.version = instrumentNode.getProperty("version").toString();
                                    desc.fileOrIdentifier = instrumentNode.getProperty("fileOrIdentifier").toString();
                                    desc.uniqueId = instrumentNode.getProperty("uid");
                                    desc.isInstrument = instrumentNode.getProperty("isInstrument");
                                    desc.numInputChannels = instrumentNode.getProperty("numInputChannels");
                                    desc.numOutputChannels = instrumentNode.getProperty("numOutputChannels");

                                    juce::String state = instrumentNode.getProperty("state").toString();
                                    loadPluginFromDescription(desc, true, trackIndex, state, false);

                                    // NEW: Restore explicit parameter changes
                                    if (vstInstruments[trackIndex] != nullptr)
                                    {
                                        auto* plugin = vstInstruments[trackIndex].get();
                                        for (auto paramChild : instrumentNode)
                                        {
                                            if (paramChild.hasType("ParamChange"))
                                            {
                                                int idx = paramChild.getProperty("index");
                                                float val = paramChild.getProperty("value");
                                                juce::String name = paramChild.getProperty("name");

                                                // 1. Force value update
                                                if (idx >= 0 && idx < plugin->getParameters().size())
                                                {
                                                    plugin->getParameters()[idx]->setValueNotifyingHost(val);
                                                }

                                                // 2. Populate the "List of turned knobs" so it saves next time
                                                parameterListenerManager.restoreChangedParam(plugin, idx, val, name);
                                            }
                                        }
                                    }
                                }

                                // Load effects
                                for (auto effectNode : trackNode)
                                {
                                    if (effectNode.hasType("Effect"))
                                    {
                                        int effectSlot = effectNode.getProperty("slot");

                                        juce::PluginDescription desc;
                                        desc.name = effectNode.getProperty("name").toString();
                                        desc.pluginFormatName = effectNode.getProperty("pluginFormatName").toString();
                                        desc.category = effectNode.getProperty("category").toString();
                                        desc.manufacturerName = effectNode.getProperty("manufacturerName").toString();
                                        desc.version = effectNode.getProperty("version").toString();
                                        desc.fileOrIdentifier = effectNode.getProperty("fileOrIdentifier").toString();
                                        desc.uniqueId = effectNode.getProperty("uid");
                                        desc.isInstrument = effectNode.getProperty("isInstrument");
                                        desc.numInputChannels = effectNode.getProperty("numInputChannels");
                                        desc.numOutputChannels = effectNode.getProperty("numOutputChannels");

                                        juce::String state = effectNode.getProperty("state").toString();

                                        currentTrack = trackIndex;
                                        loadPluginFromDescription(desc, false, effectSlot, state, false);

                                        // NEW: Restore explicit parameter changes
                                        if (vstEffects[trackIndex][effectSlot] != nullptr)
                                        {
                                            auto* plugin = vstEffects[trackIndex][effectSlot].get();
                                            for (auto paramChild : effectNode)
                                            {
                                                if (paramChild.hasType("ParamChange"))
                                                {
                                                    int idx = paramChild.getProperty("index");
                                                    float val = paramChild.getProperty("value");
                                                    juce::String name = paramChild.getProperty("name");

                                                    // 1. Force value update
                                                    if (idx >= 0 && idx < plugin->getParameters().size())
                                                    {
                                                        plugin->getParameters()[idx]->setValueNotifyingHost(val);
                                                    }

                                                    // 2. Populate the "List of turned knobs" so it saves next time
                                                    parameterListenerManager.restoreChangedParam(plugin, idx, val, name);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Load piano roll data (notes and automation points)
                        auto pianoRollState = projectState.getChildWithName("PianoRoll");
                        if (pianoRollState.isValid())
                        {
                            pianoRoll.setState(pianoRollState);
                        }

                        // Load automation assignments with precise track/slot info
                        // This happens AFTER plugins are loaded so we can match them correctly
                        juce::Timer::callAfterDelay(200, [this, projectState]()
                            {
                                for (auto child : projectState)
                                {
                                    if (child.hasType("AutomationAssignment"))
                                    {
                                        int laneIdx = child.getProperty("laneIndex");
                                        int paramIdx = child.getProperty("parameterIndex");
                                        juce::String assignmentName = child.getProperty("assignmentName");
                                        bool isInstrument = child.getProperty("isInstrument");
                                        int track = child.getProperty("track");

                                        juce::AudioPluginInstance* plugin = nullptr;

                                        if (isInstrument)
                                        {
                                            if (track >= 0 && track < 5 && vstInstruments[track] != nullptr)
                                            {
                                                plugin = vstInstruments[track].get();
                                            }
                                        }
                                        else
                                        {
                                            int effectSlot = child.getProperty("effectSlot");
                                            if (track >= 0 && track < 5 && effectSlot >= 0 && effectSlot < 3 &&
                                                vstEffects[track][effectSlot] != nullptr)
                                            {
                                                plugin = vstEffects[track][effectSlot].get();
                                            }
                                        }

                                        // Restore the assignment
                                        if (plugin != nullptr && paramIdx >= 0 && paramIdx < plugin->getParameters().size())
                                        {
                                            pianoRoll.getAutomationLane(laneIdx).assignToParameter(plugin, paramIdx, assignmentName);
                                            automationLaneLabels[laneIdx].setText(assignmentName, juce::dontSendNotification);
                                        }
                                    }
                                }
                            });

                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                            "Success",
                            "Project loaded!");
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                            "Error",
                            "Invalid project file!");
                    }
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to parse project file!");
                }
            }

            delete chooser;
        });
}

void MainComponent::exportToFLAC()
{
    // Check if at least one instrument is loaded
    bool hasInstrument = false;
    for (int i = 0; i < 5; ++i)
    {
        if (vstInstruments[i] != nullptr)
        {
            hasInstrument = true;
            break;
        }
    }

    if (!hasInstrument)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error",
            "Please load at least one instrument first!");
        return;
    }

    auto* chooser = new juce::FileChooser("Export to FLAC",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.flac");

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
            {
                // Ensure .flac extension
                if (!file.hasFileExtension(".flac"))
                    file = file.withFileExtension(".flac");

                // Stop playback during export
                bool wasPlaying = isPlaying;
                if (wasPlaying)
                {
                    isPlaying = false;
                    audioProcessor.setPlaying(false);
                    pianoRoll.setPlaying(false);
                }

                // Render settings (32nd notes = 8 per beat)
                const double sampleRate = 44100.0;
                const int numChannels = 2;
                const double bpm = audioProcessor.getBPM();
                const double beatsPerSecond = bpm / 60.0;
                const double thirtySecondNotesPerSecond = beatsPerSecond * 8.0;
                const int samplesPerStep = static_cast<int>(sampleRate / thirtySecondNotesPerSecond);
                const int totalSteps = pianoRoll.getLastOccupiedStep() + 1;
                const int totalSamples = totalSteps * samplesPerStep;

                // Create audio buffer for rendering
                juce::AudioBuffer<float> renderBuffer(numChannels, totalSamples);
                renderBuffer.clear();

                // Reset and prepare all plugins for offline rendering
                for (int track = 0; track < 5; ++track)
                {
                    if (vstInstruments[track] != nullptr)
                    {
                        vstInstruments[track]->reset();
                        vstInstruments[track]->prepareToPlay(sampleRate, 512);
                    }

                    for (int fx = 0; fx < 3; ++fx)
                    {
                        if (vstEffects[track][fx] != nullptr)
                        {
                            vstEffects[track][fx]->reset();
                            vstEffects[track][fx]->prepareToPlay(sampleRate, 512);
                        }
                    }
                }

                // Render in chunks
                const int chunkSize = 512;
                int currentSample = 0;
                bool activeNotes[5][128] = { {false}, {false}, {false}, {false}, {false} };
                int lastProcessedStep = -1;  // Track which step we last applied automation for

                juce::AudioBuffer<float> mixBuffer(numChannels, chunkSize);

                while (currentSample < totalSamples)
                {
                    int samplesThisTime = juce::jmin(chunkSize, totalSamples - currentSample);

                    mixBuffer.clear();

                    // Apply automation for all samples in this chunk (before processing tracks)
                    for (int i = 0; i < samplesThisTime; ++i)
                    {
                        int step = (currentSample + i) / samplesPerStep;
                        int prevStep = (currentSample + i - 1) / samplesPerStep;

                        // Apply automation at step boundaries (only once per step)
                        if (step != lastProcessedStep && (step != prevStep || currentSample + i == 0))
                        {
                            lastProcessedStep = step;

                            // Apply all 5 automation lanes to their assigned parameters
                            for (int lane = 0; lane < 5; ++lane)
                            {
                                const AutomationLane& autoLane = pianoRoll.getAutomationLane(lane);
                                if (autoLane.isAssigned())
                                {
                                    float value = autoLane.getStepValue(step % totalSteps);
                                    auto* plugin = autoLane.getPlugin();
                                    int paramIndex = autoLane.getParameterIndex();

                                    if (plugin != nullptr && paramIndex >= 0 && paramIndex < plugin->getParameters().size())
                                    {
                                        plugin->getParameters()[paramIndex]->setValue(value);
                                    }
                                }
                            }
                        }
                    }

                    // Create buffers for each track and track 3/5 sidechain
                    juce::AudioBuffer<float> trackBuffers[5];
                    for (int i = 0; i < 5; ++i)
                        trackBuffers[i] = juce::AudioBuffer<float>(numChannels, samplesThisTime);

                    juce::AudioBuffer<float> track3SidechainBuffer(numChannels, samplesThisTime);
                    juce::AudioBuffer<float> track5SidechainBuffer(numChannels, samplesThisTime);

                    // Get sidechain amounts and track volumes
                    float sidechainAmt = static_cast<float>(sidechainSlider.getValue());
                    float track3Vol = static_cast<float>(track3VolumeSlider.getValue());
                    float sidechain5Amt = static_cast<float>(sidechain5Slider.getValue());
                    float track5Vol = static_cast<float>(track5VolumeSlider.getValue());

                    // Process each track
                    for (int track = 0; track < 5; ++track)
                    {
                        if (vstInstruments[track] == nullptr)
                            continue;

                        juce::MidiBuffer midiMessages;

                        // Generate MIDI for this chunk
                        for (int i = 0; i < samplesThisTime; ++i)
                        {
                            int step = (currentSample + i) / samplesPerStep;
                            int prevStep = (currentSample + i - 1) / samplesPerStep;

                            if (step != prevStep || currentSample + i == 0)
                            {
                                // Check for note changes at this step
                                for (int midiNote = 0; midiNote < 128; ++midiNote)
                                {
                                    bool wasActive = activeNotes[track][midiNote];
                                    bool isActive = pianoRoll.isNoteActiveAt(track, midiNote, step % totalSteps);
                                    bool isStart = pianoRoll.isNoteStartAt(track, midiNote, step % totalSteps);

                                    if (isActive && isStart && !wasActive)
                                    {
                                        // Note starts - send note on
                                        midiMessages.addEvent(juce::MidiMessage::noteOn(1, midiNote, (juce::uint8)100), i);
                                        activeNotes[track][midiNote] = true;
                                    }
                                    else if (!isActive && wasActive)
                                    {
                                        // Note ends - send note off
                                        midiMessages.addEvent(juce::MidiMessage::noteOff(1, midiNote), i);
                                        activeNotes[track][midiNote] = false;
                                    }
                                }
                            }
                        }

                        trackBuffers[track].clear();

                        // Process instrument
                        vstInstruments[track]->processBlock(trackBuffers[track], midiMessages);

                        // For Track 5: save a copy for sidechain BEFORE effects and volume
                        if (track == 4)
                        {
                            track5SidechainBuffer.clear();
                            for (int ch = 0; ch < numChannels; ++ch)
                            {
                                track5SidechainBuffer.copyFrom(ch, 0, trackBuffers[track], ch, 0, samplesThisTime);
                            }
                        }

                        // For Track 3: save a copy for sidechain BEFORE effects and volume
                        if (track == 2)
                        {
                            track3SidechainBuffer.clear();
                            for (int ch = 0; ch < numChannels; ++ch)
                            {
                                track3SidechainBuffer.copyFrom(ch, 0, trackBuffers[track], ch, 0, samplesThisTime);
                            }
                        }

                        // Process effects chain
                        for (int fx = 0; fx < 3; ++fx)
                        {
                            if (vstEffects[track][fx] != nullptr)
                            {
                                juce::MidiBuffer emptyMidi;

                                // For Track 4: pass Track 5 sidechain if effect supports it
                                if (track == 3 && sidechain5Amt > 0.0f)
                                {
                                    int pluginInputChannels = vstEffects[track][fx]->getTotalNumInputChannels();

                                    if (pluginInputChannels > numChannels)
                                    {
                                        // Plugin supports sidechain
                                        juce::AudioBuffer<float> bufferWithSidechain(pluginInputChannels, samplesThisTime);
                                        bufferWithSidechain.clear();

                                        // Copy main signal
                                        for (int ch = 0; ch < numChannels && ch < pluginInputChannels; ++ch)
                                        {
                                            bufferWithSidechain.copyFrom(ch, 0, trackBuffers[track], ch, 0, samplesThisTime);
                                        }

                                        // Copy sidechain signal (scaled)
                                        int sidechainStartChannel = numChannels;
                                        for (int ch = 0; ch < numChannels && (sidechainStartChannel + ch) < pluginInputChannels; ++ch)
                                        {
                                            bufferWithSidechain.copyFrom(sidechainStartChannel + ch, 0, track5SidechainBuffer, ch, 0, samplesThisTime);
                                            bufferWithSidechain.applyGain(sidechainStartChannel + ch, 0, samplesThisTime, sidechain5Amt);
                                        }

                                        // Process
                                        vstEffects[track][fx]->processBlock(bufferWithSidechain, emptyMidi);

                                        // Copy back main channels only
                                        for (int ch = 0; ch < numChannels; ++ch)
                                        {
                                            trackBuffers[track].copyFrom(ch, 0, bufferWithSidechain, ch, 0, samplesThisTime);
                                        }
                                    }
                                    else
                                    {
                                        // No sidechain support
                                        vstEffects[track][fx]->processBlock(trackBuffers[track], emptyMidi);
                                    }
                                }
                                // For Track 2: pass Track 3 sidechain if effect supports it
                                else if (track == 1 && sidechainAmt > 0.0f)
                                {
                                    int pluginInputChannels = vstEffects[track][fx]->getTotalNumInputChannels();

                                    if (pluginInputChannels > numChannels)
                                    {
                                        // Plugin supports sidechain
                                        juce::AudioBuffer<float> bufferWithSidechain(pluginInputChannels, samplesThisTime);
                                        bufferWithSidechain.clear();

                                        // Copy main signal
                                        for (int ch = 0; ch < numChannels && ch < pluginInputChannels; ++ch)
                                        {
                                            bufferWithSidechain.copyFrom(ch, 0, trackBuffers[track], ch, 0, samplesThisTime);
                                        }

                                        // Copy sidechain signal (scaled)
                                        int sidechainStartChannel = numChannels;
                                        for (int ch = 0; ch < numChannels && (sidechainStartChannel + ch) < pluginInputChannels; ++ch)
                                        {
                                            bufferWithSidechain.copyFrom(sidechainStartChannel + ch, 0, track3SidechainBuffer, ch, 0, samplesThisTime);
                                            bufferWithSidechain.applyGain(sidechainStartChannel + ch, 0, samplesThisTime, sidechainAmt);
                                        }

                                        // Process
                                        vstEffects[track][fx]->processBlock(bufferWithSidechain, emptyMidi);

                                        // Copy back main channels only
                                        for (int ch = 0; ch < numChannels; ++ch)
                                        {
                                            trackBuffers[track].copyFrom(ch, 0, bufferWithSidechain, ch, 0, samplesThisTime);
                                        }
                                    }
                                    else
                                    {
                                        // No sidechain support
                                        vstEffects[track][fx]->processBlock(trackBuffers[track], emptyMidi);
                                    }
                                }
                                else
                                {
                                    // Normal processing (no sidechain)
                                    vstEffects[track][fx]->processBlock(trackBuffers[track], emptyMidi);
                                }
                            }
                        }

                        // Apply Track 5 volume (after effects, before mixing)
                        if (track == 4)
                        {
                            for (int ch = 0; ch < numChannels; ++ch)
                            {
                                trackBuffers[track].applyGain(ch, 0, samplesThisTime, track5Vol);
                            }
                        }

                        // Apply Track 3 volume (after effects, before mixing)
                        if (track == 2)
                        {
                            for (int ch = 0; ch < numChannels; ++ch)
                            {
                                trackBuffers[track].applyGain(ch, 0, samplesThisTime, track3Vol);
                            }
                        }

                        // Mix track into output buffer
                        for (int ch = 0; ch < numChannels; ++ch)
                        {
                            mixBuffer.addFrom(ch, 0, trackBuffers[track], ch, 0, samplesThisTime);
                        }
                    }

                    // Copy mixed audio to render buffer
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        renderBuffer.copyFrom(ch, currentSample, mixBuffer, ch, 0, samplesThisTime);
                    }

                    currentSample += samplesThisTime;
                }

                // Clean up plugins - reset to clear any lingering state
                for (int track = 0; track < 5; ++track)
                {
                    if (vstInstruments[track] != nullptr)
                    {
                        vstInstruments[track]->reset();
                        vstInstruments[track]->releaseResources();
                    }

                    for (int fx = 0; fx < 3; ++fx)
                    {
                        if (vstEffects[track][fx] != nullptr)
                        {
                            vstEffects[track][fx]->reset();
                            vstEffects[track][fx]->releaseResources();
                        }
                    }
                }

                // Re-prepare for normal playback
                auto* currentDevice = audioDeviceManager.getCurrentAudioDevice();
                if (currentDevice)
                {
                    for (int track = 0; track < 5; ++track)
                    {
                        if (vstInstruments[track] != nullptr)
                            vstInstruments[track]->prepareToPlay(currentDevice->getCurrentSampleRate(),
                                currentDevice->getCurrentBufferSizeSamples());

                        for (int fx = 0; fx < 3; ++fx)
                        {
                            if (vstEffects[track][fx] != nullptr)
                                vstEffects[track][fx]->prepareToPlay(currentDevice->getCurrentSampleRate(),
                                    currentDevice->getCurrentBufferSizeSamples());
                        }
                    }
                }

                // Write FLAC file
                juce::FlacAudioFormat flacFormat;
                std::unique_ptr<juce::FileOutputStream> outputStream(file.createOutputStream());

                if (outputStream != nullptr)
                {
                    std::unique_ptr<juce::AudioFormatWriter> writer(flacFormat.createWriterFor(
                        outputStream.get(),
                        sampleRate,
                        numChannels,
                        24,
                        {},
                        0));

                    if (writer != nullptr)
                    {
                        outputStream.release();
                        // Apply -6dB headroom for simplicity
                        renderBuffer.applyGain(0.5f);
                        writer->writeFromAudioSampleBuffer(renderBuffer, 0, totalSamples);
                        writer.reset();

                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                            "Success",
                            "FLAC export complete!\n\nFile saved:\n" + file.getFileName());
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                            "Error",
                            "Failed to create FLAC writer!");
                    }
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to create output file!");
                }

                // Restore playback if it was running
                if (wasPlaying)
                {
                    isPlaying = true;
                    audioProcessor.setPlaying(true);
                    pianoRoll.setPlaying(true);
                }
            }

            delete chooser;
        });
}

void MainComponent::openInstrumentGUI(int track)
{
    if (vstInstruments[track] == nullptr)
        return;

    // Check if window exists
    if (instrumentWindows[track] != nullptr)
    {
        auto* window = instrumentWindows[track].getComponent();
        if (window != nullptr)
        {
            if (!window->isVisible())
                window->setVisible(true);
            window->toFront(true);
            return;
        }
    }

    // Create new window
    instrumentWindows[track] = new PluginWindow(
        "Track " + juce::String(track + 1) + " - " + loadedInstrumentNames[track],
        vstInstruments[track].get());
}

void MainComponent::openEffectGUI(int track, int effectSlot)
{
    if (vstEffects[track][effectSlot] == nullptr)
        return;

    // Check if window exists
    if (effectWindows[track][effectSlot] != nullptr)
    {
        auto* window = effectWindows[track][effectSlot].getComponent();
        if (window != nullptr)
        {
            if (!window->isVisible())
                window->setVisible(true);
            window->toFront(true);
            return;
        }
    }

    // Create new window
    effectWindows[track][effectSlot] = new PluginWindow(
        "Track " + juce::String(track + 1) + " FX" + juce::String(effectSlot + 1) + " - " + loadedEffectNames[track][effectSlot],
        vstEffects[track][effectSlot].get());
}
bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    // Keyboard MIDI mapping
    // asdfghj = C D E F G A B
    // wetzu = C# D# F# G# A# (black keys)
    // y = octave down, x = octave up

    int keyCode = key.getKeyCode();
    int midiNote = -1;

    // Octave control
    if (keyCode == 'Y')
    {
        if (keyboardOctave > 0)
        {
            keyboardOctave--;
            // Turn off all active notes when changing octave
            for (int note : activeKeyboardNotes)
            {
                if (vstInstruments[currentTrack] != nullptr)
                {
                    juce::MidiBuffer midiBuffer;
                    midiBuffer.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                    // Send note off - we'll do this through the audio processor
                }
            }
            activeKeyboardNotes.clear();
        }
        return true;
    }
    else if (keyCode == 'X')
    {
        if (keyboardOctave < 8)
        {
            keyboardOctave++;
            // Turn off all active notes when changing octave
            for (int note : activeKeyboardNotes)
            {
                if (vstInstruments[currentTrack] != nullptr)
                {
                    juce::MidiBuffer midiBuffer;
                    midiBuffer.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                }
            }
            activeKeyboardNotes.clear();
        }
        return true;
    }

    // White keys: A S D F G H J = C D E F G A B
    int baseNote = keyboardOctave * 12;

    switch (keyCode)
    {
    case 'A': midiNote = baseNote + 0; break;  // C
    case 'S': midiNote = baseNote + 2; break;  // D
    case 'D': midiNote = baseNote + 4; break;  // E
    case 'F': midiNote = baseNote + 5; break;  // F
    case 'G': midiNote = baseNote + 7; break;  // G
    case 'H': midiNote = baseNote + 9; break;  // A
    case 'J': midiNote = baseNote + 11; break; // B

        // Black keys: W E T Z U = C# D# F# G# A#
    case 'W': midiNote = baseNote + 1; break;  // C#
    case 'E': midiNote = baseNote + 3; break;  // D#
    case 'T': midiNote = baseNote + 6; break;  // F#
    case 'Z': midiNote = baseNote + 8; break;  // G#
    case 'U': midiNote = baseNote + 10; break; // A#

    default: return false;
    }

    // Ensure note is in valid MIDI range
    if (midiNote < 0 || midiNote > 127)
        return false;

    // Check if note is already playing (prevent retriggering)
    if (activeKeyboardNotes.find(midiNote) != activeKeyboardNotes.end())
        return true;

    // Add MIDI note to the audio processor's queue (thread-safe)
    if (vstInstruments[currentTrack] != nullptr)
    {
        activeKeyboardNotes.insert(midiNote);
        audioProcessor.addKeyboardMidiNote(currentTrack, midiNote, true, 100);
    }

    return true;
}

bool MainComponent::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
    // Handle key releases
    if (!isKeyDown)
    {
        // Check all active notes and turn off any that are no longer pressed
        std::set<int> notesToRemove;

        for (int note : activeKeyboardNotes)
        {
            // Map note back to key to check if it's still pressed
            int octave = note / 12;
            int noteInOctave = note % 12;

            bool stillPressed = false;

            // Check if corresponding key is still down
            if (octave == keyboardOctave)
            {
                switch (noteInOctave)
                {
                case 0: stillPressed = juce::KeyPress::isKeyCurrentlyDown('A'); break;
                case 1: stillPressed = juce::KeyPress::isKeyCurrentlyDown('W'); break;
                case 2: stillPressed = juce::KeyPress::isKeyCurrentlyDown('S'); break;
                case 3: stillPressed = juce::KeyPress::isKeyCurrentlyDown('E'); break;
                case 4: stillPressed = juce::KeyPress::isKeyCurrentlyDown('D'); break;
                case 5: stillPressed = juce::KeyPress::isKeyCurrentlyDown('F'); break;
                case 6: stillPressed = juce::KeyPress::isKeyCurrentlyDown('T'); break;
                case 7: stillPressed = juce::KeyPress::isKeyCurrentlyDown('G'); break;
                case 8: stillPressed = juce::KeyPress::isKeyCurrentlyDown('Z'); break;
                case 9: stillPressed = juce::KeyPress::isKeyCurrentlyDown('H'); break;
                case 10: stillPressed = juce::KeyPress::isKeyCurrentlyDown('U'); break;
                case 11: stillPressed = juce::KeyPress::isKeyCurrentlyDown('J'); break;
                }
            }

            if (!stillPressed)
            {
                // Add MIDI note-off to the audio processor's queue (thread-safe)
                if (vstInstruments[currentTrack] != nullptr)
                {
                    audioProcessor.addKeyboardMidiNote(currentTrack, note, false);
                }

                notesToRemove.insert(note);
            }
        }

        // Remove notes that were released
        for (int note : notesToRemove)
        {
            activeKeyboardNotes.erase(note);
        }
    }

    return false;
}

void MainComponent::setupParameterListeners(juce::AudioPluginInstance* plugin)
{
    if (plugin == nullptr)
        return;

    // Use the new listener manager which creates a unique listener per plugin
    parameterListenerManager.addPluginListener(*this, plugin);
}

void MainComponent::updateBackgroundColor()
{
    backgroundColor = parseHexColor(backgroundColorEditor.getText());
    pianoRoll.setSidebarColor(backgroundColor);
    repaint();
}

void MainComponent::updatePianoRollColor()
{
    pianoRollColor = parseHexColor(pianoRollColorEditor.getText());
    pianoRoll.setBackgroundColor(pianoRollColor);
}

juce::Colour MainComponent::parseHexColor(const juce::String& hexString)
{
    // Remove any # prefix if present
    juce::String hex = hexString.trim().removeCharacters("#");

    // Ensure we have exactly 6 characters
    if (hex.length() != 6)
    {
        // Return current color if invalid
        return juce::Colours::darkgrey;
    }

    // Parse hex string to integer
    int colorValue = hex.getHexValue32();

    // Extract RGB components
    int r = (colorValue >> 16) & 0xFF;
    int g = (colorValue >> 8) & 0xFF;
    int b = colorValue & 0xFF;

    return juce::Colour::fromRGB(r, g, b);
}

void MainComponent::importMIDIToTrack(int track)
{
    if (track < 0 || track >= 5)
        return;

    auto chooser = std::make_unique<juce::FileChooser>("Select MIDI file to import",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.mid;*.midi");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, track](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::FileInputStream stream(file);
                if (stream.openedOk())
                {
                    juce::MidiFile midiFile;
                    if (midiFile.readFrom(stream))
                    {
                        // Clear all existing notes from this track before importing
                        pianoRoll.clearTrackNotes(track);

                        // Convert MIDI file to our Note format
                        // Get the track's color for reference
                        juce::Colour trackColors[3] = { juce::Colours::red, juce::Colours::green, juce::Colours::blue };

                        // Calculate time format - MIDI uses ticks per quarter note
                        int timeFormat = midiFile.getTimeFormat();
                        if (timeFormat <= 0)
                        {
                            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                "MIDI Import Error",
                                "Unsupported MIDI time format. Please use a standard MIDI file.");
                            return;
                        }

                        double ticksPerQuarterNote = timeFormat;
                        // We use 32nd notes, so there are 8 32nd notes per quarter note
                        double ticksPer32ndNote = ticksPerQuarterNote / 8.0;

                        // Process all tracks in the MIDI file and merge them
                        for (int midiTrack = 0; midiTrack < midiFile.getNumTracks(); ++midiTrack)
                        {
                            const juce::MidiMessageSequence* sequence = midiFile.getTrack(midiTrack);
                            if (sequence == nullptr)
                                continue;

                            for (int i = 0; i < sequence->getNumEvents(); ++i)
                            {
                                const juce::MidiMessageSequence::MidiEventHolder* event = sequence->getEventPointer(i);
                                if (event == nullptr)
                                    continue;

                                const juce::MidiMessage& message = event->message;

                                if (message.isNoteOn())
                                {
                                    int midiNote = message.getNoteNumber();
                                    double startTime = message.getTimeStamp();
                                    int startStep = juce::roundToInt(startTime / ticksPer32ndNote);

                                    // Find the corresponding note-off
                                    int length = 1; // Default to 1 32nd note if no note-off found
                                    int noteOffIndex = sequence->getIndexOfMatchingKeyUp(i);

                                    if (noteOffIndex >= 0)
                                    {
                                        const juce::MidiMessageSequence::MidiEventHolder* noteOffEvent = sequence->getEventPointer(noteOffIndex);
                                        if (noteOffEvent != nullptr)
                                        {
                                            double endTime = noteOffEvent->message.getTimeStamp();
                                            int endStep = juce::roundToInt(endTime / ticksPer32ndNote);
                                            length = juce::jmax(1, endStep - startStep);
                                        }
                                    }

                                    // Add the note to the piano roll for this track
                                    Note note(midiNote, startStep, length);
                                    pianoRoll.addNoteToTrack(track, note);
                                }
                            }
                        }

                        // Repaint to show the imported notes
                        pianoRoll.repaint();

                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                            "MIDI Import",
                            "MIDI file imported to Track " + juce::String(track + 1) + "!");
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                            "MIDI Import Error",
                            "Failed to read MIDI file. Please ensure it's a valid MIDI file.");
                    }
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "MIDI Import Error",
                        "Failed to open MIDI file.");
                }
            }
        });

    activeFileChoosers.add(chooser.release());
}

void MainComponent::exportMIDIFromTrack(int track)
{
    if (track < 0 || track >= 5)
        return;

    // Get the notes for this track
    const auto& notes = pianoRoll.getTrackNotes(track);

    if (notes.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "MIDI Export",
            "Track " + juce::String(track + 1) + " has no MIDI notes to export.");
        return;
    }

    auto chooser = std::make_unique<juce::FileChooser>("Export MIDI file",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.mid");

    auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    // Copy notes to a local variable that can be captured
    std::vector<Note> notesCopy = notes;

    chooser->launchAsync(chooserFlags, [this, track, notesCopy](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file != juce::File())
            {
                // Create a MIDI file
                juce::MidiFile midiFile;
                juce::MidiMessageSequence sequence;

                // Use 480 ticks per quarter note (standard MIDI resolution)
                int ticksPerQuarterNote = 480;
                midiFile.setTicksPerQuarterNote(ticksPerQuarterNote);

                // We use 32nd notes, so there are 8 32nd notes per quarter note
                double ticksPer32ndNote = ticksPerQuarterNote / 8.0;

                // Convert our notes to MIDI messages
                for (const auto& note : notesCopy)
                {
                    double startTime = note.startStep * ticksPer32ndNote;
                    double endTime = (note.startStep + note.length) * ticksPer32ndNote;

                    // Add note on
                    juce::MidiMessage noteOn = juce::MidiMessage::noteOn(1, note.midiNote, (juce::uint8)100);
                    noteOn.setTimeStamp(startTime);
                    sequence.addEvent(noteOn);

                    // Add note off
                    juce::MidiMessage noteOff = juce::MidiMessage::noteOff(1, note.midiNote);
                    noteOff.setTimeStamp(endTime);
                    sequence.addEvent(noteOff);
                }

                // Update note off pairs for proper MIDI format
                sequence.updateMatchedPairs();

                // Add the sequence to the MIDI file
                midiFile.addTrack(sequence);

                // Ensure file has .mid extension
                if (!file.hasFileExtension(".mid"))
                    file = file.withFileExtension(".mid");

                // Write to file
                juce::FileOutputStream stream(file);
                if (stream.openedOk())
                {
                    midiFile.writeTo(stream);
                    stream.flush();

                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                        "MIDI Export",
                        "Track " + juce::String(track + 1) + " exported to:\n" + file.getFullPathName());
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "MIDI Export Error",
                        "Failed to write MIDI file.");
                }
            }
        });

    activeFileChoosers.add(chooser.release());
}