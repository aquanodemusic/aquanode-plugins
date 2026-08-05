#include "PluginEditor.h"
#include <algorithm>

namespace layout
{
    constexpr int header = 30;
    constexpr int scope  = 300;
}

namespace col
{
    const juce::Colour bg{ 0xff121417 };
    const juce::Colour panel{ 0xff1a1d22 };
    const juce::Colour text{ 0xffd7dce3 };
    const juce::Colour dim{ 0xff7b828d };
    const juce::Colour green{ 0xff3cff96 };
    const juce::Colour purple{ 0xffa855f7 };
}

//==============================================================================
MorphLookAndFeel::MorphLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, col::bg);
    setColour(juce::Label::textColourId, col::text);
    setColour(juce::ComboBox::backgroundColourId, col::panel);
    setColour(juce::ComboBox::textColourId, col::text);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.15f));
    setColour(juce::ComboBox::arrowColourId, col::dim);
    setColour(juce::PopupMenu::backgroundColourId, col::panel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::green.withAlpha(0.25f));
    setColour(juce::PopupMenu::textColourId, col::text);
}

void MorphLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float pos, float startAngle, float endAngle,
    juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float thickness = juce::jmax(3.0f, radius * 0.16f);
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc(cx, cy, radius - thickness, radius - thickness,
        0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(cx, cy, radius - thickness, radius - thickness,
        0.0f, startAngle, angle, true);
    g.setColour(col::green);
    g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    const float a = angle - juce::MathConstants<float>::halfPi;
    const float r0 = radius * 0.25f, r1 = radius - thickness * 1.6f;
    g.setColour(col::text);
    g.drawLine(cx + r0 * std::cos(a), cy + r0 * std::sin(a),
        cx + r1 * std::cos(a), cy + r1 * std::sin(a), 2.0f);
}

void MorphLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
    bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();

    g.setColour(on ? col::purple.withAlpha(0.30f) : juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(on ? col::purple : juce::Colours::white.withAlpha(highlighted ? 0.30f : 0.16f));
    g.drawRoundedRectangle(r, 5.0f, 1.2f);

    g.setColour(on ? juce::Colours::white : col::dim);
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
SpectralMorphAudioProcessorEditor::SpectralMorphAudioProcessorEditor(SpectralMorphAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p), spectrogram(p.engine)
{
    setLookAndFeel(&lnf);
    addAndMakeVisible(spectrogram);

    // ---- FFT size / overlap -------------------------------------------------
    fftBox.addItemList({ "512", "1024", "2048", "4096", "8192", "16384", "32768" }, 1);
    addAndMakeVisible(fftBox);
    fftAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "fftSize", fftBox);

    overlapBox.addItemList({ "2x", "4x", "8x" }, 1);
    addAndMakeVisible(overlapBox);
    overlapAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "overlap", overlapBox);

    // ---- morph mode ---------------------------------------------------------
    morphModeBox.addItemList({ "Cepstral", "Spectral", "Vocoder", "Inject", "Partials" }, 1);
    addAndMakeVisible(morphModeBox);
    morphModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "morphMode", morphModeBox);
    morphModeBox.onChange = [this] { updateModeUI(); };

    auto setupCaption = [this](juce::Label& l, const juce::String& t)
        {
            l.setText(t, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setFont(juce::Font(juce::FontOptions(11.0f)));
            l.setColour(juce::Label::textColourId, col::dim);
            addAndMakeVisible(l);
        };
    setupCaption(fftLabel, "FFT SIZE");
    setupCaption(overlapLabel, "OVERLAP");
    setupCaption(morphModeLabel, "MORPH MODE");

    // ---- knobs --------------------------------------------------------------
    addKnob(morph, "morph", "MORPH");
    addKnob(clarity, "clarity", "CLARITY");
    addKnob(smooth, "smooth", "SMOOTH");
    addKnob(maxBoost, "maxBoost", "BOOST");
    addKnob(dynamics, "dynamics", "DYNAMICS");
    addKnob(mix, "mix", "MIX");
    addKnob(outGain, "outGain", "OUTPUT");

    addKnob(attack, "attack", "ATTACK");
    addKnob(release, "release", "RELEASE");
    addKnob(flatten, "flatten", "FLATTEN");
    addKnob(sibilance, "sibilance", "SIBILANCE");
    addKnob(fill, "fill", "FILL");
    addKnob(fold, "fold", "FOLD");
    addKnob(glide, "glide", "GLIDE");
    addKnob(lock, "lock", "LOCK");
    addKnob(peakFloor, "peakFloor", "PEAKS");

    // ---- toggles ------------------------------------------------------------
    addToggle(flipButton, flipAtt, "flip", "FLIP");
    addToggle(freezeButton, freezeAtt, "freezeSide", "FREEZE SIDE");
    addToggle(bypassButton, bypassAtt, "bypass", "BYPASS");

    // spectrogram on/off - not an APVTS parameter, just a display convenience
    spectrogramButton.setButtonText("SPECTROGRAM");
    spectrogramButton.setToggleState(true, juce::dontSendNotification);
    spectrogramButton.onClick = [this]
        {
            spectrogram.setActive(spectrogramButton.getToggleState());
        };
    addAndMakeVisible(spectrogramButton);

    // HD Visuals on/off - defaults to on. Reflects whatever state the engine
    // is already in (it survives editor open/close since it lives on proc.engine).
    hdVisualsButton.setButtonText("HD VISUALS");
    hdVisualsButton.setToggleState(proc.engine.isHDVisualsEnabled(), juce::dontSendNotification);
    hdVisualsButton.onClick = [this]
        {
            const bool on = hdVisualsButton.getToggleState();
            proc.engine.setHDVisualsEnabled(on);
            spectrogram.setHDVisuals(on);
        };
    addAndMakeVisible(hdVisualsButton);
    spectrogram.setHDVisuals(hdVisualsButton.getToggleState());

    flipButton.onStateChange = [this]
        {
            const bool f = flipButton.getToggleState();
            routingLabel.setText(f ? "SIDECHAIN carries  <-  MAIN timbre"
                : "MAIN carries  <-  SIDECHAIN timbre",
                juce::dontSendNotification);
        };
    routingLabel.setJustificationType(juce::Justification::centred);
    routingLabel.setFont(juce::Font(juce::FontOptions(11.5f)));
    routingLabel.setColour(juce::Label::textColourId, col::dim);
    addAndMakeVisible(routingLabel);
    flipButton.onStateChange();

    modeInfoLabel.setJustificationType(juce::Justification::centredLeft);
    modeInfoLabel.setFont(juce::Font(juce::FontOptions(11.5f)));
    modeInfoLabel.setColour(juce::Label::textColourId, col::dim);
    addAndMakeVisible(modeInfoLabel);

    updateModeUI();

    setSize(980, 700);
}

//==============================================================================
//  Each mode exposes only the controls it actually uses, so the panel stays
//  honest about what is doing the work. Anything not in the active list is
//  hidden rather than greyed - a knob that does nothing is worse than absent.
void SpectralMorphAudioProcessorEditor::updateModeUI()
{
    const int mode = juce::jmax(0, morphModeBox.getSelectedItemIndex());

    activeKnobs.clear();

    switch (mode)
    {
        case 0:     // Cepstral
        case 1:     // Spectral
            activeKnobs = { &morph, &clarity, &smooth, &maxBoost, &dynamics, &mix, &outGain };
            break;

        case 2:     // Vocoder
            activeKnobs = { &morph, &clarity, &flatten, &sibilance, &maxBoost, &dynamics,
                            &attack, &release, &mix, &outGain };
            break;

        case 3:     // Inject
            activeKnobs = { &morph, &clarity, &flatten, &sibilance, &fill, &fold,
                            &attack, &release, &maxBoost, &dynamics, &mix, &outGain };
            break;

        default:    // Partials
            activeKnobs = { &morph, &clarity, &glide, &lock, &peakFloor, &flatten,
                            &attack, &release, &maxBoost, &dynamics, &mix, &outGain };
            break;
    }

    Knob* all[] = { &morph, &clarity, &smooth, &maxBoost, &dynamics, &mix, &outGain,
                    &attack, &release, &flatten, &sibilance, &fill, &fold,
                    &glide, &lock, &peakFloor };

    for (auto* k : all)
    {
        const bool on = std::find(activeKnobs.begin(), activeKnobs.end(), k) != activeKnobs.end();
        k->slider.setVisible(on);
        k->label.setVisible(on);
    }

    // Clarity drives a different underlying quantity in the Spectral mode
    // (box-average width rather than cepstral lifter cutoff), so relabel it.
    clarity.label.setText(mode == 1 ? "DETAIL" : "CLARITY", juce::dontSendNotification);

    static const char* info[] =
    {
        "CEPSTRAL  -  liftered envelope transfer. Zero phase, no smearing. The original.",
        "SPECTRAL  -  box-averaged envelope. Cheaper, a touch softer around sharp peaks.",
        "VOCODER  -  whitens the carrier so formants can actually land, and passes the "
            "modulator's noise through. This is where consonants start to survive.",
        "INJECT  -  Vocoder plus synthesis: where the carrier has no energy to shape, "
            "the missing spectrum is added as noise or as an octave fold of the carrier.",
        "PARTIALS  -  matches carrier partials to the modulator's and moves them there, "
            "phase-locked. Glide at 0 % collapses back to Cepstral exactly."
    };
    modeInfoLabel.setText(info[juce::jlimit(0, 4, mode)], juce::dontSendNotification);

    resized();
}

SpectralMorphAudioProcessorEditor::~SpectralMorphAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SpectralMorphAudioProcessorEditor::addKnob(Knob& k, const juce::String& id,
    const juce::String& text)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 16);
    k.slider.setColour(juce::Slider::textBoxTextColourId, col::text);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(k.slider);

    k.label.setText(text, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(11.0f)));
    k.label.setColour(juce::Label::textColourId, col::dim);
    addAndMakeVisible(k.label);

    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, id, k.slider);
}

void SpectralMorphAudioProcessorEditor::addToggle(
    juce::ToggleButton& b,
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att,
    const juce::String& id, const juce::String& text)
{
    b.setButtonText(text);
    addAndMakeVisible(b);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, id, b);
}

//==============================================================================
void SpectralMorphAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);

    auto header = getLocalBounds().removeFromTop(30);
    g.setColour(col::text);
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText("SPECTRALMORPH", header.reduced(12, 0), juce::Justification::centredLeft);

    // reserve the top-right corner for the HD Visuals toggle (a real component,
    // positioned identically in resized() - nothing to paint here)
    header.removeFromRight(150);

    // legend
    auto legend = header.removeFromRight(250).reduced(10, 8);
    auto dot = [&](juce::Colour c, const juce::String& t, juce::Rectangle<int> r)
        {
            g.setColour(c);
            g.fillRoundedRectangle(r.removeFromLeft(10).toFloat().withSizeKeepingCentre(9.0f, 9.0f), 2.0f);
            g.setColour(col::dim);
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.drawText(t, r.reduced(5, 0), juce::Justification::centredLeft);
        };
    dot(col::green, "MAIN", legend.removeFromLeft(85));
    dot(col::purple, "SIDECHAIN", legend);

    // control panel background
    auto panel = getLocalBounds().withTrimmedTop(layout::header + layout::scope).reduced(8, 4);
    g.setColour(col::panel);
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
}

void SpectralMorphAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(layout::header);
    hdVisualsButton.setBounds(header.removeFromRight(150).reduced(6, 3));

    spectrogram.setBounds(r.removeFromTop(layout::scope).reduced(8, 4));

    auto panel = r.reduced(8, 4).reduced(12, 10);

    // --- top row of the panel: selectors + routing readout -------------------
    auto top = panel.removeFromTop(46);

    auto fftArea = top.removeFromLeft(110);
    fftLabel.setBounds(fftArea.removeFromTop(14));
    fftBox.setBounds(fftArea.reduced(0, 2));

    top.removeFromLeft(10);
    auto ovArea = top.removeFromLeft(90);
    overlapLabel.setBounds(ovArea.removeFromTop(14));
    overlapBox.setBounds(ovArea.reduced(0, 2));

    top.removeFromLeft(10);
    auto morphModeArea = top.removeFromLeft(100);
    morphModeLabel.setBounds(morphModeArea.removeFromTop(14));
    morphModeBox.setBounds(morphModeArea.reduced(0, 2));

    top.removeFromLeft(16);
    routingLabel.setBounds(top.removeFromLeft(200).withSizeKeepingCentre(200, 20));

    auto toggles = top.reduced(0, 8);
    const int tw = juce::jmax(62, (toggles.getWidth() - 18) / 4);
    flipButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));
    freezeButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));
    bypassButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));
    spectrogramButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));

    panel.removeFromTop(4);
    modeInfoLabel.setBounds(panel.removeFromTop(16));
    panel.removeFromTop(6);

    // --- knob rows -----------------------------------------------------------
    // Up to six per row; more than that wraps, and a short final row is centred
    // under the one above it rather than stretched to fill.
    const int n = (int)activeKnobs.size();
    if (n == 0) return;

    const int rows   = (n + 5) / 6;
    const int perRow = (n + rows - 1) / rows;
    const int rowH   = juce::jmin(150, panel.getHeight() / rows);

    auto block = panel.withSizeKeepingCentre(panel.getWidth(), rowH * rows);
    const int kw = block.getWidth() / perRow;

    int idx = 0;
    for (int row = 0; row < rows; ++row)
    {
        const int count = juce::jmin(perRow, n - idx);
        auto strip = block.removeFromTop(rowH)
                          .withSizeKeepingCentre(kw * count, rowH);

        for (int i = 0; i < count; ++i)
        {
            auto cell = strip.removeFromLeft(kw);
            auto* k = activeKnobs[(size_t)idx++];
            k->label.setBounds(cell.removeFromTop(14));
            k->slider.setBounds(cell.reduced(6, 0));
        }
    }
}