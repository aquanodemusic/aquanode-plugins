#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Custom LookAndFeel for all shift sliders
struct ShiftSliderLookAndFeel : public juce::LookAndFeel_V2
{
    ShiftSliderLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff55eedd));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff444444));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff444444));
        setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }

    void drawLinearSliderBackground(juce::Graphics& g, int x, int y, int width, int height,
        float, float, float, const juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        g.setColour(findColour(juce::Slider::backgroundColourId));
        g.fillRect(slider.getLocalBounds());
        const float trackH = 4.0f, trackY = y + (height - trackH) * 0.5f;
        g.setColour(findColour(juce::Slider::trackColourId));
        g.fillRoundedRectangle((float)x, trackY, (float)width, trackH, 2.0f);
    }

    void drawLinearSliderThumb(juce::Graphics& g, int, int, int, int height,
        float sliderPos, float, float, const juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float tw = 10.0f, th = height * 0.7f;
        g.setColour(findColour(juce::Slider::thumbColourId));
        g.fillRoundedRectangle(sliderPos - tw * 0.5f, (height - th) * 0.5f, tw, th, 3.0f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
        float sp, float mn, float mx, const juce::Slider::SliderStyle style, juce::Slider& sl) override
    {
        drawLinearSliderBackground(g, x, y, w, h, sp, mn, mx, style, sl);
        drawLinearSliderThumb(g, x, y, w, h, sp, mn, mx, style, sl);
    }
};

//==============================================================================
// Round knob LookAndFeel for the strength knobs
struct StrengthKnobLookAndFeel : public juce::LookAndFeel_V4
{
    juce::Colour accentColour;
    explicit StrengthKnobLookAndFeel(juce::Colour accent) : accentColour(accent)
    {
        setColour(juce::Slider::rotarySliderFillColourId,    accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
        setColour(juce::Slider::thumbColourId,               accent);
        setColour(juce::Slider::textBoxTextColourId,         juce::Colours::lightgrey);
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colour(0xff2a2a2a));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float startAngle, float endAngle, juce::Slider&) override
    {
        const float cx = x + width  * 0.5f;
        const float cy = y + height * 0.5f;
        const float r  = juce::jmin(width, height) * 0.5f - 2.0f;

        g.setColour(juce::Colour(0xff1e1e1e));
        g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

        juce::Path track;
        track.addArc(cx - r * 0.75f, cy - r * 0.75f, r * 1.5f, r * 1.5f,
                     startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff3a3a3a));
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        float angle = startAngle + sliderPos * (endAngle - startAngle);
        juce::Path fill;
        fill.addArc(cx - r * 0.75f, cy - r * 0.75f, r * 1.5f, r * 1.5f,
                    startAngle, angle, true);
        g.setColour(accentColour);
        g.strokePath(fill, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        const float pr = r * 0.55f;
        g.setColour(accentColour.brighter(0.5f));
        g.drawLine(cx + std::sin(angle) * (r * 0.35f),
                   cy - std::cos(angle) * (r * 0.35f),
                   cx + std::sin(angle) * pr,
                   cy - std::cos(angle) * pr, 2.0f);

        g.setColour(juce::Colour(0xff444444));
        g.drawEllipse(cx - r, cy - r, r * 2, r * 2, 1.0f);
    }
};

//==============================================================================
// Helper: style a dark TextEditor for the control panel
static void styleTextEditor(juce::TextEditor& e)
{
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    e.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    e.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    e.setFont(11.0f);
}

static void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
}

static void styleLabel(juce::Label& l)
{
    l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    l.setJustificationType(juce::Justification::centredRight);
}

//==============================================================================
SpectralFilterAudioProcessorEditor::SpectralFilterAudioProcessorEditor(SpectralFilterAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(860, 700);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);

    displayCurveDB.fill(0.0f);
    displayPhaseRad.fill(0.0f);
    displayFreqShift.fill(0.0f);
    displayPan.fill(0.0f);
    fftDisplayData.fill(0.0f);

    // ---- FFT Size Combo ----
    styleLabel(fftSizeLabel);
    fftSizeLabel.setText("FFT Size:", juce::dontSendNotification);
    addAndMakeVisible(fftSizeLabel);

    fftSizeCombo.addItem("1024", 1);
    fftSizeCombo.addItem("2048", 2);
    fftSizeCombo.addItem("4096", 3);
    fftSizeCombo.addItem("8192", 4);
    {
        int cs = audioProcessor.fftSize;
        if (cs == 1024) fftSizeCombo.setSelectedId(1, juce::dontSendNotification);
        else if (cs == 2048) fftSizeCombo.setSelectedId(2, juce::dontSendNotification);
        else if (cs == 4096) fftSizeCombo.setSelectedId(3, juce::dontSendNotification);
        else if (cs == 8192) fftSizeCombo.setSelectedId(4, juce::dontSendNotification);
    }
    fftSizeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    fftSizeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
    fftSizeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff444444));
    fftSizeCombo.onChange = [this]()
        {
            int id = fftSizeCombo.getSelectedId();
            int newSize = (id == 1) ? 1024 : (id == 2) ? 2048 : (id == 3) ? 4096 : 8192;
            audioProcessor.setFFTSize(newSize);
            const double mx = static_cast<double>(audioProcessor.fftSize - 1);
            // Reset all shift sliders
            filterShift.slider.setRange(0.0, mx, 1.0); filterShift.slider.setValue(0.0, juce::dontSendNotification); filterShift.baseline.fill(0.0f);
            phaseShift.slider.setRange(0.0, mx, 1.0);  phaseShift.slider.setValue(0.0, juce::dontSendNotification);  phaseShift.baseline.fill(0.0f);
            freqShift.slider.setRange(0.0, mx, 1.0);   freqShift.slider.setValue(0.0, juce::dontSendNotification);   freqShift.baseline.fill(0.0f);
            panShift.slider.setRange(0.0, mx, 1.0);    panShift.slider.setValue(0.0, juce::dontSendNotification);    panShift.baseline.fill(0.0f);
            globalFreqShift.slider.setRange(0.0, mx, 1.0); globalFreqShift.slider.setValue(0.0, juce::dontSendNotification);
            audioProcessor.binShiftAmount.store(0, std::memory_order_relaxed);
        };
    addAndMakeVisible(fftSizeCombo);

    // ---- Random / Reset buttons ----
    auto makeRndResetPair = [&](juce::TextButton& rnd, juce::TextButton& rst,
        const juce::String& rndText, const juce::String& rstText,
        std::function<void()> rndFn, std::function<void()> rstFn)
        {
            styleButton(rnd); rnd.setButtonText(rndText); rnd.onClick = rndFn; addAndMakeVisible(rnd);
            styleButton(rst); rst.setButtonText(rstText); rst.onClick = rstFn; addAndMakeVisible(rst);
        };

    makeRndResetPair(randomFilterButton, resetFilterButton, "Rnd Filter", "Rst Filter",
        [this] { audioProcessor.randomizeFilterCurve(); },
        [this] { audioProcessor.resetFilterCurve(); });

    makeRndResetPair(randomPhaseButton, resetPhaseButton, "Rnd Phase", "Rst Phase",
        [this] { audioProcessor.randomizePhaseCurve(); },
        [this] { audioProcessor.resetPhaseCurve(); });

    makeRndResetPair(randomFreqButton, resetFreqButton, "Rnd Freq", "Rst Freq",
        [this] { audioProcessor.randomizeFreqShiftCurve(); },
        [this] { audioProcessor.resetFreqShiftCurve(); });

    makeRndResetPair(randomPanButton, resetPanButton, "Rnd Pan", "Rst Pan",
        [this] { audioProcessor.randomizePanCurve(); },
        [this] { audioProcessor.resetPanCurve(); });

    // ---- Edit Mode ComboBox ----
    styleLabel(editModeLabel);
    editModeLabel.setText("Edit:", juce::dontSendNotification);
    addAndMakeVisible(editModeLabel);

    editModeCombo.addItem("Filter", 1);
    editModeCombo.addItem("Phase", 2);
    editModeCombo.addItem("Freq", 3);
    editModeCombo.addItem("Pan", 4);
    editModeCombo.setSelectedId(1, juce::dontSendNotification);
    editModeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    editModeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
    editModeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff444444));
    editModeCombo.onChange = [this]()
        {
            int id = editModeCombo.getSelectedId();
            if (id == 1) editMode = EditMode::Filter;
            else if (id == 2) editMode = EditMode::Phase;
            else if (id == 3) editMode = EditMode::FreqShift;
            else              editMode = EditMode::Pan;
        };
    addAndMakeVisible(editModeCombo);

    // ---- Wet Only ----
    wetOnlyButton.setButtonText("Wet Only");
    wetOnlyButton.setClickingTogglesState(true);
    wetOnlyButton.setToggleState(false, juce::dontSendNotification);
    wetOnlyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    wetOnlyButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff662233));
    wetOnlyButton.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    wetOnlyButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffff6688));
    wetOnlyButton.onClick = [this] { audioProcessor.wetOnly.store(wetOnlyButton.getToggleState(), std::memory_order_relaxed); };
    addAndMakeVisible(wetOnlyButton);

    // ---- Export IR ----
    styleButton(exportIRButton);
    exportIRButton.setButtonText("Export IR");
    exportIRButton.onClick = [this]()
        {
            auto chooser = std::make_shared<juce::FileChooser>("Save Impulse Response",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.wav");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser](const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f != juce::File{}) audioProcessor.exportImpulseResponse(f);
                });
        };
    addAndMakeVisible(exportIRButton);

    // ---- Color inputs ----
    auto makeColorRow = [&](juce::Label& lbl, const juce::String& labelText,
        juce::TextEditor& inp, const juce::String& hexVal,
        std::function<void()> updateFn)
        {
            styleLabel(lbl); lbl.setText(labelText, juce::dontSendNotification); addAndMakeVisible(lbl);
            styleTextEditor(inp); inp.setText(hexVal);
            inp.onReturnKey = updateFn; inp.onFocusLost = updateFn;
            addAndMakeVisible(inp);
        };

    makeColorRow(bgColorLabel, "BG:", bgColorInput, audioProcessor.getBackgroundColor().toDisplayString(false), [this] { updateBgColor(); });
    makeColorRow(gridColorLabel, "Grid:", gridColorInput, audioProcessor.getGridColor().toDisplayString(false), [this] { updateGridColor(); });
    makeColorRow(filterColorLabel, "Filter:", filterColorInput, audioProcessor.getCurveColor().toDisplayString(false), [this] { updateFilterColor(); });
    makeColorRow(phaseColorLabel, "Phase:", phaseColorInput, audioProcessor.getPhaseColor().toDisplayString(false), [this] { updatePhaseColor(); });
    makeColorRow(freqColorLabel, "Freq:", freqColorInput, audioProcessor.getFreqShiftColor().toDisplayString(false), [this] { updateFreqColor(); });
    makeColorRow(panColorLabel, "Pan:", panColorInput, audioProcessor.getPanColor().toDisplayString(false), [this] { updatePanColor(); });

    // ---- Reset Colors ----
    styleButton(resetColorsButton);
    resetColorsButton.setButtonText("Rst Colors");
    resetColorsButton.onClick = [this]()
        {
            audioProcessor.resetColors();
            bgColorInput.setText(audioProcessor.getBackgroundColor().toDisplayString(false), juce::dontSendNotification);
            gridColorInput.setText(audioProcessor.getGridColor().toDisplayString(false), juce::dontSendNotification);
            filterColorInput.setText(audioProcessor.getCurveColor().toDisplayString(false), juce::dontSendNotification);
            phaseColorInput.setText(audioProcessor.getPhaseColor().toDisplayString(false), juce::dontSendNotification);
            freqColorInput.setText(audioProcessor.getFreqShiftColor().toDisplayString(false), juce::dontSendNotification);
            panColorInput.setText(audioProcessor.getPanColor().toDisplayString(false), juce::dontSendNotification);
            repaint();
        };
    addAndMakeVisible(resetColorsButton);

    // ---- Strength knobs ----
    auto makeStrengthKnob = [&](juce::Slider& knob, std::unique_ptr<juce::LookAndFeel_V4>& lf,
        juce::Colour accent, const juce::String& tooltip)
        {
            lf = std::make_unique<StrengthKnobLookAndFeel>(accent);
            knob.setLookAndFeel(lf.get());
            knob.setSliderStyle(juce::Slider::Rotary);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 14);
            // Range 0–1 to match APVTS; display as 0–100%
            knob.setRange(0.0, 1.0, 0.001);
            knob.setDoubleClickReturnValue(true, 1.0);
            knob.setTooltip(tooltip);
            knob.textFromValueFunction = [](double v) -> juce::String {
                return juce::String(juce::roundToInt(v * 100.0)) + "%";
            };
            knob.valueFromTextFunction = [](const juce::String& s) -> double {
                return s.trimCharactersAtEnd("%").getDoubleValue() / 100.0;
            };
            addAndMakeVisible(knob);
        };

    makeStrengthKnob(filterStrengthKnob, filterKnobLF,
        juce::Colour(0xff55eedd), "Filter curve strength (DAW automatable)");
    makeStrengthKnob(phaseStrengthKnob, phaseKnobLF,
        juce::Colour(0xffdd55bb), "Phase curve strength (DAW automatable)");
    makeStrengthKnob(freqStrengthKnob, freqKnobLF,
        juce::Colour(0xff44ccff), "Freq shift curve strength (DAW automatable)");
    makeStrengthKnob(panStrengthKnob, panKnobLF,
        juce::Colour(0xffffcc33), "Pan curve strength (DAW automatable)");

    // Wire knobs to APVTS so the DAW can automate them
    filterStrengthAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "filterStrength", filterStrengthKnob);
    phaseStrengthAttachment  = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "phaseStrength",  phaseStrengthKnob);
    freqStrengthAttachment   = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "freqStrength",   freqStrengthKnob);
    panStrengthAttachment    = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "panStrength",    panStrengthKnob);

    // ====================================================================
    // Bottom shift strips — helper lambda to build each one
    // ====================================================================
    const double sliderMax = static_cast<double>(audioProcessor.fftSize - 1);

    auto makeShiftStrip = [&](ShiftStrip& s, const juce::String& labelText,
        juce::Colour onColour, juce::Colour textOnColour,
        std::function<void()> autoBtnFn,
        std::function<void()> dragStartFn,
        std::function<void(int)> valueChangeFn,
        std::atomic<float>& speedAtomic)
        {
            // Label
            styleLabel(s.label);
            s.label.setText(labelText, juce::dontSendNotification);
            addAndMakeVisible(s.label);

            // Slider LookAndFeel
            s.lf = std::make_unique<ShiftSliderLookAndFeel>();
            s.slider.setLookAndFeel(s.lf.get());
            s.slider.setSliderStyle(juce::Slider::LinearHorizontal);
            s.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 28);
            s.slider.setRange(0.0, sliderMax, 1.0);
            s.slider.setValue(0.0, juce::dontSendNotification);
            s.slider.setDoubleClickReturnValue(true, 0.0);
            s.slider.textFromValueFunction = [this](double val) -> juce::String
                {
                    double pct = val / static_cast<double>(audioProcessor.fftSize - 1) * 200.0;
                    return juce::String(static_cast<int>(std::round(pct))) + "%";
                };
            s.slider.valueFromTextFunction = [this](const juce::String& text) -> double
                {
                    double pct = text.trimCharactersAtEnd("%").getDoubleValue();
                    return pct / 200.0 * static_cast<double>(audioProcessor.fftSize - 1);
                };
            s.slider.onDragStart = dragStartFn;
            s.slider.onValueChange = [valueChangeFn, &s] { valueChangeFn(static_cast<int>(s.slider.getValue())); };
            s.baseline.fill(0.0f);
            addAndMakeVisible(s.slider);

            // Auto button
            s.autoBtn.setButtonText("Aut");
            s.autoBtn.setClickingTogglesState(true);
            s.autoBtn.setToggleState(false, juce::dontSendNotification);
            s.autoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
            s.autoBtn.setColour(juce::TextButton::buttonOnColourId, onColour);
            s.autoBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
            s.autoBtn.setColour(juce::TextButton::textColourOnId, textOnColour);
            s.autoBtn.onClick = autoBtnFn;
            addAndMakeVisible(s.autoBtn);

            // Speed input
            styleTextEditor(s.speedInput);
            s.speedInput.setInputRestrictions(7, "-0123456789.");
            s.speedInput.setTooltip("bins/sec");
            s.speedInput.setText(juce::String(speedAtomic.load(std::memory_order_relaxed), 2), juce::dontSendNotification);
            auto updateSpeed = [&s, &speedAtomic]()
                {
                    float spd = s.speedInput.getText().getFloatValue();
                    if (spd != 0.0f) speedAtomic.store(spd, std::memory_order_relaxed);
                };
            s.speedInput.onReturnKey = updateSpeed;
            s.speedInput.onFocusLost = updateSpeed;
            addAndMakeVisible(s.speedInput);
        };

    // ---- Strip 1: Filter Shift ----
    makeShiftStrip(filterShift, "Filter Shift:",
        juce::Colour(0xff225566), juce::Colour(0xff55eedd),
        [this]()
        {
            bool on = filterShift.autoBtn.getToggleState();
            audioProcessor.autoShiftFilter.store(on, std::memory_order_relaxed);
            audioProcessor.autoShiftFilterAccum = 0.0f;
            filterShift.slider.setEnabled(!on);
            if (!on) { audioProcessor.resetFilterCurve(); filterShift.slider.setValue(0.0, juce::dontSendNotification); filterShift.baseline.fill(0.0f); }
            updateExportIREnabled();
        },
        [this]() { audioProcessor.getFilterCurve(filterShift.baseline); filterShift.slider.setValue(0.0, juce::dontSendNotification); },
        [this](int shift) { audioProcessor.setFilterCurveShifted(filterShift.baseline, shift); },
        audioProcessor.autoShiftFilterSpeed);

    // ---- Strip 2: Phase Shift ----
    makeShiftStrip(phaseShift, "Phase Shift:",
        juce::Colour(0xff552266), juce::Colour(0xffdd55bb),
        [this]()
        {
            bool on = phaseShift.autoBtn.getToggleState();
            audioProcessor.autoShiftPhase.store(on, std::memory_order_relaxed);
            audioProcessor.autoShiftPhaseAccum = 0.0f;
            phaseShift.slider.setEnabled(!on);
            if (!on) { audioProcessor.resetPhaseCurve(); phaseShift.slider.setValue(0.0, juce::dontSendNotification); phaseShift.baseline.fill(0.0f); }
            updateExportIREnabled();
        },
        [this]() { audioProcessor.getPhaseCurve(phaseShift.baseline); phaseShift.slider.setValue(0.0, juce::dontSendNotification); },
        [this](int shift) { audioProcessor.setPhaseCurveShifted(phaseShift.baseline, shift); },
        audioProcessor.autoShiftPhaseSpeed);

    // ---- Strip 3: Freq Shift (per-bin) ----
    makeShiftStrip(freqShift, "Freq Shift:",
        juce::Colour(0xff115566), juce::Colour(0xff44ccff),
        [this]()
        {
            bool on = freqShift.autoBtn.getToggleState();
            audioProcessor.autoShiftFreq.store(on, std::memory_order_relaxed);
            audioProcessor.autoShiftFreqAccum = 0.0f;
            freqShift.slider.setEnabled(!on);
            if (!on) { audioProcessor.resetFreqShiftCurve(); freqShift.slider.setValue(0.0, juce::dontSendNotification); freqShift.baseline.fill(0.0f); }
            updateExportIREnabled();
        },
        [this]() { audioProcessor.getFreqShiftCurve(freqShift.baseline); freqShift.slider.setValue(0.0, juce::dontSendNotification); },
        [this](int shift) { audioProcessor.setFreqShiftCurveShifted(freqShift.baseline, shift); },
        audioProcessor.autoShiftFreqSpeed);

    // ---- Strip 4: Pan Shift ----
    makeShiftStrip(panShift, "Pan Shift:",
        juce::Colour(0xff554400), juce::Colour(0xffffcc33),
        [this]()
        {
            bool on = panShift.autoBtn.getToggleState();
            audioProcessor.autoShiftPan.store(on, std::memory_order_relaxed);
            audioProcessor.autoShiftPanAccum = 0.0f;
            panShift.slider.setEnabled(!on);
            if (!on) { audioProcessor.resetPanCurve(); panShift.slider.setValue(0.0, juce::dontSendNotification); panShift.baseline.fill(0.0f); }
            updateExportIREnabled();
        },
        [this]() { audioProcessor.getPanCurve(panShift.baseline); panShift.slider.setValue(0.0, juce::dontSendNotification); },
        [this](int shift) { audioProcessor.setPanCurveShifted(panShift.baseline, shift); },
        audioProcessor.autoShiftPanSpeed);

    // ---- Strip 5: Global Freq (Bin) Shift — shifts ALL bins uniformly ----
    makeShiftStrip(globalFreqShift, "Global Shift:",
        juce::Colour(0xff225566), juce::Colour(0xff55eedd),
        [this]()
        {
            bool on = globalFreqShift.autoBtn.getToggleState();
            audioProcessor.autoShiftBin.store(on, std::memory_order_relaxed);
            audioProcessor.autoShiftBinAccum = 0.0f;
            audioProcessor.autoShiftBinPos = 0;
            globalFreqShift.slider.setEnabled(!on);
            globalFreqWrapButton.setEnabled(!on);
            if (!on) { audioProcessor.binShiftAmount.store(0, std::memory_order_relaxed); globalFreqShift.slider.setValue(0.0, juce::dontSendNotification); }
            updateExportIREnabled();
        },
        [this]() { /* global shift: no curve baseline snapshot, slider directly controls binShiftAmount */
            globalFreqShift.slider.setValue(0.0, juce::dontSendNotification); },
            [this](int shift) { audioProcessor.binShiftAmount.store(shift, std::memory_order_relaxed); },
            audioProcessor.autoShiftBinSpeed);
    // Override drag start — no baseline needed for global shift
    globalFreqShift.slider.onDragStart = [this]()
        {
            // nothing — just reset display position so user sees absolute movement
        };
    globalFreqShift.slider.onValueChange = [this]()
        {
            audioProcessor.binShiftAmount.store(static_cast<int>(globalFreqShift.slider.getValue()), std::memory_order_relaxed);
        };

    // Wrap button (only for global freq shift)
    globalFreqWrapButton.setButtonText("Wrap");
    globalFreqWrapButton.setClickingTogglesState(true);
    globalFreqWrapButton.setToggleState(true, juce::dontSendNotification);
    globalFreqWrapButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    globalFreqWrapButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff226655));
    globalFreqWrapButton.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    globalFreqWrapButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff55eedd));
    globalFreqWrapButton.onClick = [this] { audioProcessor.binShiftWrap.store(globalFreqWrapButton.getToggleState(), std::memory_order_relaxed); };
    addAndMakeVisible(globalFreqWrapButton);

    startTimerHz(60);
}

SpectralFilterAudioProcessorEditor::~SpectralFilterAudioProcessorEditor()
{
    // Destroy APVTS attachments before their sliders
    filterStrengthAttachment.reset();
    phaseStrengthAttachment.reset();
    freqStrengthAttachment.reset();
    panStrengthAttachment.reset();

    filterShift.slider.setLookAndFeel(nullptr);
    phaseShift.slider.setLookAndFeel(nullptr);
    freqShift.slider.setLookAndFeel(nullptr);
    panShift.slider.setLookAndFeel(nullptr);
    globalFreqShift.slider.setLookAndFeel(nullptr);
    filterStrengthKnob.setLookAndFeel(nullptr);
    phaseStrengthKnob.setLookAndFeel(nullptr);
    freqStrengthKnob.setLookAndFeel(nullptr);
    panStrengthKnob.setLookAndFeel(nullptr);
    stopTimer();
}

//==============================================================================
void SpectralFilterAudioProcessorEditor::timerCallback()
{
    audioProcessor.getFilterCurve(displayCurveDB);
    audioProcessor.getPhaseCurve(displayPhaseRad);
    audioProcessor.getFreqShiftCurve(displayFreqShift);
    audioProcessor.getPanCurve(displayPan);
    audioProcessor.getFFTData(fftDisplayData.data(), audioProcessor.numBins);

    // Scale display curves by the corresponding strength knob (values are 0–1)
    const float fStr  = static_cast<float>(filterStrengthKnob.getValue());
    const float phStr = static_cast<float>(phaseStrengthKnob.getValue());
    const float frStr = static_cast<float>(freqStrengthKnob.getValue());
    const float pnStr = static_cast<float>(panStrengthKnob.getValue());
    for (int i = 0; i < audioProcessor.numBins; ++i)
    {
        displayCurveDB[i]  *= fStr;
        displayPhaseRad[i] *= phStr;
        displayFreqShift[i] *= frStr;
        displayPan[i]      *= pnStr;
    }

    updateExportIREnabled();
    repaint();
}

void SpectralFilterAudioProcessorEditor::updateExportIREnabled()
{
    const bool anyAuto = audioProcessor.autoShiftFilter.load(std::memory_order_relaxed)
        || audioProcessor.autoShiftBin.load(std::memory_order_relaxed)
        || audioProcessor.autoShiftPhase.load(std::memory_order_relaxed)
        || audioProcessor.autoShiftFreq.load(std::memory_order_relaxed)
        || audioProcessor.autoShiftPan.load(std::memory_order_relaxed);
    exportIRButton.setEnabled(!anyAuto);
    exportIRButton.setAlpha(anyAuto ? 0.35f : 1.0f);
}

//==============================================================================
// Coordinate helpers
//==============================================================================
int SpectralFilterAudioProcessorEditor::xToBin(float x) const
{
    const float ny = nyquist(), logMin = std::log10(20.0f), logMax = std::log10(ny);
    float norm = juce::jlimit(0.f, 1.f, x / getWidth());
    float freq = std::pow(10.f, logMin + norm * (logMax - logMin));
    float fpb = ny / static_cast<float>(audioProcessor.numBins - 1);
    return juce::jlimit(0, audioProcessor.numBins - 1, static_cast<int>(freq / fpb));
}

float SpectralFilterAudioProcessorEditor::binToX(int bin) const
{
    const float ny = nyquist(), logMin = std::log10(20.0f), logMax = std::log10(ny);
    float fpb = ny / static_cast<float>(audioProcessor.numBins - 1);
    float freq = juce::jmax(static_cast<float>(bin) * fpb, 20.0f);
    float norm = (std::log10(freq) - logMin) / (logMax - logMin);
    return norm * getWidth();
}

float SpectralFilterAudioProcessorEditor::yToDB(float y) const
{
    float norm = 1.f - juce::jlimit(0.f, 1.f, y / getHeight());
    return minDB + norm * (maxDB - minDB);
}

float SpectralFilterAudioProcessorEditor::dBToY(float dB) const
{
    float norm = (dB - minDB) / (maxDB - minDB);
    return getHeight() * (1.f - juce::jlimit(0.f, 1.f, norm));
}

float SpectralFilterAudioProcessorEditor::yToRad(float y) const
{
    float norm = 1.f - juce::jlimit(0.f, 1.f, y / getHeight());
    return -juce::MathConstants<float>::pi + norm * 2.f * juce::MathConstants<float>::pi;
}

float SpectralFilterAudioProcessorEditor::radToY(float rad) const
{
    float norm = (rad + juce::MathConstants<float>::pi) / (2.f * juce::MathConstants<float>::pi);
    return getHeight() * (1.f - juce::jlimit(0.f, 1.f, norm));
}

float SpectralFilterAudioProcessorEditor::yToFreqOffset(float y) const
{
    // y=0 → +numBins/2 (shift up), y=height → -numBins/2 (shift down), centre=0
    float half = static_cast<float>(audioProcessor.numBins / 2);
    float norm = 1.f - juce::jlimit(0.f, 1.f, y / getHeight());
    return -half + norm * 2.f * half;
}

float SpectralFilterAudioProcessorEditor::freqOffsetToY(float off) const
{
    float half = static_cast<float>(audioProcessor.numBins / 2);
    float norm = (off + half) / (2.f * half);
    return getHeight() * (1.f - juce::jlimit(0.f, 1.f, norm));
}

float SpectralFilterAudioProcessorEditor::yToPan(float y) const
{
    // y=0 → +1 (right), y=height → -1 (left), centre=0
    float norm = 1.f - juce::jlimit(0.f, 1.f, y / getHeight());
    return -1.f + norm * 2.f;
}

float SpectralFilterAudioProcessorEditor::panToY(float pan) const
{
    float norm = (pan + 1.f) / 2.f;
    return getHeight() * (1.f - juce::jlimit(0.f, 1.f, norm));
}

//==============================================================================
// Mouse
//==============================================================================
void SpectralFilterAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    isDrawing = true;
    lastDragX = static_cast<float>(e.x);
    lastDragY = static_cast<float>(e.y);
    paintSegment(lastDragX, lastDragY);
}

void SpectralFilterAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDrawing) return;
    float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
    paintSegment(x, y);
    lastDragX = x; lastDragY = y;
}

void SpectralFilterAudioProcessorEditor::mouseUp(const juce::MouseEvent&) { isDrawing = false; }
void SpectralFilterAudioProcessorEditor::mouseMove(const juce::MouseEvent& e) { hoverX = static_cast<float>(e.x); }

void SpectralFilterAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent&)
{
    switch (editMode)
    {
    case EditMode::Filter:   audioProcessor.resetFilterCurve();    break;
    case EditMode::Phase:    audioProcessor.resetPhaseCurve();     break;
    case EditMode::FreqShift:audioProcessor.resetFreqShiftCurve(); break;
    case EditMode::Pan:      audioProcessor.resetPanCurve();       break;
    }
}

void SpectralFilterAudioProcessorEditor::paintSegment(float x, float y)
{
    int sb = xToBin(lastDragX), eb = xToBin(x);
    switch (editMode)
    {
    case EditMode::Filter:
        audioProcessor.setFilterCurveRange(sb, eb, yToDB(lastDragY), yToDB(y));
        break;
    case EditMode::Phase:
        audioProcessor.setPhaseCurveRange(sb, eb, yToRad(lastDragY), yToRad(y));
        break;
    case EditMode::FreqShift:
        audioProcessor.setFreqShiftCurveRange(sb, eb, yToFreqOffset(lastDragY), yToFreqOffset(y));
        break;
    case EditMode::Pan:
        audioProcessor.setPanCurveRange(sb, eb, yToPan(lastDragY), yToPan(y));
        break;
    }
}

//==============================================================================
// Paint
//==============================================================================
void SpectralFilterAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawFFTSpectrum(g);
    drawFilterCurve(g);
    drawPhaseCurve(g);
    drawFreqShiftCurve(g);
    drawPanCurve(g);
    drawLabels(g);
}

void SpectralFilterAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    const float ny = nyquist(), logMin = std::log10(20.f), logMax = std::log10(ny);
    auto b = getLocalBounds();
    g.fillAll(audioProcessor.getBackgroundColor());

    const float gridDBs[] = { 24.f, 18.f, 12.f, 6.f, 0.f, -6.f, -12.f, -24.f, -48.f, -96.f };
    for (float dB : gridDBs)
    {
        float y = dBToY(dB);
        g.setColour(dB == 0.f ? juce::Colour(0xff44aacc) : audioProcessor.getGridColor());
        g.drawHorizontalLine(static_cast<int>(y), 0.f, static_cast<float>(b.getWidth()));
    }

    const float gridFreqs[] = { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
    for (float freq : gridFreqs)
    {
        if (freq > ny) continue;
        float x = (std::log10(freq) - logMin) / (logMax - logMin) * b.getWidth();
        g.setColour(audioProcessor.getGridColor());
        g.drawVerticalLine(static_cast<int>(x), 0.f, static_cast<float>(b.getHeight()));
    }
}

void SpectralFilterAudioProcessorEditor::drawFFTSpectrum(juce::Graphics& g)
{
    const float ny = nyquist(), logMin = std::log10(20.f), logMax = std::log10(ny);
    const float w = getWidth(), h = getHeight();
    const float fpb = ny / static_cast<float>(audioProcessor.numBins - 1);

    float maxMag = 0.001f;
    for (int i = 0; i < audioProcessor.numBins; ++i) maxMag = juce::jmax(maxMag, fftDisplayData[i]);

    juce::Path sp; bool started = false;
    for (int i = 1; i < audioProcessor.numBins; ++i)
    {
        float freq = static_cast<float>(i) * fpb;
        if (freq < 20.f) continue;
        float x = (std::log10(freq) - logMin) / (logMax - logMin) * w;
        float dB = 20.f * std::log10(fftDisplayData[i] / maxMag + 0.00001f);
        float norm = juce::jlimit(0.f, 1.f, (dB + 90.f) / 90.f);
        float y = h - norm * h * 0.65f;
        if (!started) { sp.startNewSubPath(x, h); sp.lineTo(x, y); started = true; }
        else            sp.lineTo(x, y);
    }
    if (started) { sp.lineTo(w, h); sp.closeSubPath(); }
    g.setColour(audioProcessor.getSpectrumColor().withAlpha(0.13f)); g.fillPath(sp);
    g.setColour(audioProcessor.getSpectrumColor().withAlpha(0.35f)); g.strokePath(sp, juce::PathStrokeType(1.0f));
}

void SpectralFilterAudioProcessorEditor::drawFilterCurve(juce::Graphics& g)
{
    const float ny = nyquist();
    float zeroY = dBToY(0.f);

    juce::Path fill; bool started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = dBToY(displayCurveDB[b]);
        if (!started) { fill.startNewSubPath(x, zeroY); fill.lineTo(x, y); started = true; }
        else fill.lineTo(x, y);
    }
    if (started) { fill.lineTo(binToX(audioProcessor.numBins - 1), zeroY); fill.closeSubPath(); }
    g.setColour(audioProcessor.getCurveColor().withAlpha(0.2f)); g.fillPath(fill);

    juce::Path line; started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = dBToY(displayCurveDB[b]);
        if (!started) { line.startNewSubPath(x, y); started = true; }
        else line.lineTo(x, y);
    }
    g.setColour(audioProcessor.getCurveColor());
    g.strokePath(line, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Hover tooltip (only when editing filter)
    if (hoverX >= 0.f && hoverX <= getWidth())
    {
        int   hb = xToBin(hoverX);
        float hDB = displayCurveDB[juce::jlimit(0, audioProcessor.numBins - 1, hb)];
        float hY = dBToY(hDB);

        g.setColour(juce::Colours::white);
        g.fillEllipse(hoverX - 4.f, hY - 4.f, 8.f, 8.f);

        float fpb = ny / static_cast<float>(audioProcessor.numBins - 1);
        float freq = static_cast<float>(hb) * fpb;
        juce::String freqStr = (freq < 1000.f) ? juce::String(static_cast<int>(freq)) + " Hz"
            : juce::String(freq / 1000.f, 1) + " kHz";
        juce::String tip = freqStr + "  " + ((hDB <= -144.f) ? "-inf dB" : juce::String(hDB, 1) + " dB");
        float lx = hoverX + 8.f;
        if (lx + 130.f > getWidth()) lx = hoverX - 138.f;
        float ly = hY - 20.f; if (ly < 4.f) ly = hY + 8.f;
        g.setColour(juce::Colour(0xcc111416)); g.fillRoundedRectangle(lx - 4.f, ly - 2.f, 134.f, 18.f, 3.f);
        g.setColour(juce::Colours::white); g.setFont(11.f);
        g.drawText(tip, static_cast<int>(lx), static_cast<int>(ly), 130, 16, juce::Justification::left);
    }
}

void SpectralFilterAudioProcessorEditor::drawPhaseCurve(juce::Graphics& g)
{
    float zeroY = radToY(0.f);
    juce::Path fill; bool started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = radToY(displayPhaseRad[b]);
        if (!started) { fill.startNewSubPath(x, zeroY); fill.lineTo(x, y); started = true; }
        else fill.lineTo(x, y);
    }
    if (started) { fill.lineTo(binToX(audioProcessor.numBins - 1), zeroY); fill.closeSubPath(); }
    g.setColour(audioProcessor.getPhaseColor().withAlpha(0.15f)); g.fillPath(fill);

    juce::Path line; started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = radToY(displayPhaseRad[b]);
        if (!started) { line.startNewSubPath(x, y); started = true; }
        else line.lineTo(x, y);
    }
    g.setColour(audioProcessor.getPhaseColor());
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(audioProcessor.getPhaseColor().withAlpha(0.3f));
    g.drawHorizontalLine(static_cast<int>(zeroY), 0.f, static_cast<float>(getWidth()));
}

void SpectralFilterAudioProcessorEditor::drawFreqShiftCurve(juce::Graphics& g)
{
    float zeroY = freqOffsetToY(0.f);
    juce::Path fill; bool started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = freqOffsetToY(displayFreqShift[b]);
        if (!started) { fill.startNewSubPath(x, zeroY); fill.lineTo(x, y); started = true; }
        else fill.lineTo(x, y);
    }
    if (started) { fill.lineTo(binToX(audioProcessor.numBins - 1), zeroY); fill.closeSubPath(); }
    g.setColour(audioProcessor.getFreqShiftColor().withAlpha(0.15f)); g.fillPath(fill);

    juce::Path line; started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = freqOffsetToY(displayFreqShift[b]);
        if (!started) { line.startNewSubPath(x, y); started = true; }
        else line.lineTo(x, y);
    }
    g.setColour(audioProcessor.getFreqShiftColor());
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(audioProcessor.getFreqShiftColor().withAlpha(0.3f));
    g.drawHorizontalLine(static_cast<int>(zeroY), 0.f, static_cast<float>(getWidth()));
}

void SpectralFilterAudioProcessorEditor::drawPanCurve(juce::Graphics& g)
{
    float zeroY = panToY(0.f);
    juce::Path fill; bool started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = panToY(displayPan[b]);
        if (!started) { fill.startNewSubPath(x, zeroY); fill.lineTo(x, y); started = true; }
        else fill.lineTo(x, y);
    }
    if (started) { fill.lineTo(binToX(audioProcessor.numBins - 1), zeroY); fill.closeSubPath(); }
    g.setColour(audioProcessor.getPanColor().withAlpha(0.15f)); g.fillPath(fill);

    juce::Path line; started = false;
    for (int b = 0; b < audioProcessor.numBins; ++b)
    {
        float x = binToX(b), y = panToY(displayPan[b]);
        if (!started) { line.startNewSubPath(x, y); started = true; }
        else line.lineTo(x, y);
    }
    g.setColour(audioProcessor.getPanColor());
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(audioProcessor.getPanColor().withAlpha(0.3f));
    g.drawHorizontalLine(static_cast<int>(zeroY), 0.f, static_cast<float>(getWidth()));
}

void SpectralFilterAudioProcessorEditor::drawLabels(juce::Graphics& g)
{
    const float ny = nyquist(), logMin = std::log10(20.f), logMax = std::log10(ny);
    int w = getWidth(), h = getHeight();
    g.setFont(10.f);

    // dB axis
    const float dbLabels[] = { 24.f, 12.f, 6.f, 0.f, -6.f, -12.f, -24.f, -48.f };
    for (float dB : dbLabels)
    {
        float y = dBToY(dB);
        juce::String lbl = (dB == 0.f) ? "0 dB" : juce::String(static_cast<int>(dB)) + " dB";
        g.setColour(dB == 0.f ? juce::Colour(0xff44aacc) : audioProcessor.getGridColor().brighter(0.3f));
        g.drawText(lbl, w - 44, static_cast<int>(y) - 7, 42, 14, juce::Justification::right);
    }

    // Frequency axis — drawn near the very bottom of the window (below all strips)
    const float freqLabels[] = { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
    const int freqLabelY = h - 12;   // just above the very bottom edge
    for (float freq : freqLabels)
    {
        if (freq > ny) continue;
        float x = (std::log10(freq) - logMin) / (logMax - logMin) * w;
        juce::String lbl = (freq < 1000.f) ? juce::String(static_cast<int>(freq))
            : (freq < 10000.f ? juce::String(freq / 1000.f, 1) + "k"
                : juce::String(static_cast<int>(freq / 1000.f)) + "k");
        g.setColour(audioProcessor.getGridColor().brighter(0.3f));
        g.drawText(lbl, static_cast<int>(x) - 18, freqLabelY, 36, 12, juce::Justification::centred);
    }

    // Hint bar at top
    g.setColour(audioProcessor.getGridColor().brighter(0.1f));
    juce::String modeStr;
    switch (editMode)
    {
    case EditMode::Filter:    modeStr = "Filter (dB)";      break;
    case EditMode::Phase:     modeStr = "Phase (rad)";      break;
    case EditMode::FreqShift: modeStr = "Freq Shift (bins)"; break;
    case EditMode::Pan:       modeStr = "Pan (-1..+1)";     break;
    }
    g.drawText("SpectralFilter by aquanode | Select which curve to edit, then click and drag to draw | Use auto sliders for movement. Currently selected curve:" + modeStr,
        4, 4, w - 90, 14, juce::Justification::left);
}

//==============================================================================
void SpectralFilterAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int rightEdge = w - 26;
    const int inputW = 80;
    const int labelW = 68;
    const int rowH = 26;
    const int startY = 6;

    // ---- Right panel rows (0-based) ----
    // Row 0: FFT Size
    fftSizeCombo.setBounds(rightEdge - inputW, startY, inputW, 22);
    fftSizeLabel.setBounds(rightEdge - inputW - 58, startY, 58, 22);

    // Rows 1-2: Rnd Filter / Rst Filter  +  Filter strength knob to the left
    randomFilterButton.setBounds(rightEdge - inputW, startY + rowH * 1, inputW, 22);
    resetFilterButton.setBounds(rightEdge - inputW, startY + rowH * 2, inputW, 22);
    filterStrengthKnob.setBounds(rightEdge - inputW - 48, startY + rowH * 1, 44, 50);

    // Rows 3-4: Rnd Phase / Rst Phase
    randomPhaseButton.setBounds(rightEdge - inputW, startY + rowH * 3, inputW, 22);
    resetPhaseButton.setBounds(rightEdge - inputW, startY + rowH * 4, inputW, 22);
    phaseStrengthKnob.setBounds(rightEdge - inputW - 48, startY + rowH * 3, 44, 50);

    // Rows 5-6: Rnd Freq / Rst Freq
    randomFreqButton.setBounds(rightEdge - inputW, startY + rowH * 5, inputW, 22);
    resetFreqButton.setBounds(rightEdge - inputW, startY + rowH * 6, inputW, 22);
    freqStrengthKnob.setBounds(rightEdge - inputW - 48, startY + rowH * 5, 44, 50);

    // Rows 7-8: Rnd Pan / Rst Pan
    randomPanButton.setBounds(rightEdge - inputW, startY + rowH * 7, inputW, 22);
    resetPanButton.setBounds(rightEdge - inputW, startY + rowH * 8, inputW, 22);
    panStrengthKnob.setBounds(rightEdge - inputW - 48, startY + rowH * 7, 44, 50);

    // Row 9: Edit combo
    editModeCombo.setBounds(rightEdge - inputW, startY + rowH * 9, inputW, 22);
    editModeLabel.setBounds(rightEdge - inputW - 40, startY + rowH * 9, 40, 22);

    // Row 10: Wet Only
    wetOnlyButton.setBounds(rightEdge - inputW, startY + rowH * 10, inputW, 22);

    // Row 11: Export IR
    exportIRButton.setBounds(rightEdge - inputW, startY + rowH * 11, inputW, 22);

    // Row 12: BG Color
    bgColorInput.setBounds(rightEdge - inputW, startY + rowH * 12, inputW, 22);
    bgColorLabel.setBounds(rightEdge - inputW - labelW, startY + rowH * 12, labelW, 22);

    // Row 13: Grid Color
    gridColorInput.setBounds(rightEdge - inputW, startY + rowH * 13, inputW, 22);
    gridColorLabel.setBounds(rightEdge - inputW - labelW, startY + rowH * 13, labelW, 22);

    // Row 14: Filter Color
    filterColorInput.setBounds(rightEdge - inputW, startY + rowH * 14, inputW, 22);
    filterColorLabel.setBounds(rightEdge - inputW - labelW, startY + rowH * 14, labelW, 22);

    // Row 15: Phase Color
    phaseColorInput.setBounds(rightEdge - inputW, startY + rowH * 15, inputW, 22);
    phaseColorLabel.setBounds(rightEdge - inputW - labelW, startY + rowH * 15, labelW, 22);

    // Row 16: Freq Color
    freqColorInput.setBounds(rightEdge - inputW, startY + rowH * 16, inputW, 22);
    freqColorLabel.setBounds(rightEdge - inputW - labelW, startY + rowH * 16, labelW, 22);

    // Row 17: Pan Color
    panColorInput.setBounds(rightEdge - inputW, startY + rowH * 17, inputW, 22);
    panColorLabel.setBounds(rightEdge - inputW - labelW, startY + rowH * 17, labelW, 22);

    // Row 18: Reset Colors
    resetColorsButton.setBounds(rightEdge - inputW, startY + rowH * 18, inputW, 22);

    // ---- Bottom strips ----
    // 5 strips, each sliderH=28 with gap=4 between them.
    // Below all strips: 14px for frequency labels.
    const int h = getHeight();
    const int sliderH = 28;
    const int gap = 4;
    const int freqLabelBar = 14;
    const int autoBtnW = 44;
    const int speedW = 52;
    const int lbW = 84;
    const int wrapBtnW = 48;
    const int rowSz = sliderH + gap;

    // Strip 5 (bottom-most): Global Freq Shift  — has Wrap button
    const int s5y = h - freqLabelBar - rowSz;
    globalFreqShift.label.setBounds(gap, s5y, lbW, sliderH);
    globalFreqShift.speedInput.setBounds(w - speedW - gap, s5y, speedW, sliderH);
    globalFreqShift.autoBtn.setBounds(w - speedW - autoBtnW - gap * 2, s5y, autoBtnW, sliderH);
    globalFreqWrapButton.setBounds(w - speedW - autoBtnW - wrapBtnW - gap * 3, s5y, wrapBtnW, sliderH);
    globalFreqShift.slider.setBounds(lbW + gap, s5y,
        w - lbW - wrapBtnW - autoBtnW - speedW - gap * 5, sliderH);

    // Strip 4: Pan Shift
    const int s4y = s5y - rowSz;
    panShift.label.setBounds(gap, s4y, lbW, sliderH);
    panShift.speedInput.setBounds(w - speedW - gap, s4y, speedW, sliderH);
    panShift.autoBtn.setBounds(w - speedW - autoBtnW - gap * 2, s4y, autoBtnW, sliderH);
    panShift.slider.setBounds(lbW + gap, s4y, w - lbW - autoBtnW - speedW - gap * 4, sliderH);

    // Strip 3: Freq Shift (per-bin)
    const int s3y = s4y - rowSz;
    freqShift.label.setBounds(gap, s3y, lbW, sliderH);
    freqShift.speedInput.setBounds(w - speedW - gap, s3y, speedW, sliderH);
    freqShift.autoBtn.setBounds(w - speedW - autoBtnW - gap * 2, s3y, autoBtnW, sliderH);
    freqShift.slider.setBounds(lbW + gap, s3y, w - lbW - autoBtnW - speedW - gap * 4, sliderH);

    // Strip 2: Phase Shift
    const int s2y = s3y - rowSz;
    phaseShift.label.setBounds(gap, s2y, lbW, sliderH);
    phaseShift.speedInput.setBounds(w - speedW - gap, s2y, speedW, sliderH);
    phaseShift.autoBtn.setBounds(w - speedW - autoBtnW - gap * 2, s2y, autoBtnW, sliderH);
    phaseShift.slider.setBounds(lbW + gap, s2y, w - lbW - autoBtnW - speedW - gap * 4, sliderH);

    // Strip 1 (top-most): Filter Shift
    const int s1y = s2y - rowSz;
    filterShift.label.setBounds(gap, s1y, lbW, sliderH);
    filterShift.speedInput.setBounds(w - speedW - gap, s1y, speedW, sliderH);
    filterShift.autoBtn.setBounds(w - speedW - autoBtnW - gap * 2, s1y, autoBtnW, sliderH);
    filterShift.slider.setBounds(lbW + gap, s1y, w - lbW - autoBtnW - speedW - gap * 4, sliderH);
}

//==============================================================================
// Color update helpers
//==============================================================================
static juce::Colour parseHex(const juce::String& raw)
{
    juce::String h = raw.trim();
    if (h.startsWith("#")) h = h.substring(1);
    return juce::Colour::fromString("ff" + h);
}

void SpectralFilterAudioProcessorEditor::updateBgColor()
{
    try { audioProcessor.setBackgroundColor(parseHex(bgColorInput.getText())); repaint(); }
    catch (...) { bgColorInput.setText(audioProcessor.getBackgroundColor().toDisplayString(false)); }
}
void SpectralFilterAudioProcessorEditor::updateGridColor()
{
    try { audioProcessor.setGridColor(parseHex(gridColorInput.getText())); repaint(); }
    catch (...) { gridColorInput.setText(audioProcessor.getGridColor().toDisplayString(false)); }
}
void SpectralFilterAudioProcessorEditor::updateFilterColor()
{
    try { audioProcessor.setCurveColor(parseHex(filterColorInput.getText())); repaint(); }
    catch (...) { filterColorInput.setText(audioProcessor.getCurveColor().toDisplayString(false)); }
}
void SpectralFilterAudioProcessorEditor::updatePhaseColor()
{
    try { audioProcessor.setPhaseColor(parseHex(phaseColorInput.getText())); repaint(); }
    catch (...) { phaseColorInput.setText(audioProcessor.getPhaseColor().toDisplayString(false)); }
}
void SpectralFilterAudioProcessorEditor::updateFreqColor()
{
    try { audioProcessor.setFreqShiftColor(parseHex(freqColorInput.getText())); repaint(); }
    catch (...) { freqColorInput.setText(audioProcessor.getFreqShiftColor().toDisplayString(false)); }
}
void SpectralFilterAudioProcessorEditor::updatePanColor()
{
    try { audioProcessor.setPanColor(parseHex(panColorInput.getText())); repaint(); }
    catch (...) { panColorInput.setText(audioProcessor.getPanColor().toDisplayString(false)); }
}