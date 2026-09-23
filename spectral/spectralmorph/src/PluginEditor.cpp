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

bool PercentStepSlider::keyPressed(const juce::KeyPress& key)
{
    // Leave text editing and host shortcuts to JUCE/the host.
    if (! hasKeyboardFocus(false))
        return false;

    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return juce::Slider::keyPressed(key);

    const int code = key.getKeyCode();
    const int direction = (code == juce::KeyPress::upKey
                        || code == juce::KeyPress::rightKey) ? 1
                        : (code == juce::KeyPress::downKey
                        || code == juce::KeyPress::leftKey) ? -1 : 0;
    if (direction == 0)
        return juce::Slider::keyPressed(key);

    // Percentage controls use displayed percentage points; other controls
    // use a percentage of their full range. Shift gives one tenth of the step.
    const double unit = percentageValue ? 1.0 : getRange().getLength();
    const double percent = mods.isShiftDown() ? 0.001 : 0.01;
    const double step = juce::jmax(getInterval(), unit * percent);
    juce::Slider::ScopedDragNotification drag(*this);
    setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + direction * step),
             juce::sendNotificationSync);
    return true;
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
    juce::Slider& slider)
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

    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(col::green);
        g.drawRoundedRectangle(bounds.expanded(2.0f), 5.0f, 2.0f);
    }
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

    if (b.hasKeyboardFocus(true))
    {
        g.setColour(col::green);
        g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, 2.0f);
    }
}

//==============================================================================
SpectralMorphAudioProcessorEditor::SpectralMorphAudioProcessorEditor(SpectralMorphAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p), spectrogram(p.engine)
{
    setLookAndFeel(&lnf);
    setTitle("SpectralMorph controls");
    addAndMakeVisible(visualGroup);
    addAndMakeVisible(controlsGroup);
    visualGroup.addAndMakeVisible(spectrogram);
    controlsGroup.addAndMakeVisible(analysisGroup);
    controlsGroup.addAndMakeVisible(routingGroup);
    controlsGroup.addAndMakeVisible(parametersGroup);
    parametersGroup.addAndMakeVisible(modeInfoLabel);
    parametersGroup.addAndMakeVisible(commonGroup);
    parametersGroup.addAndMakeVisible(algorithmGroup);

    // ---- FFT size / overlap -------------------------------------------------
    fftBox.addItemList({ "512", "1024", "2048", "4096", "8192", "16384", "32768" }, 1);
    fftBox.setTitle("FFT size");
    fftBox.setDescription("Analysis window size in samples. Larger sizes improve frequency resolution but increase latency.");
    fftBox.setWantsKeyboardFocus(true);
    fftBox.setExplicitFocusOrder(2);
    analysisGroup.addAndMakeVisible(fftBox);
    fftAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "fftSize", fftBox);

    overlapBox.addItemList({ "2x", "4x", "8x" }, 1);
    overlapBox.setTitle("Overlap");
    overlapBox.setDescription("Analysis overlap. Higher values sound smoother and use more CPU.");
    overlapBox.setWantsKeyboardFocus(true);
    overlapBox.setExplicitFocusOrder(3);
    analysisGroup.addAndMakeVisible(overlapBox);
    overlapAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "overlap", overlapBox);

    // ---- morph mode ---------------------------------------------------------
    morphModeBox.addItemList({ "Cepstral", "Spectral", "Vocoder", "Inject", "Partials" }, 1);
    morphModeBox.setTitle("Morph mode");
    morphModeBox.setDescription("Selects the morph algorithm and the controls shown below.");
    morphModeBox.setWantsKeyboardFocus(true);
    morphModeBox.setExplicitFocusOrder(4);
    analysisGroup.addAndMakeVisible(morphModeBox);
    morphModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "morphMode", morphModeBox);
    morphModeBox.onChange = [this] { updateModeUI(); };

    auto setupCaption = [this](juce::Label& l, const juce::String& t)
        {
            l.setText(t, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setFont(juce::Font(juce::FontOptions(11.0f)));
            l.setColour(juce::Label::textColourId, col::dim);
            l.setAccessible(false); // The adjacent control supplies its own name.
            analysisGroup.addAndMakeVisible(l);
        };
    setupCaption(fftLabel, "FFT SIZE");
    setupCaption(overlapLabel, "OVERLAP");
    setupCaption(morphModeLabel, "MORPH MODE");

    // ---- knobs --------------------------------------------------------------
    addKnob(morph, "morph", "MORPH", "Amount of sidechain timbre transferred to the carrier.");
    addKnob(clarity, "clarity", "CLARITY", "Envelope resolution. Higher values follow finer spectral detail.");
    addKnob(smooth, "smooth", "SMOOTH", "Smoothing between spectral frames.");
    addKnob(maxBoost, "maxBoost", "BOOST", "Maximum gain applied to a frequency bin, in decibels.");
    addKnob(dynamics, "dynamics", "DYNAMICS", "Amount of sidechain loudness contour transferred to the output.");
    addKnob(mix, "mix", "MIX", "Wet and dry mix.");
    addKnob(outGain, "outGain", "OUTPUT", "Final output gain in decibels.");

    addKnob(attack, "attack", "ATTACK", "Envelope rise time in milliseconds.");
    addKnob(release, "release", "RELEASE", "Envelope fall time in milliseconds.");
    addKnob(flatten, "flatten", "FLATTEN", "Amount of carrier spectral whitening.");
    addKnob(sibilance, "sibilance", "SIBILANCE", "Amount of sidechain noise character preserved.");
    addKnob(fill, "fill", "FILL", "Amount of missing spectral energy synthesized.");
    addKnob(fold, "fold", "FOLD", "Blend synthesized fill from noise to octave-folded carrier.");
    addKnob(glide, "glide", "GLIDE", "Distance carrier partials move toward matching sidechain partials.");
    addKnob(lock, "lock", "LOCK", "Phase lock amount for moved partials.");
    addKnob(peakFloor, "peakFloor", "PEAKS", "Partial detection threshold below the frame peak, in decibels.");

    // ---- toggles ------------------------------------------------------------
    addToggle(flipButton, flipAtt, "flip", "FLIP", "Swap the carrier and sidechain roles.");
    addToggle(freezeButton, freezeAtt, "freezeSide", "FREEZE SIDE", "Hold the current sidechain envelope.");
    addToggle(bypassButton, bypassAtt, "bypass", "BYPASS", "Pass audio through without processing.");
    flipButton.setExplicitFocusOrder(5);
    freezeButton.setExplicitFocusOrder(6);
    bypassButton.setExplicitFocusOrder(7);

    // spectrogram on/off - not an APVTS parameter, just a display convenience
    spectrogramButton.setButtonText("SPECTROGRAM");
    spectrogramButton.setTitle("Spectrogram");
    spectrogramButton.setDescription("Show the visual spectrogram. This does not change the sound.");
    spectrogramButton.setExplicitFocusOrder(8);
    spectrogramButton.setToggleState(true, juce::dontSendNotification);
    spectrogramButton.onClick = [this]
        {
            spectrogram.setActive(spectrogramButton.getToggleState());
        };
    routingGroup.addAndMakeVisible(spectrogramButton);

    // HD Visuals on/off - defaults to on. Reflects whatever state the engine
    // is already in (it survives editor open/close since it lives on proc.engine).
    hdVisualsButton.setButtonText("HD VISUALS");
    hdVisualsButton.setTitle("HD visuals");
    hdVisualsButton.setDescription("Use a sharper spectrogram display. This does not change the sound.");
    hdVisualsButton.setExplicitFocusOrder(1);
    hdVisualsButton.setToggleState(proc.engine.isHDVisualsEnabled(), juce::dontSendNotification);
    hdVisualsButton.onClick = [this]
        {
            const bool on = hdVisualsButton.getToggleState();
            proc.engine.setHDVisualsEnabled(on);
            spectrogram.setHDVisuals(on);
        };
    visualGroup.addAndMakeVisible(hdVisualsButton);
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
    routingGroup.addAndMakeVisible(routingLabel);
    flipButton.onStateChange();

    modeInfoLabel.setJustificationType(juce::Justification::centredLeft);
    modeInfoLabel.setFont(juce::Font(juce::FontOptions(11.5f)));
    modeInfoLabel.setColour(juce::Label::textColourId, col::dim);
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
    bool focusedKnobWillHide = false;

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
        if (! on && k->slider.hasKeyboardFocus(true))
            focusedKnobWillHide = true;
        k->slider.setVisible(on);
        k->label.setVisible(on);
    }

    Knob* common[] = { &morph, &clarity, &maxBoost, &dynamics, &mix, &outGain };
    for (size_t i = 0; i < std::size(common); ++i)
        common[i]->slider.setExplicitFocusOrder(10 + (int) i);

    algorithmKnobs.clear();
    for (auto* k : activeKnobs)
        if (std::find(std::begin(common), std::end(common), k) == std::end(common))
            algorithmKnobs.push_back(k);

    for (size_t i = 0; i < algorithmKnobs.size(); ++i)
        algorithmKnobs[i]->slider.setExplicitFocusOrder(20 + (int) i);

    static const char* modeNames[] = { "Cepstral", "Spectral", "Vocoder", "Inject", "Partials" };
    algorithmGroup.setTitle(juce::String(modeNames[juce::jlimit(0, 4, mode)]) + " parameters");

    // A host can automate the mode while a knob has focus. Keep keyboard users
    // on a visible control instead of leaving focus inside a hidden slider.
    if (focusedKnobWillHide)
        morphModeBox.grabKeyboardFocus();

    // Clarity drives a different underlying quantity in the Spectral mode
    // (box-average width rather than cepstral lifter cutoff), so relabel it.
    clarity.label.setText(mode == 1 ? "DETAIL" : "CLARITY", juce::dontSendNotification);
    clarity.slider.setTitle(mode == 1 ? "Detail" : "Clarity");
    clarity.slider.setDescription(mode == 1
        ? "Width of the spectral envelope average. Higher values preserve finer detail."
        : "Envelope resolution. Higher values follow finer spectral detail.");

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

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::structureChanged);

    resized();
}

SpectralMorphAudioProcessorEditor::~SpectralMorphAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SpectralMorphAudioProcessorEditor::addKnob(Knob& k, const juce::String& id,
    const juce::String& text, const juce::String& help)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 16);
    k.slider.setPercentageValue(id == "morph" || id == "clarity" || id == "smooth"
                             || id == "dynamics" || id == "mix" || id == "flatten"
                             || id == "sibilance" || id == "fill" || id == "fold"
                             || id == "glide" || id == "lock");
    k.slider.setTitle(proc.apvts.getParameter(id)->getName(100));
    k.slider.setDescription(help);
    k.slider.setTooltip("Arrow keys adjust by one percent step. Shift and an arrow key use a tenth of that step. The text box accepts precise values.");
    k.slider.setWantsKeyboardFocus(true);
    k.slider.setColour(juce::Slider::textBoxTextColourId, col::text);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    const bool isCommon = id == "morph" || id == "clarity" || id == "maxBoost"
                       || id == "dynamics" || id == "mix" || id == "outGain";
    auto& group = isCommon ? static_cast<juce::Component&>(commonGroup)
                           : static_cast<juce::Component&>(algorithmGroup);
    group.addAndMakeVisible(k.slider);

    k.label.setText(text, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(11.0f)));
    k.label.setColour(juce::Label::textColourId, col::dim);
    k.label.setAccessible(false);
    group.addAndMakeVisible(k.label);

    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, id, k.slider);
}

void SpectralMorphAudioProcessorEditor::addToggle(
    juce::ToggleButton& b,
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att,
    const juce::String& id, const juce::String& text, const juce::String& help)
{
    b.setButtonText(text);
    b.setTitle(proc.apvts.getParameter(id)->getName(100));
    b.setDescription(help);
    b.setWantsKeyboardFocus(true);
    routingGroup.addAndMakeVisible(b);
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
    visualGroup.setBounds(r.removeFromTop(layout::header + layout::scope));
    auto header = visualGroup.getLocalBounds().removeFromTop(layout::header);
    hdVisualsButton.setBounds(header.removeFromRight(150).reduced(6, 3));

    spectrogram.setBounds(visualGroup.getLocalBounds()
                              .withTrimmedTop(layout::header).reduced(8, 4));

    controlsGroup.setBounds(r);
    auto panel = controlsGroup.getLocalBounds().reduced(8, 4).reduced(12, 10);

    // --- top row of the panel: selectors + routing readout -------------------
    auto top = panel.removeFromTop(46);
    analysisGroup.setBounds(top.removeFromLeft(320));
    top.removeFromLeft(16);
    routingGroup.setBounds(top);

    auto selectors = analysisGroup.getLocalBounds();
    auto fftArea = selectors.removeFromLeft(110);
    fftLabel.setBounds(fftArea.removeFromTop(14));
    fftBox.setBounds(fftArea.reduced(0, 2));

    selectors.removeFromLeft(10);
    auto ovArea = selectors.removeFromLeft(90);
    overlapLabel.setBounds(ovArea.removeFromTop(14));
    overlapBox.setBounds(ovArea.reduced(0, 2));

    selectors.removeFromLeft(10);
    auto morphModeArea = selectors.removeFromLeft(100);
    morphModeLabel.setBounds(morphModeArea.removeFromTop(14));
    morphModeBox.setBounds(morphModeArea.reduced(0, 2));

    auto routing = routingGroup.getLocalBounds();
    routingLabel.setBounds(routing.removeFromLeft(200).withSizeKeepingCentre(200, 20));

    auto toggles = routing.reduced(0, 8);
    const int tw = juce::jmax(62, (toggles.getWidth() - 18) / 4);
    flipButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));
    freezeButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));
    bypassButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));
    spectrogramButton.setBounds(toggles.removeFromLeft(tw).reduced(3, 0));

    panel.removeFromTop(4);
    parametersGroup.setBounds(panel);
    auto parameters = parametersGroup.getLocalBounds();
    modeInfoLabel.setBounds(parameters.removeFromTop(16));
    parameters.removeFromTop(6);

    // One visual and accessible group per row keeps the layout and reading
    // order consistent, including when the mode exposes different controls.
    const int rowH = juce::jmin(150, parameters.getHeight() / 2);
    auto block = parameters.withSizeKeepingCentre(parameters.getWidth(), rowH * 2);
    commonGroup.setBounds(block.removeFromTop(rowH));
    algorithmGroup.setBounds(block.removeFromTop(rowH));

    auto layoutRow = [](juce::Component& group, const std::vector<Knob*>& knobs)
    {
        if (knobs.empty())
            return;

        const int kw = group.getWidth() / (int) knobs.size();
        auto strip = group.getLocalBounds().withSizeKeepingCentre(kw * (int) knobs.size(),
                                                                  group.getHeight());
        for (auto* k : knobs)
        {
            auto cell = strip.removeFromLeft(kw);
            k->label.setBounds(cell.removeFromTop(14));
            k->slider.setBounds(cell.reduced(6, 0));
        }
    };

    layoutRow(commonGroup, { &morph, &clarity, &maxBoost, &dynamics, &mix, &outGain });
    layoutRow(algorithmGroup, algorithmKnobs);
}
