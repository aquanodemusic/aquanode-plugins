#include "PluginProcessor.h"
#include "PluginEditor.h"

AudioStretcherAudioProcessorEditor::AudioStretcherAudioProcessorEditor(AudioStretcherAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), progressBar(progressValue), waveformDisplay(p)
{
    // Apply custom look and feel
    setLookAndFeel(&purpleLookAndFeel);

    // Setup title
    titleLabel.setText("Audio Stretcher (using RubberBand by breakfastquay)", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    // Preview button (top right)
    previewButton.setButtonText("Preview");
    previewButton.setEnabled(false);
    previewButton.onClick = [this] {
        if (audioProcessor.isPreviewPlaying())
        {
            audioProcessor.stopPreview();
            previewButton.setButtonText("Preview");
        }
        else if (audioProcessor.hasPreviewRendered())
        {
            audioProcessor.startPreview();
            previewButton.setButtonText("Stop");
        }
        else
        {
            // Need to render first
            previewButton.setEnabled(false);
            progressBar.setVisible(true);
            progressValue = 0.0;
            statusLabel.setText("Rendering preview...", juce::dontSendNotification);

            // Capture current values
            float currentPitch = (float)pitchSlider.getValue();
            float currentStretch = 1.0f / (float)timeStretchSlider.getValue();
            bool currentUseNaive = useNaiveMethodToggle.getToggleState();

            audioProcessor.setProgressCallback([this](float progress) {
                progressValue = progress;
                });

            audioProcessor.setStageCallback([this](const juce::String& stage) {
                progressStage = stage;
                });

            *audioProcessor.pitchShiftParam = currentPitch;
            *audioProcessor.timeStretchParam = currentStretch;
            *audioProcessor.useNaiveMethodParam = currentUseNaive;

            juce::Thread::launch([this]() {
                audioProcessor.renderPreview();

                juce::MessageManager::callAsync([this]() {
                    progressBar.setVisible(false);
                    progressValue = 0.0;
                    statusLabel.setText("Preview ready", juce::dontSendNotification);
                    previewButton.setEnabled(true);
                    previewButton.setButtonText("Play");
                    audioProcessor.setProgressCallback(nullptr);
                    audioProcessor.setStageCallback(nullptr);
                    });
                });
        }
        };
    addAndMakeVisible(previewButton);

    // Waveform display
    addAndMakeVisible(waveformDisplay);

    // File info
    fileLabel.setText("No file loaded. Drag and drop or click Load.", juce::dontSendNotification);
    fileLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(fileLabel);

    // Load button
    loadButton.setButtonText("Load Audio File");
    loadButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select an audio file...",
            juce::File(),
            "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff");

        auto flags = juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                audioProcessor.loadAudioFile(file);
                waveformDisplay.repaint();
            }
            });
        };
    addAndMakeVisible(loadButton);

    // Pitch controls
    pitchLabel.setText("Pitch Shift (Semitones):", juce::dontSendNotification);
    addAndMakeVisible(pitchLabel);

    pitchSlider.setRange(-36.0, 36.0, 0.01);
    pitchSlider.setValue(0.0);
    pitchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    pitchSlider.setTextValueSuffix(" st");
    pitchSlider.onValueChange = [this] {
        audioProcessor.clearPreview();
        previewButton.setButtonText("Preview");
        };
    addAndMakeVisible(pitchSlider);

    // Time stretch controls
    timeStretchLabel.setText("Playback Speed:", juce::dontSendNotification);
    addAndMakeVisible(timeStretchLabel);

    timeStretchSlider.setRange(0.1, 10.0, 0.01);
    timeStretchSlider.setValue(1.0);
    timeStretchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timeStretchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    timeStretchSlider.setTextValueSuffix("x");
    timeStretchSlider.onValueChange = [this] {
        audioProcessor.clearPreview();
        previewButton.setButtonText("Preview");
        };
    addAndMakeVisible(timeStretchSlider);

    // Naive method checkbox
    useNaiveMethodToggle.setButtonText("Use Naive Method (Not recommended as it does not sound correct)");
    useNaiveMethodToggle.onClick = [this] {
        // Update parameter when clicked
        *audioProcessor.useNaiveMethodParam = useNaiveMethodToggle.getToggleState();
        audioProcessor.clearPreview();
        previewButton.setButtonText("Preview");
        };
    addAndMakeVisible(useNaiveMethodToggle);

    // Export buttons
    exportFlacButton.setButtonText("Export FLAC 24-bit");
    exportFlacButton.setEnabled(false);
    exportFlacButton.onClick = [this] { exportAudio(AudioStretcherAudioProcessor::ExportFormat::FLAC24); };
    addAndMakeVisible(exportFlacButton);

    exportMp3Button.setButtonText("Export FLAC 16-bit");
    exportMp3Button.setEnabled(false);
    exportMp3Button.onClick = [this] { exportAudio(AudioStretcherAudioProcessor::ExportFormat::FLAC16); };
    addAndMakeVisible(exportMp3Button);

    // Status label
    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    // Progress bar
    progressBar.setPercentageDisplay(true);
    addAndMakeVisible(progressBar);
    progressBar.setVisible(false);

    // Start timer for UI updates
    startTimerHz(10);

    setSize(600, 560);
}

void AudioStretcherAudioProcessorEditor::exportAudio(AudioStretcherAudioProcessor::ExportFormat format)
{
    juce::String extension = "*.flac";
    juce::String description = (format == AudioStretcherAudioProcessor::ExportFormat::FLAC24)
        ? "Save as FLAC 24-bit..." : "Save as FLAC 16-bit...";

    auto* chooser = new juce::FileChooser(
        description,
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        extension);

    auto flags = juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles;

    // Capture current slider values and checkbox state
    float currentPitch = (float)pitchSlider.getValue();
    float currentStretch = 1.0f / (float)timeStretchSlider.getValue();
    bool currentUseNaive = useNaiveMethodToggle.getToggleState();

    chooser->launchAsync(flags, [this, chooser, currentPitch, currentStretch, currentUseNaive, format](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File())
        {
            if (!file.hasFileExtension(".flac"))
                file = file.withFileExtension(".flac");

            isProcessing = true;
            progressValue = 0.0;
            progressBar.setVisible(true);
            statusLabel.setText("Processing...", juce::dontSendNotification);
            exportFlacButton.setEnabled(false);
            exportMp3Button.setEnabled(false);
            previewButton.setEnabled(false);

            audioProcessor.setProgressCallback([this](float progress) {
                progressValue = progress;
                });

            audioProcessor.setStageCallback([this](const juce::String& stage) {
                progressStage = stage;
                });

            *audioProcessor.pitchShiftParam = currentPitch;
            *audioProcessor.timeStretchParam = currentStretch;
            *audioProcessor.useNaiveMethodParam = currentUseNaive;

            juce::Thread::launch([this, file, format]() {
                audioProcessor.processAndExport(file, format);

                juce::MessageManager::callAsync([this, file]() {
                    isProcessing = false;
                    progressBar.setVisible(false);
                    progressValue = 0.0;
                    progressStage = "Ready";
                    statusLabel.setText("Export complete: " + file.getFileName(), juce::dontSendNotification);
                    exportFlacButton.setEnabled(true);
                    exportMp3Button.setEnabled(true);
                    previewButton.setEnabled(true);
                    audioProcessor.setProgressCallback(nullptr);
                    audioProcessor.setStageCallback(nullptr);
                    });
                });
        }
        delete chooser;
        });
}

AudioStretcherAudioProcessorEditor::~AudioStretcherAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void AudioStretcherAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient gradient(
        juce::Colour(0xffff00ff), 0, 0,
        juce::Colour(0xff3c096c), 0, (float)getHeight(),
        false
    );
    gradient.addColour(0.5, juce::Colour(0xff550099));
    g.setGradientFill(gradient);
    g.fillAll();

    juce::ColourGradient radialGlow(
        juce::Colour(0xffc77dff).withAlpha(0.1f), getWidth() * 0.5f, getHeight() * 0.3f,
        juce::Colour(0x00000000), getWidth() * 0.5f, getHeight() * 0.3f,
        true
    );
    radialGlow.addColour(0.0, juce::Colour(0xffc77dff).withAlpha(0.15f));
    radialGlow.addColour(0.6, juce::Colour(0xff9d4edd).withAlpha(0.05f));
    radialGlow.addColour(1.0, juce::Colour(0x00000000));
    g.setGradientFill(radialGlow);
    g.fillEllipse(getWidth() * 0.2f, -50, getWidth() * 0.6f, getHeight() * 0.8f);

    if (progressBar.isVisible() && progressStage.isNotEmpty())
    {
        auto progressBounds = progressBar.getBounds();
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(progressStage, progressBounds, juce::Justification::centred);
    }
}

void AudioStretcherAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    auto topRow = area.removeFromTop(40);
    titleLabel.setBounds(topRow.removeFromLeft(300));
    previewButton.setBounds(topRow.removeFromRight(160));
    area.removeFromTop(10);

    waveformDisplay.setBounds(area.removeFromTop(80));
    area.removeFromTop(10);

    fileLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    loadButton.setBounds(area.removeFromTop(40).reduced(100, 0));
    area.removeFromTop(20);

    pitchLabel.setBounds(area.removeFromTop(25));
    pitchSlider.setBounds(area.removeFromTop(30).reduced(0, 2));
    area.removeFromTop(20);

    timeStretchLabel.setBounds(area.removeFromTop(25));
    timeStretchSlider.setBounds(area.removeFromTop(30).reduced(0, 2));
    area.removeFromTop(20);

    useNaiveMethodToggle.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    auto exportRow = area.removeFromTop(40);
    exportFlacButton.setBounds(exportRow.removeFromLeft(exportRow.getWidth() / 2).reduced(5, 0));
    exportMp3Button.setBounds(exportRow.reduced(5, 0));
    area.removeFromTop(10);

    progressBar.setBounds(area.removeFromTop(40).reduced(30, 0));
    area.removeFromTop(10);

    statusLabel.setBounds(area.removeFromTop(30));
}

bool AudioStretcherAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav") ||
            file.endsWithIgnoreCase(".mp3") ||
            file.endsWithIgnoreCase(".flac") ||
            file.endsWithIgnoreCase(".ogg") ||
            file.endsWithIgnoreCase(".aif") ||
            file.endsWithIgnoreCase(".aiff"))
        {
            return true;
        }
    }
    return false;
}

void AudioStretcherAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.size() > 0)
    {
        juce::File file(files[0]);
        if (file.existsAsFile())
        {
            audioProcessor.loadAudioFile(file);
            waveformDisplay.repaint();
        }
    }
}

void AudioStretcherAudioProcessorEditor::timerCallback()
{
    if (audioProcessor.hasAudioLoaded())
    {
        fileLabel.setText(audioProcessor.getLoadedFileName(), juce::dontSendNotification);
        exportFlacButton.setEnabled(true);
        exportMp3Button.setEnabled(true);
        previewButton.setEnabled(true);

        if (statusLabel.getText() == "Ready")
        {
            statusLabel.setText("File loaded: " + juce::String(audioProcessor.getLoadedSampleCount()) +
                " samples at " + juce::String(audioProcessor.getLoadedSampleRate()) + " Hz",
                juce::dontSendNotification);
        }
    }

    if (audioProcessor.isPreviewPlaying())
    {
        previewButton.setButtonText("Stop");
    }
    else if (audioProcessor.hasPreviewRendered() && previewButton.getButtonText() != "Play")
    {
        previewButton.setButtonText("Play");
    }
}