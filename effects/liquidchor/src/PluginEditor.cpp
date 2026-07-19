/*
  ==============================================================================
    LiquidChor - Roland Juno BBD Chorus
    PluginEditor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::Slider& LiquidChorAudioProcessorEditor::makeKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setLookAndFeel (&lnf);
    addAndMakeVisible (s);
    return s;
}

juce::Label& LiquidChorAudioProcessorEditor::makeLabel (juce::Label& l,
                                                         const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (juce::FontOptions().withHeight (10.5f)));
    l.setColour (juce::Label::textColourId, LCColours::inkFaint);
    l.setLookAndFeel (&lnf);
    addAndMakeVisible (l);
    return l;
}

void LiquidChorAudioProcessorEditor::placeKnob (juce::Slider& s, juce::Label& l,
                                                  int cx, int cy)
{
    s.setBounds (cx - KNOB_SZ / 2, cy - KNOB_SZ / 2, KNOB_SZ, KNOB_SZ);
    l.setBounds (cx - LABEL_W / 2, cy + KNOB_SZ / 2 + 2, LABEL_W, LABEL_H);
}

void LiquidChorAudioProcessorEditor::placeKnobN (juce::Slider& s, juce::Label& l,
                                                   int cx, int cy)
{
    s.setBounds (cx - KNOB_SZ / 2, cy - KNOB_SZ / 2, KNOB_SZ, KNOB_SZ);
    l.setBounds (cx - LABEL_WN / 2, cy + KNOB_SZ / 2 + 2, LABEL_WN, LABEL_H);
}

//==============================================================================
LiquidChorAudioProcessorEditor::LiquidChorAudioProcessorEditor (
    LiquidChorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (714, 425);
    setLookAndFeel (&lnf);

    // ── DELAY ───────────────────────────────────────────────────────────
    makeKnob  (time1Slider);    makeLabel (time1Label,    "TIME 1");
    makeKnob  (time2Slider);    makeLabel (time2Label,    "TIME 2");
    makeKnob  (feedbackSlider); makeLabel (feedbackLabel, "FEEDBACK");
    makeKnob  (hpassSlider);    makeLabel (hpassLabel,    "H PASS");
    makeKnob  (lpassSlider);    makeLabel (lpassLabel,    "L PASS");

    time1Slider   .setTextValueSuffix (" ms");
    time2Slider   .setTextValueSuffix (" ms");
    hpassSlider   .setTextValueSuffix (" Hz");
    lpassSlider   .setTextValueSuffix (" Hz");

    // ── MODULATION ──────────────────────────────────────────────────────
    makeKnob  (startPhaseSlider); makeLabel (startPhaseLabel, "START PHASE");
    makeKnob  (lrPhaseSlider);    makeLabel (lrPhaseLabel,    "LR PHASE");
    makeKnob  (speedSlider);      makeLabel (speedLabel,      "SPEED");

    startPhaseSlider.setTextValueSuffix ("deg");
    lrPhaseSlider   .setTextValueSuffix ("deg");
    speedSlider     .setTextValueSuffix (" Hz");

    lfoTypeBox.addItem ("Sine", 1);
    lfoTypeBox.addItem ("Saw",  2);
    lfoTypeBox.setLookAndFeel (&lnf);
    addAndMakeVisible (lfoTypeBox);
    makeLabel (lfoTypeLabel, "LFO TYPE");
    lfoTypeLabel.setColour (juce::Label::textColourId, LCColours::inkFaint);

    tempoSyncButton.setButtonText ("SYNC");
    tempoSyncButton.setLookAndFeel (&lnf);
    addAndMakeVisible (tempoSyncButton);

    syncDivBox.addItem ("1/16", 1);
    syncDivBox.addItem ("1/8",  2);
    syncDivBox.addItem ("1/4",  3);
    syncDivBox.addItem ("1/2",  4);
    syncDivBox.addItem ("1/1",  5);
    syncDivBox.setLookAndFeel (&lnf);
    addAndMakeVisible (syncDivBox);
    makeLabel (syncDivLabel, "SYNC DIV");
    syncDivLabel.setColour (juce::Label::textColourId, LCColours::inkFaint);

    // ── LEVELS ──────────────────────────────────────────────────────────
    makeKnob (noiseSlider); makeLabel (noiseLabel, "NOISE");
    makeKnob (gainSlider);  makeLabel (gainLabel,  "GAIN");
    makeKnob (mixSlider);   makeLabel (mixLabel,   "MIX");
    gainSlider.setTextValueSuffix (" dB");

    invertWetButton.setButtonText ("INV WET");
    invertWetButton.setLookAndFeel (&lnf);
    addAndMakeVisible (invertWetButton);

    noiseGateButton.setButtonText ("N GATE");
    noiseGateButton.setLookAndFeel (&lnf);
    addAndMakeVisible (noiseGateButton);

    // Noise mode combo
    noiseModeBox.addItem ("R to L",      1);
    noiseModeBox.addItem ("L to R",      2);
    noiseModeBox.addItem ("Back & Forth",           3);
    noiseModeBox.addItem ("L / R Alt.",             4);
    noiseModeBox.setLookAndFeel (&lnf);
    addAndMakeVisible (noiseModeBox);
    makeLabel (noiseModeLabel, "NOISE SWEEP");
    noiseModeLabel.setColour (juce::Label::textColourId, LCColours::inkFaint);

    // ── Attachments ─────────────────────────────────────────────────────
    auto& apvts = audioProcessor.apvts;

    time1Att      = std::make_unique<SliderAtt> (apvts, "time1",      time1Slider);
    time2Att      = std::make_unique<SliderAtt> (apvts, "time2",      time2Slider);
    feedbackAtt   = std::make_unique<SliderAtt> (apvts, "feedback",   feedbackSlider);
    hpassAtt      = std::make_unique<SliderAtt> (apvts, "hpass",      hpassSlider);
    lpassAtt      = std::make_unique<SliderAtt> (apvts, "lpass",      lpassSlider);

    startPhaseAtt = std::make_unique<SliderAtt> (apvts, "startPhase", startPhaseSlider);
    lrPhaseAtt    = std::make_unique<SliderAtt> (apvts, "lrPhase",    lrPhaseSlider);
    speedAtt      = std::make_unique<SliderAtt> (apvts, "speed",      speedSlider);
    lfoTypeAtt    = std::make_unique<ComboAtt>  (apvts, "lfoType",    lfoTypeBox);
    syncDivAtt    = std::make_unique<ComboAtt>  (apvts, "syncDiv",    syncDivBox);
    tempoSyncAtt  = std::make_unique<ButtonAtt> (apvts, "tempoSync",  tempoSyncButton);

    invertWetAtt  = std::make_unique<ButtonAtt> (apvts, "invertWet",  invertWetButton);
    noiseGateAtt  = std::make_unique<ButtonAtt> (apvts, "noiseGate",  noiseGateButton);
    noiseAtt      = std::make_unique<SliderAtt> (apvts, "noise",      noiseSlider);
    gainAtt       = std::make_unique<SliderAtt> (apvts, "gain",       gainSlider);
    mixAtt        = std::make_unique<SliderAtt> (apvts, "mix",        mixSlider);
    noiseModeAtt  = std::make_unique<ComboAtt>  (apvts, "noiseMode",  noiseModeBox);

    // ── Tempo sync initial visibility ────────────────────────────────────
    const bool syncedNow =
        *apvts.getRawParameterValue ("tempoSync") > 0.5f;
    speedSlider  .setVisible (!syncedNow);
    speedLabel   .setVisible (!syncedNow);
    syncDivBox   .setVisible ( syncedNow);
    syncDivLabel .setVisible ( syncedNow);

    apvts.addParameterListener ("tempoSync", this);
}

LiquidChorAudioProcessorEditor::~LiquidChorAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener ("tempoSync", this);
    setLookAndFeel (nullptr);

    for (auto* c : std::initializer_list<juce::Component*> {
            &time1Slider, &time2Slider, &feedbackSlider, &hpassSlider, &lpassSlider,
            &startPhaseSlider, &lrPhaseSlider, &speedSlider,
            &noiseSlider, &gainSlider, &mixSlider,
            &lfoTypeBox, &syncDivBox, &noiseModeBox,
            &tempoSyncButton, &invertWetButton, &noiseGateButton,
            &time1Label, &time2Label, &feedbackLabel, &hpassLabel, &lpassLabel,
            &startPhaseLabel, &lrPhaseLabel, &speedLabel,
            &noiseLabel, &gainLabel, &mixLabel,
            &lfoTypeLabel, &syncDivLabel, &noiseModeLabel })
    {
        c->setLookAndFeel (nullptr);
    }
}

//==============================================================================
void LiquidChorAudioProcessorEditor::parameterChanged (const juce::String& paramID,
                                                        float newValue)
{
    if (paramID == "tempoSync")
    {
        const bool synced = newValue > 0.5f;
        juce::MessageManager::callAsync ([this, synced]
        {
            speedSlider .setVisible (!synced);
            speedLabel  .setVisible (!synced);
            syncDivBox  .setVisible ( synced);
            syncDivLabel.setVisible ( synced);
        });
    }
}

//==============================================================================
void LiquidChorAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W       = getWidth();
    const int H       = getHeight();
    const int headerH = 60;
    const int secW    = W / 3;

    // ── Background — warm washi paper ────────────────────────────────────
    g.setColour (LCColours::paper);
    g.fillAll();

    // Subtle paper grain lines
    g.setColour (LCColours::paperDeep.withAlpha (0.35f));
    for (int y = headerH + 8; y < H; y += 12)
        g.drawLine (0.0f, (float)y, (float)W, (float)y, 0.5f);

    // ── Header ────────────────────────────────────────────────────────────
    g.setColour (LCColours::headerBg);
    g.fillRect (0, 0, W, headerH);

    {
        g.setColour (LCColours::cyanDeep.withAlpha (0.4f));
        const float wy = (float)headerH * 0.65f;
        juce::Path wave1;
        wave1.startNewSubPath (0, wy);
        for (float x = 0; x <= W; x += 2.0f)
            wave1.lineTo (x, wy + 5.0f * std::sin (x * 0.07f));
        g.strokePath (wave1, juce::PathStrokeType (1.2f));

        g.setColour (LCColours::cyanBright.withAlpha (0.2f));
        juce::Path wave2;
        wave2.startNewSubPath (0, wy + 8.0f);
        for (float x = 0; x <= W; x += 2.0f)
            wave2.lineTo (x, wy + 8.0f + 4.0f * std::sin (x * 0.05f + 1.0f));
        g.strokePath (wave2, juce::PathStrokeType (1.0f));
    }

    g.setColour (LCColours::headerText);
    g.setFont (juce::Font (juce::FontOptions().withHeight (26.0f).withStyle ("Bold")));
    g.drawText ("LIQUIDCHOR", 18, 8, 260, 34, juce::Justification::centredLeft);

    g.setColour (LCColours::cyanGlow);
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.5f)));
    g.drawText ("Classic BBD Character Chorus", 18, 38, 260, 16,
                juce::Justification::centredLeft);

    g.setColour (LCColours::headerText.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.5f)));
    g.drawText ("v1.0", W - 34, headerH - 18, 28, 14,
                juce::Justification::centredRight);

    // ── Section panels ────────────────────────────────────────────────────
    const int panelTop = headerH + 2;
    const int panelH   = H - panelTop;

    g.setColour (LCColours::paperDark.withAlpha (0.45f));
    g.fillRect (secW, panelTop, secW, panelH);

    g.setColour (LCColours::paperDeep);
    g.fillRect (secW - 1, panelTop + 4, 2, panelH - 8);
    g.fillRect (secW * 2 - 1, panelTop + 4, 2, panelH - 8);

    // ── Section title bars ────────────────────────────────────────────────
    auto drawSectionTitle = [&] (const juce::String& text, int sx)
    {
        const int ty = headerH + 8;
        g.setColour (LCColours::cyanDeep.withAlpha (0.12f));
        g.fillRoundedRectangle ((float)sx + 10, (float)ty,
                                (float)secW - 20, 20.0f, 4.0f);
        g.setColour (LCColours::cyanDeep);
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)
                                                  .withStyle ("Bold")));
        g.drawText (text, sx + 10, ty, secW - 20, 20,
                    juce::Justification::centred);
    };

    drawSectionTitle ("DELAY",      0);
    drawSectionTitle ("MODULATION", secW);
    drawSectionTitle ("LEVELS",     secW * 2);

    // ── Subtle mid-row separators ─────────────────────────────────────────
    g.setColour (LCColours::paperDeep.withAlpha (0.6f));
    const int midY = headerH + 2 + panelH / 2;
    g.drawLine (12.0f,              (float)midY, (float)secW - 12,       (float)midY, 0.5f);
    g.drawLine ((float)secW + 12,   (float)midY, (float)secW * 2 - 12,   (float)midY, 0.5f);
    g.drawLine ((float)secW * 2 + 12, (float)midY, (float)W - 12,         (float)midY, 0.5f);
}

//==============================================================================
void LiquidChorAudioProcessorEditor::resized()
{
    const int W    = getWidth();
    const int secW = W / 3;
    const int hdrH = 60;

    // ── Column centre X — each section split into two equal columns ───────
    const int d_c1  = secW / 4;              // DELAY 2-col: left
    const int d_c2  = secW * 3 / 4;          //              right

    // 3-column positions used for DELAY row 2
    const int d3_c1 = secW / 6;              // ~40
    const int d3_c2 = secW / 2;              // ~119
    const int d3_c3 = secW * 5 / 6;          // ~198

    const int m_c1  = secW + secW / 4;       // MOD   col 1
    const int m_c2  = secW + secW * 3 / 4;   //       col 2
    const int l_c1  = secW * 2 + secW / 4;   // LEVELS col 1
    const int l_c2  = secW * 2 + secW * 3/4; //        col 2

    // ── Row Y centres (tighter than before) ───────────────────────────────
    const int togY   = hdrH + 34;    // toggle buttons
    const int row1Y  = hdrH + 105;   // first knob row
    const int ctrlY  = hdrH + 200;   // combo / button strip between rows
    const int row2Y  = hdrH + 300;   // second knob row

    // ── DELAY ─────────────────────────────────────────────────────────────
    // Row 1: TIME 1, TIME 2
    placeKnob (time1Slider, time1Label, d_c1, row1Y);
    placeKnob (time2Slider, time2Label, d_c2, row1Y);

    // Row 2: FEEDBACK, L PASS, H PASS  (3 columns — narrow labels)
    placeKnobN (feedbackSlider, feedbackLabel, d3_c1, row2Y);
    placeKnobN (lpassSlider,    lpassLabel,    d3_c2, row2Y);
    placeKnobN (hpassSlider,    hpassLabel,    d3_c3, row2Y);

    // ── MODULATION ────────────────────────────────────────────────────────
    // Row 1: START PHASE, LR PHASE
    placeKnob (startPhaseSlider, startPhaseLabel, m_c1, row1Y);
    placeKnob (lrPhaseSlider,    lrPhaseLabel,    m_c2, row1Y);

    // Controls strip
    lfoTypeLabel  .setBounds (m_c1 - 38, ctrlY - 14, 76, 14);
    lfoTypeBox    .setBounds (m_c1 - 36, ctrlY,      72, 24);
    tempoSyncButton.setBounds (m_c2 - 36, ctrlY,     72, 24);

    // Row 2: SPEED knob (hidden when tempoSync) or SYNC DIV combo
    placeKnob (speedSlider, speedLabel, m_c1, row2Y);

    const int sdY = row2Y - KNOB_SZ / 2;
    syncDivLabel.setBounds (m_c2 - 38, sdY - 14, 76, 14);
    syncDivBox  .setBounds (m_c2 - 36, sdY + 16, 72, 24);

    // ── LEVELS ────────────────────────────────────────────────────────────
    // Toggle row
    invertWetButton.setBounds (l_c1 - 36, togY, 72, 24);
    noiseGateButton.setBounds (l_c2 - 36, togY, 72, 24);

    // Row 1: NOISE, GAIN
    placeKnob (noiseSlider, noiseLabel, l_c1, row1Y);
    placeKnob (gainSlider,  gainLabel,  l_c2, row1Y);

    // Noise sweep combo (below NOISE knob, aligned with ctrlY)
    noiseModeLabel.setBounds (l_c1 - 54, ctrlY - 14, 108, 14);
    noiseModeBox  .setBounds (l_c1 - 54, ctrlY,      108, 24);

    // Row 2: MIX centred between both columns
    const int mixCX = (l_c1 + l_c2) / 2;
    placeKnob (mixSlider, mixLabel, mixCX, row2Y);
}
