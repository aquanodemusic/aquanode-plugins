#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Konstanten
static constexpr int nOps = 12;
static constexpr int nParams = 8;
static constexpr int rowH = 52;
static constexpr int knobW = 55;
static constexpr int mSize = 22;
static constexpr int leftOff = 45;

FM12SynthAudioProcessorEditor::FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Pre-allocate vectors to avoid reallocations
    opKnobs.reserve(nOps * nParams);
    opKnobAttachments.reserve(nOps * nParams);
    matrixButtons.reserve(nOps * nOps);
    matrixAttachments.reserve(nOps * nOps);

    static constexpr const char* pNames[] = { "attack", "decay", "sustain", "release", "level", "fm", "ratio", "phase" };

    // Knobs erstellen
    for (int op = 0; op < nOps; ++op) {
        for (int i = 0; i < nParams; ++i) {
            auto s = std::make_unique<juce::Slider>();
            s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);
            addAndMakeVisible(s.get());

            const juce::String paramID = "op" + juce::String(op) + "_" + juce::String(pNames[i]);

            if (processor.apvts.getParameter(paramID) != nullptr) {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramID, *s);
                opKnobAttachments.push_back(std::move(att));
            }
            opKnobs.push_back(std::move(s));
        }
    }

    // Matrix erstellen
    for (int f = 0; f < nOps; ++f) {
        for (int t = 0; t < nOps; ++t) {
            auto b = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible(b.get());

            const juce::String rID = "route_" + juce::String(f) + "_" + juce::String(t);

            if (processor.apvts.getParameter(rID) != nullptr) {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, rID, *b);
                matrixAttachments.push_back(std::move(att));
            }
            matrixButtons.push_back(std::move(b));
        }
    }

    adsrFMToggle = std::make_unique<juce::ToggleButton>("Real FM Mode");
    adsrFMToggle->setButtonText("Real FM Mode");
    addAndMakeVisible(adsrFMToggle.get());
    adsrFMAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "adsrAffectsFM", *adsrFMToggle);

    setSize(900, 720);
    
    // Disable automatic background painting for better performance
    setOpaque(true);
}

FM12SynthAudioProcessorEditor::~FM12SynthAudioProcessorEditor() {}

void FM12SynthAudioProcessorEditor::resized()
{
    constexpr int startY = 60;
    constexpr int knobStartX = leftOff;
    constexpr int knobSpacing = knobW + 4;
    constexpr int matrixStartX = knobStartX + (nParams * (knobW + 5)) + 40;

    // Position knobs - optimized loop
    const int totalKnobs = static_cast<int>(opKnobs.size());
    for (int i = 0; i < totalKnobs; ++i)
    {
        const int row = i / nParams;
        const int col = i % nParams;
        const int xPos = knobStartX + col * knobSpacing;
        const int yPos = startY + row * rowH;
        opKnobs[i]->setBounds(xPos, yPos, knobW, rowH - 2);
    }

    // Position matrix buttons - optimized loop
    const int totalButtons = static_cast<int>(matrixButtons.size());
    for (int i = 0; i < totalButtons; ++i)
    {
        const int row = i / nOps;
        const int col = i % nOps;
        const int xPos = matrixStartX + col * mSize;
        const int yPos = startY + row * rowH + (rowH - mSize) / 2;
        matrixButtons[i]->setBounds(xPos, yPos, mSize, mSize);
    }

    // Position ADSR toggles
    constexpr int toggleX = matrixStartX;
    constexpr int toggleY = 10;
    constexpr int toggleWidth = 170;
    constexpr int toggleHeight = 24;
    
    adsrFMToggle->setBounds(toggleX + toggleWidth + 10, toggleY, toggleWidth, toggleHeight);
}

void FM12SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Hintergrund (DX-Style Beige-Verlauf)
    g.setGradientFill(juce::ColourGradient(juce::Colour(205, 190, 160), 0, 0,
        juce::Colour(170, 150, 115), 0, (float)getHeight(), false));
    g.fillAll();

    // --- BRANDING ---
    const juce::String brandingText = "FM12 by aquanode";
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    
    // Optimized glow effect - single draw with shadow
    g.drawText(brandingText, 20, 10, 400, 30, juce::Justification::left, true);

    // --- KNOB COLUMN LABELS ---
    static constexpr const char* knobLabels[] = { "ATT", "DEC", "SUS", "REL", "LVL", "FM", "RATIO", "PHASE" };
    
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.8f));

    constexpr int knobStartX = leftOff;
    constexpr int labelY = 45;
    constexpr int knobSpacing = knobW + 4;
    
    for (int i = 0; i < nParams; ++i)
    {
        const int xPos = knobStartX + i * knobSpacing;
        g.drawText(knobLabels[i], xPos, labelY, knobW, 14, juce::Justification::centred);
    }

    // --- OPERATOR ROW LABELS ---
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.setColour(juce::Colours::black);

    constexpr int startY = 60;
    constexpr int opYOffset = (rowH / 2) - 7;
    
    for (int i = 0; i < nOps; ++i)
    {
        const int yPos = startY + i * rowH + opYOffset;
        g.drawText("OP" + juce::String(i + 1), 5, yPos, 38, 14, juce::Justification::centredRight);
    }

    // --- MATRIX SECTION ---
    constexpr int matrixStartX = knobStartX + (nParams * (knobW + 5)) + 40;
    
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawText("ROUTING MATRIX", matrixStartX, 38, nOps * mSize, 18, juce::Justification::centred);

    // Matrix column numbers
    g.setFont(juce::Font(10.0f));
    for (int i = 0; i < nOps; ++i)
    {
        const int xPos = matrixStartX + i * mSize;
        g.drawText(juce::String(i + 1), xPos, 56, mSize, 14, juce::Justification::centred);
    }

    // Vertical separator line
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawVerticalLine(matrixStartX - 20, 40.0f, (float)getHeight() - 10.0f);
}
