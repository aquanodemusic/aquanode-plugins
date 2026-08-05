#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// ─── Layout Constants ────────────────────────────────────────────────────────
static constexpr int nOps = 12;
static constexpr int nParams = 6;           // ATT DEC SUS REL VOL RATIO
static constexpr int rowH = 52;
static constexpr int knobW = 55;
static constexpr int mSize = 24;
static constexpr int fbKnobSize = 30;
static constexpr int leftOff = 45;
static constexpr int knobSpacing = knobW + 4;  // 59 px

// nKnobCols = 7: used only as an anchor for the CNCL column position
static constexpr int nKnobCols = nParams + 1; // 7

// Change these in the Layout Constants section:
static constexpr int delayColX = 50; // Place right after OP number (width ~40)
static constexpr int knobStartX = delayColX + knobW + 10; // Shift ATT onwards right
// Update dependencies:
static constexpr int cancelColX = knobStartX + nParams * knobSpacing;
static constexpr int cancelColW = 30;
static constexpr int oscColX = cancelColX + cancelColW + 8;
static constexpr int oscComboW = 80;
static constexpr int matrixStartX = oscColX + oscComboW + 30;

static constexpr int pluginWidth = matrixStartX + nOps * mSize + 30;  // 976

//==============================================================================
// RatioKnob — Slider subclass that adds right-click drag snapping to 0.25 steps.
//
// Right-click + drag UP/DOWN moves the ratio value in 0.25 increments.
// Left-click behaves exactly like a standard RotaryHorizontalVerticalDrag slider.
//
// Implementation note: for right-click we still call Slider::mouseDown() so that
// JUCE captures the mouse (enabling subsequent mouseDrag callbacks).  We immediately
// restore the value to prevent any spurious jump, then handle all subsequent drag
// movement ourselves, never delegating to the base class while right-dragging.
//==============================================================================
class RatioKnob : public juce::Slider
{
public:
    RatioKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);
        // Disable JUCE's right-click popup so we can own right-click behaviour.
        setPopupMenuEnabled(false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            snapStartValue = getValue();
            snapStartScreenY = e.getScreenY();
            rightDragging = true;
            // Call base to capture mouse; immediately restore the value.
            juce::Slider::mouseDown(e);
            setValue(snapStartValue, juce::dontSendNotification);
            return;
        }
        rightDragging = false;
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (rightDragging)
        {
            // Each 8 px of upward travel = one 0.25 step
            const double deltaSteps = std::round(
                (double)(snapStartScreenY - e.getScreenY()) / 8.0);
            const double snapped = juce::jlimit(0.0, 20.0,
                snapStartValue + deltaSteps * 0.25);
            setValue(snapped, juce::sendNotification);
            return;
        }
        juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        rightDragging = false;
        juce::Slider::mouseUp(e);
    }

private:
    bool   rightDragging = false;
    double snapStartValue = 0.0;
    int    snapStartScreenY = 0;
};

//==============================================================================
FM12SynthAudioProcessorEditor::FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Apply the custom look to EVERYTHING in this editor
    setLookAndFeel(&myCustomLNF);
    getLookAndFeel().setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::orange);

    opKnobs.reserve(nOps * nParams);
    opKnobAttachments.reserve(nOps * nParams);
    matrixButtons.reserve(nOps * (nOps - 1));
    matrixAttachments.reserve(nOps * (nOps - 1));
    feedbackKnobs.reserve(nOps);
    feedbackAttachments.reserve(nOps);
    cancelToggles.reserve(nOps);
    cancelAttachments.reserve(nOps);
    delayKnobs.reserve(nOps);
    delayAttachments.reserve(nOps);
    oscCombos.reserve(nOps);

    // ── Parameter names ───────────────────────────────────────────────────────
    static constexpr const char* pNames[] =
    {
        "attack", "decay", "sustain", "release", "level", "ratio"
    };

    // ── Operator knobs ────────────────────────────────────────────────────────
    // Index 5 within each operator block is RATIO — uses RatioKnob for right-click
    // drag snapping (0.25 steps).  All other knobs are standard juce::Slider.
    for (int op = 0; op < nOps; ++op)
    {
        for (int i = 0; i < nParams; ++i)
        {
            std::unique_ptr<juce::Slider> s;

            if (i == 5) // RATIO
                s = std::make_unique<RatioKnob>();
            else
            {
                s = std::make_unique<juce::Slider>();
                s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);
            }

            addAndMakeVisible(s.get());

            const juce::String paramID = "op" + juce::String(op) + "_" + juce::String(pNames[i]);
            if (processor.apvts.getParameter(paramID) != nullptr)
            {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.apvts, paramID, *s);
                opKnobAttachments.push_back(std::move(att));
            }
            opKnobs.push_back(std::move(s));
        }
    }

    // ── Cancel toggles ────────────────────────────────────────────────────────
    for (int op = 0; op < nOps; ++op)
    {
        auto t = std::make_unique<juce::ToggleButton>("");
        addAndMakeVisible(t.get());

        const juce::String cancelID = "op" + juce::String(op) + "_cancel";
        if (processor.apvts.getParameter(cancelID) != nullptr)
        {
            auto att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.apvts, cancelID, *t);
            cancelAttachments.push_back(std::move(att));
        }
        cancelToggles.push_back(std::move(t));
    }

    // ── DELAY knobs ───────────────────────────────────────────────────────────
    // One per operator.  Delays the ADSR note-on by 0–10 s after the MIDI note.
    for (int op = 0; op < nOps; ++op)
    {
        auto s = std::make_unique<juce::Slider>();
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);

        // Show value as seconds with two decimal places.
        s->textFromValueFunction = [](double v) { return juce::String(v, 2) + "s"; };
        s->valueFromTextFunction = [](const juce::String& t)
            { return t.getDoubleValue(); };

        addAndMakeVisible(s.get());

        const juce::String paramID = "op" + juce::String(op) + "_delay";
        if (processor.apvts.getParameter(paramID) != nullptr)
        {
            auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, paramID, *s);
            delayAttachments.push_back(std::move(att));
        }
        delayKnobs.push_back(std::move(s));
    }

    // ── OSC waveform comboboxes ───────────────────────────────────────────────
    // One per operator.  Items map directly to the "op{N}_osc" float param:
    //   comboId 1 → param 0 = Sine   (default)
    //   comboId 2 → param 1 = Tri
    //   comboId 3 → param 2 = Saw
    //   comboId 4 → param 3 = Sqr
    //   comboId 5 → param 4 = User (512 samples)
    //   comboId 6 → param 5 = User (1024 samples)
    //   comboId 7 → param 6 = User (2048 samples)
    //
    // We do NOT use a APVTS ComboBoxAttachment here because we need to intercept
    // the "User" selections and open a file chooser.  timerCallback() polls the
    // param and keeps the visual selection in sync (e.g. after preset load).
    for (int op = 0; op < nOps; ++op)
    {
        auto c = std::make_unique<juce::ComboBox>();
        c->addItem("Sine", 1);
        c->addItem("Tri", 2);
        c->addItem("Saw", 3);
        c->addItem("Square", 4);
        c->addSeparator();
        c->addItem("512", 5);
        c->addItem("1024", 6);
        c->addItem("2048", 7);

        // Initialise from the current param value.
        if (auto* param = processor.apvts.getParameter("op" + juce::String(op) + "_osc"))
        {
            const int val = (int)std::round(param->convertFrom0to1(param->getValue()));
            c->setSelectedId(juce::jlimit(1, 7, val + 1), juce::dontSendNotification);
        }
        else
        {
            c->setSelectedId(1, juce::dontSendNotification); // Sine
        }

        c->onChange = [this, op]()
            {
                const int sel = oscCombos[op]->getSelectedId(); // 1–7
                const int paramVal = sel - 1;                         // 0–6

                // Push the new waveform index into the APVTS parameter.
                if (auto* param = processor.apvts.getParameter("op" + juce::String(op) + "_osc"))
                    param->setValueNotifyingHost(param->convertTo0to1((float)paramVal));

                // For User options, open a file chooser to load a single-cycle wave.
                if (sel >= 5)
                {
                    const int targetSize = (sel == 5) ? 512 : (sel == 6) ? 1024 : 2048;
                    loadUserWave(op, targetSize);
                }
            };

        addAndMakeVisible(c.get());
        oscCombos.push_back(std::move(c));
    }

    // Register WAV, AIFF, etc. so loadUserWave() can open standard audio files.
    formatManager.registerBasicFormats();

    // ── Routing matrix ────────────────────────────────────────────────────────
    for (int f = 0; f < nOps; ++f)
    {
        for (int t = 0; t < nOps; ++t)
        {
            if (f == t) continue;

            auto b = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible(b.get());

            const juce::String rID = "route_" + juce::String(f) + "_" + juce::String(t);
            if (processor.apvts.getParameter(rID) != nullptr)
            {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    processor.apvts, rID, *b);
                matrixAttachments.push_back(std::move(att));
            }
            matrixButtons.push_back(std::move(b));
        }
    }

    // ── Feedback knobs (on diagonal) ─────────────────────────────────────────
    for (int op = 0; op < nOps; ++op)
    {
        auto fb = std::make_unique<juce::Slider>();
        fb->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        fb->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(fb.get());

        const juce::String fbID = "feedback_" + juce::String(op);
        if (processor.apvts.getParameter(fbID) != nullptr)
        {
            auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, fbID, *fb);
            feedbackAttachments.push_back(std::move(att));
        }
        feedbackKnobs.push_back(std::move(fb));
    }

    // ── Top-bar controls ──────────────────────────────────────────────────────

    randomizeButton = std::make_unique<juce::TextButton>("Randomize");
    randomizeButton->onClick = [this] { randomizeMatrix(); };
    addAndMakeVisible(randomizeButton.get());

    stabButton = std::make_unique<juce::TextButton>("Stab");
    stabButton->onClick = [this] { randomizeStab(); };
    addAndMakeVisible(stabButton.get());

    saveButton = std::make_unique<juce::TextButton>("Save");
    saveButton->onClick = [this] { savePreset(); };
    addAndMakeVisible(saveButton.get());

    loadButton = std::make_unique<juce::TextButton>("Load");
    loadButton->onClick = [this] { loadPreset(); };
    addAndMakeVisible(loadButton.get());

    halveModsButton = std::make_unique<juce::TextButton>("Halve Vol");
    halveModsButton->onClick = [this] { halveModulators(); };
    addAndMakeVisible(halveModsButton.get());

    // EXP FB toggle
    expFeedbackToggle = std::make_unique<juce::ToggleButton>("Feedback Self-FM");
    addAndMakeVisible(expFeedbackToggle.get());
    expFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "feedbackModeExp", *expFeedbackToggle);

    // FM Engine Mode combobox
    fmEngineModeComboBox = std::make_unique<juce::ComboBox>();
    fmEngineModeComboBox->addItem("Phase Modulation", 1);
    fmEngineModeComboBox->addItem("Linear FM Mode", 2);
    fmEngineModeComboBox->addItem("Lin. FM Through Zero", 3);
    fmEngineModeComboBox->addItem("Exponential FM Mode", 4);
    fmEngineModeComboBox->addItem("Exp. FM Through Zero", 5);
    addAndMakeVisible(fmEngineModeComboBox.get());
    fmEngineModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "fmEngineMode", *fmEngineModeComboBox);

    chorusAmountKnob = std::make_unique<juce::Slider>();
    chorusAmountKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chorusAmountKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(chorusAmountKnob.get());
    chorusAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "chorusAmount", *chorusAmountKnob);

    chorusWidthKnob = std::make_unique<juce::Slider>();
    chorusWidthKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chorusWidthKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(chorusWidthKnob.get());
    chorusWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "chorusWidth", *chorusWidthKnob);

    nyquistSlider = std::make_unique<juce::Slider>();
    nyquistSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    nyquistSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    nyquistSlider->setTextValueSuffix(" Hz");
    addAndMakeVisible(nyquistSlider.get());

    nyquistAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "nyquistLimit", *nyquistSlider);

    nyquistSlider->textFromValueFunction = [](double value) {
        return juce::String(juce::roundToInt(value));
        };
    nyquistSlider->valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue();
        };
    nyquistSlider->updateText();

    // ── View OP combo box ─────────────────────────────────────────────────────
    viewOpComboBox = std::make_unique<juce::ComboBox>();
    viewOpComboBox->addItem("All", 1);
    for (int i = 1; i <= 12; ++i)
        viewOpComboBox->addItem(juce::String(i), i + 1);
    viewOpComboBox->addSeparator();
    viewOpComboBox->addItem("1 & 2", 14);
    viewOpComboBox->addItem("3 & 4", 15);
    viewOpComboBox->addItem("5 & 6", 16);
    viewOpComboBox->addItem("7 & 8", 17);
    viewOpComboBox->addItem("9 & 10", 18);
    viewOpComboBox->addItem("11 & 12", 19);
    viewOpComboBox->addSeparator();
    viewOpComboBox->addItem("1 to 4", 20);
    viewOpComboBox->addItem("5 to 8", 21);
    viewOpComboBox->addItem("9 to 12", 22);
    viewOpComboBox->addSeparator();
    viewOpComboBox->addItem("1 to 6", 23);
    viewOpComboBox->addItem("7 to 12", 24);
    viewOpComboBox->setSelectedId(2, juce::dontSendNotification); // default: OP 1
    viewOpComboBox->onChange = [this]
        {
            currentViewOp = viewOpComboBox->getSelectedId() - 1;
            updateViewLayout();
        };
    addAndMakeVisible(viewOpComboBox.get());

    // Poll OSC param values at 10 Hz to keep comboboxes in sync after preset loads.
    startTimerHz(10);

    setSize(pluginWidth, 715);
    setOpaque(true);
}

FM12SynthAudioProcessorEditor::~FM12SynthAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
// timerCallback — keeps OSC comboboxes in sync with APVTS (e.g. after preset load)
//==============================================================================
void FM12SynthAudioProcessorEditor::timerCallback()
{
    for (int op = 0; op < nOps; ++op)
    {
        if (auto* param = processor.apvts.getParameter("op" + juce::String(op) + "_osc"))
        {
            const int paramVal = (int)std::round(param->convertFrom0to1(param->getValue()));
            const int comboId = juce::jlimit(1, 7, paramVal + 1);

            if (oscCombos[op]->getSelectedId() != comboId)
                oscCombos[op]->setSelectedId(comboId, juce::dontSendNotification);
        }
    }
}

//==============================================================================
// loadUserWave — file chooser + single-cycle wave loader
//==============================================================================
void FM12SynthAudioProcessorEditor::loadUserWave(int op, int numSamples)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load Single-Cycle Wave (" + juce::String(numSamples) + " samples) for OP"
        + juce::String(op + 1),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aif;*.aiff;*.flac");

    const auto flags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, op, numSamples, chooser](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File{}) return;   // user cancelled

            auto* rawReader = formatManager.createReaderFor(file);
            if (rawReader == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Load Error",
                    "Could not open the audio file.  Please use a standard WAV, AIFF or FLAC.", "OK");
                return;
            }

            std::unique_ptr<juce::AudioFormatReader> reader(rawReader);

            // Allocate a mono buffer of exactly numSamples and zero it (so that
            // files shorter than numSamples are automatically zero-padded).
            juce::AudioBuffer<float> buf(1, numSamples);
            buf.clear();

            const int samplesToRead = (int)juce::jmin(
                (juce::int64)numSamples, reader->lengthInSamples);

            reader->read(&buf, 0, samplesToRead, 0, true /*L*/, false /*R*/);

            // Normalise so the loudest sample is ±1 — keeps downstream level stable.
            const float peak = buf.getMagnitude(0, numSamples);
            if (peak > 0.0f)
                buf.applyGain(1.0f / peak);

            // Copy into the processor's shared wavetable under the spinlock so that
            // audio-thread reads remain consistent.
            {
                juce::SpinLock::ScopedLockType lock(processor.userWaveLock);
                juce::FloatVectorOperations::copy(
                    processor.userWaveData[op],
                    buf.getReadPointer(0),
                    numSamples);
                processor.userWaveSize[op] = numSamples;
            }
        });
}

//==============================================================================
// getViewRange — maps currentViewOp to {firstOp (0-indexed), opCount}
//==============================================================================
std::pair<int, int> FM12SynthAudioProcessorEditor::getViewRange() const
{
    if (currentViewOp == 0)  return { 0, 12 };   // All

    if (currentViewOp <= 12) return { currentViewOp - 1, 1 }; // Single OP

    // Pairs
    if (currentViewOp == 13) return { 0, 2 };
    if (currentViewOp == 14) return { 2, 2 };
    if (currentViewOp == 15) return { 4, 2 };
    if (currentViewOp == 16) return { 6, 2 };
    if (currentViewOp == 17) return { 8, 2 };
    if (currentViewOp == 18) return { 10, 2 };

    // Quads
    if (currentViewOp == 19) return { 0, 4 };
    if (currentViewOp == 20) return { 4, 4 };
    if (currentViewOp == 21) return { 8, 4 };

    // Sixes
    if (currentViewOp == 22) return { 0, 6 };
    if (currentViewOp == 23) return { 6, 6 };

    return { 0, 12 }; // fallback
}

//==============================================================================
void FM12SynthAudioProcessorEditor::resized()
{
    constexpr int startY = 80;

    auto [firstOp, opCount] = getViewRange();

    // ── Operator knobs + Cancel toggles + DELAY knobs + OSC combos ───────────
    for (int op = 0; op < nOps; ++op)
    {
        const int  relIdx = op - firstOp;
        const bool vis = (relIdx >= 0 && relIdx < opCount);
        const int  displayRow = vis ? relIdx : 0;
        const int  yBase = startY + displayRow * rowH;

        // ATT DEC SUS REL VOL RATIO
        for (int i = 0; i < nParams; ++i)
        {
            opKnobs[op * nParams + i]->setVisible(vis);
            // This now correctly starts at the shifted knobStartX
            opKnobs[op * nParams + i]->setBounds(knobStartX + i * knobSpacing, yBase, knobW, rowH - 2);
        }

        // Position the Delay knob before the Attack knob
        delayKnobs[op]->setVisible(vis);
        delayKnobs[op]->setBounds(delayColX, yBase, knobW, rowH - 2);

        // Cancel toggle — column 6, slightly inset
        const int cancelX = knobStartX + nParams * knobSpacing; // 399
        cancelToggles[op]->setVisible(vis);
        cancelToggles[op]->setBounds(cancelX + 5, yBase + (rowH - 20) / 2, cancelColW, 20);

        // OSC combobox — column after DELAY, vertically centred in the row
        oscCombos[op]->setVisible(vis);
        oscCombos[op]->setBounds(oscColX, yBase + (rowH - 24) / 2, oscComboW, 24);
    }

    // ── Routing matrix (excluding diagonal) ──────────────────────────────────
    {
        int buttonIdx = 0;
        for (int row = 0; row < nOps; ++row)
        {
            const int  relIdx = row - firstOp;
            const bool vis = (relIdx >= 0 && relIdx < opCount);
            const int  displayRow = vis ? relIdx : 0;
            const int  yPos = startY + displayRow * rowH + (rowH - mSize) / 2;

            for (int col = 0; col < nOps; ++col)
            {
                if (row == col) continue;
                matrixButtons[buttonIdx]->setVisible(vis);
                matrixButtons[buttonIdx]->setBounds(matrixStartX + col * mSize, yPos, mSize, mSize);
                ++buttonIdx;
            }
        }
    }

    // ── Feedback knobs (diagonal) ─────────────────────────────────────────────
    for (int op = 0; op < nOps; ++op)
    {
        const int  relIdx = op - firstOp;
        const bool vis = (relIdx >= 0 && relIdx < opCount);
        const int  displayRow = vis ? relIdx : 0;
        const int  xPos = matrixStartX + op * mSize + (mSize - fbKnobSize) / 2;
        const int  yPos = startY + displayRow * rowH + (rowH - fbKnobSize) / 2;
        feedbackKnobs[op]->setVisible(vis);
        feedbackKnobs[op]->setBounds(xPos, yPos, fbKnobSize, fbKnobSize);
    }

    // ─── TOP BAR ─────────────────────────────────────────────────────────────
    constexpr int r1y = 20;
    constexpr int rH = 28;

    viewOpComboBox->setBounds(68, r1y, 60, rH);
    nyquistSlider->setBounds(125, r1y, 100, rH);

    constexpr int bGap = 4;
    int bx = 230;
    saveButton->setBounds(bx, r1y, 47, rH);  bx += 47 + bGap;
    loadButton->setBounds(bx, r1y, 47, rH);  bx += 47 + bGap;
    randomizeButton->setBounds(bx, r1y, 83, rH); bx += 83 + bGap;
    stabButton->setBounds(bx, r1y, 47, rH);  bx += 47 + bGap;
    halveModsButton->setBounds(bx, r1y, 70, rH);

    constexpr int cKnob = 28;
    constexpr int chorusX = 477 + 140;
    chorusAmountKnob->setBounds(chorusX - 5, r1y - 3, cKnob + 10, cKnob + 10);
    chorusWidthKnob->setBounds(chorusX - 5 + cKnob, r1y - 3, cKnob + 10, cKnob + 10);

    const int rightEdge = 530 + 140;
    expFeedbackToggle->setBounds(rightEdge + 4, r1y, 100, rH);
    fmEngineModeComboBox->setBounds(rightEdge + 90, r1y, 150, rH);
}

//==============================================================================
void FM12SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(205, 190, 160), 0, 0,
        juce::Colour(170, 150, 115), 0, (float)getHeight(), false));
    g.fillAll();

    // ── Branding ──────────────────────────────────────────────────────────────
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("FM12", 8, 14, 58, 28, juce::Justification::left, true);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("aquanode", 9, 27, 58, 28, juce::Justification::left, true);

    // ── Top Bar Micro-labels ──────────────────────────────────────────────────
    g.setFont(juce::Font(8.5f));
    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.drawText("VIEW OP", 68, 11, 60, 9, juce::Justification::centred, true);
    g.drawText("NYQUIST", 165, 11, 70, 9, juce::Justification::left, true);
    g.drawText("CHORUS", 475 + 140, 11, 60, 9, juce::Justification::centred, true);

    // ── Knob column labels ────────────────────────────────────────────────────

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.8f));

    constexpr int labelY = 66;

    // Draw the rest (ATT, DEC, etc.) starting at the new knobStartX
    static constexpr const char* knobLabels[] = { "ATT", "DEC", "SUS", "REL", "VOL", "RATIO" };
    for (int i = 0; i < nParams; ++i)
        g.drawText(knobLabels[i], knobStartX + i * knobSpacing, labelY, knobW, 13,
            juce::Justification::centred);

    // Cancel column header
    const int cancelLabelX = knobStartX + nParams * knobSpacing; // 399
    g.drawText("CNCL", cancelLabelX, labelY, cancelColW, 13, juce::Justification::centred);

    // Draw DELAY label at the new X
    g.drawText("DELAY", delayColX, labelY, knobW, 13, juce::Justification::centred);

    // OSC column header
    g.drawText("WAVEFORM", oscColX, labelY, oscComboW, 13, juce::Justification::centred);

    // ── Operator row labels ────────────────────────────────────────────────────
    constexpr int startY = 80;
    constexpr int opYOffset = (rowH / 2) - 7;

    auto [firstOp, opCount] = getViewRange();

    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.setColour(juce::Colours::black);

    for (int i = 0; i < nOps; ++i)
    {
        const int relIdx = i - firstOp;
        if (relIdx < 0 || relIdx >= opCount) continue;
        g.drawText("OP" + juce::String(i + 1),
            5, startY + relIdx * rowH + opYOffset, 38, 14,
            juce::Justification::centredRight);
    }

    // ── Matrix section header ─────────────────────────────────────────────────
    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.drawText("ROUTING MATRIX  (Empty Row = Carrier)",
        matrixStartX, 58, nOps * mSize, 16,
        juce::Justification::centred);

    g.setFont(juce::Font(10.0f));
    for (int i = 0; i < nOps; ++i)
        g.drawText(juce::String(i + 1),
            matrixStartX + i * mSize, 72, mSize, 12,
            juce::Justification::centred);

    // Separator line
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawVerticalLine(matrixStartX - 15, 60.0f, (float)getHeight() - 10.0f);
}

//==============================================================================
void FM12SynthAudioProcessorEditor::updateViewLayout()
{
    auto [firstOp, opCount] = getViewRange();
    const int newHeight = 80 + opCount * rowH + 16;

    if (getHeight() != newHeight)
        setSize(pluginWidth, newHeight);
    else
        resized();
    repaint();
}

//==============================================================================
void FM12SynthAudioProcessorEditor::randomizeMatrix()
{
    juce::Random rng(juce::Time::currentTimeMillis());

    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
    {
        if (row == 0)
        {
            buttonIdx += (nOps - 1);
            continue;
        }

        for (int col = 0; col < nOps; ++col)
        {
            if (row == col) continue;
            const bool shouldEnable = rng.nextFloat() < 0.3f;
            matrixButtons[buttonIdx]->setToggleState(shouldEnable, juce::sendNotificationSync);
            buttonIdx++;
        }
    }

    auto& apvts = processor.apvts;
    for (int op = 0; op < nOps; ++op)
    {
        const float vol = (op == 0) ? 1.0f : rng.nextFloat();
        if (auto* p = apvts.getParameter("op" + juce::String(op) + "_level"))
            p->setValueNotifyingHost(p->convertTo0to1(vol));
    }
}

//==============================================================================
void FM12SynthAudioProcessorEditor::halveModulators()
{
    auto& apvts = processor.apvts;

    bool isModulator[nOps] = {};
    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
        for (int col = 0; col < nOps; ++col)
        {
            if (row == col) continue;
            if (matrixButtons[buttonIdx]->getToggleState())
                isModulator[row] = true;
            buttonIdx++;
        }

    for (int op = 0; op < nOps; ++op)
    {
        if (!isModulator[op]) continue;

        const juce::String id = "op" + juce::String(op) + "_level";
        if (auto* p = apvts.getParameter(id))
        {
            const float current = p->convertFrom0to1(p->getValue());
            const float halved = current * 0.5f;
            if (halved >= 0.001f)
                p->setValueNotifyingHost(p->convertTo0to1(halved));
        }
    }
}

//==============================================================================
void FM12SynthAudioProcessorEditor::randomizeStab()
{
    juce::Random rng(juce::Time::currentTimeMillis());
    auto& apvts = processor.apvts;

    static constexpr float harmonicRatios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    static constexpr float inharmonicRatios[] = { 0.5f, 1.0f, 1.41f, 1.5f, 2.0f, 2.76f, 3.0f, 3.5f, 5.0f, 7.0f, 9.0f, 11.0f, 14.0f };
    static constexpr int   numHarmonic = (int)(sizeof(harmonicRatios) / sizeof(harmonicRatios[0]));
    static constexpr int   numInharmonic = (int)(sizeof(inharmonicRatios) / sizeof(inharmonicRatios[0]));

    auto setParam = [&](const juce::String& id, float value)
        {
            if (auto* p = apvts.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(value));
        };

    auto applyStabEnv = [&](int op, bool longDecay)
        {
            const juce::String pre = "op" + juce::String(op) + "_";
            setParam(pre + "attack", rng.nextFloat() * 0.02f);
            setParam(pre + "decay", longDecay ? 0.12f + rng.nextFloat() * 0.55f
                : 0.05f + rng.nextFloat() * 0.35f);
            setParam(pre + "sustain", rng.nextFloat() * 0.25f);
            setParam(pre + "release", 0.02f + rng.nextFloat() * 0.15f);
        };

    auto clearMatrix = [&]()
        {
            int idx = 0;
            for (int row = 0; row < nOps; ++row)
                for (int col = 0; col < nOps; ++col)
                {
                    if (row == col) continue;
                    matrixButtons[idx]->setToggleState(false, juce::sendNotificationSync);
                    idx++;
                }
        };

    auto setRoute = [&](int from, int to, bool enabled)
        {
            int idx = 0;
            for (int row = 0; row < nOps; ++row)
                for (int col = 0; col < nOps; ++col)
                {
                    if (row == col) continue;
                    if (row == from && col == to)
                    {
                        matrixButtons[idx]->setToggleState(enabled, juce::sendNotificationSync);
                        return;
                    }
                    idx++;
                }
        };

    // ── Classic layer (OP1 carrier, OP2–OP5 modulators) ──────────────────────
    auto applyClassicParams = [&]() -> int
        {
            const int numActiveOps = 2 + rng.nextInt(4);

            for (int op = 0; op < 5; ++op)
            {
                const bool active = (op < numActiveOps);
                const juce::String p = "op" + juce::String(op) + "_";
                applyStabEnv(op, false);
                setParam(p + "level", (op == 0) ? 0.7f : (active ? 0.2f + rng.nextFloat() * 0.5f : 0.0f));
                setParam(p + "ratio", harmonicRatios[rng.nextInt(numHarmonic)]);
            }
            return numActiveOps;
        };

    auto applyClassicRoutes = [&](int numActiveOps)
        {
            for (int row = 1; row < numActiveOps; ++row)
                for (int col = 0; col < row; ++col)
                    if (rng.nextFloat() < 0.6f)
                        setRoute(row, col, true);
        };

    auto applyClassicFeedback = [&](int)
        {
            for (int op = 0; op < 5; ++op)
                setParam("feedback_" + juce::String(op), 0.0f);
        };

    auto silenceClassicOps = [&]()
        {
            for (int op = 0; op < 5; ++op)
            {
                applyStabEnv(op, false);
                setParam("op" + juce::String(op) + "_level", 0.0f);
                setParam("op" + juce::String(op) + "_ratio", 1.0f);
                setParam("feedback_" + juce::String(op), 0.0f);
            }
        };

    // ── OP6-carrier layer (OP6 carrier, OP7–OP12 modulators) ─────────────────
    constexpr int op6CarrierIdx = 5;
    constexpr int op6ModStart = 6;
    constexpr int op6NumMods = 6;

    auto applyOp6Params = [&](bool useInharmonic) -> int
        {
            const int numActiveMods = 2 + rng.nextInt(op6NumMods - 1);

            applyStabEnv(op6CarrierIdx, false);
            setParam("op" + juce::String(op6CarrierIdx) + "_level", 0.7f);
            setParam("op" + juce::String(op6CarrierIdx) + "_ratio",
                harmonicRatios[rng.nextInt(numHarmonic)]);

            for (int i = 0; i < op6NumMods; ++i)
            {
                const int  op = op6ModStart + i;
                const bool active = (i < numActiveMods);
                const juce::String p = "op" + juce::String(op) + "_";
                applyStabEnv(op, active);
                setParam(p + "level", active ? 0.3f + rng.nextFloat() * 0.4f : 0.0f);
                setParam(p + "ratio", useInharmonic
                    ? inharmonicRatios[rng.nextInt(numInharmonic)]
                    : harmonicRatios[rng.nextInt(numHarmonic)]);
            }
            return numActiveMods;
        };

    auto applyOp6Routes = [&](int numActiveMods)
        {
            const int flavour = rng.nextInt(3);

            if (flavour == 0)
            {
                for (int i = numActiveMods - 1; i >= 0; --i)
                {
                    const int src = op6ModStart + i;
                    const int dest = (i == 0) ? op6CarrierIdx : (op6ModStart + i - 1);
                    setRoute(src, dest, true);
                }
            }
            else if (flavour == 1)
            {
                for (int i = 0; i < numActiveMods; ++i)
                    setRoute(op6ModStart + i, op6CarrierIdx, true);
            }
            else
            {
                for (int i = 0; i < numActiveMods; ++i)
                {
                    const int op = op6ModStart + i;
                    if (rng.nextFloat() < 0.70f)
                        setRoute(op, op6CarrierIdx, true);
                    if (i > 0 && rng.nextFloat() < 0.40f)
                        setRoute(op, op6ModStart + rng.nextInt(i), true);
                }
                setRoute(op6ModStart, op6CarrierIdx, true);
            }
        };

    auto applyOp6Feedback = [&](int)
        {
            setParam("feedback_" + juce::String(op6CarrierIdx), 0.0f);
            for (int i = 0; i < op6NumMods; ++i)
                setParam("feedback_" + juce::String(op6ModStart + i), 0.0f);
        };

    auto silenceOp6Layer = [&]()
        {
            applyStabEnv(op6CarrierIdx, false);
            setParam("op" + juce::String(op6CarrierIdx) + "_level", 0.0f);
            setParam("op" + juce::String(op6CarrierIdx) + "_ratio", 1.0f);
            setParam("feedback_" + juce::String(op6CarrierIdx), 0.0f);
            for (int i = 0; i < op6NumMods; ++i)
            {
                const int op = op6ModStart + i;
                applyStabEnv(op, false);
                setParam("op" + juce::String(op) + "_level", 0.0f);
                setParam("op" + juce::String(op) + "_ratio", 1.0f);
                setParam("feedback_" + juce::String(op), 0.0f);
            }
        };

    // ── 33/33/33 mode selector ────────────────────────────────────────────────
    const int mode = rng.nextInt(3);
    clearMatrix();

    if (mode == 0)
    {
        const int n = applyClassicParams();
        silenceOp6Layer();
        applyClassicRoutes(n);
        applyClassicFeedback(n);
    }
    else if (mode == 1)
    {
        silenceClassicOps();
        const int n = applyOp6Params(true);
        applyOp6Routes(n);
        applyOp6Feedback(n);
    }
    else
    {
        const int nClassic = applyClassicParams();
        const int nOp6 = applyOp6Params(rng.nextFloat() >= 0.5f);
        applyClassicRoutes(nClassic);
        applyOp6Routes(nOp6);
        applyClassicFeedback(nClassic);
        applyOp6Feedback(nOp6);
    }
}

//==============================================================================
void FM12SynthAudioProcessorEditor::savePreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save FM12 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.fm12preset");

    auto flags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;

            if (!file.hasFileExtension(".fm12preset"))
                file = file.withFileExtension(".fm12preset");

            juce::MemoryBlock memoryBlock;
            processor.getStateInformation(memoryBlock);

            if (file.replaceWithData(memoryBlock.getData(), memoryBlock.getSize()))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                    "Success", "Preset saved successfully!", "OK");
            else
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Error", "Failed to save preset file.", "OK");
        });
}

//==============================================================================
void FM12SynthAudioProcessorEditor::loadPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load FM12 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.fm12preset");

    auto flags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File() || !file.existsAsFile()) return;

            juce::MemoryBlock memoryBlock;
            if (file.loadFileAsData(memoryBlock))
                processor.setStateInformation(memoryBlock.getData(),
                    static_cast<int>(memoryBlock.getSize()));
            else
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Error", "Failed to read preset file.", "OK");
        });
}