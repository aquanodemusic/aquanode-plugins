#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Colour palette  --  Sleek Silver & Cyan Blue Aesthetic
static const juce::Colour BG_TOP{ 0xFFE6ECF2 };       // Bright metallic silver-white
static const juce::Colour BG_BOT{ 0xFFBDC9D6 };       // Deep brushed silver-gray
static const juce::Colour PANEL_COL{ 0xFFF0F4F8 };    // Frosted glass / Light silver face
static const juce::Colour PANEL_EDGE{ 0xFF8CA0B3 };   // Polished aluminum rim
static const juce::Colour GLOW_COL{ 0xFF00D2FF };     // Electric cyan glow / Accent
static const juce::Colour GLOW2{ 0xFFE0FAFF };        // Bright silver-cyan highlight
static const juce::Colour KNOB_TRACK{ 0xFFD2DCE5 };   // Matte silver track background
static const juce::Colour KNOB_FILL{ 0xFF00A8E8 };    // Solid cyan indicator fill
static const juce::Colour TEXT_MAIN{ 0xFF1C2D3D };    // Deep charcoal for crisp readability
static const juce::Colour TEXT_DIM{ 0xFF5C7287 };     // Muted steel blue for secondary text

//==============================================================================
struct CPLookAndFeel : public juce::LookAndFeel_V4
{
    CPLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId, KNOB_FILL);
        setColour(juce::Slider::rotarySliderOutlineColourId, KNOB_TRACK);
        setColour(juce::Slider::thumbColourId, TEXT_MAIN);
        setColour(juce::Slider::textBoxTextColourId, TEXT_MAIN);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, KNOB_FILL.withAlpha(0.3f));
        setColour(juce::Label::textColourId, TEXT_MAIN);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override
    {
        // Tightened radius and lifted the Y center higher to create space below for text
        const float radius = (float)juce::jmin(width, height) * 0.38f - 4.f;
        const float cx = (float)x + (float)width * 0.5f;
        const float cy = (float)y + (float)height * 0.38f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Soft Cyan Outer Glow Halo
        for (int i = 2; i >= 1; --i)
        {
            float r = radius + 2.f + (float)i * 2.f;
            g.setColour(GLOW_COL.withAlpha(0.06f * (float)(3 - i)));
            g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.5f);
        }

        // Track arc
        juce::Path track;
        track.addCentredArc(cx, cy, radius, radius, 0.f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(KNOB_TRACK);
        g.strokePath(track, juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Fill arc with vibrant Cyan
        if (sliderPos > 0.f)
        {
            juce::Path fill;
            fill.addCentredArc(cx, cy, radius, radius, 0.f, rotaryStartAngle, angle, true);
            g.setColour(KNOB_FILL);
            g.strokePath(fill, juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(GLOW2.withAlpha(0.5f));
            g.strokePath(fill, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Body - Metallic Silver Gradient Circle
        const float bodyR = radius - 6.f;
        juce::ColourGradient bodyGrad(juce::Colours::white, cx, cy - bodyR, BG_BOT, cx, cy + bodyR, false);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.f, bodyR * 2.f);

        g.setColour(PANEL_EDGE);
        g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2.f, bodyR * 2.f, 1.5f);

        // Pointer
        const float pLen = bodyR * 0.6f;
        const float px = cx + std::sin(angle) * pLen;
        const float py = cy - std::cos(angle) * pLen;
        g.setColour(TEXT_MAIN);
        g.drawLine(cx, cy, px, py, 3.0f);

        g.setColour(GLOW_COL);
        g.fillEllipse(px - 3.f, py - 3.f, 6.f, 6.f);
    }
};

static CPLookAndFeel cpLAF;

//==============================================================================
ClipPreserveAudioProcessorEditor::ClipPreserveAudioProcessorEditor(ClipPreserveAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    driveAtt(p.apvts, "drive", driveKnob.slider),
    threshAtt(p.apvts, "threshold", threshKnob.slider),
    preserveAtt(p.apvts, "preserve", preserveKnob.slider),
    hpFreqAtt(p.apvts, "hpfreq", hpFreqKnob.slider),
    outputAtt(p.apvts, "output", outputKnob.slider)
{
    setLookAndFeel(&cpLAF);

    for (auto* k : { &driveKnob, &threshKnob, &preserveKnob, &hpFreqKnob, &outputKnob })
    {
        addAndMakeVisible(k);
        // Make the value textbox clean, clear, and slightly taller for readability
        k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
    }

    setSize(1040, 520);
    startTimerHz(30);
}

ClipPreserveAudioProcessorEditor::~ClipPreserveAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void ClipPreserveAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();

    // Silver metallic gradient background
    juce::ColourGradient bgGrad(BG_TOP, 0.f, 0.f, BG_BOT, 0.f, H, false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    // Main control dashboard panel
    const float px = 30.f;
    const float py = 110.f;
    const float pw = W - (px * 2.f);
    const float ph = H - py - 40.f;

    // Outer subtle shadow for panel depth
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.fillRoundedRectangle(px + 2.f, py + 4.f, pw, ph, 8.f);

    // Panel face
    g.setColour(PANEL_COL);
    g.fillRoundedRectangle(px, py, pw, ph, 8.f);

    // Beveled Edge
    g.setColour(PANEL_EDGE.withAlpha(0.6f));
    g.drawRoundedRectangle(px, py, pw, ph, 8.f, 1.5f);

    // ---- Title Branding ----
    g.setFont(juce::Font(juce::FontOptions("Helvetica", 36.f, juce::Font::bold)));
    g.setColour(TEXT_MAIN);
    g.drawText("CLIPPRESERVE", 30, 20, 400, 45, juce::Justification::centredLeft);

    // Subtitle
    g.setFont(juce::Font(juce::FontOptions("Helvetica", 16.f, juce::Font::plain)));
    g.setColour(TEXT_DIM);
    g.drawText("Detail-Preserving Clipper", 32, 65, 400, 25, juce::Justification::centredLeft);

    // Cyan Neon Accent Strip below title
    juce::ColourGradient accentLine(GLOW_COL, 30.f, 95.f, juce::Colours::transparentBlack, 350.f, 95.f, false);
    g.setGradientFill(accentLine);
    g.fillRect(30.f, 95.f, 320.f, 3.f);

    // ---- Dynamic Section Layout Calculations ----
    const float sectionWidth = pw / 5.f;

    g.setFont(juce::Font(juce::FontOptions("Helvetica", 18.f, juce::Font::bold)));
    g.setColour(TEXT_MAIN);

    // Header Groups
    g.drawText("CLIPPER", px, py + 15.f, sectionWidth * 2.f, 25.f, juce::Justification::centred);
    g.drawText("PRESERVE", px + (sectionWidth * 2.f), py + 15.f, sectionWidth * 2.f, 25.f, juce::Justification::centred);
    g.drawText("GAIN", px + (sectionWidth * 4.f), py + 15.f, sectionWidth, 25.f, juce::Justification::centred);

    // Vertical Clean Dividers
    g.setColour(PANEL_EDGE.withAlpha(0.4f));
    g.fillRect(px + (sectionWidth * 2.f), py + 15.f, 1.f, ph - 30.f);
    g.fillRect(px + (sectionWidth * 4.f), py + 15.f, 1.f, ph - 30.f);

    // ---- Large Knob Labels (Positioned directly above the value boxes) ----
    // Font scale bumped up significantly to look bold and clear
    g.setFont(juce::Font(juce::FontOptions("Helvetica", 17.f, juce::Font::bold)));
    g.setColour(TEXT_DIM);

    // Calculate Y target dynamically right above the textbox bounds
    const float labelY = py + ph - 104.f;
    const float labelH = 24.f;

    g.drawText("DRIVE", px + (sectionWidth * 0.f), labelY, sectionWidth, labelH, juce::Justification::centred);
    g.drawText("THRESHOLD", px + (sectionWidth * 1.f), labelY, sectionWidth, labelH, juce::Justification::centred);
    g.drawText("PRESERVE", px + (sectionWidth * 2.f), labelY, sectionWidth, labelH, juce::Justification::centred);
    g.drawText("HP FREQ", px + (sectionWidth * 3.f), labelY, sectionWidth, labelH, juce::Justification::centred);
    g.drawText("OUTPUT", px + (sectionWidth * 4.f), labelY, sectionWidth, labelH, juce::Justification::centred);
}

void ClipPreserveAudioProcessorEditor::resized()
{
    const int px = 30;
    const int py = 110;
    const int pw = getWidth() - (px * 2);
    const int ph = getHeight() - py - 40;

    const int sectionWidth = pw / 5;

    // Adjusted knob sizing system:
    // The slider extends all the way down to house the text box, 
    // while lookAndFeel draws the dial graphic exclusively near the top hemisphere.
    const int knobY = py + 45;
    const int knobH = ph - 65;

    driveKnob.setBounds(px + 10, knobY, sectionWidth - 20, knobH);
    threshKnob.setBounds(px + sectionWidth + 10, knobY, sectionWidth - 20, knobH);

    preserveKnob.setBounds(px + (sectionWidth * 2) + 10, knobY, sectionWidth - 20, knobH);
    hpFreqKnob.setBounds(px + (sectionWidth * 3) + 10, knobY, sectionWidth - 20, knobH);

    outputKnob.setBounds(px + (sectionWidth * 4) + 10, knobY, sectionWidth - 20, knobH);
}

//==============================================================================
void ClipPreserveAudioProcessorEditor::timerCallback()
{
    auto smooth = [](float cur, float target) {
        return cur + (target - cur) * 0.25f;
        };

    meterInL = smooth(meterInL, audioProcessor.inputLevelL.load());
    meterInR = smooth(meterInR, audioProcessor.inputLevelR.load());
    meterOutL = smooth(meterOutL, audioProcessor.outputLevelL.load());
    meterOutR = smooth(meterOutR, audioProcessor.outputLevelR.load());

    repaint();
}