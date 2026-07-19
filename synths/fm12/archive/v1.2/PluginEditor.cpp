#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Konstanten
static constexpr int nOps = 12;
static constexpr int nParams = 7;
static constexpr int rowH = 52;
static constexpr int knobW = 55;
static constexpr int mSize = 22;
static constexpr int fbKnobSize = 30;  // Dedicated size for feedback knobs
static constexpr int leftOff = 45;

FM12SynthAudioProcessorEditor::FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Pre-allocate vectors to avoid reallocations
    opKnobs.reserve(nOps * nParams);
    opKnobAttachments.reserve(nOps * nParams);
    matrixButtons.reserve(nOps * (nOps - 1)); // Excluding diagonal
    matrixAttachments.reserve(nOps * (nOps - 1));
    feedbackKnobs.reserve(nOps);
    feedbackAttachments.reserve(nOps);

    static constexpr const char* pNames[] =
    {
        "attack", "decay", "sustain", "release",
        "level", "ratio", "phase"
    };

    // Knobs erstellen
    for (int op = 0; op < nOps; ++op) {
        for (int i = 0; i < nParams; ++i) {
            auto s = std::make_unique<juce::Slider>();
            s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);
            addAndMakeVisible(s.get());

            const juce::String paramID = "op" + juce::String(op) + "_" + juce::String(pNames[i]);

            if (processor.apvts.getParameter(paramID) != nullptr) {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramID, *s);
                opKnobAttachments.push_back(std::move(att));
            }
            opKnobs.push_back(std::move(s));
        }
    }

    // Matrix erstellen first (so they're behind feedback knobs)
    for (int f = 0; f < nOps; ++f) {
        for (int t = 0; t < nOps; ++t) {
            // Skip diagonal - that's where feedback knobs go
            if (f == t)
                continue;

            auto b = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible(b.get());

            const juce::String rID = "route_" + juce::String(f) + "_" + juce::String(t);

            if (processor.apvts.getParameter(rID) != nullptr) {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, rID, *b);
                matrixAttachments.push_back(std::move(att));
            }
            matrixButtons.push_back(std::move(b));
        }
    }

    // Feedback knobs erstellen AFTER matrix (so they appear in front)
    for (int op = 0; op < nOps; ++op)
    {
        auto fb = std::make_unique<juce::Slider>();
        fb->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        fb->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(fb.get());

        const juce::String fbID = "feedback_" + juce::String(op);

        if (processor.apvts.getParameter(fbID) != nullptr) {
            auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, fbID, *fb);
            feedbackAttachments.push_back(std::move(att));
        }
        feedbackKnobs.push_back(std::move(fb));
    }

    adsrFMToggle = std::make_unique<juce::ToggleButton>("PM/FM");
    adsrFMToggle->setButtonText("PM/FM");
    addAndMakeVisible(adsrFMToggle.get());
    adsrFMAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "adsrAffectsFM", *adsrFMToggle);

    randomizeButton = std::make_unique<juce::TextButton>("Rnd");
    randomizeButton->onClick = [this] { randomizeMatrix(); };
    addAndMakeVisible(randomizeButton.get());

    stabButton = std::make_unique<juce::TextButton>("Stb");
    stabButton->onClick = [this] { randomizeStab(); };
    addAndMakeVisible(stabButton.get());

    // Create Save and Load buttons
    saveButton = std::make_unique<juce::TextButton>("Save");
    saveButton->onClick = [this] { savePreset(); };
    addAndMakeVisible(saveButton.get());

    loadButton = std::make_unique<juce::TextButton>("Load");
    loadButton->onClick = [this] { loadPreset(); };
    addAndMakeVisible(loadButton.get());

    halveModsButton = std::make_unique<juce::TextButton>("/2");
    halveModsButton->onClick = [this] { halveModulators(); };
    addAndMakeVisible(halveModsButton.get());

    expFeedbackToggle = std::make_unique<juce::ToggleButton>("EXP FB");
    expFeedbackToggle->setButtonText("EXPFB");
    addAndMakeVisible(expFeedbackToggle.get());
    expFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "feedbackModeExp", *expFeedbackToggle);

    chorusAmountKnob = std::make_unique<juce::Slider>();
    chorusAmountKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chorusAmountKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(chorusAmountKnob.get());
    chorusAmountAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "chorusAmount", *chorusAmountKnob);

    chorusWidthKnob = std::make_unique<juce::Slider>();
    chorusWidthKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chorusWidthKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(chorusWidthKnob.get());
    chorusWidthAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "chorusWidth", *chorusWidthKnob);

    nyquistSlider = std::make_unique<juce::Slider>();
    nyquistSlider->setSliderStyle(juce::Slider::LinearHorizontal);
    nyquistSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    nyquistSlider->setTextValueSuffix(" Hz");
    addAndMakeVisible(nyquistSlider.get());
    nyquistAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "nyquistLimit", *nyquistSlider);

    setSize(800, 700);

    // Disable automatic background painting for better performance
    setOpaque(true);
}

FM12SynthAudioProcessorEditor::~FM12SynthAudioProcessorEditor() {}

void FM12SynthAudioProcessorEditor::resized()
{
    constexpr int startY = 60;
    constexpr int knobStartX = leftOff;
    constexpr int knobSpacing = knobW + 4;
    constexpr int matrixStartX = knobStartX + (nParams * (knobW + 5)) + 40;

    // --- Operator knobs ---
    const int totalKnobs = static_cast<int>(opKnobs.size());
    for (int i = 0; i < totalKnobs; ++i)
    {
        const int row = i / nParams;
        const int col = i % nParams;
        const int xPos = knobStartX + col * knobSpacing;
        const int yPos = startY + row * rowH;
        opKnobs[i]->setBounds(xPos, yPos, knobW, rowH - 2);
    }

    // --- Routing matrix (excluding diagonal) ---
    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
    {
        for (int col = 0; col < nOps; ++col)
        {
            // Skip diagonal
            if (row == col)
                continue;

            const int xPos = matrixStartX + col * mSize;
            const int yPos = startY + row * rowH + (rowH - mSize) / 2;
            matrixButtons[buttonIdx]->setBounds(xPos, yPos, mSize, mSize);
            buttonIdx++;
        }
    }

    // --- Feedback knobs (diagonal) - positioned AFTER matrix buttons for z-order ---
    for (int op = 0; op < nOps; ++op)
    {
        const int xPos = matrixStartX + op * mSize + (mSize - fbKnobSize) / 2;
        const int yPos = startY + op * rowH + (rowH - fbKnobSize) / 2;
        feedbackKnobs[op]->setBounds(xPos, yPos, fbKnobSize, fbKnobSize);
    }

    // --- Chorus + controls ---
    constexpr int toggleX = matrixStartX;
    constexpr int toggleY = 12;

    constexpr int knobSize = 40;
    constexpr int buttonHeight = 24;

    // Chorus knobs (unchanged)
    chorusAmountKnob->setBounds(toggleX + 20, 4, knobSize, knobSize);
    chorusWidthKnob->setBounds(toggleX + knobSize + 8, 4, knobSize, knobSize);

    // Nyquist slider — between branding and Save button (x=197 to x=326)
    nyquistSlider->setBounds(197, 14, 128, 20);

    // Save and Load — original positions
    constexpr int smallButtonWidth = 55;
    const int buttonStartX = toggleX + 2 * knobSize + 36;
    saveButton->setBounds(buttonStartX - 283, toggleY, smallButtonWidth, buttonHeight);
    loadButton->setBounds(buttonStartX - 283 + smallButtonWidth + 5, toggleY, smallButtonWidth, buttonHeight);
    halveModsButton->setBounds(buttonStartX - 283 + smallButtonWidth + 5 + smallButtonWidth + 5, toggleY, 28, buttonHeight);

    // Rnd | Stb | PM/FM | EXP FB
    // Old layout had two 90px buttons starting at buttonStartX-30. Now four narrower ones.
    const int triStart = buttonStartX - 30;
    constexpr int rndW = 34;
    constexpr int stbW = 32;
    constexpr int pmfmW = 60;
    constexpr int expW = 60;
    constexpr int gap = 4;
    randomizeButton->setBounds(triStart, toggleY, rndW, buttonHeight);
    stabButton->setBounds(triStart + rndW + gap, toggleY, stbW, buttonHeight);
    adsrFMToggle->setBounds(triStart + rndW + gap + stbW + gap, toggleY, pmfmW, buttonHeight);
    expFeedbackToggle->setBounds(triStart + rndW + gap + stbW + gap + pmfmW + gap - 2, toggleY, expW, buttonHeight);
}

void FM12SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Hintergrund
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(205, 190, 160), 0, 0,
        juce::Colour(170, 150, 115), 0, (float)getHeight(), false));
    g.fillAll();

    // --- BRANDING (UNVERÄNDERT) ---
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("FM12 by aquanode", 20, 8, 200, 30,
        juce::Justification::left, true);

    // --- NYQUIST label (above slider) ---
    g.setFont(juce::Font(9.0f));
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.drawText("NYQUIST", 197, 4, 60, 10, juce::Justification::left, true);

    const juce::String explanation2 =
        "Chorus";

    g.drawText(explanation2,
        500,
        5,
        100,
        40,
        juce::Justification::left, true);

    // --- KNOB COLUMN LABELS ---
    static constexpr const char* knobLabels[] =
    { "ATT", "DEC", "SUS", "REL", "VOL", "RATIO", "PHASE" };

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.8f));

    constexpr int knobStartX = leftOff;
    constexpr int labelY = 45;
    constexpr int knobSpacing = knobW + 4;

    for (int i = 0; i < nParams; ++i)
    {
        const int xPos = knobStartX + i * knobSpacing;
        g.drawText(knobLabels[i], xPos, labelY, knobW, 14,
            juce::Justification::centred);
    }

    // --- OPERATOR ROW LABELS ---
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.setColour(juce::Colours::black);

    constexpr int startY = 60;
    constexpr int opYOffset = (rowH / 2) - 7;

    for (int i = 0; i < nOps; ++i)
    {
        const int yPos = startY + i * rowH + opYOffset;
        g.drawText("OP" + juce::String(i + 1),
            5, yPos, 38, 14,
            juce::Justification::centredRight);
    }

    // --- MATRIX SECTION ---
    constexpr int matrixStartX = knobStartX + (nParams * (knobW + 5)) + 40;

    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("ROUTING MATRIX (Empty Row = Carrier)",
        matrixStartX, 38, nOps * mSize, 18,
        juce::Justification::centred);

    g.setFont(juce::Font(10.0f));
    for (int i = 0; i < nOps; ++i)
    {
        const int xPos = matrixStartX + i * mSize;
        g.drawText(juce::String(i + 1),
            xPos, 56, mSize, 14,
            juce::Justification::centred);
    }

    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawVerticalLine(matrixStartX - 20, 40.0f,
        (float)getHeight() - 10.0f);
}


void FM12SynthAudioProcessorEditor::randomizeMatrix()
{
    juce::Random rng(juce::Time::currentTimeMillis());

    // Iterate through matrix buttons (which excludes diagonal)
    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
    {
        // Skip first row (operator 0)
        if (row == 0)
        {
            buttonIdx += (nOps - 1); // Skip all buttons in row 0
            continue;
        }

        for (int col = 0; col < nOps; ++col)
        {
            // Skip diagonal (shouldn't exist in matrixButtons anyway)
            if (row == col)
                continue;

            // 30% chance to enable each connection for sparse routing
            const bool shouldEnable = rng.nextFloat() < 0.3f;
            matrixButtons[buttonIdx]->setToggleState(shouldEnable, juce::sendNotificationSync);
            buttonIdx++;
        }
    }

    // Randomize all operator volumes (OP1 always full, rest 0–1)
    auto& apvts = processor.apvts;
    for (int op = 0; op < nOps; ++op)
    {
        const float vol = (op == 0) ? 1.0f : rng.nextFloat();
        if (auto* p = apvts.getParameter("op" + juce::String(op) + "_level"))
            p->setValueNotifyingHost(p->convertTo0to1(vol));
    }
}

void FM12SynthAudioProcessorEditor::halveModulators()
{
    auto& apvts = processor.apvts;

    // An operator is a modulator if it has at least one outgoing route enabled.
    // Matrix rows = source op, columns = destination op (diagonal excluded).
    // Walk matrixButtons in the same row-major order used everywhere else.
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

void FM12SynthAudioProcessorEditor::randomizeStab()
{
    juce::Random rng(juce::Time::currentTimeMillis());
    auto& apvts = processor.apvts;

    // ── Ratio palettes ────────────────────────────────────────────────────────
    static constexpr float harmonicRatios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    static constexpr float inharmonicRatios[] = { 0.5f, 1.0f, 1.41f, 1.5f, 2.0f, 2.76f, 3.0f, 3.5f, 5.0f, 7.0f, 9.0f, 11.0f, 14.0f };
    static constexpr int   numHarmonic = (int)(sizeof(harmonicRatios) / sizeof(harmonicRatios[0]));
    static constexpr int   numInharmonic = (int)(sizeof(inharmonicRatios) / sizeof(inharmonicRatios[0]));

    // ── Shared helpers ────────────────────────────────────────────────────────

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
    // Returns numActiveOps so the caller can apply feedback consistently.

    auto applyClassicParams = [&]() -> int
        {
            const int numActiveOps = 2 + rng.nextInt(4); // 2–5
            for (int op = 0; op < 5; ++op)               // indices 0–4 = OP1–OP5
            {
                const bool active = (op < numActiveOps);
                const juce::String p = "op" + juce::String(op) + "_";
                applyStabEnv(op, false);
                setParam(p + "level", (op == 0) ? 0.7f : (active ? 0.2f + rng.nextFloat() * 0.5f : 0.0f));
                setParam(p + "ratio", harmonicRatios[rng.nextInt(numHarmonic)]);
                setParam(p + "phase", rng.nextFloat());
            }
            return numActiveOps;
        };

    auto applyClassicRoutes = [&](int numActiveOps)
        {
            // Active ops above OP1 have 60% chance to modulate any active lower op
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
                setParam("op" + juce::String(op) + "_phase", rng.nextFloat());
                setParam("feedback_" + juce::String(op), 0.0f);
            }
        };

    // ── OP6-carrier layer (OP6 carrier, OP7–OP12 modulators) ─────────────────
    // Indices: OP6=5, OP7=6 … OP12=11

    constexpr int op6CarrierIdx = 5;
    constexpr int op6ModStart = 6;
    constexpr int op6NumMods = 6; // OP7–OP12

    auto applyOp6Params = [&](bool useInharmonic) -> int
        {
            const int numActiveMods = 2 + rng.nextInt(op6NumMods - 1); // 2–6

            // Carrier (OP6)
            applyStabEnv(op6CarrierIdx, false);
            setParam("op" + juce::String(op6CarrierIdx) + "_level", 0.7f);
            setParam("op" + juce::String(op6CarrierIdx) + "_ratio", harmonicRatios[rng.nextInt(numHarmonic)]);
            setParam("op" + juce::String(op6CarrierIdx) + "_phase", rng.nextFloat());

            // Modulators (OP7–OP12)
            for (int i = 0; i < op6NumMods; ++i)
            {
                const int  op = op6ModStart + i;
                const bool active = (i < numActiveMods);
                const juce::String p = "op" + juce::String(op) + "_";
                applyStabEnv(op, active); // active mods get longer decay for evolving timbre
                setParam(p + "level", active ? 0.3f + rng.nextFloat() * 0.4f : 0.0f);
                setParam(p + "ratio", useInharmonic ? inharmonicRatios[rng.nextInt(numInharmonic)]
                    : harmonicRatios[rng.nextInt(numHarmonic)]);
                setParam(p + "phase", rng.nextFloat());
            }
            return numActiveMods;
        };

    auto applyOp6Routes = [&](int numActiveMods)
        {
            // Three routing flavours: STACK / CLUSTER / HYBRID
            const int flavour = rng.nextInt(3);

            if (flavour == 0)
            {
                // STACK: serial chain ending at OP6
                for (int i = numActiveMods - 1; i >= 0; --i)
                {
                    const int src = op6ModStart + i;
                    const int dest = (i == 0) ? op6CarrierIdx : (op6ModStart + i - 1);
                    setRoute(src, dest, true);
                }
            }
            else if (flavour == 1)
            {
                // CLUSTER: all active mods hit OP6 directly
                for (int i = 0; i < numActiveMods; ++i)
                    setRoute(op6ModStart + i, op6CarrierIdx, true);
            }
            else
            {
                // HYBRID: mix of direct paths and inter-mod chains
                for (int i = 0; i < numActiveMods; ++i)
                {
                    const int op = op6ModStart + i;
                    if (rng.nextFloat() < 0.70f)
                        setRoute(op, op6CarrierIdx, true);
                    if (i > 0 && rng.nextFloat() < 0.40f)
                        setRoute(op, op6ModStart + rng.nextInt(i), true);
                }
                setRoute(op6ModStart, op6CarrierIdx, true); // guarantee a path
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
            setParam("op" + juce::String(op6CarrierIdx) + "_phase", rng.nextFloat());
            setParam("feedback_" + juce::String(op6CarrierIdx), 0.0f);
            for (int i = 0; i < op6NumMods; ++i)
            {
                const int op = op6ModStart + i;
                applyStabEnv(op, false);
                setParam("op" + juce::String(op) + "_level", 0.0f);
                setParam("op" + juce::String(op) + "_ratio", 1.0f);
                setParam("op" + juce::String(op) + "_phase", rng.nextFloat());
                setParam("feedback_" + juce::String(op), 0.0f);
            }
        };

    // ── 33 / 33 / 33 MODE SELECTOR ───────────────────────────────────────────
    // 0 = Classic only | 1 = OP6-carrier only | 2 = Both simultaneously
    const int mode = rng.nextInt(3);

    clearMatrix(); // always clear once up front

    if (mode == 0)
    {
        // ── CLASSIC ONLY ────────────────────────────────────────────────────
        const int n = applyClassicParams();
        silenceOp6Layer();
        applyClassicRoutes(n);
        applyClassicFeedback(n);
    }
    else if (mode == 1)
    {
        // ── OP6-CARRIER ONLY ────────────────────────────────────────────────
        silenceClassicOps();
        const int n = applyOp6Params(true); // always inharmonic in solo mode
        applyOp6Routes(n);
        applyOp6Feedback(n);
    }
    else
    {
        // ── BOTH SIMULTANEOUSLY ──────────────────────────────────────────────
        // OP1–OP5 block and OP6–OP12 block occupy entirely separate matrix zones
        // so their routes never conflict.
        const int nClassic = applyClassicParams();
        const int nOp6 = applyOp6Params(rng.nextFloat() >= 0.5f); // 50% inharmonic, 50% harmonic
        applyClassicRoutes(nClassic);
        applyOp6Routes(nOp6);
        applyClassicFeedback(nClassic);
        applyOp6Feedback(nOp6);
    }
}

void FM12SynthAudioProcessorEditor::savePreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save FM12 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.fm12preset");

    auto flags = juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
            {
                // Add extension if not present
                if (!file.hasFileExtension(".fm12preset"))
                    file = file.withFileExtension(".fm12preset");

                // Get the current state from the processor
                juce::MemoryBlock memoryBlock;
                processor.getStateInformation(memoryBlock);

                // Write to file
                if (file.replaceWithData(memoryBlock.getData(), memoryBlock.getSize()))
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Success",
                        "Preset saved successfully!",
                        "OK");
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to save preset file.",
                        "OK");
                }
            }
        });
}

void FM12SynthAudioProcessorEditor::loadPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load FM12 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.fm12preset");

    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File() && file.existsAsFile())
            {
                // Read the file
                juce::MemoryBlock memoryBlock;

                if (file.loadFileAsData(memoryBlock))
                {
                    // Set the state in the processor
                    processor.setStateInformation(memoryBlock.getData(),
                        static_cast<int>(memoryBlock.getSize()));
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to read preset file.",
                        "OK");
                }
            }
        });
}