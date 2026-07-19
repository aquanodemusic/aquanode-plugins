#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Colour palette (matches Springer's cyan/white/blue aesthetic)
namespace AquaColors
{
    const juce::Colour background1  = juce::Colour(0xff00d4e8);  // Bright cyan (top)
    const juce::Colour background2  = juce::Colour(0xff0094a8);  // Deep cyan (bottom)
    const juce::Colour panelBg      = juce::Colour(0x22000000);  // Dark tint for panels
    const juce::Colour knobThumb    = juce::Colour(0xffffffff);  // White thumb
    const juce::Colour knobTrack    = juce::Colour(0xff007f8f);  // Dark cyan track
    const juce::Colour labelText    = juce::Colour(0xffffffff);  // White labels
    const juce::Colour subText      = juce::Colour(0xff003c44);  // Dark cyan sub-labels
    const juce::Colour btnNormal    = juce::Colour(0x33ffffff);  // Semi-transparent white
    const juce::Colour btnActive    = juce::Colour(0xff00ffff);  // Bright cyan active
    const juce::Colour gravityRed   = juce::Colour(0xffffaa00);  // Amber for gravity > 1 zone
    const juce::Colour divider      = juce::Colour(0x55ffffff);  // Faint white divider
}

//==============================================================================
AquatonAudioProcessorEditor::AquatonAudioProcessorEditor(AquatonAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    //--- Helper lambdas ---
    auto attach = [&](juce::Slider& s, const juce::String& id) {
        sliderAttachments.push_back(
            std::make_unique<SliderAttach>(audioProcessor.apvts, id, s));
    };

    //--- Setup all knobs ---
    auto makeKnob = [&](juce::Slider& s, const juce::String& id) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId,    AquaColors::labelText);
        s.setColour(juce::Slider::rotarySliderFillColourId, AquaColors::knobThumb);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, AquaColors::knobTrack);
        s.setMouseDragSensitivity(200);
        addAndMakeVisible(s);
        attach(s, id);
    };

    // Row 1 – Core reverb
    makeKnob(sizeSlider,    "size");
    makeKnob(gravitySlider, "gravity");
    makeKnob(lpSlider,      "lpCutoff");
    makeKnob(hpSlider,      "hpCutoff");
    makeKnob(mixSlider,     "mix");

    // Row 2 – Character
    makeKnob(preDiffSlider,  "preDiffuse");
    makeKnob(tankDiffSlider, "tankDiffuse");
    makeKnob(modRateSlider,  "modRate");
    makeKnob(modDepthSlider, "modDepth");
    makeKnob(spreadSlider,   "spread");

    // Gravity knob gets an amber accent tint to hint it can exceed unity
    gravitySlider.setColour(juce::Slider::rotarySliderFillColourId, AquaColors::gravityRed);

    // Horizontal Tank Stages slider
    tankStagesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tankStagesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    tankStagesSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tankStagesSlider.setColour(juce::Slider::textBoxTextColourId, AquaColors::labelText);
    tankStagesSlider.setColour(juce::Slider::thumbColourId, AquaColors::knobThumb);
    tankStagesSlider.setColour(juce::Slider::trackColourId, AquaColors::knobTrack);
    addAndMakeVisible(tankStagesSlider);
    attach(tankStagesSlider, "tankStages");

    //--- Buttons ---
    randomizeBtn.setButtonText("Randomize\nMatrix");
    randomizeBtn.setColour(juce::TextButton::buttonColourId,   AquaColors::btnNormal);
    randomizeBtn.setColour(juce::TextButton::textColourOffId,  AquaColors::labelText);
    randomizeBtn.onClick = [this]() { audioProcessor.randomizeMatrix(); };
    addAndMakeVisible(randomizeBtn);

    wetOnlyBtn.setButtonText("Wet Only");
    wetOnlyBtn.setClickingTogglesState(true);
    wetOnlyBtn.setColour(juce::TextButton::buttonColourId,   AquaColors::btnNormal);
    wetOnlyBtn.setColour(juce::TextButton::buttonOnColourId, AquaColors::btnActive);
    wetOnlyBtn.setColour(juce::TextButton::textColourOffId,  AquaColors::labelText);
    wetOnlyBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
    wetOnlyAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, "wetOnly", wetOnlyBtn);
    addAndMakeVisible(wetOnlyBtn);

    //--- Parallel knob/label array for paint() ---
    knobs = { {
        { &sizeSlider,    "Size"       },
        { &gravitySlider, "Gravity"    },
        { &lpSlider,      "LP Freq"    },
        { &hpSlider,      "HP Freq"    },
        { &mixSlider,     "Mix"        },
        { &preDiffSlider,  "Pre-Diffuse"  },
        { &tankDiffSlider, "Tank-Diffuse" },
        { &modRateSlider,  "Mod Rate"     },
        { &modDepthSlider, "Mod Depth"    },
        { &spreadSlider,   "Spread"       }
    } };

    setSize(600, 430);
}

AquatonAudioProcessorEditor::~AquatonAudioProcessorEditor() {}

//==============================================================================
void AquatonAudioProcessorEditor::paint(juce::Graphics& g)
{
    // --- Gradient background ---
    juce::ColourGradient grad(AquaColors::background1, 0.f, 0.f,
                              AquaColors::background2, 0.f, (float)getHeight(), false);
    g.setFillType(juce::FillType(grad));
    g.fillAll();

    // --- Header ---
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(38.0f, juce::Font::bold));
    g.drawText("Aquaton", juce::Rectangle<int>(28, 14, 220, 44),
               juce::Justification::centredLeft, false);

    g.setFont(14.0f);
    g.setColour(AquaColors::subText);
    g.drawText("Reverb  by aquanode", juce::Rectangle<int>(30, 54, 220, 20),
               juce::Justification::centredLeft, false);

    // Small badge: "FDN 8-line"
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.drawText("FDN \xc2\xb7 8\xe2\x80\x93Line \xc2\xb7 Hadamard",
               juce::Rectangle<int>(28, 72, 200, 16),
               juce::Justification::centredLeft, false);

    // --- Gravity zone indicator (amber line at 1.0 on the gravity knob) ---
    // Draw a small "!  > 1.0 = infinite growth" hint next to gravity knob
    g.setFont(juce::Font(10.0f));
    g.setColour(AquaColors::gravityRed.withAlpha(0.85f));
    auto& gs = *knobs[1].slider;   // gravitySlider
    g.drawText("> 1.0 = grow",
               gs.getBounds().withY(gs.getBottom() + 2).withHeight(12),
               juce::Justification::centred, false);

    // --- Panel backgrounds behind each row of knobs ---
    const float radius = 10.0f;
    g.setColour(AquaColors::panelBg);
    // Row 1 panel
    auto r1 = getLocalBounds().reduced(18).withTop(90).withBottom(220);
    g.fillRoundedRectangle(r1.toFloat(), radius);
    // Row 2 panel
    auto r2 = getLocalBounds().reduced(18).withTop(225).withBottom(355);
    g.fillRoundedRectangle(r2.toFloat(), radius);
    // Bottom controls panel
    auto r3 = getLocalBounds().reduced(18).withTop(360).withBottom(410);
    g.fillRoundedRectangle(r3.toFloat(), radius);

    // --- Knob labels (above each knob) ---
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(AquaColors::labelText);
    for (auto& k : knobs)
    {
        auto b = k.slider->getBounds();
        g.drawFittedText(k.label,
                         b.withY(b.getY() - 14).withHeight(14),
                         juce::Justification::centredBottom, 1);
    }

    // --- Tank stages label ---
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(AquaColors::labelText);
    auto tb = tankStagesSlider.getBounds();
    g.drawFittedText("Tank Stages",
                     tb.withX(tb.getX() - 85).withWidth(82),
                     juce::Justification::centredRight, 1);

    // --- Divider line between rows ---
    g.setColour(AquaColors::divider);
    g.drawHorizontalLine(222, 28.f, (float)(getWidth() - 28));
}

//==============================================================================
void AquatonAudioProcessorEditor::resized()
{
    const int marginH = 22;
    const int marginV = 95;      // Space for header
    const int knobW   = 100;
    const int knobH   = 110;
    const int rowGap  = 10;
    const int rowY1   = marginV;
    const int rowY2   = rowY1 + knobH + rowGap;

    // 5 knobs per row, centered
    const int totalRowWidth = 5 * knobW;
    const int startX = (getWidth() - totalRowWidth) / 2;

    // Row 1: Size, Gravity, LP, HP, Mix
    juce::Slider* row1[5] = { &sizeSlider, &gravitySlider, &lpSlider,
                               &hpSlider,   &mixSlider };
    for (int i = 0; i < 5; ++i)
        row1[i]->setBounds(startX + i * knobW, rowY1, knobW, knobH);

    // Row 2: Pre-Diffuse, Tank-Diffuse, Mod Rate, Mod Depth, Spread
    juce::Slider* row2[5] = { &preDiffSlider, &tankDiffSlider, &modRateSlider,
                               &modDepthSlider, &spreadSlider };
    for (int i = 0; i < 5; ++i)
        row2[i]->setBounds(startX + i * knobW, rowY2, knobW, knobH);

    // Bottom controls row
    const int bottomY    = rowY2 + knobH + rowGap + 8;
    const int btnW       = 110;
    const int btnH       = 36;
    const int stageSlW   = 200;

    // [Randomize Matrix btn]  [Wet Only btn]  [──Tank Stages slider──]
    int cx = marginH + 10;
    randomizeBtn.setBounds(cx, bottomY, btnW, btnH);
    cx += btnW + 12;
    wetOnlyBtn.setBounds(cx, bottomY, btnW, btnH);
    cx += btnW + 30;
    // Label is painted; slider placed after space for label
    tankStagesSlider.setBounds(cx + 88, bottomY + 8, stageSlW, btnH - 16);
}
