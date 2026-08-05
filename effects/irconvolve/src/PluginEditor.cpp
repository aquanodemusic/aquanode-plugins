#include "PluginProcessor.h"
#include "PluginEditor.h"

class IRVisualizer : public juce::Component
{
public:
    IRVisualizer()
    {
        // Initialize with empty buffer
        displayBuffer.setSize(1, 1);
        displayBuffer.clear();
    }
    
    void updateBuffer(juce::AudioBuffer<float>&& newBuffer)
    {
        if (newBuffer.getNumChannels() > 0 && newBuffer.getNumSamples() > 0)
        {
            displayBuffer = std::move(newBuffer);
            shouldUpdateWaveform = true;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);

        if (shouldUpdateWaveform && getWidth() > 0 && getHeight() > 0)
        {
            updateWaveform();
            shouldUpdateWaveform = false;
        }

        g.setColour(juce::Colour(0xdd00ffff));
        g.strokePath(waveformPath, juce::PathStrokeType(1.5f));
    }

    void resized() override
    {
        shouldUpdateWaveform = true;
        repaint();
    }

private:
    void updateWaveform()
    {
        waveformPath.clear();

        const int numSamples = displayBuffer.getNumSamples();
        const int numChannels = displayBuffer.getNumChannels();
        
        if (numSamples <= 0 || numChannels <= 0 || getWidth() <= 0 || getHeight() <= 0)
            return;

        const float ratio = static_cast<float>(numSamples) / static_cast<float>(getWidth());
        const float* bufferData = displayBuffer.getReadPointer(0);

        waveformPath.startNewSubPath(0.0f, static_cast<float>(getHeight()) * 0.5f);

        for (int x = 0; x < getWidth(); ++x)
        {
            const int sampleIdx = juce::jlimit(0, numSamples - 1, static_cast<int>(x * ratio));
            const float sample = juce::jlimit(-1.0f, 1.0f, bufferData[sampleIdx]);
            const float y = juce::jmap(sample, -1.0f, 1.0f, static_cast<float>(getHeight()), 0.0f);
            
            waveformPath.lineTo(static_cast<float>(x), y);
        }
    }

    juce::Path waveformPath;
    juce::AudioBuffer<float> displayBuffer;
    bool shouldUpdateWaveform = false;
};

//==============================================================================

IRConvolverAudioProcessorEditor::IRConvolverAudioProcessorEditor(IRConvolverAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Load button
    loadButton.setButtonText("Load IR");
    loadButton.onClick = [this] { loadButtonClicked(); };
    addAndMakeVisible(loadButton);

    // IR label
    irLabel.setJustificationType(juce::Justification::centred);
    irLabel.setColour(juce::Label::backgroundColourId, juce::Colours::darkgrey);
    irLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(irLabel);

    // IR Visualizer
    irVisualizer = std::make_unique<IRVisualizer>();
    addAndMakeVisible(*irVisualizer);

    // Mix slider
    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(mixLabel);

    mixSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    mixSlider.setRange(0.0, 1.0, 0.01);
    mixSlider.setValue(*audioProcessor.getMixParameter());
    mixSlider.onValueChange = [this]
        {
            audioProcessor.getMixParameter()->setValueNotifyingHost(
                static_cast<float>(mixSlider.getValue()));
        };
    addAndMakeVisible(mixSlider);

    // Gain slider
    gainLabel.setText("Gain (dB)", juce::dontSendNotification);
    gainLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gainLabel);

    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    gainSlider.setRange(-24.0, 12.0, 0.1);
    gainSlider.setValue(*audioProcessor.getGainParameter());
    gainSlider.onValueChange = [this]
        {
            auto* param = audioProcessor.getGainParameter();
            float normalizedValue = param->getNormalisableRange().convertTo0to1(
                static_cast<float>(gainSlider.getValue()));
            param->setValueNotifyingHost(normalizedValue);
        };
    addAndMakeVisible(gainSlider);

    // Initial IR display update
    updateIRDisplay();

    startTimerHz(30);

    setSize(500, 400);
}

IRConvolverAudioProcessorEditor::~IRConvolverAudioProcessorEditor()
{
    stopTimer();
}

void IRConvolverAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff00cccc));

    g.setColour(juce::Colours::white);
    g.setFont(24.0f);
    g.drawText("Simple IR Convolver", getLocalBounds().removeFromTop(50),
        juce::Justification::centred, true);
}

void IRConvolverAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    area.removeFromTop(50); // Title space

    loadButton.setBounds(area.removeFromTop(40).reduced(100, 0));
    area.removeFromTop(10);

    irLabel.setBounds(area.removeFromTop(30).reduced(20, 0));
    area.removeFromTop(10);

    irVisualizer->setBounds(area.removeFromTop(80).reduced(10, 0));
    area.removeFromTop(20);

    auto controlsArea = area.removeFromTop(120);
    auto mixArea = controlsArea.removeFromLeft(getWidth() / 2);
    auto gainArea = controlsArea;

    mixLabel.setBounds(mixArea.removeFromTop(20));
    mixSlider.setBounds(mixArea.reduced(40, 0));

    gainLabel.setBounds(gainArea.removeFromTop(20));
    gainSlider.setBounds(gainArea.reduced(40, 0));
}

void IRConvolverAudioProcessorEditor::timerCallback()
{
    // Update sliders from processor
    mixSlider.setValue(*audioProcessor.getMixParameter(), juce::dontSendNotification);
    gainSlider.setValue(*audioProcessor.getGainParameter(), juce::dontSendNotification);

    // Check if IR needs updating
    if (needsIRUpdate)
    {
        updateIRDisplay();
        needsIRUpdate = false;
    }
}

void IRConvolverAudioProcessorEditor::loadButtonClicked()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select an Impulse Response file...",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aif;*.aiff;*.mp3;*.flac");

    auto chooserFlags = juce::FileBrowserComponent::openMode | 
                       juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File{})
            {
                audioProcessor.loadImpulseResponse(file);
                needsIRUpdate = true;
            }
        });
}

void IRConvolverAudioProcessorEditor::updateIRDisplay()
{
    if (audioProcessor.isIRLoaded())
    {
        juce::String irName = audioProcessor.getCurrentIRName();
        juce::File irFile(irName);
        irLabel.setText(irFile.getFileName(), juce::dontSendNotification);

        // Get IR buffer copy (thread-safe)
        juce::AudioBuffer<float> irBufferCopy;
        audioProcessor.copyIRBufferTo(irBufferCopy);
        
        // Move buffer to visualizer
        irVisualizer->updateBuffer(std::move(irBufferCopy));
    }
    else
    {
        irLabel.setText("No IR loaded", juce::dontSendNotification);
    }
}
