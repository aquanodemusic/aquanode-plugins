#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
CepstralIREditor::CepstralIREditor(CepstralIRProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&laf);
    setSize(660, 520);
    setResizable(true, true);
    setResizeLimits(560, 440, 1200, 900);

    // -------------------------------------------------------------------------
    // Title
    titleLabel.setText("CEPSTRAL IR", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff006688));
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // -------------------------------------------------------------------------
    // Section labels
    sourceLabel.setText("SOURCE SAMPLE", juce::dontSendNotification);
    sourceLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    sourceLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00aa99));
    addAndMakeVisible(sourceLabel);

    irLabel.setText("EXTRACTED IMPULSE RESPONSE", juce::dontSendNotification);
    irLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    irLabel.setColour(juce::Label::textColourId, juce::Colour(0xff0088cc));
    addAndMakeVisible(irLabel);

    // -------------------------------------------------------------------------
    // Waveform displays
    addAndMakeVisible(sourceDisplay);
    addAndMakeVisible(irDisplay);

    // -------------------------------------------------------------------------
    // Buttons
    loadButton.onClick = [this]
        {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Choose an audio file",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");

            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
                [this, chooser](const juce::FileChooser& fc)
                {
                    if (fc.getResults().isEmpty()) return;
                    loadFile(fc.getResult());
                });
        };
    addAndMakeVisible(loadButton);

    saveIRButton.onClick = [this]
        {
            if (!processorRef.hasIRExtracted())
            {
                statusLabel.setText("Load a sample first!", juce::dontSendNotification);
                return;
            }

            auto chooser = std::make_shared<juce::FileChooser>(
                "Save Impulse Response",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("ImpulseResponse.wav"),
                "*.wav");

            chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles,
                [this, chooser](const juce::FileChooser& fc)
                {
                    if (fc.getResults().isEmpty()) return;
                    processorRef.saveImpulseResponse(fc.getResult());
                    statusLabel.setText(processorRef.getStatusMessage(), juce::dontSendNotification);
                });
        };
    addAndMakeVisible(saveIRButton);

    reprocessButton.onClick = [this] { triggerReprocess(); };
    addAndMakeVisible(reprocessButton);

    // -------------------------------------------------------------------------
    // Sliders
    auto setupSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& name,
        double min, double max, double val, const juce::String& suffix)
        {
            s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
            s.setRange(min, max);
            s.setValue(val);
            s.setTextValueSuffix(suffix);
            s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff003355));
            s.onValueChange = [this] { triggerReprocess(); };
            addAndMakeVisible(s);

            l.setText(name, juce::dontSendNotification);
            l.setFont(juce::Font(11.0f, juce::Font::bold));
            l.setColour(juce::Label::textColourId, juce::Colour(0xff0088cc));
            l.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(l);
        };

    setupSlider(irLengthSlider, irLenLabel, "IR LENGTH", 0.01, 1.0, 0.2, "");
    setupSlider(smoothingSlider, smoothLabel, "QUEFRENCY", 4.0, 2048.0, 128.0, "");
    setupSlider(fftSizeSlider, fftLabel, "FFT 2 ** n", 12, 20, 15, "");

    fftSizeSlider.setNumDecimalPlacesToDisplay(0);
    fftSizeSlider.onValueChange = [this] { triggerReprocess(); };

    // Window toggle
    windowToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff003355));
    windowToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00aa99));
    windowToggle.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xffb0b0b0));
    windowToggle.setToggleState(true, juce::dontSendNotification);
    windowToggle.onStateChange = [this] { triggerReprocess(); };
    addAndMakeVisible(windowToggle);

    // Linear phase toggle
    linearPhaseToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff003355));
    linearPhaseToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00aa99));
    linearPhaseToggle.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xffb0b0b0));
    linearPhaseToggle.setToggleState(false, juce::dontSendNotification);
    linearPhaseToggle.onStateChange = [this] { triggerReprocess(); };
    addAndMakeVisible(linearPhaseToggle);

    // -------------------------------------------------------------------------
    // Status label
    statusLabel.setText("Drop an audio file here, or click LOAD SAMPLE",
        juce::dontSendNotification);
    statusLabel.setFont(juce::Font(13.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff004466));
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    // -------------------------------------------------------------------------
    // Hint label
    hintLabel.setText("For good resolution: Highest possible FFT (very slow!), High Quefrency (256-512), IR whatever fits",
        juce::dontSendNotification);
    hintLabel.setFont(juce::Font(11.0f));
    hintLabel.setColour(juce::Label::textColourId, juce::Colour(0xff0088cc));
    hintLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(hintLabel);

    // -------------------------------------------------------------------------
    // Drag and drop
    setInterceptsMouseClicks(true, true);

    startTimerHz(10);
}

CepstralIREditor::~CepstralIREditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void CepstralIREditor::paint(juce::Graphics& g)
{
    // Background - bright off-white with cyan tint
    g.fillAll(juce::Colour(0xfff0f8ff));

    auto bounds = getLocalBounds();

    // Subtle grid texture - light cyan
    g.setColour(juce::Colour(0xffe0f0f8));
    for (int x = 0; x < bounds.getWidth(); x += 20)
        g.drawVerticalLine(x, 0.0f, (float)bounds.getHeight());
    for (int y = 0; y < bounds.getHeight(); y += 20)
        g.drawHorizontalLine(y, 0.0f, (float)bounds.getWidth());

    // Header gradient - light cyan
    juce::ColourGradient headerGrad(juce::Colour(0xffc0e8f0), 0, 0,
        juce::Colour(0xffd0f0f8), 0, 56, false);
    g.setGradientFill(headerGrad);
    g.fillRect(0, 0, bounds.getWidth(), 56);

    // Header accent line - bright cyan
    g.setColour(juce::Colour(0xff00cccc).withAlpha(0.8f));
    g.drawHorizontalLine(56, 0.0f, (float)bounds.getWidth());

    // Separator between source and IR sections
    int midY = 56 + 18 + 120 + 6;
    g.setColour(juce::Colour(0xffc0e0e8));
    g.drawHorizontalLine(midY, 12.0f, (float)bounds.getWidth() - 12.0f);

    // Drag-over overlay
    if (isDragOver)
    {
        g.setColour(juce::Colour(0x8800ffcc));
        g.fillAll();
        g.setColour(juce::Colour(0xff0088aa));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("DROP AUDIO FILE", bounds, juce::Justification::centred);
    }
}

//==============================================================================
void CepstralIREditor::resized()
{
    auto area = getLocalBounds().reduced(12, 0);

    // Header row
    auto headerArea = area.removeFromTop(56);
    headerArea.reduce(0, 12);
    titleLabel.setBounds(headerArea.removeFromLeft(180));

    // Button row
    auto btnArea = area.removeFromTop(36);
    loadButton.setBounds(btnArea.removeFromLeft(140).reduced(2, 0));
    reprocessButton.setBounds(btnArea.removeFromLeft(140).reduced(2, 0));
    saveIRButton.setBounds(btnArea.removeFromLeft(180).reduced(2, 0));

    area.removeFromTop(6);

    // Source waveform
    sourceLabel.setBounds(area.removeFromTop(14));
    sourceDisplay.setBounds(area.removeFromTop(110).reduced(0, 2));

    area.removeFromTop(8);

    // IR waveform
    irLabel.setBounds(area.removeFromTop(14));
    irDisplay.setBounds(area.removeFromTop(110).reduced(0, 2));

    area.removeFromTop(6);

    // Controls row
    auto controlArea = area.removeFromTop(100);
    int knobW = 90;

    auto sliderBounds = [&](juce::Slider& s, juce::Label& l)
        {
            auto cell = controlArea.removeFromLeft(knobW);
            l.setBounds(cell.removeFromTop(14));
            s.setBounds(cell);
        };

    sliderBounds(irLengthSlider, irLenLabel);
    sliderBounds(smoothingSlider, smoothLabel);
    sliderBounds(fftSizeSlider, fftLabel);

    // Toggles
    auto toggleArea = controlArea.removeFromLeft(180).withTrimmedTop(30);
    windowToggle.setBounds(toggleArea.removeFromTop(30));
    linearPhaseToggle.setBounds(toggleArea.removeFromTop(30));

    // Status label
    area.removeFromTop(4);
    statusLabel.setBounds(area.removeFromTop(24));

    // Hint label
    hintLabel.setBounds(area.removeFromTop(20));
}

//==============================================================================
void CepstralIREditor::timerCallback()
{
    juce::String current = processorRef.getStatusMessage();
    if (current != lastStatus)
    {
        lastStatus = current;
        statusLabel.setText(current, juce::dontSendNotification);

        if (processorRef.hasSourceLoaded())
            sourceDisplay.setBuffer(&processorRef.getSourceBuffer());
        if (processorRef.hasIRExtracted())
            irDisplay.setBuffer(&processorRef.getImpulseResponse());

        repaint();
    }
}

//==============================================================================
void CepstralIREditor::loadFile(const juce::File& f)
{
    // Sync parameters from UI to processor before loading
    *processorRef.irLengthParam = (float)irLengthSlider.getValue();
    *processorRef.smoothingParam = (float)smoothingSlider.getValue();
    *processorRef.applyWindowParam = windowToggle.getToggleState();
    *processorRef.fftSizeParam = (int)fftSizeSlider.getValue();
    *processorRef.linearPhaseParam = linearPhaseToggle.getToggleState();

    statusLabel.setText("Processing...", juce::dontSendNotification);
    processorRef.loadSampleFile(f);

    if (processorRef.hasSourceLoaded())
        sourceDisplay.setBuffer(&processorRef.getSourceBuffer());
    if (processorRef.hasIRExtracted())
        irDisplay.setBuffer(&processorRef.getImpulseResponse());

    statusLabel.setText(processorRef.getStatusMessage(), juce::dontSendNotification);
    repaint();
}

void CepstralIREditor::triggerReprocess()
{
    if (!processorRef.hasSourceLoaded()) return;

    *processorRef.irLengthParam = (float)irLengthSlider.getValue();
    *processorRef.smoothingParam = (float)smoothingSlider.getValue();
    *processorRef.applyWindowParam = windowToggle.getToggleState();
    *processorRef.fftSizeParam = (int)fftSizeSlider.getValue();
    *processorRef.linearPhaseParam = linearPhaseToggle.getToggleState();

    statusLabel.setText("Reprocessing...", juce::dontSendNotification);
    processorRef.reprocessIR();

    if (processorRef.hasIRExtracted())
        irDisplay.setBuffer(&processorRef.getImpulseResponse());

    statusLabel.setText(processorRef.getStatusMessage(), juce::dontSendNotification);
    repaint();
}

//==============================================================================
bool CepstralIREditor::isInterestedInDragSource(const SourceDetails& details)
{
    if (auto* desc = details.description.getArray())
    {
        for (auto& item : *desc)
        {
            juce::File f(item.toString());
            if (f.existsAsFile())
                return true;
        }
    }
    return true; // Accept anything, filter in itemDropped
}

void CepstralIREditor::itemDropped(const SourceDetails& details)
{
    isDragOver = false;

    if (auto* desc = details.description.getArray())
    {
        for (auto& item : *desc)
        {
            juce::File f(item.toString());
            if (f.existsAsFile())
            {
                loadFile(f);
                return;
            }
        }
    }

    // Try to get a file from the drag description directly
    juce::File f(details.description.toString());
    if (f.existsAsFile())
        loadFile(f);

    repaint();
}