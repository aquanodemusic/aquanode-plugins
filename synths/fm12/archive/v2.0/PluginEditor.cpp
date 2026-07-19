#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// ─── Layout Constants ────────────────────────────────────────────────────────
static constexpr int nOps      = 12;
static constexpr int nParams   = 6;    // ATT DEC SUS REL VOL RATIO
static constexpr int rowH      = 52;
static constexpr int knobW     = 55;
static constexpr int mSize     = 24;
static constexpr int fbKnobSize = 30;
static constexpr int leftOff   = 45;
static constexpr int knobSpacing = knobW + 4;  // 59 px

// 7 visual columns (6 knobs + 1 lock-ratio toggle) before the matrix gap
static constexpr int nKnobCols   = nParams + 1;                          // 7
static constexpr int matrixStartX = leftOff + nKnobCols * (knobW + 5) + 40; // 505
static constexpr int pluginWidth  = 820;   // widened to fit fmEngineMode combobox

//==============================================================================
FM12SynthAudioProcessorEditor::FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    opKnobs.reserve(nOps * nParams);
    opKnobAttachments.reserve(nOps * nParams);
    matrixButtons.reserve(nOps * (nOps - 1));
    matrixAttachments.reserve(nOps * (nOps - 1));
    feedbackKnobs.reserve(nOps);
    feedbackAttachments.reserve(nOps);
    lockRatioToggles.reserve(nOps);

    // ── Parameter names (phase removed) ──────────────────────────────────────
    static constexpr const char* pNames[] =
    {
        "attack", "decay", "sustain", "release", "level", "ratio"
    };

    // ── Operator knobs ────────────────────────────────────────────────────────
    for (int op = 0; op < nOps; ++op)
    {
        for (int i = 0; i < nParams; ++i)
        {
            auto s = std::make_unique<juce::Slider>();
            s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);
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

    // ── Lock Ratio toggles ────────────────────────────────────────────────────
    // Created after knobs so we can reference both in the snap callbacks.
    for (int op = 0; op < nOps; ++op)
    {
        auto t = std::make_unique<juce::ToggleButton>("");
        addAndMakeVisible(t.get());
        lockRatioToggles.push_back(std::move(t));
    }

    // ── Ratio snap callbacks ─────────────────────────────────────────────────
    // Index 5 = RATIO knob within each operator's nParams block.
    for (int op = 0; op < nOps; ++op)
    {
        auto* ratioKnob  = opKnobs[op * nParams + 5].get();
        auto* lockToggle = lockRatioToggles[op].get();

        // Snap on every value change while lock is active
        ratioKnob->onValueChange = [ratioKnob, lockToggle]()
        {
            if (!lockToggle->getToggleState()) return;
            double val     = ratioKnob->getValue();
            double snapped = std::round(val * 4.0) / 4.0;
            snapped        = juce::jlimit(0.0, 20.0, snapped);
            if (std::abs(val - snapped) > 1e-4)
                ratioKnob->setValue(snapped, juce::sendNotification);
        };

        // Also snap immediately when lock is turned ON
        lockToggle->onClick = [ratioKnob, lockToggle]()
        {
            if (!lockToggle->getToggleState()) return;
            double val     = ratioKnob->getValue();
            double snapped = std::round(val * 4.0) / 4.0;
            snapped        = juce::jlimit(0.0, 20.0, snapped);
            ratioKnob->setValue(snapped, juce::sendNotification);
        };
    }

    // ── Routing matrix (behind feedback knobs) ────────────────────────────────
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

    // ── Feedback knobs (in front of matrix, on diagonal) ─────────────────────
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

    randomizeButton = std::make_unique<juce::TextButton>("Rand");
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

    halveModsButton = std::make_unique<juce::TextButton>("/2");
    halveModsButton->onClick = [this] { halveModulators(); };
    addAndMakeVisible(halveModsButton.get());

    // EXP FB toggle — anyFM-style delay ring-buffer feedback (unchanged)
    expFeedbackToggle = std::make_unique<juce::ToggleButton>("Feedback Self-FM");
    addAndMakeVisible(expFeedbackToggle.get());
    expFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "feedbackModeExp", *expFeedbackToggle);

    // FM Engine Mode combobox — replaces "Real FM" + "Through Zero FM" toggles.
    // Item IDs must start at 1 for ComboBoxAttachment (maps to choice index 0..4).
    fmEngineModeComboBox = std::make_unique<juce::ComboBox>();
    fmEngineModeComboBox->addItem("Phase Modulation Mode", 1);
    fmEngineModeComboBox->addItem("Linear FM Mode",        2);
    fmEngineModeComboBox->addItem("Lin. FM Through Zero",  3);
    fmEngineModeComboBox->addItem("Exponential FM Mode",   4);
    fmEngineModeComboBox->addItem("Exp. FM Through Zero",  5);
    addAndMakeVisible(fmEngineModeComboBox.get());
    // Attach AFTER adding items so the attachment can set the current value correctly
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

    // 1. Create the attachment FIRST
    nyquistAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "nyquistLimit", *nyquistSlider);

    // 2. Set the lambdas AFTER the attachment to override the APVTS default behavior
    nyquistSlider->textFromValueFunction = [](double value) {
        return juce::String(juce::roundToInt(value));
        };

    nyquistSlider->valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue();
        };

    // 3. Force an update immediately so the current value loses the .0
    nyquistSlider->updateText();

    viewOpComboBox = std::make_unique<juce::ComboBox>();
    viewOpComboBox->addItem("All", 1);
    for (int i = 1; i <= 12; ++i)
        viewOpComboBox->addItem(juce::String(i), i + 1);
    viewOpComboBox->setSelectedId(2, juce::dontSendNotification); // default: OP 1
    viewOpComboBox->onChange = [this]
    {
        currentViewOp = viewOpComboBox->getSelectedId() - 1; // 0=All, 1–12=single
        updateViewLayout();
    };
    addAndMakeVisible(viewOpComboBox.get());

    setSize(pluginWidth, 80 + rowH + 16);
    setOpaque(true);
}

FM12SynthAudioProcessorEditor::~FM12SynthAudioProcessorEditor() {}

//==============================================================================
void FM12SynthAudioProcessorEditor::resized()
{
    constexpr int startY      = 80;
    constexpr int knobStartX  = leftOff;

    const bool showAll = (currentViewOp == 0);

    // ── Operator knobs ────────────────────────────────────────────────────────
    for (int op = 0; op < nOps; ++op)
    {
        const bool vis        = showAll || (op == currentViewOp - 1);
        const int  displayRow = showAll ? op : 0;
        const int  yBase      = startY + displayRow * rowH;

        for (int i = 0; i < nParams; ++i)
        {
            opKnobs[op * nParams + i]->setVisible(vis);
            opKnobs[op * nParams + i]->setBounds(knobStartX + i * knobSpacing, yBase, knobW, rowH - 2);
        }

        // Lock Ratio toggle — sits in the freed column-6 slot
        const int lockX = knobStartX + nParams * knobSpacing; // = 45 + 6*59 = 399
        lockRatioToggles[op]->setVisible(vis);
        lockRatioToggles[op]->setBounds(lockX + 15, yBase + (rowH - 20) / 2, knobW, 20);
    }

    // ── Routing matrix (excluding diagonal) ──────────────────────────────────
    {
        int buttonIdx = 0;
        for (int row = 0; row < nOps; ++row)
        {
            const bool vis        = showAll || (row == currentViewOp - 1);
            const int  displayRow = showAll ? row : 0;
            const int  yPos       = startY + displayRow * rowH + (rowH - mSize) / 2;

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
        const bool vis        = showAll || (op == currentViewOp - 1);
        const int  displayRow = showAll ? op : 0;
        const int  xPos       = matrixStartX + op * mSize + (mSize - fbKnobSize) / 2;
        const int  yPos       = startY + displayRow * rowH + (rowH - fbKnobSize) / 2;
        feedbackKnobs[op]->setVisible(vis);
        feedbackKnobs[op]->setBounds(xPos, yPos, fbKnobSize, fbKnobSize);
    }

    // ─── TOP BAR (Single Row) ────────────────────────────────────────────────
    constexpr int r1y = 20;
    constexpr int rH = 28;

    // View Combo & Nyquist (x start, y start, width, height)
    viewOpComboBox->setBounds(68, r1y, 60, rH);
    nyquistSlider->setBounds(125, r1y, 100, rH);

    // Button Cluster
    constexpr int bGap = 4;
    int bx = 230;
    saveButton->setBounds(bx, r1y, 47, rH); bx += 47 + bGap;
    loadButton->setBounds(bx, r1y, 47, rH); bx += 47 + bGap;
    randomizeButton->setBounds(bx, r1y, 47, rH); bx += 47 + bGap;
    stabButton->setBounds(bx, r1y, 47, rH); bx += 47 + bGap;
    halveModsButton->setBounds(bx, r1y, 35, rH);

    // Chorus Knobs (Tight pair)
    constexpr int cKnob = 28;
    constexpr int chorusX = 477;
    chorusAmountKnob->setBounds(chorusX - 5, r1y - 3, cKnob + 10, cKnob + 10);
    chorusWidthKnob->setBounds(chorusX -8 + cKnob, r1y - 3, cKnob + 10, cKnob + 10);

    // Right-aligned controls
    const int rightEdge = 530;
    expFeedbackToggle->setBounds(rightEdge + 1, r1y, 100, rH);
    // FM Engine Mode combobox — replaces the old "Through Zero FM" + "Real FM" toggles
    fmEngineModeComboBox->setBounds(rightEdge + 90, r1y, 180, rH);
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
    g.drawText("aquanode", 8, 27, 58, 28, juce::Justification::left, true);

    // ── Top Bar Micro-labels ──────────────────────────────────────────────────
    g.setFont(juce::Font(8.5f));
    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.drawText("VIEW OP", 68, 11, 60, 9, juce::Justification::centred, true);
    g.drawText("NYQUIST", 165, 11, 70, 9, juce::Justification::left, true);
    g.drawText("CHORUS", 475, 11, 60, 9, juce::Justification::centred, true);

    // ── Knob column labels ────────────────────────────────────────────────────
    static constexpr const char* knobLabels[] = { "ATT", "DEC", "SUS", "REL", "VOL", "RATIO" };

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.8f));

    // Shifted down from 46 to 66 to clear the top bar buttons
    constexpr int labelY = 66;
    constexpr int knobStartX = leftOff;

    for (int i = 0; i < nParams; ++i)
        g.drawText(knobLabels[i], knobStartX + i * knobSpacing, labelY, knobW, 13,
            juce::Justification::centred);

    // Lock Ratio column header
    const int lockLabelX = knobStartX + nParams * knobSpacing;
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("LOCK", lockLabelX, labelY, knobW, 13, juce::Justification::centred);

    // ── Operator row labels ────────────────────────────────────────────────────
    constexpr int startY = 80;
    constexpr int opYOffset = (rowH / 2) - 7;
    const bool showAll = (currentViewOp == 0);

    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.setColour(juce::Colours::black);

    for (int i = 0; i < nOps; ++i)
    {
        if (!showAll && i != currentViewOp - 1) continue;
        const int displayRow = showAll ? i : 0;
        // Increased width from 38 to 58 to prevent "OP12" clipping
        g.drawText("OP" + juce::String(i + 1),
            5, startY + displayRow * rowH + opYOffset, 38, 14,
            juce::Justification::centredRight);
    }

    // ── Matrix section header ─────────────────────────────────────────────────
    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.85f));

    // Shifted title down from 38 to 58
    g.drawText("ROUTING MATRIX  (Empty Row = Carrier)",
        matrixStartX, 58, nOps * mSize, 16,
        juce::Justification::centred);

    g.setFont(juce::Font(10.0f));
    for (int i = 0; i < nOps; ++i)
        // Shifted numbers down from 54 to 72
        g.drawText(juce::String(i + 1),
            matrixStartX + i * mSize, 72, mSize, 12,
            juce::Justification::centred);

    // Separator line - Shifted start Y from 40 to 60
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawVerticalLine(matrixStartX - 30, 60.0f, (float)getHeight() - 10.0f);
}

//==============================================================================
void FM12SynthAudioProcessorEditor::updateViewLayout()
{
    const bool showAll   = (currentViewOp == 0);
    const int  numVisible = showAll ? nOps : 1;
    const int  newHeight  = 80 + numVisible * rowH + 16;

    if (getHeight() != newHeight)
        setSize(pluginWidth, newHeight);
    else
        resized();
}

//==============================================================================
void FM12SynthAudioProcessorEditor::randomizeMatrix()
{
    juce::Random rng(juce::Time::currentTimeMillis());

    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
    {
        // Keep row 0 (OP1) clean so there's always a carrier
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

    // OP1 always full volume, others random
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
            const float halved  = current * 0.5f;
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

    static constexpr float harmonicRatios[]   = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    static constexpr float inharmonicRatios[] = { 0.5f, 1.0f, 1.41f, 1.5f, 2.0f, 2.76f, 3.0f, 3.5f, 5.0f, 7.0f, 9.0f, 11.0f, 14.0f };
    static constexpr int   numHarmonic    = (int)(sizeof(harmonicRatios)   / sizeof(harmonicRatios[0]));
    static constexpr int   numInharmonic  = (int)(sizeof(inharmonicRatios) / sizeof(inharmonicRatios[0]));

    auto setParam = [&](const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    auto applyStabEnv = [&](int op, bool longDecay)
    {
        const juce::String pre = "op" + juce::String(op) + "_";
        setParam(pre + "attack",  rng.nextFloat() * 0.02f);
        setParam(pre + "decay",   longDecay ? 0.12f + rng.nextFloat() * 0.55f
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
    constexpr int op6ModStart   = 6;
    constexpr int op6NumMods    = 6;

    auto applyOp6Params = [&](bool useInharmonic) -> int
    {
        const int numActiveMods = 2 + rng.nextInt(op6NumMods - 1);

        applyStabEnv(op6CarrierIdx, false);
        setParam("op" + juce::String(op6CarrierIdx) + "_level", 0.7f);
        setParam("op" + juce::String(op6CarrierIdx) + "_ratio",
                 harmonicRatios[rng.nextInt(numHarmonic)]);

        for (int i = 0; i < op6NumMods; ++i)
        {
            const int  op     = op6ModStart + i;
            const bool active = (i < numActiveMods);
            const juce::String p = "op" + juce::String(op) + "_";
            applyStabEnv(op, active);
            setParam(p + "level", active ? 0.3f + rng.nextFloat() * 0.4f : 0.0f);
            setParam(p + "ratio", useInharmonic
                ? inharmonicRatios[rng.nextInt(numInharmonic)]
                : harmonicRatios  [rng.nextInt(numHarmonic)]);
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
                const int src  = op6ModStart + i;
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
        const int nOp6     = applyOp6Params(rng.nextFloat() >= 0.5f);
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
