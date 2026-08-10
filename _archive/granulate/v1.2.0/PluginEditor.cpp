#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a5555));
    
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
    
    // Try to get buffer access - if we can't (loading in progress), skip this frame
    auto& buffer = processor.getSampleBuffer();
    auto bounds = getLocalBounds().reduced(4);
    
    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    
    // Safety check
    if (numSamples == 0 || numChannels == 0)
        return;
    
    if (numSamples > 0)
    {
        juce::Path waveformPath;
        
        // Reduce samples to make waveform cleaner (less "blobby")
        int displayWidth = bounds.getWidth();
        int samplesPerPixel = juce::jmax(1, numSamples / (displayWidth * 2)); // More aggressive downsampling
        
        // Build waveform outline (no fill, just stroke)
        for (int x = 0; x < displayWidth; ++x)
        {
            // Use 64-bit math to prevent overflow with large files
            juce::int64 startSample64 = ((juce::int64)x * (juce::int64)numSamples) / (juce::int64)displayWidth;
            juce::int64 endSample64 = ((juce::int64)(x + 1) * (juce::int64)numSamples) / (juce::int64)displayWidth;
            
            int startSample = (int)juce::jlimit((juce::int64)0, (juce::int64)numSamples - 1, startSample64);
            int endSample = (int)juce::jlimit((juce::int64)0, (juce::int64)numSamples - 1, endSample64);
            
            float maxVal = 0.0f;
            float minVal = 0.0f;
            
            // Sample at intervals for cleaner look
            for (int s = startSample; s < endSample; s += samplesPerPixel)
            {
                if (s >= numSamples) break;
                
                float sample = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    // Bounds check for safety
                    if (s >= 0 && s < buffer.getNumSamples())
                        sample += buffer.getReadPointer(ch)[s];
                }
                sample /= (float)numChannels;
                
                maxVal = juce::jmax(maxVal, sample);
                minVal = juce::jmin(minVal, sample);
            }
            
            float centerY = bounds.getCentreY();
            float maxY = centerY - maxVal * bounds.getHeight() * 0.45f;
            float minY = centerY - minVal * bounds.getHeight() * 0.45f;
            
            if (x == 0)
                waveformPath.startNewSubPath(bounds.getX() + x, maxY);
            else
                waveformPath.lineTo(bounds.getX() + x, maxY);
        }
        
        // Bottom half
        for (int x = displayWidth - 1; x >= 0; --x)
        {
            // Use 64-bit math to prevent overflow
            juce::int64 startSample64 = ((juce::int64)x * (juce::int64)numSamples) / (juce::int64)displayWidth;
            juce::int64 endSample64 = ((juce::int64)(x + 1) * (juce::int64)numSamples) / (juce::int64)displayWidth;
            
            int startSample = (int)juce::jlimit((juce::int64)0, (juce::int64)numSamples - 1, startSample64);
            int endSample = (int)juce::jlimit((juce::int64)0, (juce::int64)numSamples - 1, endSample64);
            
            float minVal = 0.0f;
            
            for (int s = startSample; s < endSample; s += samplesPerPixel)
            {
                if (s >= numSamples) break;
                
                float sample = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    // Bounds check for safety
                    if (s >= 0 && s < buffer.getNumSamples())
                        sample += buffer.getReadPointer(ch)[s];
                }
                sample /= (float)numChannels;
                
                minVal = juce::jmin(minVal, sample);
            }
            
            float centerY = bounds.getCentreY();
            float minY = centerY - minVal * bounds.getHeight() * 0.45f;
            
            waveformPath.lineTo(bounds.getX() + x, minY);
        }
        
        waveformPath.closeSubPath();
        
        // Cyan waveform
        g.setColour(juce::Colours::cyan.withAlpha(0.7f));
        g.fillPath(waveformPath);
        
        // Cyan outline for definition
        g.setColour(juce::Colours::cyan);
        g.strokePath(waveformPath, juce::PathStrokeType(1.0f));
        
        // Draw window region
        float position = processor.getValueTreeState().getRawParameterValue("grainPosition")->load();
        float windowSize = processor.getValueTreeState().getRawParameterValue("windowSize")->load();
        
        int windowStartX = bounds.getX() + (int)(position * bounds.getWidth());
        int windowWidth = (int)(windowSize * bounds.getWidth());
        
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.fillRect(windowStartX, bounds.getY(), windowWidth, bounds.getHeight());
        
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawRect(windowStartX, bounds.getY(), windowWidth, bounds.getHeight(), 2);
        
        // Draw position marker
        g.setColour(juce::Colours::yellow.withAlpha(0.8f));
        g.drawLine(windowStartX, bounds.getY(), windowStartX, bounds.getBottom(), 2.0f);
        
        // Draw active grain playheads
        const auto activeGrains = processor.getActiveGrains();

        for (const auto& grain : activeGrains)
        {
            // Use double precision to avoid overflow with large sample counts
            double normalizedPos = grain.samplePosition / (double)numSamples;
            int playheadX = bounds.getX() + (int)(normalizedPos * bounds.getWidth());

            float brightness = 1.0f - grain.grainEnvelopePhase;

            g.setColour(juce::Colours::lime.withAlpha(0.7f * brightness));
            g.drawLine(playheadX, bounds.getY(), playheadX, bounds.getBottom(), 1.5f);
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
    if (processor.hasSample())
    {
        float position = juce::jlimit(0.0f, 1.0f, (float)event.x / getWidth());
        processor.setMousePosition(position);
        repaint();
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    if (processor.hasSample())
    {
        float position = juce::jlimit(0.0f, 1.0f, (float)event.x / getWidth());
        processor.setMousePosition(position);
        repaint();
    }
}

void WaveformDisplay::mouseUp(const juce::MouseEvent& event)
{
    processor.releaseMousePosition();
}

//==============================================================================
// GranulateAudioProcessorEditor Implementation
//==============================================================================

GranulateAudioProcessorEditor::GranulateAudioProcessorEditor (GranulateAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), waveformDisplay(p)
{
    setSize (770, 420);  // Taller to accommodate both ADSR sections
    
    addAndMakeVisible(waveformDisplay);
    
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
                    auto file = fc.getResult();
                    if (file.existsAsFile())
                    {
                        audioProcessor.loadSample(file);
                        waveformDisplay.repaint();
                    }
                });
        };
    
    // Setup slider helper function with closer label positioning
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, 
                              const juce::String& labelText,
                              const juce::String& paramID,
                              std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        
        addAndMakeVisible(label);
        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(11.0f));
        label.attachToComponent(&slider, false);
        
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getValueTreeState(), paramID, slider);
    };
    
    // Grain controls
    setupSlider(numGrainsSlider, numGrainsLabel, "Grains", "numGrains", numGrainsAttachment);
    setupSlider(grainSizeSlider, grainSizeLabel, "Size", "grainSize", grainSizeAttachment);
    setupSlider(grainPositionSlider, grainPositionLabel, "Position", "grainPosition", grainPositionAttachment);
    setupSlider(spraySlider, sprayLabel, "Spray", "spray", sprayAttachment);
    setupSlider(windowSizeSlider, windowSizeLabel, "Window", "windowSize", windowSizeAttachment);
    
    // Grain ADSR
    setupSlider(grainAttackSlider, grainAttackLabel, "G-Attack", "grainAttack", grainAttackAttachment);
    setupSlider(grainDecaySlider, grainDecayLabel, "G-Decay", "grainDecay", grainDecayAttachment);
    setupSlider(grainSustainSlider, grainSustainLabel, "G-Sustain", "grainSustain", grainSustainAttachment);
    setupSlider(grainReleaseSlider, grainReleaseLabel, "G-Release", "grainRelease", grainReleaseAttachment);
    
    // Note ADSR
    setupSlider(noteAttackSlider, noteAttackLabel, "N-Attack", "noteAttack", noteAttackAttachment);
    setupSlider(noteDecaySlider, noteDecayLabel, "N-Decay", "noteDecay", noteDecayAttachment);
    setupSlider(noteSustainSlider, noteSustainLabel, "N-Sustain", "noteSustain", noteSustainAttachment);
    setupSlider(noteReleaseSlider, noteReleaseLabel, "N-Release", "noteRelease", noteReleaseAttachment);
    
    // Modulation controls
    setupSlider(amplitudeModSlider, amplitudeModLabel, "AM", "amplitudeMod", amplitudeModAttachment);
    setupSlider(amDispersionSlider, amDispersionLabel, "AM Disp", "amDispersion", amDispersionAttachment);
    setupSlider(pitchDispersionSlider, pitchDispersionLabel, "Pitch Disp", "pitchDispersion", pitchDispersionAttachment);
    setupSlider(pitchSlider, pitchLabel, "Pitch (Mouse)", "pitch", pitchAttachment);
    setupSlider(stereoSpreadSlider, stereoSpreadLabel, "Stereo", "stereoSpread", stereoSpreadAttachment);
    setupSlider(volumeSlider, volumeLabel, "Volume", "volume", volumeAttachment);
    
    startTimerHz(30);
}

GranulateAudioProcessorEditor::~GranulateAudioProcessorEditor()
{
    stopTimer();
}

void GranulateAudioProcessorEditor::timerCallback()
{
    waveformDisplay.repaint();
}

void GranulateAudioProcessorEditor::paint(juce::Graphics& g)
{
    // =========================
    // Background
    // =========================
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient background(
        juce::Colours::silver,
        r.getTopLeft(),
        juce::Colours::cyan,
        r.getBottomRight(),
        false
    );

    g.setGradientFill(background);
    g.fillAll();

    // =========================
    // Gloss overlay
    // =========================
    auto gloss = r.withHeight(r.getHeight() * 0.35f);

    juce::ColourGradient glossGradient(
        juce::Colours::white.withAlpha(0.35f),
        gloss.getTopLeft(),
        juce::Colours::white.withAlpha(0.05f),
        gloss.getBottomLeft(),
        false
    );

    g.setGradientFill(glossGradient);
    g.fillRect(gloss);

    // =========================
    // Title text
    // =========================
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);

    g.drawText(
        "Granulate by aquanode                           Play MIDI note for resampling or click on the waveform",
        15, 10, 750, 30,
        juce::Justification::left
    );

    // =========================
    // G / N hint text   centered in the layout gap
    // =========================
    const int xStart = 20;
    const int spacingX = 75;

    // Gap begins after: Grains, Size, Position, Spray, Window (5 knobs)
    int gapStartX = xStart + (5 * spacingX);
    int gapWidth = 2 * spacingX;

    // Text box centered inside the gap
    int textWidth = 150;
    int textX = gapStartX + (gapWidth - textWidth) / 2;
    int textY = 160; // vertically aligned with first knob row

    g.setFont(12.0f);

    g.drawText(
        "G = per Grain",
        textX - 50, textY + 25,
        textWidth, 40,
        juce::Justification::centred
    );
    g.drawText(
        "N = per Note",
        textX - 52, textY + 45,
        textWidth, 40,
        juce::Justification::centred
    );
   
    // =========================
    // Inner bevel
    // =========================
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(b, 6.0f, 1.0f);

        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawRoundedRectangle(b.reduced(1.0f), 6.0f, 1.0f);
    }

    // =========================
    // Inner bevel around controls section
    // =========================
    {
        const int controlsTop = 160;
        const int controlsBottom = getHeight() - 10;

        auto controlBounds = juce::Rectangle<float>(
            7.0f,
            (float)controlsTop,
            (float)getWidth() - 20.0f,
            (float)(controlsBottom - controlsTop - 10.0f)
        ).reduced(2.0f);

        // Top/left highlight
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawRoundedRectangle(controlBounds, 8.0f, 1.0f);

        // Bottom/right shadow
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.drawRoundedRectangle(controlBounds.reduced(1.5f), 8.0f, 1.0f);
    }
}


void GranulateAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // =========================
    // TOP: waveform
    // =========================
    auto topSection = bounds.removeFromTop(150);
    topSection.removeFromTop(40);
    waveformDisplay.setBounds(topSection.reduced(10));

    loadButton.setBounds(212, 13, 100, 25);

    // =========================
    // KNOB GRID
    // =========================
    const int xStart = 12;
    const int knobSize = 65;
    const int spacingX = 75;
    const int spacingY = 95;
    const int labelGap = 15;

    int x = xStart;
    int y = 170;

    // ---------- ROW 1 ----------
    numGrainsSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainSizeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainPositionSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    spraySlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    windowSizeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += 2 * spacingX;

    grainAttackSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainDecaySlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainSustainSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainReleaseSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20);

    // ---------- ROW 2 ----------
    x = xStart;
    y += spacingY + 10;

    amplitudeModSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    amDispersionSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    pitchDispersionSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    pitchSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    stereoSpreadSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    volumeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;

    noteAttackSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    noteDecaySlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    noteSustainSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    noteReleaseSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20);
}
