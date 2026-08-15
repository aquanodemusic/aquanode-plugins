#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::Slider& setupRotary(juce::Slider& s, double lo, double hi, double step, double defaultVal, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
            juce::MathConstants<float>::pi * 2.8f, true);
        s.setRange(lo, hi, step);
        s.setValue(defaultVal, juce::dontSendNotification);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 22);
        s.setTextValueSuffix(suffix);
        s.setDoubleClickReturnValue(true, defaultVal);
        return s;
    }

    juce::Slider& setupLinear(juce::Slider& s, double lo, double hi, double step, double defaultVal, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setRange(lo, hi, step);
        s.setValue(defaultVal, juce::dontSendNotification);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
        s.setTextValueSuffix(suffix);
        s.setDoubleClickReturnValue(true, defaultVal);
        return s;
    }
}

AudioStretcherAudioProcessorEditor::AudioStretcherAudioProcessorEditor(AudioStretcherAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), progressBar(progressValue), waveformDisplay(p)
{
    setLookAndFeel(&sakuraLookAndFeel);

    titleLabel.setText("AudioStretcher", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, Sakura::blossomHi);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(waveformDisplay);

    fileLabel.setText("No track loaded", juce::dontSendNotification);
    fileLabel.setJustificationType(juce::Justification::centred);
    fileLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    fileLabel.setFont(13.0f);
    addAndMakeVisible(fileLabel);

    // Load
    loadButton.setButtonText("Load");
    loadButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Select an audio file...",
                juce::File(),
                "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff");

            auto flags = juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles;

            fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
                {
                    auto url = chooser.getURLResult();
                    if (!url.isEmpty())
                    {
                        audioProcessor.loadAudioFile(url);
                        waveformDisplay.resetSelection();
                        resetStretchControlsToDefault();

                        if (audioProcessor.hasAudioLoaded())
                            statusLabel.setText("Ready", juce::dontSendNotification);
                        else
                            statusLabel.setText("Could not read that file - see log", juce::dontSendNotification);
                    }
                    else
                    {
                        juce::Logger::writeToLog("Load: FileChooser returned an empty URL "
                            "(picker was cancelled, or the platform handed back no result)");
                    }
                });
        };
    addAndMakeVisible(loadButton);

    // Save (always exports the current selection only)
    saveButton.setButtonText("Save");
    saveButton.setEnabled(false);
    saveButton.onClick = [this] { saveSelection(); };
    addAndMakeVisible(saveButton);

    // Play / Pause preview of the current selection with the current knob settings
    playButton.setButtonText("Play");
    playButton.setEnabled(false);
    playButton.onClick = [this] { togglePlay(); };
    addAndMakeVisible(playButton);

    pauseButton.setButtonText("Pause");
    pauseButton.setEnabled(false);
    pauseButton.onClick = [this] { audioProcessor.pausePreview(); };
    addAndMakeVisible(pauseButton);

    // Loop toggle for the preview - when on, playback wraps back to the
    // start of the selection instead of stopping at the end.
    loopButton.setButtonText("Loop");
    loopButton.setClickingTogglesState(true);
    loopButton.setColour(juce::TextButton::buttonOnColourId, Sakura::reddish);
    loopButton.onClick = [this] { audioProcessor.setPreviewLooping(loopButton.getToggleState()); };
    addAndMakeVisible(loopButton);

    // Resets Speed/Pitch/Fine-Tune back to 1x/0st/0ct without having to
    // load a new track.
    resetButton.setButtonText("Reset");
    resetButton.onClick = [this] { resetStretchControlsToDefault(); };
    addAndMakeVisible(resetButton);

    // Speed: 0.2x (5x slower) .. 4x (4x faster), with 1x (unity) at the
    // knob's centre position rather than off to one side.
    setupRotary(speedKnob, 0.2, 4.0, 0.01, 1.0, "x");
    speedKnob.setSkewFactorFromMidPoint(1.0); // must come after setRange (setupRotary), which resets skew
    // Reflect whatever the engine actually holds right now - not always
    // the hardcoded default. On the standalone app this matters a lot:
    // JUCE's standalone wrapper restores the processor's last-used
    // parameter values on startup (via setStateInformation), so the
    // engine can already be at e.g. 0.5x the moment this editor is built.
    // Showing 1.0x here regardless would silently lie to the user about
    // what the engine is about to apply. dontSendNotification because
    // this just mirrors the parameter's existing value - no change to push.
    speedKnob.setValue(*audioProcessor.timeStretchParam, juce::dontSendNotification);
    speedKnob.onValueChange = [this]
        {
            *audioProcessor.timeStretchParam = (float)speedKnob.getValue();
            audioProcessor.clearPreview();
            playButton.setButtonText("Play");
        };
    addAndMakeVisible(speedKnob);
    speedLabel.setText("SPEED", juce::dontSendNotification);
    speedLabel.setJustificationType(juce::Justification::centred);
    speedLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    speedLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    addAndMakeVisible(speedLabel);

    // Pitch: -36 .. +36 semitones
    setupRotary(pitchKnob, -36.0, 36.0, 0.01, 0.0, " st");
    pitchKnob.setValue(*audioProcessor.pitchShiftParam, juce::dontSendNotification); // see speedKnob comment above
    pitchKnob.onValueChange = [this]
        {
            *audioProcessor.pitchShiftParam = (float)pitchKnob.getValue();
            audioProcessor.clearPreview();
            playButton.setButtonText("Play");
        };
    addAndMakeVisible(pitchKnob);
    pitchLabel.setText("PITCH", juce::dontSendNotification);
    pitchLabel.setJustificationType(juce::Justification::centred);
    pitchLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    pitchLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    addAndMakeVisible(pitchLabel);

    // Fine tune: -100 .. +100 cents
    setupRotary(fineTuneKnob, -100.0, 100.0, 0.1, 0.0, " ct");
    fineTuneKnob.setValue(*audioProcessor.fineTuneParam, juce::dontSendNotification); // see speedKnob comment above
    fineTuneKnob.onValueChange = [this]
        {
            *audioProcessor.fineTuneParam = (float)fineTuneKnob.getValue();
            audioProcessor.clearPreview();
            playButton.setButtonText("Play");
        };
    addAndMakeVisible(fineTuneKnob);
    fineTuneLabel.setText("FINE TUNE", juce::dontSendNotification);
    fineTuneLabel.setJustificationType(juce::Justification::centred);
    fineTuneLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    fineTuneLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    addAndMakeVisible(fineTuneLabel);

    // Fade In: 0 .. 5 seconds, skewed so short fades (up to ~200ms) get
    // extra travel on the slider - matches the skew on the underlying
    // parameter (see fadeRange in PluginProcessor's constructor).
    setupLinear(fadeInSlider, 0.0, 5.0, 0.001, 0.0, " s");
    fadeInSlider.setSkewFactorFromMidPoint(0.2); // must come after setRange (setupLinear), which resets skew
    fadeInSlider.setValue(*audioProcessor.fadeInParam, juce::dontSendNotification); // see speedKnob comment above
    fadeInSlider.onValueChange = [this]
        {
            *audioProcessor.fadeInParam = (float)fadeInSlider.getValue();
            audioProcessor.clearPreview();
            playButton.setButtonText("Play");
        };
    addAndMakeVisible(fadeInSlider);
    fadeInLabel.setText("FADE IN", juce::dontSendNotification);
    fadeInLabel.setJustificationType(juce::Justification::centredLeft);
    fadeInLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    fadeInLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    addAndMakeVisible(fadeInLabel);

    // Fade Out: same range and skew as Fade In.
    setupLinear(fadeOutSlider, 0.0, 5.0, 0.001, 0.0, " s");
    fadeOutSlider.setSkewFactorFromMidPoint(0.2);
    fadeOutSlider.setValue(*audioProcessor.fadeOutParam, juce::dontSendNotification);
    fadeOutSlider.onValueChange = [this]
        {
            *audioProcessor.fadeOutParam = (float)fadeOutSlider.getValue();
            audioProcessor.clearPreview();
            playButton.setButtonText("Play");
        };
    addAndMakeVisible(fadeOutSlider);
    fadeOutLabel.setText("FADE OUT", juce::dontSendNotification);
    fadeOutLabel.setJustificationType(juce::Justification::centredLeft);
    fadeOutLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    fadeOutLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    addAndMakeVisible(fadeOutLabel);

    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setColour(juce::Label::textColourId, Sakura::textDim);
    statusLabel.setFont(13.0f);
    addAndMakeVisible(statusLabel);

    progressBar.setPercentageDisplay(true);
    addAndMakeVisible(progressBar);
    progressBar.setVisible(false);

    startTimerHz(10);

    setSize(400, 690);
    setResizable(true, false);
}

AudioStretcherAudioProcessorEditor::~AudioStretcherAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void AudioStretcherAudioProcessorEditor::togglePlay()
{
    if (!audioProcessor.hasAudioLoaded())
        return;

    if (audioProcessor.hasPreviewRendered())
    {
        audioProcessor.startPreview();
        return;
    }

    // Nothing rendered yet for the current selection/knob settings — render
    // in the background, then start playback automatically once it's ready.
    playButton.setEnabled(false);
    statusLabel.setText("Rendering...", juce::dontSendNotification);

    juce::Thread::launch([this]
        {
            audioProcessor.renderPreview();

            juce::MessageManager::callAsync([this]
                {
                    playButton.setEnabled(true);
                    if (audioProcessor.hasPreviewRendered())
                    {
                        audioProcessor.startPreview();
                        statusLabel.setText("Playing", juce::dontSendNotification);
                    }
                    else
                    {
                        statusLabel.setText("Nothing to play", juce::dontSendNotification);
                    }
                });
        });
}

void AudioStretcherAudioProcessorEditor::resetStretchControlsToDefault()
{
    // sendNotification (the default) makes each setValue() fire the
    // knob's onValueChange, which is what actually writes the value into
    // *audioProcessor.speedKnob/pitchKnob/fineTuneKnob's parameter - so the
    // engine is guaranteed to end up in sync with what the knobs show.
    speedKnob.setValue(1.0, juce::sendNotification);
    pitchKnob.setValue(0.0, juce::sendNotification);
    fineTuneKnob.setValue(0.0, juce::sendNotification);
    fadeInSlider.setValue(0.0, juce::sendNotification);
    fadeOutSlider.setValue(0.0, juce::sendNotification);
}

void AudioStretcherAudioProcessorEditor::saveSelection()
{
    if (!audioProcessor.hasAudioLoaded())
        return;

    // Save always exports whatever is currently selected on the waveform —
    // AudioProcessor::processAndExport already crops to regionStart/regionEnd.
    // The output extension mirrors the source file (WAV/AIFF/FLAC); MP3/OGG
    // sources fall back to FLAC since JUCE has no MP3 encoder to write with.
    bool matchingSource = audioProcessor.canExportInSourceFormat();
    juce::String ext = audioProcessor.getRecommendedExportExtension(); // includes the dot

    auto* chooser = new juce::FileChooser(
        "Save selection as...",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*" + ext);

    auto flags = juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser, ext, matchingSource](const juce::FileChooser& fc)
        {
            auto url = fc.getURLResult();
            if (!url.isEmpty())
            {
                // Only local files can have their extension fixed up by
                // rebuilding the path - a content:// URI (Android SAF) is an
                // opaque handle, not a string we can safely append to, and
                // the SAF "create document" dialog already applies the
                // extension from the *ext filter we passed in above.
                if (url.isLocalFile())
                {
                    auto file = url.getLocalFile();
                    if (!file.hasFileExtension(ext))
                        url = juce::URL(file.withFileExtension(ext));
                }

                isProcessing = true;
                progressValue = 0.0;
                progressBar.setVisible(true);
                statusLabel.setText("Processing selection...", juce::dontSendNotification);
                saveButton.setEnabled(false);
                loadButton.setEnabled(false);

                audioProcessor.setProgressCallback([this](float progress) { progressValue = progress; });
                audioProcessor.setStageCallback([this](const juce::String& stage) { progressStage = stage; });

                juce::Thread::launch([this, url, matchingSource]
                    {
                        audioProcessor.processAndExport(url);

                        juce::MessageManager::callAsync([this, url, matchingSource]
                            {
                                isProcessing = false;
                                progressBar.setVisible(false);
                                progressValue = 0.0;
                                progressStage.clear();
                                statusLabel.setText(matchingSource
                                    ? "Saved: " + url.getFileName()
                                    : "Saved as FLAC (MP3 encoding isn't supported): " + url.getFileName(),
                                    juce::dontSendNotification);
                                saveButton.setEnabled(true);
                                loadButton.setEnabled(true);
                                audioProcessor.setProgressCallback(nullptr);
                                audioProcessor.setStageCallback(nullptr);
                            });
                    });
            }
            delete chooser;
        });
}

void AudioStretcherAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient gradient(
        Sakura::bgTop, 0, 0,
        Sakura::bgBottom, 0, (float)getHeight(),
        false);
    g.setGradientFill(gradient);
    g.fillAll();

    juce::ColourGradient glow(
        Sakura::reddishGlow.withAlpha(0.12f), getWidth() * 0.5f, getHeight() * 0.15f,
        juce::Colour(0x00000000), getWidth() * 0.5f, getHeight() * 0.15f,
        true);
    glow.addColour(0.6, Sakura::blossom.withAlpha(0.05f));
    g.setGradientFill(glow);
    g.fillEllipse(-getWidth() * 0.2f, -getHeight() * 0.1f, getWidth() * 1.4f, getHeight() * 0.6f);

    if (progressBar.isVisible() && progressStage.isNotEmpty())
    {
        g.setColour(Sakura::textMain);
        g.setFont(13.0f);
        g.drawText(progressStage, progressBar.getBounds(), juce::Justification::centred);
    }
}

void AudioStretcherAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(14);

    titleLabel.setBounds(area.removeFromTop(34));
    area.removeFromTop(6);

    waveformDisplay.setBounds(area.removeFromTop(120));
    area.removeFromTop(6);

    fileLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);

    auto buttonRow = area.removeFromTop(46);
    loadButton.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth() / 2).reduced(6, 0));
    saveButton.setBounds(buttonRow.reduced(6, 0));
    area.removeFromTop(8);

    auto transportRow = area.removeFromTop(44);
    playButton.setBounds(transportRow.removeFromLeft(transportRow.getWidth() / 2).reduced(6, 0));
    pauseButton.setBounds(transportRow.reduced(6, 0));
    area.removeFromTop(8);

    auto utilityRow = area.removeFromTop(40);
    loopButton.setBounds(utilityRow.removeFromLeft(utilityRow.getWidth() / 2).reduced(6, 0));
    resetButton.setBounds(utilityRow.reduced(6, 0));
    area.removeFromTop(14);

    // Three knobs side by side
    auto knobRow = area.removeFromTop(140);
    int knobWidth = knobRow.getWidth() / 3;

    auto speedArea = knobRow.removeFromLeft(knobWidth);
    speedLabel.setBounds(speedArea.removeFromTop(18));
    speedKnob.setBounds(speedArea.reduced(8));

    auto pitchArea = knobRow.removeFromLeft(knobWidth);
    pitchLabel.setBounds(pitchArea.removeFromTop(18));
    pitchKnob.setBounds(pitchArea.reduced(8));

    auto fineArea = knobRow;
    fineTuneLabel.setBounds(fineArea.removeFromTop(18));
    fineTuneKnob.setBounds(fineArea.reduced(8));

    area.removeFromTop(12);

    // Two fade sliders, stacked, below the knobs, and the progress information
    auto fadeInRow = area.removeFromTop(40);
    fadeInLabel.setBounds(fadeInRow.removeFromLeft(80));
    fadeInSlider.setBounds(fadeInRow.reduced(4, 6));
    area.removeFromTop(6);
    auto fadeOutRow = area.removeFromTop(40);
    fadeOutLabel.setBounds(fadeOutRow.removeFromLeft(80));
    fadeOutSlider.setBounds(fadeOutRow.reduced(4, 6));
    area.removeFromTop(2);
    statusLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(4);
    progressBar.setBounds(area.removeFromTop(26).reduced(20, 0));
}

bool AudioStretcherAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& file : files)
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3") ||
            file.endsWithIgnoreCase(".flac") || file.endsWithIgnoreCase(".ogg") ||
            file.endsWithIgnoreCase(".aif") || file.endsWithIgnoreCase(".aiff"))
            return true;
    return false;
}

void AudioStretcherAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() > 0)
    {
        juce::File file(files[0]);
        if (file.existsAsFile())
        {
            audioProcessor.loadAudioFile(juce::URL(file));
            waveformDisplay.resetSelection();
            resetStretchControlsToDefault();
        }
    }
}

void AudioStretcherAudioProcessorEditor::timerCallback()
{
    if (audioProcessor.hasAudioLoaded())
    {
        fileLabel.setText(audioProcessor.getLoadedFileName() +
            "  •  " + juce::String(audioProcessor.getLoadedSampleRate() / 1000.0, 1) + " kHz" +
            "  •  " + juce::String(audioProcessor.getLoadedBitsPerSample()) + "-bit",
            juce::dontSendNotification);
        saveButton.setEnabled(!isProcessing);
        playButton.setEnabled(!isProcessing);
        pauseButton.setEnabled(!isProcessing && audioProcessor.isPreviewPlaying());
    }

    playButton.setButtonText(audioProcessor.isPreviewPlaying() ? "Playing..." : "Play");
}