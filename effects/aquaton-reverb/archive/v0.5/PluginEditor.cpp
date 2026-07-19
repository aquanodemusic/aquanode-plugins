#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Colour palette
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
    const juce::Colour divider     = juce::Colour(0x55ffffff);
}

//==============================================================================
AquatonAudioProcessorEditor::AquatonAudioProcessorEditor(AquatonAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Attachment helper
    auto attach = [&](juce::Slider& s, const juce::String& id)
    {
        sliderAttachments.push_back(
            std::make_unique<SliderAttach>(audioProcessor.apvts, id, s));
    };

    // Knob factory: sets up rotary style, colours, and attaches to parameter
    auto makeKnob = [&](juce::Slider& s, const juce::String& id)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId,          AquaColors::labelText);
        s.setColour(juce::Slider::rotarySliderFillColourId,     AquaColors::knobThumb);
        s.setColour(juce::Slider::rotarySliderOutlineColourId,  AquaColors::knobTrack);
        s.setMouseDragSensitivity(200);
        addAndMakeVisible(s);
        attach(s, id);
    };

    //--- Row 1: Core reverb ---
    makeKnob(sizeSlider,     "size");
    makeKnob(feedbackSlider, "feedback"); 
    makeKnob(lpSlider,       "lpCutoff");
    makeKnob(hpSlider,       "hpCutoff");
    makeKnob(mixSlider,      "mix");

    //--- Row 2: Character ---
    makeKnob(preDiffSlider,  "preDiffuse");
    makeKnob(tankDiffSlider, "tankDiffuse");
    makeKnob(modRateSlider,  "modRate");
    makeKnob(modDepthSlider, "modDepth");
    makeKnob(spreadSlider,   "spread");

    //--- Row 3: Spatial / HF ---
    makeKnob(bloomAmtSlider,  "bloomAmount");
    makeKnob(bloomTimeSlider, "bloomTime");
    makeKnob(tapAmtSlider,    "tapAmount");
    makeKnob(hfWashHPSlider,  "hfWashHP");
    makeKnob(hfWashAmtSlider, "hfWashAmt");

    // Show integer values for the tap amount knob
    tapAmtSlider.setNumDecimalPlacesToDisplay(0);

    //--- Tank Stages horizontal slider (0–256) ---
    tankStagesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tankStagesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    tankStagesSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tankStagesSlider.setColour(juce::Slider::textBoxTextColourId,    AquaColors::labelText);
    tankStagesSlider.setColour(juce::Slider::thumbColourId,          AquaColors::knobThumb);
    tankStagesSlider.setColour(juce::Slider::trackColourId,          AquaColors::knobTrack);
    tankStagesSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(tankStagesSlider);
    attach(tankStagesSlider, "tankStages");

    //--- FDN Order horizontal slider (1–32) ---
    fdnOrderSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fdnOrderSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    fdnOrderSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    fdnOrderSlider.setColour(juce::Slider::textBoxTextColourId,    AquaColors::labelText);
    fdnOrderSlider.setColour(juce::Slider::thumbColourId,          AquaColors::knobThumb);
    fdnOrderSlider.setColour(juce::Slider::trackColourId,          AquaColors::knobTrack);
    fdnOrderSlider.setNumDecimalPlacesToDisplay(0);
    addAndMakeVisible(fdnOrderSlider);
    attach(fdnOrderSlider, "fdnOrder");

    //--- Buttons ---
    randomizeBtn.setButtonText("Rand. Matrix");
    randomizeBtn.setColour(juce::TextButton::buttonColourId,  AquaColors::btnNormal);
    randomizeBtn.setColour(juce::TextButton::textColourOffId, AquaColors::labelText);
    randomizeBtn.onClick = [this]() { audioProcessor.randomizeMatrix(); };
    addAndMakeVisible(randomizeBtn);

    randomizeAllBtn.setButtonText("Randomize");
    randomizeAllBtn.setColour(juce::TextButton::buttonColourId,  AquaColors::btnNormal);
    randomizeAllBtn.setColour(juce::TextButton::textColourOffId, AquaColors::labelText);
    randomizeAllBtn.onClick = [this]()
    {
        juce::Random rng;
        for (auto* param : audioProcessor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
                rp->setValueNotifyingHost(rng.nextFloat());
    };
    addAndMakeVisible(randomizeAllBtn);

    wetOnlyBtn.setButtonText("Wet Only");
    wetOnlyBtn.setClickingTogglesState(true);
    wetOnlyBtn.setColour(juce::TextButton::buttonColourId,   AquaColors::btnNormal);
    wetOnlyBtn.setColour(juce::TextButton::buttonOnColourId, AquaColors::btnActive);
    wetOnlyBtn.setColour(juce::TextButton::textColourOffId,  AquaColors::labelText);
    wetOnlyBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
    wetOnlyAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, "wetOnly", wetOnlyBtn);
    addAndMakeVisible(wetOnlyBtn);

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

    //--- Knob/label parallel array for paint() ---
    knobs = { {
        // Row 1
        { &sizeSlider,      "Size"        },
        { &feedbackSlider,  "Feedback"    },
        { &lpSlider,        "LP Freq"     },
        { &hpSlider,        "HP Freq"     },
        { &mixSlider,       "Mix"         },
        // Row 2
        { &preDiffSlider,   "Pre-Diffuse"  },
        { &tankDiffSlider,  "Tank-Diffuse" },
        { &modRateSlider,   "Mod Rate"     },
        { &modDepthSlider,  "Mod Depth"    },
        { &spreadSlider,    "Spread"       },
        // Row 3
        { &bloomAmtSlider,  "Bloom Amount" },
        { &bloomTimeSlider, "Bloom Time"   },
        { &tapAmtSlider,    "Tap Amount"   },
        { &hfWashHPSlider,  "HF Wash HP"   },
        { &hfWashAmtSlider, "HF Wash Amt"  },
    } };

    setSize(600, 526);
}

AquatonAudioProcessorEditor::~AquatonAudioProcessorEditor() {}

//==============================================================================
void AquatonAudioProcessorEditor::saveState()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Aquaton Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result != juce::File{})
            {
                auto state = audioProcessor.apvts.copyState();
                if (auto xml = state.createXml())
                    xml->writeTo(result);
            }
        });
}

void AquatonAudioProcessorEditor::loadState()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Aquaton Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                if (auto xml = juce::XmlDocument::parse(result))
                {
                    if (xml->hasTagName(audioProcessor.apvts.state.getType()))
                        audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*xml));
                }
            }
        });
}

//==============================================================================
void AquatonAudioProcessorEditor::paint(juce::Graphics& g)
{
    //--- Gradient background ---
    juce::ColourGradient grad(AquaColors::background1, 0.f, 0.f,
                              AquaColors::background2, 0.f, (float)getHeight(), false);
    g.setFillType(juce::FillType(grad));
    g.fillAll();

    //--- Header ---
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(38.f, juce::Font::bold));
    g.drawText("Aquaton", juce::Rectangle<int>(28, 14, 220, 44),
               juce::Justification::centredLeft, false);

    g.setFont(14.f);
    g.setColour(AquaColors::subText);
    g.drawText("Large Reverb by aquanode", juce::Rectangle<int>(30, 54, 220, 20),
               juce::Justification::centredLeft, false);

    //--- Knob labels (drawn 14px above each knob) ---
    g.setFont(juce::Font(11.f, juce::Font::bold));
    g.setColour(AquaColors::labelText);
    for (auto& k : knobs)
    {
        auto b = k.slider->getBounds();
        g.drawFittedText(k.label,
                         b.withY(b.getY() - 14).withHeight(14),
                         juce::Justification::centredBottom, 1);
    }

    //--- Tank Stages label (drawn to the left of the slider) ---
    g.setFont(juce::Font(11.f, juce::Font::bold));
    g.setColour(AquaColors::labelText);
    {
        auto tb = tankStagesSlider.getBounds();
        g.drawFittedText("Tank Stages (CPU)",
                         tb.withX(tb.getX() - 88).withWidth(85),
                         juce::Justification::centredRight, 1);
    }

    //--- FDN Order label ---
    {
        auto fb = fdnOrderSlider.getBounds();
        g.drawFittedText("FDN Order",
                         fb.withX(fb.getX() - 88).withWidth(85),
                         juce::Justification::centredRight, 1);
    }

    //--- Section divider lines between knob rows ---
    g.setColour(AquaColors::divider);
    if (knobs[5].slider->isVisible())
        g.drawHorizontalLine(knobs[5].slider->getY() + 4, 28.f, (float)(getWidth() - 28));
    if (knobs[5].slider->isVisible())
        g.drawHorizontalLine(knobs[5].slider->getY() - 110, 28.f, (float)(getWidth() - 28));
    if (knobs[10].slider->isVisible())
        g.drawHorizontalLine(knobs[10].slider->getY() + 4, 28.f, (float)(getWidth() - 28));
}

//==============================================================================
void AquatonAudioProcessorEditor::resized()
{
    const int knobW  = 100;
    const int knobH  = 100;
    const int startX = (getWidth() - 5 * knobW) / 2;  // (600-500)/2 = 50

    // Three knob rows — pushed down to leave breathing room below the header
    const int rowY1 = 105;
    const int rowY2 = rowY1 + knobH + 12;  // 217
    const int rowY3 = rowY2 + knobH + 12;  // 329
    const int btmY  = rowY3 + knobH + 10;  // 439

    //--- Row 1: Size, Feedback, LP Freq, HP Freq, Mix ---
    juce::Slider* row1[] = { &sizeSlider, &feedbackSlider, &lpSlider, &hpSlider, &mixSlider };
    for (int i = 0; i < 5; ++i)
        row1[i]->setBounds(startX + i * knobW, rowY1, knobW, knobH);

    //--- Row 2: Pre-Diffuse, Tank-Diffuse, Mod Rate, Mod Depth, Spread ---
    juce::Slider* row2[] = { &preDiffSlider, &tankDiffSlider, &modRateSlider,
                              &modDepthSlider, &spreadSlider };
    for (int i = 0; i < 5; ++i)
        row2[i]->setBounds(startX + i * knobW, rowY2, knobW, knobH);

    //--- Row 3: Bloom Amt, Bloom Time, Tap Amount, HF Wash HP, HF Wash Amt ---
    juce::Slider* row3[] = { &bloomAmtSlider, &bloomTimeSlider, &tapAmtSlider,
                              &hfWashHPSlider, &hfWashAmtSlider };
    for (int i = 0; i < 5; ++i)
        row3[i]->setBounds(startX + i * knobW, rowY3, knobW, knobH);

    //--- Randomize All / Save / Load buttons (header, top-right) ---
    randomizeAllBtn.setBounds(getWidth() - 358, 22, 105, 30);
    saveBtn.setBounds        (getWidth() - 245, 22, 105, 30);
    loadBtn.setBounds        (getWidth() - 132, 22, 105, 30);

    //--- Bottom controls ---
    const int btnH   = 36;
    const int margin = 22;
    int cx = margin;

    randomizeBtn.setBounds(cx, btmY, 110, btnH);  cx += 122;
    wetOnlyBtn.setBounds  (cx, btmY, 100, btnH);  cx += 116;

    // Tank Stages: label is painted, slider placed 88px after cx
    tankStagesSlider.setBounds(cx + 88, btmY + 8, 180, btnH - 16);

    // FDN Order: same x alignment as Tank Stages, one row below
    fdnOrderSlider.setBounds(cx + 88, btmY + 8 + 36, 180, btnH - 16);
    // cx + 88 + 180 = 22 + 122 + 116 + 88 + 180 = 528 — fits within 600
}
