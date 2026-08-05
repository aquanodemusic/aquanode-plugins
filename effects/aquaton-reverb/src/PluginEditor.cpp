#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace AquaColors
{
    const juce::Colour background1 = juce::Colour(0xff00d4e8);
    const juce::Colour background2 = juce::Colour(0xff0094a8);
    const juce::Colour knobThumb   = juce::Colour(0xffffffff);
    const juce::Colour knobTrack   = juce::Colour(0xff007f8f);
    const juce::Colour labelText   = juce::Colour(0xffffffff);
    const juce::Colour subText     = juce::Colour(0xff003c44);
    const juce::Colour btnNormal   = juce::Colour(0x33ffffff);
    const juce::Colour btnActive   = juce::Colour(0xff00ffff);
    const juce::Colour divider     = juce::Colour(0x66ffffff);
}

//==============================================================================
AquatonAudioProcessorEditor::AquatonAudioProcessorEditor(AquatonAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    auto attach = [&](juce::Slider& s, const juce::String& id)
    {
        sliderAttachments.push_back(
            std::make_unique<SliderAttach>(audioProcessor.apvts, id, s));
    };

    auto makeKnob = [&](juce::Slider& s, const juce::String& id)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
        s.setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId,         AquaColors::labelText);
        s.setColour(juce::Slider::rotarySliderFillColourId,    AquaColors::knobThumb);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, AquaColors::knobTrack);
        s.setMouseDragSensitivity(200);
        addAndMakeVisible(s);
        attach(s, id);
    };

    //--- Row 1 ---
    makeKnob(sizeSlider,     "size");
    makeKnob(feedbackSlider, "feedback");
    makeKnob(tailSlider,     "tail");
    makeKnob(lpSlider,       "lpCutoff");
    makeKnob(hpSlider,       "hpCutoff");
    makeKnob(mixSlider,      "mix");

    //--- Row 2 ---
    makeKnob(tapAmtSlider,   "tapAmount");
    tapAmtSlider.setNumDecimalPlacesToDisplay(0);
    makeKnob(preDiffSlider,  "preDiffuse");
    makeKnob(tankDiffSlider, "tankDiffuse");
    makeKnob(apfModSlider,   "apfMod");
    apfModSlider.setNumDecimalPlacesToDisplay(2);
    makeKnob(modRateSlider,  "modRate");
    makeKnob(modDepthSlider, "modDepth");

    //--- Row 3 ---
    makeKnob(spreadSlider,      "spread");
    makeKnob(bloomAmtSlider,    "bloomAmount");
    makeKnob(bloomTimeSlider,   "bloomTime");
    makeKnob(hfWashHPSlider,    "hfWashHP");
    makeKnob(hfWashAmtSlider,   "hfWashAmt");
    makeKnob(polarityAmtSlider, "polarityAmt");
    polarityAmtSlider.setNumDecimalPlacesToDisplay(2);

    //--- Row 4 – Pre-delay, FDN Order, Tank Stages knobs ---
    // Pre-delay: centre of range is 0 ms so the knob sits naturally at 12 o'clock
    makeKnob(predelaySlider,   "preDelay");
    predelaySlider.setNumDecimalPlacesToDisplay(0);

    // FDN Order and Tank Stages promoted from horizontal sliders to knobs
    makeKnob(fdnOrderSlider,   "fdnOrder");
    fdnOrderSlider.setNumDecimalPlacesToDisplay(0);
    makeKnob(tankStagesSlider, "tankStages");
    tankStagesSlider.setNumDecimalPlacesToDisplay(0);

    //--- Row 4 – Buttons ---
    // Randomize Param: all knob params (skip FDN/tank to avoid CPU spike)
    randomizeParamBtn.setButtonText("Rnd Param");
    randomizeParamBtn.setColour(juce::TextButton::buttonColourId,  AquaColors::btnNormal);
    randomizeParamBtn.setColour(juce::TextButton::textColourOffId, AquaColors::labelText);
    randomizeParamBtn.onClick = [this]()
    {
        static const juce::StringArray skipIds{ "tankStages", "fdnOrder", "preDelay", "freeze" };
        juce::Random rng;
        for (auto* param : audioProcessor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
                if (!skipIds.contains(rp->getParameterID()))
                    rp->setValueNotifyingHost(rng.nextFloat());
    };
    addAndMakeVisible(randomizeParamBtn);

    // Randomize Matrix: Hadamard signs and delay times (thread-safe)
    randomizeMatrixBtn.setButtonText("Rnd Matrix");
    randomizeMatrixBtn.setColour(juce::TextButton::buttonColourId,  AquaColors::btnNormal);
    randomizeMatrixBtn.setColour(juce::TextButton::textColourOffId, AquaColors::labelText);
    randomizeMatrixBtn.onClick = [this]() { audioProcessor.randomizeMatrix(); };
    addAndMakeVisible(randomizeMatrixBtn);

    // Freeze: toggle — infinite sustain, gates new input, feedback at unity
    freezeBtn.setButtonText("Freeze");
    freezeBtn.setClickingTogglesState(true);
    freezeBtn.setColour(juce::TextButton::buttonColourId,   AquaColors::btnNormal);
    freezeBtn.setColour(juce::TextButton::buttonOnColourId, AquaColors::btnActive);
    freezeBtn.setColour(juce::TextButton::textColourOffId,  AquaColors::labelText);
    freezeBtn.setColour(juce::TextButton::textColourOnId,   AquaColors::subText);
    addAndMakeVisible(freezeBtn);
    freezeAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, "freeze", freezeBtn);

    //--- Header save/load buttons ---
    saveBtn.setButtonText("Save Preset");
    saveBtn.setColour(juce::TextButton::buttonColourId,  AquaColors::btnNormal);
    saveBtn.setColour(juce::TextButton::textColourOffId, AquaColors::labelText);
    saveBtn.onClick = [this]() { saveState(); };
    addAndMakeVisible(saveBtn);

    loadBtn.setButtonText("Load Preset");
    loadBtn.setColour(juce::TextButton::buttonColourId,  AquaColors::btnNormal);
    loadBtn.setColour(juce::TextButton::textColourOffId, AquaColors::labelText);
    loadBtn.onClick = [this]() { loadState(); };
    addAndMakeVisible(loadBtn);

    //--- Knob / label array (21 total: 6+6+6+3) ---
    knobs = { {
        // Row 1
        { &sizeSlider,        "Size"          },
        { &feedbackSlider,    "Feedback"      },
        { &tailSlider,        "Tail"          },
        { &lpSlider,          "LP Freq"       },
        { &hpSlider,          "HP Freq"       },
        { &mixSlider,         "Mix"           },
        // Row 2
        { &tapAmtSlider,      "Tap Amount"    },
        { &preDiffSlider,     "Pre-Diffuse"   },
        { &tankDiffSlider,    "Tank-Diffuse"  },
        { &apfModSlider,      "APF Mod"       },
        { &modRateSlider,     "Mod Rate"      },
        { &modDepthSlider,    "Mod Depth"     },
        // Row 3
        { &spreadSlider,      "Spread"        },
        { &bloomAmtSlider,    "Bloom Amount"  },
        { &bloomTimeSlider,   "Bloom Time"    },
        { &hfWashHPSlider,    "HF Wash HP"    },
        { &hfWashAmtSlider,   "HF Wash Amt"   },
        { &polarityAmtSlider, "Polarity Amt"  },
        // Row 4 (knob slots only)
        { &predelaySlider,    "Pre-Delay ms"  },
        { &fdnOrderSlider,    "FDN Order"     },
        { &tankStagesSlider,  "Tank Stages"   },
    } };

    setSize(620, 520);
}

AquatonAudioProcessorEditor::~AquatonAudioProcessorEditor() {}

//==============================================================================
void AquatonAudioProcessorEditor::saveState()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Aquaton Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result != juce::File{})
            {
                auto state = audioProcessor.apvts.copyState();
                if (auto xml = state.createXml()) xml->writeTo(result);
            }
        });
}

void AquatonAudioProcessorEditor::loadState()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Aquaton Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xml");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
                if (auto xml = juce::XmlDocument::parse(result))
                    if (xml->hasTagName(audioProcessor.apvts.state.getType()))
                        audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*xml));
        });
}

//==============================================================================
void AquatonAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient grad(AquaColors::background1, 0.f, 0.f,
        AquaColors::background2, 0.f, (float)getHeight(), false);
    g.setFillType(juce::FillType(grad));
    g.fillAll();

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(36.f, juce::Font::bold));
    g.drawText("Aquaton", juce::Rectangle<int>(22, 12, 200, 42),
        juce::Justification::centredLeft, false);

    // Subtitle
    g.setFont(13.f);
    g.setColour(AquaColors::subText);
    g.drawText("Large Reverb by aquanode", juce::Rectangle<int>(24, 52, 220, 18),
        juce::Justification::centredLeft, false);

    // Knob labels (drawn above each knob in the array)
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(AquaColors::labelText);
    for (auto& k : knobs)
    {
        auto b = k.slider->getBounds();
        g.drawFittedText(k.label,
            b.withY(b.getY() - 13).withHeight(13),
            juce::Justification::centredBottom, 1);
    }

    // Button labels for the row-4 button slots
    // (the buttons themselves carry text, but we also draw a small header
    //  above them at the same height as the knob labels for visual consistency)
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(AquaColors::labelText);
    auto drawBtnLabel = [&](juce::Button& btn, const juce::String& lbl)
    {
        auto b = btn.getBounds();
        g.drawFittedText(lbl,
            b.withY(b.getY() - 13).withHeight(13),
            juce::Justification::centredBottom, 1);
    };
    drawBtnLabel(randomizeParamBtn,  "Randomise");
    drawBtnLabel(randomizeMatrixBtn, "Matrix");
    drawBtnLabel(freezeBtn,          "Hold");

    // Section dividers
    g.setColour(AquaColors::divider);
    const float lineL = 22.f;
    const float lineR = (float)(getWidth() - 22);

    // Between rows 1 and 2
    if (knobs[6].slider->isVisible())
        g.drawHorizontalLine(knobs[6].slider->getY() - 17, lineL, lineR);

    // Between rows 2 and 3
    if (knobs[12].slider->isVisible())
        g.drawHorizontalLine(knobs[12].slider->getY() - 17, lineL, lineR);

    // Between rows 3 and 4
    if (knobs[18].slider->isVisible())
        g.drawHorizontalLine(knobs[18].slider->getY() - 17, lineL, lineR);
}

//==============================================================================
void AquatonAudioProcessorEditor::resized()
{
    // -----------------------------------------------------------------------
    // Layout constants
    // -----------------------------------------------------------------------
    constexpr int knobW  = 90;
    constexpr int knobH  = 82;    // 64 px arc + 18 px textbox
    constexpr int rowGap = 22;    // vertical gap between rows (holds divider + label space)

    // 6 knobs × 90 = 540 px; centred in 620 px → 40 px margin each side
    const int startX = (getWidth() - 6 * knobW) / 2;  // = 40

    const int rowY1 = 98;
    const int rowY2 = rowY1 + knobH + rowGap;   // 202
    const int rowY3 = rowY2 + knobH + rowGap;   // 306
    const int rowY4 = rowY3 + knobH + rowGap;   // 410

    // -----------------------------------------------------------------------
    // Rows 1–3: unchanged
    // -----------------------------------------------------------------------
    juce::Slider* row1[] = { &sizeSlider, &feedbackSlider, &tailSlider,
                              &lpSlider,  &hpSlider,       &mixSlider };
    for (int i = 0; i < 6; ++i)
        row1[i]->setBounds(startX + i * knobW, rowY1, knobW, knobH);

    juce::Slider* row2[] = { &tapAmtSlider,  &preDiffSlider,  &tankDiffSlider,
                              &apfModSlider, &modRateSlider,  &modDepthSlider };
    for (int i = 0; i < 6; ++i)
        row2[i]->setBounds(startX + i * knobW, rowY2, knobW, knobH);

    juce::Slider* row3[] = { &spreadSlider,      &bloomAmtSlider,   &bloomTimeSlider,
                              &hfWashHPSlider,    &hfWashAmtSlider,  &polarityAmtSlider };
    for (int i = 0; i < 6; ++i)
        row3[i]->setBounds(startX + i * knobW, rowY3, knobW, knobH);

    // -----------------------------------------------------------------------
    // Row 4 – slots 0-2: knobs  |  slots 3-5: buttons (vertically centred)
    // -----------------------------------------------------------------------
    predelaySlider  .setBounds(startX + 0 * knobW, rowY4, knobW, knobH);
    fdnOrderSlider  .setBounds(startX + 1 * knobW, rowY4, knobW, knobH);
    tankStagesSlider.setBounds(startX + 2 * knobW, rowY4, knobW, knobH);

    constexpr int btnH  = 36;
    const     int btnY  = rowY4 + (knobH - btnH) / 2;   // vertically centred in knobH
    constexpr int btnPad = 5;

    randomizeParamBtn .setBounds(startX + 3 * knobW + btnPad, btnY, knobW - btnPad * 2, btnH);
    randomizeMatrixBtn.setBounds(startX + 4 * knobW + btnPad, btnY, knobW - btnPad * 2, btnH);
    freezeBtn         .setBounds(startX + 5 * knobW + btnPad, btnY, knobW - btnPad * 2, btnH);

    // -----------------------------------------------------------------------
    // Header buttons (top-right)
    // -----------------------------------------------------------------------
    saveBtn.setBounds(getWidth() - 218, 20, 96, 28);
    loadBtn.setBounds(getWidth() - 114, 20, 96, 28);
}
