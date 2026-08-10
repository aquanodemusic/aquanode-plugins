#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
// PhismOscLookAndFeel
//==============================================================================
PhismOscLookAndFeel::PhismOscLookAndFeel()
{
    setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.7f));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x33000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a1a6e));
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff6ec7));
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1a0a2e));
    setColour(juce::TextEditor::textColourId, juce::Colour(0xff6ec6ff));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff6040a0));
}

void PhismOscLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos,
    float startAngle, float endAngle,
    juce::Slider& /*slider*/)
{
    auto bounds = juce::Rectangle<float>((float)x, (float)y,
        (float)width, (float)height).reduced(4.0f);
    float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    float r = bounds.getWidth() * 0.5f;

    // Outer glow ring
    juce::ColourGradient glowGrad(accent1.withAlpha(0.35f), cx, cy,
        accent2.withAlpha(0.0f), cx + r, cy, true);
    g.setGradientFill(glowGrad);
    g.fillEllipse(bounds.expanded(4.0f));

    // Rim gradient (pink to blue depending on position)
    juce::Colour rimCol = accent1.interpolatedWith(accent2, sliderPos);
    g.setColour(rimCol);
    g.drawEllipse(bounds, 2.0f);

    // Knob body
    juce::ColourGradient bodyGrad(knobFace.brighter(0.15f), cx - r * 0.3f, cy - r * 0.4f,
        knobFace.darker(0.3f), cx + r * 0.3f, cy + r * 0.4f, true);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(bounds.reduced(2.0f));

    // Pointer line
    float angle = startAngle + sliderPos * (endAngle - startAngle);
    float px = cx + (r - 6.0f) * std::sin(angle);
    float py = cy - (r - 6.0f) * std::cos(angle);

    juce::Path pointer;
    pointer.startNewSubPath(cx + (r * 0.15f) * std::sin(angle),
        cy - (r * 0.15f) * std::cos(angle));
    pointer.lineTo(px, py);

    g.setColour(rimCol.brighter(0.5f));
    g.strokePath(pointer, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // Dot at tip
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.fillEllipse(px - 2.5f, py - 2.5f, 5.0f, 5.0f);

    // Arc track
    juce::Path arc;
    arc.addCentredArc(cx, cy, r + 3.0f, r + 3.0f, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.strokePath(arc, juce::PathStrokeType(1.5f));

    // Filled arc
    juce::Path arcFill;
    arcFill.addCentredArc(cx, cy, r + 3.0f, r + 3.0f, 0.0f,
        startAngle, startAngle + sliderPos * (endAngle - startAngle), true);
    g.setColour(rimCol.withAlpha(0.6f));
    g.strokePath(arcFill, juce::PathStrokeType(2.5f));
}

void PhismOscLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.fillAll(juce::Colour(0x00000000));
    auto text = label.getText();
    auto font = getLabelFont(label);
    g.setFont(font);
    g.setColour(label.findColour(juce::Label::textColourId));
    g.drawFittedText(text, label.getLocalBounds(),
        label.getJustificationType(),
        juce::jmax(1, (int)((float)label.getHeight() / font.getHeight())));
}

juce::Font PhismOscLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions().withHeight(11.0f));
}

//==============================================================================
// PhismOscKnob
//==============================================================================
PhismOscKnob::PhismOscKnob(const juce::String& labelText,
    juce::AudioProcessorValueTreeState& apvts,
    const juce::String& paramID,
    PhismOscLookAndFeel& laf)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
    slider.setLookAndFeel(&laf);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setLookAndFeel(&laf);
    addAndMakeVisible(label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (apvts, paramID, slider);
}

void PhismOscKnob::resized()
{
    auto b = getLocalBounds();
    label.setBounds(b.removeFromBottom(16));
    slider.setBounds(b);
}

//==============================================================================
// WavetableDisplay
//==============================================================================
WavetableDisplay::WavetableDisplay(TranswaveAudioProcessor& p) : proc(p)
{
    startTimerHz(30);
}

WavetableDisplay::~WavetableDisplay()
{
    stopTimer();
}

void WavetableDisplay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Background
    juce::ColourGradient bg(juce::Colour(0xff0d0520), b.getTopLeft(),
        juce::Colour(0xff051030), b.getBottomRight(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(b, 6.0f);

    // Border
    g.setColour(juce::Colour(0xff6040a0));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

    if (!proc.isWavetableLoaded())
    {
        g.setColour(juce::Colour(0xff6040a0));
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        g.drawText("Load a .wav wavetable to begin",
            b, juce::Justification::centred);
        return;
    }

    // Wavetable name
    g.setColour(juce::Colour(0xaaff6ec7));
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(proc.getWavetableName(),
        b.reduced(8.0f, 4.0f),
        juce::Justification::topLeft);

    // Num frames info
    juce::String info = juce::String(proc.getNumFrames()) + " frames  /  "
        + juce::String(proc.getCycleSamples()) + " smp/cycle";
    g.setColour(juce::Colour(0xaa6ec6ff));
    g.drawText(info, b.reduced(8.0f, 4.0f), juce::Justification::topRight);

    // Draw the actual waveform slice
    int nf = proc.getNumFrames();
    int w = (int)b.getWidth();

    if (nf > 0 && w > 0)
    {
        juce::Path wavePath;
        float midY = b.getCentreY();
        float halfH = b.getHeight() * 0.45f;
        float fp = proc.getCurrentFramePos();
        float frameIndex = fp * (float)(nf - 1);

        for (int px = 0; px < w; ++px)
        {
            double phase = (double)px / (double)(w - 1);

            // Calls the nearest-neighbor logic from your processor. 
            // Note: Make sure sampleFrameNearest is public in PluginProcessor.h!
            float sample = proc.sampleFrameNearest(frameIndex, phase);

            float y = midY - sample * halfH;

            if (px == 0) wavePath.startNewSubPath((float)px + b.getX(), y);
            else         wavePath.lineTo((float)px + b.getX(), y);
        }

        // Draw an aesthetic gradient stroke
        juce::ColourGradient waveGrad(juce::Colour(0xffff6ec7), b.getTopLeft(),
            juce::Colour(0xff6ec6ff), b.getBottomRight(), false);
        g.setGradientFill(waveGrad);
        g.strokePath(wavePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
// Editor
//==============================================================================
TranswaveAudioProcessorEditor::TranswaveAudioProcessorEditor(TranswaveAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    // --- ROW 1 ---
    // Wavetable
    knobPosition("Position", p.apvts, "position", laf),
    knobGain("Gain", p.apvts, "gain", laf),
    // Evolution
    knobEvolution("Evo Speed", p.apvts, "evolution", laf),
    knobEvoLFORate("Evo LFO Hz", p.apvts, "evoLFORate", laf),
    knobEvoLFODepth("Evo LFO D", p.apvts, "evoLFODepth", laf),
    knobPosLFORate("Pos LFO Hz", p.apvts, "posLFORate", laf),
    knobPosLFODepth("Pos LFO D", p.apvts, "posLFODepth", laf),
    // Pitch
    knobDetune("Detune ct", p.apvts, "detune", laf),
    knobPitchLFO("Pitch LFO", p.apvts, "pitchLFO", laf),
    knobPitchLFORate("Ptch LFO Hz", p.apvts, "pitchLFORate", laf),

    // --- ROW 2 ---
    // ADSR
    knobAttack("Attack", p.apvts, "attack", laf),
    knobDecay("Decay", p.apvts, "decay", laf),
    knobSustain("Sustain", p.apvts, "sustain", laf),
    knobRelease("Release", p.apvts, "release", laf),
    // Grit
    knobBitCrush("Bit Depth", p.apvts, "bitCrush", laf),
    knobGrit("Grit", p.apvts, "grit", laf),
    // Scan Style
    knobScanStyle("Scan Dir", p.apvts, "scanStyle", laf),
    knobScanJump("Rnd Jump", p.apvts, "jumpProb", laf),
    // Filter
    knobFilterFreq("Cutoff", p.apvts, "filterFreq", laf),
    knobFilterRes("Resonance", p.apvts, "filterQ", laf),

    // Display
    wtDisplay(p)
{
    setLookAndFeel(&laf);
    setSize(760, 480);

    // Section headers
    auto setupSection = [&](juce::Label& lbl, const juce::String& txt)
        {
            lbl.setText(txt, juce::dontSendNotification);
            lbl.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
            lbl.setColour(juce::Label::textColourId, juce::Colour(0xffff6ec7));
            lbl.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(lbl);
        };

    setupSection(sectionWT, "WAVETABLE");
    setupSection(sectionEvo, "EVOLUTION");
    setupSection(sectionPitch, "PITCH");
    setupSection(sectionADSR, "ENVELOPE");
    setupSection(sectionGrit, "CHARACTER");
    setupSection(sectionScan, "SCAN STYLE");
    setupSection(sectionFilter, "FILTER");

    // Load button
    loadButton.setButtonText("LOAD .WAV");
    loadButton.setLookAndFeel(&laf);
    loadButton.onClick = [this] { loadWavetableClicked(); };
    addAndMakeVisible(loadButton);

    // Cycle size editor
    cycleSizeLabel.setText("Cycle Samples:", juce::dontSendNotification);
    cycleSizeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6ec6ff));
    cycleSizeLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    addAndMakeVisible(cycleSizeLabel);

    cycleSizeEditor.setText("2048");
    cycleSizeEditor.setInputRestrictions(6, "0123456789");
    cycleSizeEditor.setLookAndFeel(&laf);
    addAndMakeVisible(cycleSizeEditor);

    // Info label
    infoLabel.setText("Transwave-style Wavetable Synthesizer",
        juce::dontSendNotification);
    infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xaaff6ec7));
    infoLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    infoLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(infoLabel);

    // Wavetable display
    addAndMakeVisible(wtDisplay);

    // All knobs ROW 1
    addAndMakeVisible(knobPosition);
    addAndMakeVisible(knobGain);
    addAndMakeVisible(knobEvolution);
    addAndMakeVisible(knobEvoLFORate);
    addAndMakeVisible(knobEvoLFODepth);
    addAndMakeVisible(knobPosLFORate);
    addAndMakeVisible(knobPosLFODepth);
    addAndMakeVisible(knobDetune);
    addAndMakeVisible(knobPitchLFO);
    addAndMakeVisible(knobPitchLFORate);

    // All knobs ROW 2
    addAndMakeVisible(knobAttack);
    addAndMakeVisible(knobDecay);
    addAndMakeVisible(knobSustain);
    addAndMakeVisible(knobRelease);
    addAndMakeVisible(knobBitCrush);
    addAndMakeVisible(knobGrit);
    addAndMakeVisible(knobScanStyle);
    addAndMakeVisible(knobScanJump);
    addAndMakeVisible(knobFilterFreq);
    addAndMakeVisible(knobFilterRes);

    startTimerHz(20);
}

TranswaveAudioProcessorEditor::~TranswaveAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void TranswaveAudioProcessorEditor::timerCallback() {}

//==============================================================================
void TranswaveAudioProcessorEditor::loadWavetableClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select wavetable .wav",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav"
    );

    auto fileChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(fileChooserFlags, [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file.existsAsFile())
            {
                int cycleSize = cycleSizeEditor.getText().getIntValue();
                if (cycleSize < 16) cycleSize = 2048;

                audioProcessor.loadWavetable(file, cycleSize);
            }
        });
}

//==============================================================================
void TranswaveAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Main background: deep purple to deep blue
    juce::ColourGradient bg(juce::Colour(0xff120820), 0.0f, 0.0f,
        juce::Colour(0xff051030), b.getWidth(), b.getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    // Subtle scanline texture overlay
    for (int y = 0; y < (int)b.getHeight(); y += 3)
    {
        g.setColour(juce::Colours::black.withAlpha(0.07f));
        g.drawHorizontalLine(y, 0.0f, b.getWidth());
    }

    // Pink-blue accent gradient strip at top and bottom
    juce::ColourGradient strip(juce::Colour(0xffff6ec7), 0.0f, 0.0f,
        juce::Colour(0xff6ec6ff), b.getWidth(), 0.0f, false);
    g.setGradientFill(strip);
    g.fillRect(0.0f, 0.0f, b.getWidth(), 3.0f);
    g.fillRect(0.0f, b.getHeight() - 3.0f, b.getWidth(), 3.0f);

    // Title
    g.setFont(juce::Font(juce::FontOptions().withHeight(22.0f)));
    juce::ColourGradient titleGrad(juce::Colour(0xffff6ec7), 20.0f, 14.0f,
        juce::Colour(0xff6ec6ff), 220.0f, 14.0f, false);
    g.setGradientFill(titleGrad);
    g.drawText("PhizmOsc", 18, 8, 280, 28, juce::Justification::centredLeft);

    // Section panel backgrounds
    auto drawPanel = [&](juce::Rectangle<int> r)
        {
            g.setColour(juce::Colour(0x22ffffff));
            g.fillRoundedRectangle(r.toFloat(), 6.0f);
            g.setColour(juce::Colour(0x33ff6ec7));
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 0.8f);
        };

    // --- ROW 1 PANELS ---
    drawPanel({ 10,  50, 144, 114 }); // WT Box (2 Knobs)
    drawPanel({ 164, 50, 348, 114 }); // Evo Box (5 Knobs)
    drawPanel({ 522, 50, 212, 114 }); // Pitch Box (3 Knobs)

    // --- ROW 2 PANELS ---
    drawPanel({ 10,  174, 280, 114 }); // Envelope Box (4 Knobs)
    drawPanel({ 300, 174, 144, 114 }); // Character Box (2 Knobs)
    drawPanel({ 454, 174, 144, 114 }); // Scan Style Box (2 Knobs)
    drawPanel({ 608, 174, 144, 114 }); // Filter Box (2 Knobs)
}

//==============================================================================
void TranswaveAudioProcessorEditor::resized()
{
    const int kw = 68;   // knob width
    const int kh = 80;   // knob height
    const int ky1 = 68;  // Row 1 Knobs Y
    const int ky2 = 192; // Row 2 Knobs Y

    // Header info
    infoLabel.setBounds(70, 15, 360, 18);

    // ========== ROW 1 LABELS ===========
    sectionWT.setBounds(14, 52, 110, 14);
    sectionEvo.setBounds(168, 52, 110, 14);
    sectionPitch.setBounds(526, 52, 110, 14);

    // ========== ROW 1 KNOBS ===========
    // Wavetable Section
    knobPosition.setBounds(14, ky1, kw, kh);
    knobGain.setBounds(14 + kw, ky1, kw, kh);

    // Evolution Section
    int evoX = 168;
    knobEvolution.setBounds(evoX, ky1, kw, kh);
    knobEvoLFORate.setBounds(evoX + kw, ky1, kw, kh);
    knobEvoLFODepth.setBounds(evoX + kw * 2, ky1, kw, kh);
    knobPosLFORate.setBounds(evoX + kw * 3, ky1, kw, kh);
    knobPosLFODepth.setBounds(evoX + kw * 4, ky1, kw, kh);

    // Pitch Section
    int pitchX = 526;
    knobDetune.setBounds(pitchX, ky1, kw, kh);
    knobPitchLFO.setBounds(pitchX + kw, ky1, kw, kh);
    knobPitchLFORate.setBounds(pitchX + kw * 2, ky1, kw, kh);


    // ========== ROW 2 LABELS ===========
    sectionADSR.setBounds(14, 176, 110, 14);
    sectionGrit.setBounds(304, 176, 110, 14);
    sectionScan.setBounds(458, 176, 110, 14);
    sectionFilter.setBounds(612, 176, 110, 14);

    // ========== ROW 2 KNOBS ===========
    // Envelope Section
    int envX = 14;
    knobAttack.setBounds(envX, ky2, kw, kh);
    knobDecay.setBounds(envX + kw, ky2, kw, kh);
    knobSustain.setBounds(envX + kw * 2, ky2, kw, kh);
    knobRelease.setBounds(envX + kw * 3, ky2, kw, kh);

    // Character Section
    int charX = 304;
    knobBitCrush.setBounds(charX, ky2, kw, kh);
    knobGrit.setBounds(charX + kw, ky2, kw, kh);

    // Scan Style Section
    int scanX = 458;
    knobScanStyle.setBounds(scanX, ky2, kw, kh);
    knobScanJump.setBounds(scanX + kw, ky2, kw, kh);

    // Filter Section
    int filtX = 612;
    knobFilterFreq.setBounds(filtX, ky2, kw, kh);
    knobFilterRes.setBounds(filtX + kw, ky2, kw, kh);

    // ========== DISPLAYS & CONTROLS ===========
    wtDisplay.setBounds(10, 305, 740, 120);

    loadButton.setBounds(10, 440, 100, 26);
    cycleSizeLabel.setBounds(120, 444, 90, 18);
    cycleSizeEditor.setBounds(212, 440, 70, 26);
}