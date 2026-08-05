#include "PluginEditor.h"
#include "PluginProcessor.h"

// Set from PhizmoAudioProcessorEditor::resized(); every font in the plugin
// is derived from it so the text scales with the window.
float PhizmoLookAndFeel::guiScale = 1.0f;

//==============================================================================
// PhizmoLookAndFeel — black domed knobs, magenta rim, white pointer.
//==============================================================================
PhizmoLookAndFeel::PhizmoLookAndFeel()
{
    setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.75f));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x33000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a1638));
    setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.85f));
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1a0a2e));
    setColour(juce::TextEditor::textColourId, juce::Colour(0xffff8ad4));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff6040a0));
}

void PhizmoLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float sliderPos, float startA, float endA, juce::Slider&)
{
    // The value arc sits at R + arcOffset and is stroked again with a 6px glow,
    // so the drawing area has to leave room for both or the arc gets clipped
    // top and bottom. Working from a centred square also stops wide-but-short
    // cells from distorting the knob.
    const float arcOffset = 2.5f, arcGlow = 3.5f;
    const float inset = arcOffset + arcGlow + 1.0f;
    const float d = juce::jmin((float)w, (float)h);
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h)
        .withSizeKeepingCentre(d, d)
        .reduced(inset);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float R = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
    const float ang = startA + sliderPos * (endA - startA);

    // --- drop shadow so the knob sits on the panel like the real hardware ---
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(cx - R * 0.96f, cy - R * 0.96f + R * 0.10f, R * 1.92f, R * 1.92f);

    // --- value arc ---
    const float aR = R + arcOffset;
    juce::Path track; track.addCentredArc(cx, cy, aR, aR, 0.f, startA, endA, true);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc(cx, cy, aR, aR, 0.f, startA, ang, true);
    g.setColour(juce::Colour(col_accent1));
    g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // arc glow — width must stay within arcGlow*2 or the knob clips
    g.setColour(juce::Colour(col_accent1).withAlpha(0.25f));
    g.strokePath(val, juce::PathStrokeType(arcGlow * 2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // --- ribbed skirt: the Phizmo knobs have a fluted outer collar ---
    const float rOuter = R, rInner = R * 0.78f;
    g.setColour(juce::Colour(0xff17131c));
    g.fillEllipse(cx - rOuter, cy - rOuter, rOuter * 2.f, rOuter * 2.f);
    for (int i = 0; i < 22; ++i)
    {
        const float a = ang + (float)i * juce::MathConstants<float>::twoPi / 22.f;
        const float ca = std::cos(a), sa = std::sin(a);
        // light catches one side of each rib
        const float lit = 0.5f + 0.5f * std::cos(a - 2.4f);
        g.setColour(juce::Colours::white.withAlpha(0.03f + 0.09f * lit));
        juce::Path rib;
        rib.startNewSubPath(cx + rInner * sa, cy - rInner * ca);
        rib.lineTo(cx + rOuter * sa, cy - rOuter * ca);
        g.strokePath(rib, juce::PathStrokeType(juce::jmax(1.0f, R * 0.10f)));
    }
    // collar edge
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(cx - rOuter, cy - rOuter, rOuter * 2.f, rOuter * 2.f, 1.0f);

    // --- domed cap ---
    juce::ColourGradient dome(juce::Colour(0xff4a4152), cx - R * 0.42f, cy - R * 0.55f,
        juce::Colour(0xff08060c), cx + R * 0.45f, cy + R * 0.6f, true);
    dome.addColour(0.55, juce::Colour(0xff1c1722));
    g.setGradientFill(dome);
    g.fillEllipse(cx - rInner, cy - rInner, rInner * 2.f, rInner * 2.f);

    // specular highlight
    g.setColour(juce::Colours::white.withAlpha(0.13f));
    g.fillEllipse(cx - rInner * 0.62f, cy - rInner * 0.80f, rInner * 1.05f, rInner * 0.62f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawEllipse(cx - rInner, cy - rInner, rInner * 2.f, rInner * 2.f, 1.0f);

    // --- white pointer, from centre out across the cap ---
    const float sa = std::sin(ang), ca = std::cos(ang);
    juce::Path ptr;
    ptr.startNewSubPath(cx + rInner * 0.10f * sa, cy - rInner * 0.10f * ca);
    ptr.lineTo(cx + (rInner - 1.5f) * sa, cy - (rInner - 1.5f) * ca);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.strokePath(ptr, juce::PathStrokeType(R * 0.20f + 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.96f));
    g.strokePath(ptr, juce::PathStrokeType(juce::jmax(1.8f, R * 0.17f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PhizmoLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(getLabelFont(label));
    g.drawFittedText(label.getText(), label.getLocalBounds(),
        juce::Justification::centred, 1, 1.0f);
}

juce::Font PhizmoLookAndFeel::getLabelFont(juce::Label&)
{
    return panelFont();
}

void PhizmoLookAndFeel::drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) {}
void PhizmoLookAndFeel::drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) {}

//==============================================================================
// PhButton — oval domed button with off-centre LED
//==============================================================================
void PhButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    float rad = b.getHeight() * 0.5f;

    bool lit = ledLit || getToggleState();

    // dome body
    juce::ColourGradient body(juce::Colour(0xff37243f), b.getTopLeft(),
        juce::Colour(0xff0c0714), b.getBottomLeft(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(b, rad);
    if (down) { g.setColour(juce::Colours::black.withAlpha(0.3f)); g.fillRoundedRectangle(b, rad); }
    g.setColour(juce::Colour(0xff54406a).withAlpha(over ? 1.f : 0.7f));
    g.drawRoundedRectangle(b, rad, 1.2f);
    // top glint
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(b.withHeight(b.getHeight() * 0.45f).reduced(3.f, 1.5f), rad * 0.5f);

    // off-centre LED (upper-left, like the hardware's sideways-glancing eyes).
    // Momentary buttons (LOAD, SAVE, TAP, ...) carry no state, so they draw no
    // lamp at all rather than a permanently-dark one.
    float ledR = juce::jmin(4.5f * PhizmoLookAndFeel::guiScale, b.getHeight() * 0.22f);
    float lx = b.getX() + ledR + 3.f, ly = b.getCentreY();
    if (!hasLed)
    {
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(PhizmoLookAndFeel::panelFont());
        g.drawFittedText(getButtonText(), b.toNearestInt(),
            juce::Justification::centred, 1, 1.0f);
        return;
    }
    juce::Colour ledCol = lit ? juce::Colour(PhizmoLookAndFeel::col_led)
        : juce::Colour(0xff3a1414);
    if (lit) { g.setColour(ledCol.withAlpha(0.4f)); g.fillEllipse(lx - ledR * 2.f, ly - ledR * 2.f, ledR * 4.f, ledR * 4.f); }
    g.setColour(ledCol); g.fillEllipse(lx - ledR, ly - ledR, ledR * 2.f, ledR * 2.f);
    g.setColour(juce::Colours::white.withAlpha(lit ? 0.7f : 0.2f));
    g.fillEllipse(lx - ledR * 0.4f, ly - ledR * 0.5f, ledR * 0.7f, ledR * 0.7f);

    // Text sits in the area to the right of the LED. Centre it there, but only
    // trim the left (for the LED) — trimming both sides shrank narrow buttons
    // like the numbered VOICE keys until the label vanished.
    const float ledClear = (lx + ledR) - b.getX() + 2.f;
    // If the label is short enough to fit centred in the whole button without
    // colliding with the LED, centre it in the whole button (looks best for the
    // 1-4 number keys); otherwise centre it in the post-LED area.
    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.setFont(PhizmoLookAndFeel::panelFont());
    juce::GlyphArrangement ga;
    ga.addLineOfText(g.getCurrentFont(), getButtonText(), 0.0f, 0.0f);
    const float textW = ga.getBoundingBox(0, -1, true).getWidth();
    // Seat the label in the clear area to the right of the LED when it fits
    // there. On the very narrow buttons (the 1-4 VOICE keys) that strip is only
    // a few px wide, so drawFittedText used to collapse the digit into an
    // ellipsis — in that case centre across the whole button instead, letting
    // the glyph sit just past the little lamp.
    const float postLedW = b.getWidth() - ledClear - 2.f;
    auto tArea = (textW <= postLedW)
        ? b.withTrimmedLeft(ledClear).withTrimmedRight(2.f).toNearestInt()
        : b.toNearestInt();
    g.drawFittedText(getButtonText(), tArea, juce::Justification::centred, 1, 1.0f);
}

//==============================================================================
// LCDDisplay — red 7-seg-ish window
//==============================================================================
void LCDDisplay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1a0405)); g.fillRoundedRectangle(b, 4.f);
    g.setColour(juce::Colour(0xff5a1a1a)); g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 1.2f);
    // faint 'off' segments
    g.setColour(juce::Colour(0xff3a0c0c));
    g.setFont(juce::Font(juce::FontOptions().withHeight(b.getHeight() * 0.6f).withStyle("Bold")));
    g.drawText("8888", b, juce::Justification::centred);
    // lit text
    g.setColour(juce::Colour(0xffff4030));
    g.drawText(text, b, juce::Justification::centred);
    g.setColour(juce::Colour(0x22ff8080)); // glow
    g.drawText(text, b, juce::Justification::centred);
}

//==============================================================================
// SectionPanel — screened title + left divider
//==============================================================================
void SectionPanel::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    // left screened divider line
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(b.getX() + 0.5f, b.getY() + 2.f, b.getX() + 0.5f, b.getBottom() - 2.f, 1.f);
    // title
    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.setFont(PhizmoLookAndFeel::panelFont());
    g.drawFittedText(title, b.removeFromTop(16.f * PhizmoLookAndFeel::guiScale)
        .toNearestInt().withTrimmedLeft(4),
        juce::Justification::centredLeft, 1, 1.0f);
}

//==============================================================================
// PhKnob
//==============================================================================
PhKnob::PhKnob(juce::AudioProcessorValueTreeState& s, juce::LookAndFeel* l,
    const juce::String& caption, const juce::String& paramID, bool stepped)
    : state(s)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setLookAndFeel(l);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(PhizmoLookAndFeel::col_accent1));
    if (stepped) slider.setVelocityBasedMode(false);
    addAndMakeVisible(slider);

    label.setText(caption, juce::dontSendNotification);
    label.setLookAndFeel(l);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    if (paramID.isNotEmpty()) bind(paramID);
}

void PhKnob::bind(const juce::String& paramID)
{
    att.reset();
    if (state.getParameter(paramID) != nullptr)
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramID, slider);
}

void PhKnob::resized()
{
    auto b = getLocalBounds();
    // caption strip scales with the window so the legend never crowds the knob
    const int capH = juce::jlimit(11, 40,
        juce::roundToInt(15.f * PhizmoLookAndFeel::guiScale));
    label.setBounds(b.removeFromBottom(capH));
    slider.setBounds(b);
}


//==============================================================================
// Small factory helpers
//==============================================================================
std::unique_ptr<PhKnob> PhizmoAudioProcessorEditor::makeKnob(
    const juce::String& cap, const juce::String& id, bool stepped)
{
    auto k = std::make_unique<PhKnob>(audioProcessor.apvts, &laf, cap, id, stepped);
    addAndMakeVisible(*k);
    return k;
}

std::unique_ptr<PhButton> PhizmoAudioProcessorEditor::makeButton(const juce::String& txt, bool toggle)
{
    auto b = std::make_unique<PhButton>(txt);
    b->setLookAndFeel(&laf);
    if (toggle) b->setClickingTogglesState(true);
    addAndMakeVisible(*b);
    return b;
}

// For momentary action buttons that have no on/off state to show.
std::unique_ptr<PhButton> PhizmoAudioProcessorEditor::makeActionButton(const juce::String& txt)
{
    auto b = makeButton(txt, false);
    b->hasLed = false;
    return b;
}

//==============================================================================
PhizmoAudioProcessorEditor::PhizmoAudioProcessorEditor(PhizmoAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&laf);

    // Load the product wordmark (compiled in as a binary resource by Projucer)
    // and trim it to the opaque wordmark so it can be placed precisely.
    logoImg = juce::ImageCache::getFromMemory(BinaryData::Phizmo_Logo_png,
        BinaryData::Phizmo_Logo_pngSize);
    if (logoImg.isValid())
        logoImg = logoImg.getClippedImage(juce::Rectangle<int>(143, 405, 1257, 186));

    // Wavetable LOAD list (and other menus) drawn in Phizmo's cyan-blue instead
    // of the default grey, with dark text so the names stay legible.
    laf.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff8fd8ff));
    laf.setColour(juce::PopupMenu::textColourId, juce::Colour(0xff0c1424));
    laf.setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff3f9fd6));
    laf.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    laf.setColour(juce::PopupMenu::headerTextColourId, juce::Colour(0xff0c1424));

    // Section panels
    for (auto* s : { &secVolume,&secPreset,&secSound,&secNoise,&secKbd,&secOsc,&secPitch,&secGlide,
                     &secEnv,&secAmp,&secTempo,&secArp,&secFx,&secWave,&secFilter,&secMod,
                     &secTwEnv,&secRT,&secEvo,&secRev })
        addAndMakeVisible(*s);

    addAndMakeVisible(lcd);

    // ---- Volume ----
    kVolume = makeKnob("VOLUME", "gain");

    // ---- Preset ----
    bLoad = makeActionButton("LOAD"); bSave = makeActionButton("SAVE");
    bLoad->onClick = [this] { loadPresetClicked(); };
    bSave->onClick = [this] { savePresetClicked(); };

    // ---- Sound ----
    // Two clearly separated rows, so nothing is hidden behind a second press:
    //   EDIT 1..4 — exclusive, chooses which Sound the whole panel edits
    //   ON   1..4 — independent, switches that Sound in and out of the Preset
    for (int i = 0; i < 4; ++i)
    {
        bSoundEdit[i] = makeButton(juce::String(i + 1));
        bSoundEdit[i]->onClick = [this, i] { selectSound(i); };

        bSoundOn[i] = makeButton(juce::String(i + 1), true);
        buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, "soundOn" + juce::String(i), *bSoundOn[i]));
    }
    // Per-Sound layer controls (follow the focused Sound)
    kSoundLow = makeKnob("LOW KEY", "soundLow0", true);
    kSoundHigh = makeKnob("HIGH KEY", "soundHigh0", true);
    kSoundLevel = makeKnob("V.LEVEL", "soundGain0");

    // ---- Keyboard (replaces the old dead MIDI Edit / Dump buttons) ----
    kVelCurve = makeKnob("CURVE", "velCurve", true);
    kBendRange = makeKnob("BEND", "bendRange", true);

    // ---- OSC select ----
    // Focus (which oscillator the shared knobs edit) and enable are separate.
    bOsc1 = makeButton("OSC 1"); bOsc2 = makeButton("OSC 2");
    bOsc1->onClick = [this] { selectOsc(0); };
    bOsc2->onClick = [this] { selectOsc(1); };
    bOscOn1 = makeButton("ON", true); bOscOn2 = makeButton("ON", true);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "oscAOn", *bOscOn1));
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "oscBOn", *bOscOn2));

    // ---- Modulation-source buttons (one per destination, as on the panel) ----
    auto stepModSrc = [this](const juce::String& base)
        {
            const juce::String id = base + (currentOsc == 0 ? "A" : "B");
            if (auto* pr = audioProcessor.apvts.getParameter(id))
            {
                int cur = (int)(pr->convertFrom0to1(pr->getValue()) + 0.5f);
                pr->setValueNotifyingHost(pr->convertTo0to1((float)((cur + 1) % 25)));
            }
        };
    bPitchModSel = makeActionButton("LFO");
    bPitchModSel->onClick = [stepModSrc] { stepModSrc("pitchModSrc"); };
    bWaveModSel = makeActionButton("LFO");
    bWaveModSel->onClick = [stepModSrc] { stepModSrc("waveModSrc"); };
    bFiltModSel = makeActionButton("LFO");
    bFiltModSel->onClick = [stepModSrc] { stepModSrc("filtModSrc"); };

    // ---- Pitch (retargeted) ----
    kTune = makeKnob("TUNE", "tuneA");
    kFine = makeKnob("FINE", "fineA");
    kPitchMod = makeKnob("LFO", "pitchLFO");
    kPitchAmt = makeKnob("AMOUNT", "pitchModAmtA");

    // ---- Glide ----
    bMono = makeButton("MONO", true);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "mono", *bMono));
    kGlide = makeKnob("TIME", "glide");

    // ---- Envelope (retargeted per EG) ----
    bEgSelect = makeActionButton("SELECT");
    bEgSelect->onClick = [this] { selectEG((currentEG + 1) % 3); };
    bEgMode = makeActionButton("MODE");
    bEgMode->onClick = [this] {
        if (auto* pr = audioProcessor.apvts.getParameter("envMode")) {
            int n = (int)(pr->convertFrom0to1(pr->getValue()) + 0.5f);
            n = (n + 1) % 3;
            pr->setValueNotifyingHost(pr->convertTo0to1((float)n));
        }
        };
    kAttack = makeKnob("ATTACK", "attack");
    kDecay = makeKnob("DECAY", "decay");
    kSustain = makeKnob("SUSTAIN", "sustain");
    kRelease = makeKnob("RELEASE", "release");
    kEnvVel = makeKnob("VELOCITY", "ampVelAmt");

    // ---- Amplitude (retargeted) ----
    kLevel = makeKnob("LEVEL", "levelA");
    kPan = makeKnob("PAN", "panA");

    // ---- Tempo ----
    bTap = makeActionButton("TAP");
    bTap->onClick = [this] { tapTempo(); };
    kTempo = makeKnob("TEMPO", "tempo");

    // ---- Arpeggiator ----
    bArpOn = makeButton("OFF", true);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "arpOn", *bArpOn));
    // Arpeggiator Keyboard button (User's Guide p.12): when lit the keyboard
    // feeds the arpeggiator, when unlit it bypasses it and plays the Preset.
    bArpLatch = makeButton("KEYBD", true);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "arpKeyboard", *bArpLatch));
    kArpRange = makeKnob("RANGE", "arpRange", true);
    kArpMode = makeKnob("MODE", "arpMode", true);
    kArpRate = makeKnob("RATE", "arpRate", true);

    // ---- Effects ----
    // Insert effect: algorithm / variation / mix  (the Phizmo Effects section)
    bFxSelect = makeActionButton("INSERT");
    bFxSelect->onClick = [this] {
        // step through the insert algorithms
        if (auto* pr = audioProcessor.apvts.getParameter("fxAlgo"))
        {
            const int n = phz::PhizmoInsert::NumAlgos;
            int cur = (int)(pr->convertFrom0to1(pr->getValue()) + 0.5f);
            pr->setValueNotifyingHost(pr->convertTo0to1((float)((cur + 1) % n)));
        }
        };
    kFxAlgo = makeKnob("EFFECT", "fxAlgo", true);
    kFxVariation = makeKnob("DEPTH", "fxVariation");
    kFxMix = makeKnob("MIX", "fxMix");

    // Global reverb: variation + amount
    kRevVariation = makeKnob("REVERB", "revVariation", true);
    kRevAmount = makeKnob("AMOUNT", "revAmount");

    // Effect Bus: per-Sound send into the insert effect
    // Per-oscillator routing is now a plain two-way toggle: Insert or Dry.
    // Its LED means "this oscillator goes through the insert effect".
    bFxBusSelect = makeButton("WET");
    bFxBusSelect->onClick = [this] {
        const juce::String id = juce::String(currentOsc == 0 ? "fxBusA" : "fxBusB")
            + juce::String(audioProcessor.getEditSound());
        if (auto* pr = audioProcessor.apvts.getParameter(id))
            pr->setValueNotifyingHost(pr->getValue() > 0.5f ? 0.f : 1.f);
        };

    // ---- Wave (retargeted) ----
    kWaveMod = makeKnob("MOD", "waveModA");
    kWaveAmt = makeKnob("AMOUNT", "waveStartA");

    // ---- Filter ----
    bFilterType = makeActionButton("LP4");
    bFilterType->onClick = [this] {
        if (auto* pr = audioProcessor.apvts.getParameter("filterType")) {
            int n = (int)(pr->convertFrom0to1(pr->getValue()) + 0.5f);
            n = (n + 1) % 3;
            pr->setValueNotifyingHost(pr->convertTo0to1((float)n));
        }
        };
    kCutoff = makeKnob("CUTOFF", "filterFreq");
    kReso = makeKnob("RESON.", "filterQ");
    kFiltMod = makeKnob("LFO", "filterLFODep");
    kFiltAmt = makeKnob("AMOUNT", "filtModAmtA");
    kFiltKbd = makeKnob("KEYBD", "filterKeytrack");

    // ---- Modulation ----
    // The panel's Select button chooses which generator the section shows, so
    // the LFO and the noise generator each get their own rate + sync, exactly
    // as on the hardware where they are separate modulators.
    bModSelect = makeButton("LFO", true);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "modSelect", *bModSelect));
    kLfoShape = makeKnob("SHAPE", "lfoShape", true);
    kLfoSpeed = makeKnob("SPEED", "lfoSpeed");
    kLfoSync = makeKnob("SYNC", "lfoSync", true);
    kNoiseRate = makeKnob("SPEED", "noiseRate");
    kNoiseSync = makeKnob("SYNC", "noiseSync", true);
    kNoise = makeKnob("NOISE", "noiseLevel");
    kNoiseFreq = makeKnob("N.FREQ", "noiseFreq");
    kNoiseQ = makeKnob("N.RESO", "noiseQ");
    kNoiseType = makeKnob("N.TYPE", "noiseFilterType", true);
    kEvoLFORate = makeKnob("EVO LFO", "evoLFORate");
    kEvoLFODepth = makeKnob("LFO AMT", "evoLFODepth");

    // ---- Realtime F I Z M O ----
    // The five realtime knobs spell the plugin name: Ph I Z M O. The big
    // screened letters to the right of each knob are the labels, so the knob
    // captions below are left blank to avoid saying it twice.
    kF = makeKnob("", "phizF"); kI = makeKnob("", "phizI");  kZ = makeKnob("", "phizZ");
    kM = makeKnob("", "phizM");  kO = makeKnob("", "phizO");
    kODest = makeKnob("O DEST", "phizODest", true);

    // ---- Transwave / Evolution strip ----
    evoA = std::make_unique<EvoCurveEditor>(audioProcessor, 0, "OSC 1 EVOLUTION");
    evoB = std::make_unique<EvoCurveEditor>(audioProcessor, 1, "OSC 2 EVOLUTION");
    addAndMakeVisible(*evoA); addAndMakeVisible(*evoB);
    wtA = std::make_unique<WavetableDisplay>(audioProcessor, 0, "OSC1");
    wtB = std::make_unique<WavetableDisplay>(audioProcessor, 1, "OSC2");
    addAndMakeVisible(*wtA); addAndMakeVisible(*wtB);
    bLoadA = makeActionButton("LOAD"); bLoadB = makeActionButton("LOAD");
    bLoadA->onClick = [this] { loadFactoryWavetable(audioProcessor.getEditSound() * 2 + 0); };
    bLoadB->onClick = [this] { loadFactoryWavetable(audioProcessor.getEditSound() * 2 + 1); };
    // Scan phase-offset, one small knob per evolution pad (top-right of each).
    // Created after the pads so they sit on top of them. Osc A gained its own
    // "evoPhaseOff" param to mirror osc B's existing "evoBPhaseOff".
    kEvoOffA = makeKnob("OFS", "evoPhaseOff");
    kEvoOffB = makeKnob("OFS", "evoBPhaseOff");
    // SLW = curve fade time, one per pad, sitting just left of each OFS knob.
    kEvoSlewA = makeKnob("SLW", "evoSlew");
    kEvoSlewB = makeKnob("SLW", "evoSlewB");
    kEvoTime = makeKnob("EVO", "evoTime");
    kScanStyle = makeKnob("SCAN", "scanStyle", true);
    kOscMix = makeKnob("A/B MIX", "oscMix");
    kSpread = makeKnob("SPREAD", "spread");

    // --- Advanced / deep parameters (bottom "ADVANCED" row) ---
    // Filter envelope depth — the one genuine "missing knob" flagged earlier.
    kFiltEnvAmt = makeKnob("F.ENV", "filterEnvAmt");
    // Per-oscillator tuning (rebind to A/B in selectOsc).
    kDetune = makeKnob("DETUNE", "detune");
    // OCTAVE is now a cyclic per-osc button (Oct -2 … Oct +2) that lives under
    // the OSC PITCH TUNE knob — see bOctave, created below.
    kKeytrack = makeKnob("KEY TRK", "keytrack");
    // Transwave position envelope.
    kTwAmt = makeKnob("TW AMT", "twAmt");
    kTwAtt = makeKnob("TW ATT", "twAtt");
    kTwDec = makeKnob("TW DEC", "twDec");
    kTwToFilt = makeKnob("TW>FLT", "twToFilter");
    kTwVel = makeKnob("TW VEL", "twVelAmt");
    // Frame / texture.
    kFrameSnap = makeKnob("SNAP", "frameSnap");
    kGrit = makeKnob("GRIT", "grit");
    kJumpProb = makeKnob("JUMP", "jumpProb");
    kRingMod = makeKnob("RING", "ringMod");
    kStWidth = makeKnob("WIDTH", "stereoWidth");
    kUniDet = makeKnob("UNISON", "uniDetune");
    // VEL>FR is a plain on/off, so it is a toggle button under OSC ENV VELOCITY.
    kPosLFORate = makeKnob("POS LFO", "posLFORate");
    kPosLFODep = makeKnob("POS AMT", "posLFODepth");

    // OCTAVE button — cycles the focused oscillator's octave -2..+2, sitting
    // under OSC PITCH's TUNE knob as a coarse companion to the (finer) TUNE.
    bOctave = makeActionButton("Oct 0");
    bOctave->onClick = [this]
        {
            const juce::String id = currentOsc == 0 ? "octaveA" : "octaveB";
            if (auto* pr = audioProcessor.apvts.getParameter(id))
            {
                int cur = juce::roundToInt(pr->convertFrom0to1(pr->getValue()));  // -2..+2
                int nxt = ((cur + 2 + 1) % 5) - 2;                                // wrap
                pr->setValueNotifyingHost(pr->convertTo0to1((float)nxt));
            }
            updateOctaveButton();
        };

    // VEL>FR toggle — velocity-to-frame is a 0/1 switch, so it is a lamp button
    // under the OSC ENVELOPE VELOCITY knob.
    bVelFrame = makeButton("VEL>FR", true);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "velToFrame", *bVelFrame));

    selectSound(0);
    selectOsc(0);
    selectEG(2);
    refreshLeds();

    // Show the correct half of the Modulation section straight away, otherwise
    // the LFO and noise knobs overlap until the first timer tick.
    {
        const bool isLfo = bModSelect->getToggleState();
        kLfoSpeed->setVisible(isLfo);
        kLfoSync->setVisible(isLfo);
        kNoiseRate->setVisible(!isLfo);
        kNoiseSync->setVisible(!isLfo);
    }

    startTimerHz(20);
    setResizable(true, true);
    // The ADVANCED row is gone, so the panel is shorter: virtual 1300 x 714.
    setResizeLimits(900, 494, 2600, 1428);
    getConstrainer()->setFixedAspectRatio(1300.0 / 714.0);
    setSize(audioProcessor.getGuiWidth(), audioProcessor.getGuiHeight());
    editorReady = true;
}

PhizmoAudioProcessorEditor::~PhizmoAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}


//==============================================================================
// Focus one of the four Sounds in the Preset. The engine swaps its parameter
// snapshot into the edit buffer, so every knob on the panel now edits that
// Sound — exactly the hardware workflow.
void PhizmoAudioProcessorEditor::selectSound(int snd)
{
    snd = juce::jlimit(0, 3, snd);
    audioProcessor.setEditSound(snd);

    // layer controls follow the focused Sound
    kSoundLow->bind("soundLow" + juce::String(snd));
    kSoundHigh->bind("soundHigh" + juce::String(snd));
    kSoundLevel->bind("soundGain" + juce::String(snd));

    // the two wavetable displays show this Sound's oscillator slots
    if (wtA) wtA->setSlot(snd * 2 + 0);
    if (wtB) wtB->setSlot(snd * 2 + 1);

    selectOsc(currentOsc);      // re-bind the per-osc knobs to the new values
    refreshSoundButtons();
    refreshLeds();
}

// The EDIT row is exclusive (LED marks the Sound the panel is editing); the ON
// row is independent and reflects each Sound's soundOnN parameter.
void PhizmoAudioProcessorEditor::refreshSoundButtons()
{
    const int focus = audioProcessor.getEditSound();
    for (int i = 0; i < 4; ++i)
    {
        if (bSoundEdit[i]) bSoundEdit[i]->ledLit = (focus == i);
        if (bSoundOn[i])   bSoundOn[i]->ledLit = audioProcessor.isSoundEnabled(i);
    }
}

//==============================================================================
// "Select OSC then edit" — re-point the shared per-osc knobs.
void PhizmoAudioProcessorEditor::selectOsc(int osc)
{
    currentOsc = juce::jlimit(0, 1, osc);
    const juce::String s = currentOsc == 0 ? "A" : "B";
    kTune->bind("tune" + s);
    kFine->bind("fine" + s);
    kLevel->bind("level" + s);
    kPan->bind("pan" + s);
    kWaveMod->bind("waveMod" + s);
    kWaveAmt->bind("waveStart" + s);
    // Effect Bus is per oscillator (inS / r1 / r2 / r3 / dry)
    // modulation-matrix amounts follow the selected oscillator
    kPitchAmt->bind("pitchModAmt" + s);
    kFiltAmt->bind("filtModAmt" + s);
    // Evo time and scan style follow the focused oscillator: OSC 1 edits the
    // A-side parameters, OSC 2 the B-side. One knob each instead of A/B pairs.
    kEvoTime->bind(currentOsc == 0 ? "evoTime" : "evoTimeB");
    kScanStyle->bind(currentOsc == 0 ? "scanStyle" : "scanStyleB");
    updateOctaveButton();       // OCTAVE button follows the focused oscillator
    refreshLeds();
}

// Refresh the OCTAVE button's label from the focused oscillator's octave param.
void PhizmoAudioProcessorEditor::updateOctaveButton()
{
    if (!bOctave) return;
    const juce::String id = currentOsc == 0 ? "octaveA" : "octaveB";
    int v = 0;
    if (auto* pr = audioProcessor.apvts.getParameter(id))
        v = juce::roundToInt(pr->convertFrom0to1(pr->getValue()));
    bOctave->setButtonText("Oct " + (v > 0 ? "+" + juce::String(v) : juce::String(v)));
}

// "Select EG then edit" — re-point the ADSR knobs at Pitch / Filter / Amp EG.
void PhizmoAudioProcessorEditor::selectEG(int eg)
{
    currentEG = juce::jlimit(0, 2, eg);
    kRelease->getSlider().setEnabled(true);
    switch (currentEG)
    {
    case 0: // Pitch EG (attack, decay, amount; no sus/rel on the pitch env)
        kAttack->bind("pitchEnvAtt");  kAttack->setCaption("ATTACK");
        kDecay->bind("pitchEnvDec");  kDecay->setCaption("DECAY");
        kSustain->bind("pitchEnvAmt"); kSustain->setCaption("AMOUNT");
        kRelease->bind({});            kRelease->setCaption("--");
        kRelease->getSlider().setEnabled(false);
        break;
    case 1: // Filter EG
        kAttack->bind("filterAtt");  kAttack->setCaption("ATTACK");
        kDecay->bind("filterDec");  kDecay->setCaption("DECAY");
        kSustain->bind("filterSus"); kSustain->setCaption("SUSTAIN");
        kRelease->bind("filterRel"); kRelease->setCaption("RELEASE");
        break;
    default: // Amp EG
        kAttack->bind("attack");   kAttack->setCaption("ATTACK");
        kDecay->bind("decay");    kDecay->setCaption("DECAY");
        kSustain->bind("sustain"); kSustain->setCaption("SUSTAIN");
        kRelease->bind("release"); kRelease->setCaption("RELEASE");
        break;
    }
    refreshLeds();
}

void PhizmoAudioProcessorEditor::refreshLeds()
{
    if (bOsc1) bOsc1->ledLit = (currentOsc == 0);
    if (bOsc2) bOsc2->ledLit = (currentOsc == 1);
    repaint();
}

//==============================================================================
void PhizmoAudioProcessorEditor::loadFactoryWavetable(int slot)
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Phizmo").getChildFile("Wavetables");

    juce::PopupMenu menu;
    menu.setLookAndFeel(&laf);   // Phizmo cyan-blue background for the name list
    juce::Array<juce::File> wavs;
    if (dir.isDirectory())
        wavs = dir.findChildFiles(juce::File::findFiles, false, "*.wav");

    if (!wavs.isEmpty())
    {
        for (int i = 0; i < wavs.size(); ++i)
            menu.addItem(i + 1, wavs[i].getFileNameWithoutExtension());
        menu.addSeparator();
        menu.addItem(9000, "Load from file...");
        addGenerateMenuItems(menu, slot);
        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this, wavs, slot](int r)
            {
                if (r >= 1 && r <= wavs.size())
                    audioProcessor.loadWavetable(wavs[r - 1], 2048, slot);
                else if (r == 9000)
                    loadFactoryWavetableFromChooser(slot);
                else
                    handleGenerateMenuResult(r, slot);
            });
        return;
    }

    // No factory folder yet: still offer generation, plus the chooser.
    menu.addItem(9000, "Load from file...");
    addGenerateMenuItems(menu, slot);
    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, slot](int r)
        {
            if (r == 9000) loadFactoryWavetableFromChooser(slot);
            else           handleGenerateMenuResult(r, slot);
        });
}

//==============================================================================
// 2.0 — wave generation entries, shared by the slot popup and the wavetable
// display's right-click menu.
//==============================================================================
void PhizmoAudioProcessorEditor::addGenerateMenuItems(juce::PopupMenu& menu, int slot)
{
    const int frames = (int)(audioProcessor.apvts.getRawParameterValue("genFrames")
        ? audioProcessor.apvts.getRawParameterValue("genFrames")->load() : 32.f);
    const bool slice = audioProcessor.apvts.getRawParameterValue("genUnpitched")
        && audioProcessor.apvts.getRawParameterValue("genUnpitched")->load() > 0.5f;

    menu.addSeparator();
    menu.addSectionHeader("Generate (" + juce::String(frames) + " frames)");
    menu.addItem(9100, "Random Transwave");
    menu.addItem(9101, "Transwavify audio file...");

    const int origin = audioProcessor.getSlotOrigin(slot);
    menu.addItem(9102, origin == 1 ? "Re-roll (new seed)"
        : origin == 2 ? "Re-analyse (flip pitched/slice)"
        : "Re-roll", origin != 0);
    menu.addItem(9103, "Export wave as .wav...", audioProcessor.isWavetableLoaded(slot));

    juce::PopupMenu framesMenu;
    for (int i = 0; i < 6; ++i)
    {
        const int f = 8 << i;                       // 8,16,32,64,128,256
        framesMenu.addItem(9200 + i, juce::String(f), true, f == frames);
    }
    menu.addSubMenu("Frames", framesMenu);
    menu.addItem(9110, "Slice mode (skip pitch detection)", true, slice);

    // Archetype picker for the random generator.
    if (auto* ap = dynamic_cast<juce::AudioParameterChoice*>(
        audioProcessor.apvts.getParameter("genArchetype")))
    {
        juce::PopupMenu archMenu;
        const int cur = ap->getIndex();
        for (int i = 0; i < ap->choices.size(); ++i)
            archMenu.addItem(9300 + i, ap->choices[i], true, i == cur);
        menu.addSubMenu("Archetype", archMenu);
    }

    const juce::String status = audioProcessor.getSlotStatus(slot);
    if (status.isNotEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader(status);
    }
}

bool PhizmoAudioProcessorEditor::handleGenerateMenuResult(int result, int slot)
{
    auto* framesP = audioProcessor.apvts.getParameter("genFrames");
    auto* sliceP = audioProcessor.apvts.getParameter("genUnpitched");

    const int frames = (int)(audioProcessor.apvts.getRawParameterValue("genFrames")
        ? audioProcessor.apvts.getRawParameterValue("genFrames")->load() : 32.f);
    const bool slice = audioProcessor.apvts.getRawParameterValue("genUnpitched")
        && audioProcessor.apvts.getRawParameterValue("genUnpitched")->load() > 0.5f;
    const int cycle = audioProcessor.getCycleSizeParam(slot % 2);

    juce::String status;

    if (result >= 9300 && result <= 9320)
    {
        if (auto* ap = dynamic_cast<juce::AudioParameterChoice*>(
            audioProcessor.apvts.getParameter("genArchetype")))
            ap->setValueNotifyingHost(ap->convertTo0to1((float)(result - 9300)));
        return true;
    }
    if (result >= 9200 && result <= 9205)
    {
        if (framesP) framesP->setValueNotifyingHost(framesP->convertTo0to1((float)(8 << (result - 9200))));
        return true;
    }
    if (result == 9110)
    {
        if (sliceP) sliceP->setValueNotifyingHost(slice ? 0.f : 1.f);
        return true;
    }
    if (result == 9100)
    {
        audioProcessor.generateRandomWavetable(slot, 0, frames, cycle, status);
        showGenerateStatus(status);
        return true;
    }
    if (result == 9102)
    {
        audioProcessor.rerollSlot(slot, status);
        showGenerateStatus(status);
        return true;
    }
    if (result == 9103)
    {
        exportSlotAsWavClicked(slot);
        return true;
    }
    if (result == 9101)
    {
        if (fileChooser != nullptr) return true;
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose audio to Transwavify",
            PhizmoAudioProcessor::getSamplesDirectory(),
            "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
            [this, slot, frames, cycle, slice](const juce::FileChooser& ch)
            {
                auto f = ch.getResult();
                if (f.existsAsFile())
                {
                    juce::String st;
                    audioProcessor.transwavifyFile(slot, f, frames, cycle, slice, st);
                    showGenerateStatus(st);
                }
                fileChooser.reset();
            });
        return true;
    }
    return false;
}

void PhizmoAudioProcessorEditor::showGenerateStatus(const juce::String& text)
{
    if (text.isEmpty()) return;
    lastGenerateStatus = text;
    repaint();
}

void PhizmoAudioProcessorEditor::exportSlotAsWavClicked(int slot)
{
    // Replacing a chooser that is still open destroys it mid-flight (same
    // guard used everywhere else a FileChooser is launched from this editor).
    if (fileChooser != nullptr) return;

    auto startDir = PhizmoAudioProcessor::getSamplesDirectory()
        .getChildFile("Wavetable" + juce::String(slot) + ".wav");
    fileChooser = std::make_unique<juce::FileChooser>("Export slot as .wav...", startDir, "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this, slot](const juce::FileChooser& ch)
        {
            auto dest = ch.getResult();
            if (dest != juce::File{})
            {
                if (dest.getFileExtension().toLowerCase() != ".wav") dest = dest.withFileExtension(".wav");
                juce::String status;
                audioProcessor.exportSlotAsWav(slot, dest, status);
                showGenerateStatus(status);
            }
            fileChooser.reset();
        });
}

void PhizmoAudioProcessorEditor::loadFactoryWavetableFromChooser(int slot)
{
    // Documents/Phizmo Samples, created if absent
    auto startDir = PhizmoAudioProcessor::getSamplesDirectory();

    // Replacing a chooser that is still open destroys it mid-flight and the
    // dialog never appears, which is why Load sometimes did nothing. Ignore
    // the click while one is already up.
    if (fileChooser != nullptr) return;
    fileChooser = std::make_unique<juce::FileChooser>("Select wavetable .wav", startDir, "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot](const juce::FileChooser& ch)
        {
            auto f = ch.getResult();
            if (f.existsAsFile())
                audioProcessor.loadWavetable(f, 2048, slot);   // Phizmo tables are 2048-sample cycles
            fileChooser.reset();
        });
}

//==============================================================================
void PhizmoAudioProcessorEditor::savePresetClicked()
{
    if (fileChooser != nullptr) return;
    fileChooser = std::make_unique<juce::FileChooser>("Save preset as...",
        audioProcessor.getEffectivePresetsDirectory().getChildFile("New Preset.phizmo"), "*.phizmo");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& ch)
        {
            auto dest = ch.getResult(); if (dest == juce::File{}) return;
            if (dest.getFileExtension().toLowerCase() != ".phizmo") dest = dest.withFileExtension(".phizmo");
            if (audioProcessor.savePreset(dest))
            {
                audioProcessor.setCurrentPresetName(dest.getFileNameWithoutExtension());
                lcd.setText(dest.getFileNameWithoutExtension().substring(0, 4).toUpperCase());
            }
            else lcd.setText("ERR");
            fileChooser.reset();
        });
}

void PhizmoAudioProcessorEditor::loadPresetClicked()
{
    if (fileChooser != nullptr) return;
    fileChooser = std::make_unique<juce::FileChooser>("Open preset...",
        audioProcessor.getEffectivePresetsDirectory(), "*.phizmo");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& ch)
        {
            auto src = ch.getResult();
            if (src.existsAsFile()) loadPresetFromFile(src);
            fileChooser.reset();
        });
}

void PhizmoAudioProcessorEditor::loadPresetFromFile(const juce::File& src)
{
    if (audioProcessor.loadPreset(src))
    {
        audioProcessor.setCurrentPresetName(src.getFileNameWithoutExtension());
        lcd.setText(src.getFileNameWithoutExtension().substring(0, 4).toUpperCase());
        // Re-bind the shared knobs so they show the newly loaded values
        selectOsc(currentOsc);
        selectEG(currentEG);
    }
}

//==============================================================================
void PhizmoAudioProcessorEditor::tapTempo()
{
    double now = juce::Time::getMillisecondCounterHiRes();
    if (lastTapTime > 0.0)
    {
        double delta = now - lastTapTime;
        if (delta > 200.0 && delta < 3000.0)          // 20..300 BPM window
        {
            float bpm = (float)(60000.0 / delta);
            if (auto* pr = audioProcessor.apvts.getParameter("tempo"))
                pr->setValueNotifyingHost(pr->convertTo0to1(juce::jlimit(25.f, 320.f, bpm)));
        }
    }
    lastTapTime = now;
}

//==============================================================================
void PhizmoAudioProcessorEditor::timerCallback()
{
    // LED states for the Sound rows / arp
    refreshSoundButtons();
    updateOctaveButton();   // keep the OCTAVE label in sync with automation/presets
    if (bFxSelect)
    {
        if (auto* pv = audioProcessor.apvts.getRawParameterValue("fxAlgo"))
            bFxSelect->setButtonText(phz::PhizmoInsert::algoShort((int)pv->load()));
    }
    if (bFxBusSelect)
    {
        const juce::String id = juce::String(currentOsc == 0 ? "fxBusA" : "fxBusB")
            + juce::String(audioProcessor.getEditSound());
        int b = 0;
        if (auto* pv = audioProcessor.apvts.getRawParameterValue(id)) b = (int)pv->load();
        const bool toInsert = (b == 0);
        bFxBusSelect->setButtonText(toInsert ? "WET" : "DRY");
        bFxBusSelect->ledLit = toInsert;
    }
    {
        static const char* ms[25] = { "OFF","FULL","LFO","noiS","LPF","En1","En2","En3",
            "tch","tPrS","notE","BrD","PrS","Ptch","CtL1","CtPr","Foot","SuSt","SOSt",
            "SYS1","SYS2","SYS3","SYS4","PSEL","GLFO" };
        const juce::String o(currentOsc == 0 ? "A" : "B");
        auto lbl = [&](PhButton* b, const juce::String& base)
            {
                if (b == nullptr) return;
                int i = 0;
                if (auto* pv = audioProcessor.apvts.getRawParameterValue(base + o)) i = (int)pv->load();
                b->setButtonText(ms[juce::jlimit(0, 24, i)]);
                b->ledLit = (i != 0);
            };
        lbl(bPitchModSel.get(), "pitchModSrc");
        lbl(bWaveModSel.get(), "waveModSrc");
        lbl(bFiltModSel.get(), "filtModSrc");
    }
    if (bArpOn)
    {
        const bool on = bArpOn->getToggleState();
        bArpOn->ledLit = on;
        bArpOn->setButtonText(on ? "ON" : "OFF");
    }
    if (bMono)  bMono->ledLit = bMono->getToggleState();
    if (bModSelect)
    {
        const bool isLfo = bModSelect->getToggleState();
        bModSelect->ledLit = isLfo;
        bModSelect->setButtonText(isLfo ? "LFO" : "NOISE");
        // The section shows one generator at a time, exactly as the hardware's
        // Modulation Select button does.
        if (kLfoSpeed)  kLfoSpeed->setVisible(isLfo);
        if (kLfoSync)   kLfoSync->setVisible(isLfo);
        if (kNoiseRate) kNoiseRate->setVisible(!isLfo);
        if (kNoiseSync) kNoiseSync->setVisible(!isLfo);
    }

    // Filter type button label follows the parameter
    if (bFilterType)
    {
        if (auto* pv = audioProcessor.apvts.getRawParameterValue("filterType"))
        {
            static const char* names[3] = { "LP4", "BP", "HP" };
            bFilterType->setButtonText(names[juce::jlimit(0, 2, (int)pv->load())]);
        }
    }

    // Envelope select button shows which EG is being edited
    if (bEgSelect)
    {
        static const char* egn[3] = { "PITCH", "FILTER", "AMP" };
        bEgSelect->setButtonText(egn[juce::jlimit(0, 2, currentEG)]);
    }
    if (bEgMode)
    {
        if (auto* pv = audioProcessor.apvts.getRawParameterValue("envMode"))
        {
            static const char* mn[3] = { "nor", "fin", "rPt" };
            bEgMode->setButtonText(mn[juce::jlimit(0, 2, (int)pv->load())]);
        }
    }

    // LCD: show current preset name (4 chars), like the hardware window
    {
        juce::String n = audioProcessor.getCurrentPresetName();
        if (n.isEmpty()) n = "PHZM";
        lcd.setText(n.substring(0, 4).toUpperCase());
    }

    // Persist window width so the DAW restores it
    if (editorReady && getWidth() > 0 && getWidth() != audioProcessor.getGuiWidth())
        audioProcessor.setGuiWidth(getWidth());

    repaint();
}

//==============================================================================
bool PhizmoAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".phizmo")) return true;
    return false;
}

void PhizmoAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".phizmo")) { loadPresetFromFile(juce::File(f)); break; }
}

//==============================================================================
// paint() — the the hardware's magenta/purple airbrushed panel with speckle spray.
//==============================================================================
void PhizmoAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float S = (float)getWidth() / 1300.f;      // uniform scale factor

    // Base gradient: deep violet (left/bottom) into hot magenta (right/top)
    juce::ColourGradient bg(juce::Colour(0xff7a1470), b.getTopRight(),
        juce::Colour(0xff2d0b46), b.getBottomLeft(), false);
    bg.addColour(0.45, juce::Colour(0xff5a1060));
    g.setGradientFill(bg);
    g.fillRect(b);

    // Airbrush speckle spray (deterministic so it doesn't crawl between frames)
    juce::Random rnd(0x5EED12);
    for (int i = 0; i < 2600; ++i)
    {
        float x = rnd.nextFloat() * b.getWidth();
        float y = rnd.nextFloat() * b.getHeight();
        // spray density is highest at the left edge, fading right (like the unit)
        float density = 1.f - juce::jlimit(0.f, 1.f, x / (b.getWidth() * 0.55f));
        if (rnd.nextFloat() > density * 0.9f) continue;
        float sz = (0.6f + rnd.nextFloat() * 1.6f) * S;
        juce::Colour c = rnd.nextBool() ? juce::Colour(0xff8fd8ff) : juce::Colour(0xffffc0f0);
        g.setColour(c.withAlpha(0.10f + rnd.nextFloat() * 0.35f));
        g.fillEllipse(x, y, sz, sz);
    }

    // Subtle vignette
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.drawRect(b, 3.f * S);


    // VOICE row labels: EDIT selects which Voice the panel edits, ON toggles it
    if (secSound.getWidth() > 0)
    {
        auto vb = secSound.getBounds().toFloat();
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(PhizmoLookAndFeel::panelFont());
        const float rowH = 20.f * S;
        g.drawText("EDIT", juce::Rectangle<float>(vb.getX() + 5.f * S, vb.getY() + 32.f * S, 32.f * S, rowH),
            juce::Justification::centredLeft);
        g.drawText("ON", juce::Rectangle<float>(vb.getX() + 5.f * S, vb.getY() + 58.f * S, 32.f * S, rowH),
            juce::Justification::centredLeft);
    }

    // Product wordmark, bottom-right of the evolution area. The former text
    // title is kept (commented out) — the logo image is drawn in its place.
    {
        // -- former text title (replaced by Phizmo_Logo.png; kept for reference)
        // g.setFont(PhizmoLookAndFeel::font(26.f, true));
        // juce::ColourGradient tg(juce::Colour(0xffff6ec7), 690.f * S, 0.f,
        //                         juce::Colour(0xff8fd8ff), 1280.f * S, 0.f, false);
        // g.setGradientFill(tg);
        // g.drawText("PHIZMO",
        //            juce::Rectangle<float>(690.f * S, 638.f * S, 590.f * S, 34.f * S),
        //            juce::Justification::centredRight);
        if (logoImg.isValid())
        {
            const float lw = 144.f, lh = lw * 186.f / 1257.f;    // aspect-correct downscale
            const float lx = 1284.f - lw;                        // right-aligned, clear of POS AMT
            const float ly = 636.f + (40.f - lh) * 0.5f;         // centred in the old title band
            g.drawImage(logoImg, juce::Rectangle<float>(lx * S, ly * S, lw * S, lh * S),
                juce::RectanglePlacement::stretchToFit, false);
        }
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(PhizmoLookAndFeel::font(12.f, false));
        g.drawText("transwave synthesizer",
            juce::Rectangle<float>(690.f * S, 676.f * S, 590.f * S, 18.f * S),
            juce::Justification::centredRight);
    }

    // Black strip behind the Ph-I-Z-M-O realtime row
    if (kF)
    {
        auto strip = juce::Rectangle<float>(secRT.getBounds().toFloat()).expanded(4.f * S, 2.f * S);
        g.setColour(juce::Colour(0xff08060c));
        g.fillRoundedRectangle(strip, 6.f * S);
        g.setColour(juce::Colour(0xff3a2a48));
        g.drawRoundedRectangle(strip.reduced(0.5f), 6.f * S, 1.f);

        // Big screened Ph I Z M O letters beside each knob
        static const char* letters[5] = { "Ph","I","Z","M","O" };
        PhKnob* ks[5] = { kF.get(), kI.get(), kZ.get(), kM.get(), kO.get() };
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(PhizmoLookAndFeel::font(26.f));
        for (int i = 0; i < 5; ++i)
            if (ks[i] != nullptr)
            {
                auto kb = ks[i]->getBounds().toFloat();
                // "Ph" needs a wider box than the single letters
                const float lw = (juce::String(letters[i]).length() > 1 ? 42.f : 26.f) * S;
                g.drawText(letters[i],
                    juce::Rectangle<float>(kb.getRight() + 2.f * S, kb.getY() + kb.getHeight() * 0.18f,
                        lw, 30.f * S),
                    juce::Justification::centredLeft);
            }
    }

    // Screened captions for the promoted sub-groups beside the sound-layer
    // knobs. X ranges track the setBounds positions in resized().
    {
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.setFont(PhizmoLookAndFeel::font(10.f, true));
        auto lbl = [&](const char* t, float x0, float w)
            {
                g.drawText(t, juce::Rectangle<float>(x0 * S, 627.f * S, w * S, 10.f * S),
                    juce::Justification::centred);
            };
        lbl("STEREO", 596.f, 170.f);   // SPREAD + WIDTH + UNISON
        lbl("DIGITAL", 779.f, 227.f);   // SNAP + GRIT + JUMP + RING
        lbl("POSITION", 1019.f, 113.f);  // POS LFO + POS AMT
    }

    // Fine grey divider lines in the last row, matching the section dividers in
    // the rows above — placed in the gaps between groups without moving knobs.
    {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        const float y0 = 638.f * S, y1s = 702.f * S;
        for (float lx : { 398.f, 591.f, 772.f, 1012.f })
            g.drawLine(lx * S, y0, lx * S, y1s, 1.f);
    }
}

//==============================================================================
// resized() — laid out to mirror the hardware panel photo. Everything is
// designed in a virtual 1300 x 720 space and scaled uniformly.
//==============================================================================
void PhizmoAudioProcessorEditor::resized()
{
    const float S = (float)getWidth() / 1300.f;
    PhizmoLookAndFeel::guiScale = S;      // every font scales from this

    auto R = [S](float x, float y, float w, float h)
        {
            return juce::Rectangle<int>(juce::roundToInt(x * S), juce::roundToInt(y * S),
                juce::roundToInt(w * S), juce::roundToInt(h * S));
        };

    const float KW = 56.f, KH = 68.f;     // knob cell (knob + caption)
    const float BH = 20.f;                // button cell height — taller, for 12px text
    const float MBH = 18.f;               // modulation-source button height
    const float SECH = 108.f;             // section height, rows 1 and 2

    // ================= ROW 1 : y 24 .. 132 =================
    const float y1 = 24.f, ky1 = y1 + 18.f, mb1 = ky1 + KH + 2.f;
    float x = 10.f;

    secVolume.setBounds(R(x, y1, 66, SECH));
    kVolume->setBounds(R(x + 5, ky1, KW, KH));
    x += 70.f;

    // PRESET — display plus the only two actions that exist: load and save.
    secPreset.setBounds(R(x, y1, 100, SECH));
    lcd.setBounds(R(x + 6, ky1, 88, 34));
    bLoad->setBounds(R(x + 6, ky1 + 40, 88, BH));
    bSave->setBounds(R(x + 6, ky1 + 64, 88, BH));
    x += 104.f;

    // EDIT / ACTIVATE VOICE — the EDIT row picks which Voice the panel edits;
    // the ON row switches each Voice in and out of the Preset. Width trimmed to
    // sit right up against the buttons with no dead space on the right.
    {
        const float labelW = 36.f, bw = 26.f, gap = 4.f;
        const float rowW = 4.f * bw + 3.f * gap;
        const float secW = labelW + rowW + 12.f;
        secSound.setBounds(R(x, y1, secW, SECH));
        const float sx = x + labelW + 4.f;
        for (int i = 0; i < 4; ++i)
        {
            bSoundEdit[i]->setBounds(R(sx + (float)i * (bw + gap), ky1 + 14, bw, BH));
            bSoundOn[i]->setBounds(R(sx + (float)i * (bw + gap), ky1 + 40, bw, BH));
        }
        x += secW + 4.f;
    }

    // EDIT / ON OSC — top row: OSC 1 / OSC 2 (which oscillator the knobs edit),
    // bottom row: the ON toggles for each. Matches the VOICE section's layout.
    {
        const float secW = 138.f, bw = 60.f;
        secOsc.setBounds(R(x, y1, secW, SECH));
        bOsc1->setBounds(R(x + 6, ky1 + 14, bw, BH));
        bOsc2->setBounds(R(x + 70, ky1 + 14, bw, BH));
        bOscOn1->setBounds(R(x + 6, ky1 + 40, bw, BH));
        bOscOn2->setBounds(R(x + 70, ky1 + 40, bw, BH));
        x += secW + 4.f;
    }

    // OSC PITCH: TUNE FINE LFO AMOUNT. The LFO source button spans the LFO and
    // AMOUNT knobs it applies to.
    secPitch.setBounds(R(x, y1, 4 * KW + 12, SECH));
    kTune->setBounds(R(x + 6 + 0 * KW, ky1, KW, KH));
    kFine->setBounds(R(x + 6 + 1 * KW, ky1, KW, KH));
    kPitchMod->setBounds(R(x + 6 + 2 * KW, ky1, KW, KH));
    kPitchAmt->setBounds(R(x + 6 + 3 * KW, ky1, KW, KH));
    bPitchModSel->setBounds(R(x + 6 + 2 * KW, mb1, 2 * KW, MBH));
    bOctave->setBounds(R(x + 6 + 0 * KW, mb1, KW, MBH));   // coarse octave under TUNE
    x += 4 * KW + 16.f;

    // VOICE GLIDE — TIME knob at the standard knob height; MONO button aligned
    // with the OSC PITCH LFO source button below it.
    secGlide.setBounds(R(x, y1, KW + 12, SECH));
    kGlide->setBounds(R(x + 6, ky1, KW, KH));
    bMono->setBounds(R(x + 6, mb1, KW, MBH));
    x += KW + 16.f;

    // ENVELOPE: SELECT / MODE buttons, then A D S R VELOCITY
    secEnv.setBounds(R(x, y1, 5 * KW + 74, SECH));
    bEgSelect->setBounds(R(x + 4, ky1 + 12, 66, BH));
    bEgMode->setBounds(R(x + 4, ky1 + 38, 66, BH));
    {
        const float ex = x + 74.f;
        kAttack->setBounds(R(ex + 0 * KW, ky1, KW, KH));
        kDecay->setBounds(R(ex + 1 * KW, ky1, KW, KH));
        kSustain->setBounds(R(ex + 2 * KW, ky1, KW, KH));
        kRelease->setBounds(R(ex + 3 * KW, ky1, KW, KH));
        kEnvVel->setBounds(R(ex + 4 * KW, ky1, KW, KH));
        bVelFrame->setBounds(R(ex + 4 * KW, mb1, KW, MBH));   // velocity→frame toggle under VELOCITY
    }
    x += 5 * KW + 78.f;

    secAmp.setBounds(R(x, y1, 2 * KW + 12, SECH));
    kLevel->setBounds(R(x + 6 + 0 * KW, ky1, KW, KH));
    kPan->setBounds(R(x + 6 + 1 * KW, ky1, KW, KH));

    // ================= ROW 2 : y 140 .. 248 =================
    const float y2 = 140.f, ky2 = y2 + 18.f, mb2 = ky2 + KH + 2.f;
    x = 10.f;

    // GLOBAL EFFECTS — the algorithm-select button sits below the EFFECT knob
    // so it no longer collides with the section title.
    secFx.setBounds(R(x, y2, 3 * KW + 12, SECH));
    kFxAlgo->setBounds(R(x + 6 + 0 * KW, ky2, KW, KH));
    kFxVariation->setBounds(R(x + 6 + 1 * KW, ky2, KW, KH));
    kFxMix->setBounds(R(x + 6 + 2 * KW, ky2, KW, KH));
    bFxSelect->setBounds(R(x + 6, mb2, KW, MBH));
    x += 3 * KW + 13.f;   // tightened row-2 gaps to make room for KEY TRK

    // GLOBAL REVERB — VARIATION + AMOUNT, and the per-oscillator WET/DRY route
    // toggle sits below the AMOUNT knob (WET = this oscillator feeds the
    // insert effect that the reverb send picks up).
    secRev.setBounds(R(x, y2, 2 * KW + 12, SECH));
    kRevVariation->setBounds(R(x + 6 + 0 * KW, ky2, KW, KH));
    kRevAmount->setBounds(R(x + 6 + 1 * KW, ky2, KW, KH));
    bFxBusSelect->setBounds(R(x + 6 + 1 * KW, mb2, KW, MBH));
    x += 2 * KW + 13.f;

    // OSC WAVE — MOD + AMOUNT, LFO source button spanning both.
    secWave.setBounds(R(x, y2, 2 * KW + 12, SECH));
    kWaveMod->setBounds(R(x + 6 + 0 * KW, ky2, KW, KH));
    kWaveAmt->setBounds(R(x + 6 + 1 * KW, ky2, KW, KH));
    bWaveModSel->setBounds(R(x + 6, mb2, 2 * KW, MBH));
    x += 2 * KW + 13.f;

    // VOICE FILTER — one filter per voice (shared by both oscillators); also
    // carries F.ENV (filter-envelope depth), lifted out of the ADVANCED row
    // into the spare column on the right of this section.
    secFilter.setBounds(R(x, y2, 6 * KW + 78, SECH));
    bFilterType->setBounds(R(x + 4, ky2 + 12, 66, BH));
    bFiltModSel->setBounds(R(x + 4, ky2 + 38, 66, BH));   // LFO under the type button
    {
        const float fx2 = x + 74.f;
        kCutoff->setBounds(R(fx2 + 0 * KW, ky2, KW, KH));
        kReso->setBounds(R(fx2 + 1 * KW, ky2, KW, KH));
        kFiltMod->setBounds(R(fx2 + 2 * KW, ky2, KW, KH));
        kFiltAmt->setBounds(R(fx2 + 3 * KW, ky2, KW, KH));
        kFiltKbd->setBounds(R(fx2 + 4 * KW, ky2, KW, KH));
        kFiltEnvAmt->setBounds(R(fx2 + 5 * KW, ky2, KW, KH));
    }
    x += 6 * KW + 79.f;

    // MODULATION — the Select button swaps the section between the LFO and the
    // noise generator; it sits below the three knobs.
    secMod.setBounds(R(x, y2, 3 * KW + 12, SECH));
    {
        const float mx = x + 6.f;
        kLfoShape->setBounds(R(mx + 0 * KW, ky2, KW, KH));
        kLfoSpeed->setBounds(R(mx + 1 * KW, ky2, KW, KH));
        kLfoSync->setBounds(R(mx + 2 * KW, ky2, KW, KH));
        kNoiseRate->setBounds(R(mx + 1 * KW, ky2, KW, KH));
        kNoiseSync->setBounds(R(mx + 2 * KW, ky2, KW, KH));
        bModSelect->setBounds(R(mx + 1 * KW, mb2, KW, MBH));
    }
    x += 3 * KW + 13.f;

    // TEMPO — TAP below the knob so the section can be a single column wide.
    secTempo.setBounds(R(x, y2, KW + 12, SECH));
    kTempo->setBounds(R(x + 6, ky2, KW, KH));
    bTap->setBounds(R(x + 6, mb2, KW, MBH));
    x += KW + 13.f;

    // GLOBAL KEYBOARD — CURVE + BEND, and now KEY TRK (frame keytrack), pulled
    // up here from the old ADVANCED row. The row-2 separator gaps above were
    // tightened a little to win back the width for this third column.
    secKbd.setBounds(R(x, y2, 3 * KW + 12, SECH));
    kVelCurve->setBounds(R(x + 6 + 0 * KW, ky2, KW, KH));
    kBendRange->setBounds(R(x + 6 + 1 * KW, ky2, KW, KH));
    kKeytrack->setBounds(R(x + 6 + 2 * KW, ky2, KW, KH));

    // ================= ROW 3 : y 256 .. 352 =================
    const float y3 = 256.f, ky3 = y3 + 18.f;
    x = 10.f;

    // GLOBAL ARPEGGIATOR leads row 3.
    secArp.setBounds(R(x, y3, 3 * KW + 78, 96));
    bArpOn->setBounds(R(x + 4, ky3 + 8, 66, BH));
    bArpLatch->setBounds(R(x + 4, ky3 + 34, 66, BH));
    {
        const float ax = x + 74.f;
        kArpRange->setBounds(R(ax + 0 * KW, ky3, KW, KH));
        kArpMode->setBounds(R(ax + 1 * KW, ky3, KW, KH));
        kArpRate->setBounds(R(ax + 2 * KW, ky3, KW, KH));
    }
    x += 3 * KW + 82.f;

    // VOICE NOISE moves up beside the arpeggiator: NOISE + its own filter.
    secNoise.setBounds(R(x, y3, 4 * KW + 12, 96));
    kNoise->setBounds(R(x + 6 + 0 * KW, ky3, KW, KH));
    kNoiseFreq->setBounds(R(x + 6 + 1 * KW, ky3, KW, KH));
    kNoiseQ->setBounds(R(x + 6 + 2 * KW, ky3, KW, KH));
    kNoiseType->setBounds(R(x + 6 + 3 * KW, ky3, KW, KH));
    x += 4 * KW + 16.f;

    // TRANSWAVE ENV — the transwave position-envelope knobs (amount / attack /
    // decay / to-filter / velocity), promoted from the old ADVANCED row into the
    // gap that used to be empty black panel between VOICE NOISE and the realtime
    // strip. Packed a touch tighter than the standard cell so the FIZMO letters
    // in the strip keep their breathing room.
    {
        const float twKW = 50.f;
        const float twW = 5.f * twKW + 10.f;          // 260
        secTwEnv.setBounds(R(x, y3, twW, 96));
        const float tx = x + 6.f;
        kTwAmt->setBounds(R(tx + 0 * twKW, ky3, twKW, KH));
        kTwAtt->setBounds(R(tx + 1 * twKW, ky3, twKW, KH));
        kTwDec->setBounds(R(tx + 2 * twKW, ky3, twKW, KH));
        kTwToFilt->setBounds(R(tx + 3 * twKW, ky3, twKW, KH));
        kTwVel->setBounds(R(tx + 4 * twKW, ky3, twKW, KH));
        x += twW + 4.f;
    }

    // Ph-I-Z-M-O realtime strip — now starts after the TRANSWAVE ENV group, so
    // the black panel is shorter than before but still wide enough for the big
    // screened letters. Cell spacing fills the remaining width.
    {
        const float rtX = x, rtW = 1290.f - rtX;
        secRT.setBounds(R(rtX, y3, rtW, 96));
        // Spread the six controls evenly across the whole strip. The five FIZMO
        // knobs (56 wide, each with a big letter to its right) and O DEST (60)
        // land with equal centre spacing, O DEST sitting near the right edge so
        // no dead black is left over and "Ph" clears the I knob.
        const float pad = 18.f;
        const float firstC = rtX + pad + 28.f;           // centre of the Ph knob
        const float lastC = rtX + rtW - pad - 30.f;     // centre of O DEST
        const float cstep = (lastC - firstC) / 5.f;
        PhKnob* fz[5] = { kF.get(), kI.get(), kZ.get(), kM.get(), kO.get() };
        for (int i = 0; i < 5; ++i)
            fz[i]->setBounds(R(firstC + (float)i * cstep - 28.f, ky3, 56, KH));
        kODest->setBounds(R(lastC - 30.f, ky3, 60, KH));
    }

    // ================= EVOLUTION : y 360 .. 708 =================
    const float y4 = 360.f;
    secEvo.setBounds(R(10, y4, 1280, 348));

    const float half = 628.f;
    for (int sIdx = 0; sIdx < 2; ++sIdx)
    {
        const float hx = 18.f + (float)sIdx * (half + 12.f);
        auto* wt = (sIdx == 0) ? wtA.get() : wtB.get();
        auto* evo = (sIdx == 0) ? evoA.get() : evoB.get();
        auto* ld = (sIdx == 0) ? bLoadA.get() : bLoadB.get();
        auto* ofs = (sIdx == 0) ? kEvoOffA.get() : kEvoOffB.get();
        auto* slw = (sIdx == 0) ? kEvoSlewA.get() : kEvoSlewB.get();
        // Wavetable viewer now spans the same width as the evolution pad below
        // it, with the LOAD button tucked into its bottom-right corner.
        wt->setBounds(R(hx, y4 + 20, half - 10.f, 82));
        ld->setBounds(R(hx + half - 10.f - 72.f, y4 + 20 + 82.f - BH - 6.f, 66, BH));
        evo->setBounds(R(hx, y4 + 110, half - 10.f, 150));
        // SLW + OFS knobs, top-right of the evolution pad, clear of STEP.
        slw->setBounds(R(hx + half - 10.f - 100.f, y4 + 113, 46, 46));
        ofs->setBounds(R(hx + half - 10.f - 52.f, y4 + 113, 46, 46));
    }

    // ---- bottom utility row (the last row now that ADVANCED is gone) ----
    // Nudged down a few px for a little more air under the evolution editors.
    // Main run: evolution controls (which follow the focused oscillator), DETUNE
    // and the sound-layer key/level knobs.
    const float by = y4 + 276.f, step = KW + 8.f;   // 636 (was 628)
    kEvoTime->setBounds(R(18 + 0 * step, by, KW, KH));
    kScanStyle->setBounds(R(18 + 1 * step, by, KW, KH));
    kEvoLFORate->setBounds(R(18 + 2 * step, by, KW, KH));
    kEvoLFODepth->setBounds(R(18 + 3 * step, by, KW, KH));
    kOscMix->setBounds(R(18 + 4 * step, by, KW, KH));
    kDetune->setBounds(R(18 + 5 * step, by, KW, KH));   // SPREAD has moved to STEREO
    kSoundLow->setBounds(R(18 + 6 * step, by, KW, KH));
    kSoundHigh->setBounds(R(18 + 7 * step, by, KW, KH));
    kSoundLevel->setBounds(R(18 + 8 * step, by, KW, KH));

    // Relocated engine knobs, promoted into the band left of the PHIZMO wordmark
    // as three tight, captioned sub-groups (captions drawn in paint(); keep the
    // X positions here in step with those).
    {
        const float ps = KW + 1.f;                  // 57 — packed tight
        const float sx = 596.f;                     // STEREO: SPREAD WIDTH UNISON
        kSpread->setBounds(R(sx + 0 * ps, by, KW, KH));
        kStWidth->setBounds(R(sx + 1 * ps, by, KW, KH));
        kUniDet->setBounds(R(sx + 2 * ps, by, KW, KH));
        const float dx = sx + 3.f * ps + 12.f;      // DIGITAL: SNAP GRIT JUMP RING
        kFrameSnap->setBounds(R(dx + 0 * ps, by, KW, KH));
        kGrit->setBounds(R(dx + 1 * ps, by, KW, KH));
        kJumpProb->setBounds(R(dx + 2 * ps, by, KW, KH));
        kRingMod->setBounds(R(dx + 3 * ps, by, KW, KH));
        const float qx = dx + 4.f * ps + 12.f;      // POSITION: POS LFO POS AMT
        kPosLFORate->setBounds(R(qx + 0 * ps, by, KW, KH));
        kPosLFODep->setBounds(R(qx + 1 * ps, by, KW, KH));
    }
}

//==============================================================================
EvoCurveEditor::EvoCurveEditor(PhizmoAudioProcessor& p, int oscIndex, const juce::String& titleText)
    : proc(p), osc(oscIndex), title(titleText)
{
    startTimerHz(30);

    btnReset.onClick = [this] {
        for (int i = 0; i < EVO_POINTS; ++i) proc.setCurvePoint(i, 0.5f, osc);
        repaint(); };
    btnRamp.onClick = [this] {
        for (int i = 0; i < EVO_POINTS; ++i) proc.setCurvePoint(i, (float)i / (float)(EVO_POINTS - 1), osc);
        repaint(); };
    btnStepped.onClick = [this] {
        if (auto* p2 = proc.apvts.getParameter(steppedParamID()))
            p2->setValueNotifyingHost(p2->getValue() > 0.5f ? 0.f : 1.f);
        repaint(); };

    // EVO mode: cycle NORM → RAND → LOCK → FIX → NORM. No LED (the label already
    // shows the state), so it uses the full-width centred text branch.
    btnEvoMode.hasLed = false;
    btnEvoMode.onClick = [this] {
        if (auto* p = proc.apvts.getParameter(modeParamID()))
        {
            int cur = (int)std::round(proc.apvts.getRawParameterValue(modeParamID())->load());
            int nxt = (cur + 1) & 3;                       // 0..3 wrap
            p->setValueNotifyingHost((float)nxt / 3.0f);   // 0..3 range → normalised
        }
        refreshEvoModeLabel();
        repaint(); };

    addAndMakeVisible(btnReset);
    addAndMakeVisible(btnRamp);
    addAndMakeVisible(btnStepped);
    addAndMakeVisible(btnEvoMode);
    refreshEvoModeLabel();
}

EvoCurveEditor::~EvoCurveEditor() { stopTimer(); }

juce::String EvoCurveEditor::steppedParamID() const { return osc == 0 ? "evoStepped" : "evoSteppedB"; }
juce::String EvoCurveEditor::modeParamID()    const { return osc == 0 ? "evoMode" : "evoModeB"; }

void EvoCurveEditor::refreshEvoModeLabel()
{
    static const char* names[4] = { "EVO: NORM", "EVO: RAND", "EVO: LOCK", "EVO: FIX" };
    int m = 0;
    if (auto* v = proc.apvts.getRawParameterValue(modeParamID()))
        m = juce::jlimit(0, 3, (int)std::round(v->load()));
    if (btnEvoMode.getButtonText() != names[m])
        btnEvoMode.setButtonText(names[m]);
}

void EvoCurveEditor::resized()
{
    // Buttons scale with the component width; they sit in their own row below the
    // title. Four buttons now share the exact span the old three occupied: the
    // three-button span was 3*btnW + 3*gap, so four narrower buttons with the
    // same gaps get (span - 3*gap)/4 each.
    const int w = getWidth();
    const int btnY = 24, btnH = juce::jmax(16, getHeight() / 15);
    const int btnW = juce::jmax(40, w / 4 - 8);
    const int gap = juce::jmax(2, w / 50);
    const int span = 3 * btnW + 3 * gap;          // width the old 3 buttons spanned
    const int bw = juce::jmax(28, (span - 3 * gap) / 4);
    auto slot = [&](int i) { return gap + i * (bw + gap); };
    btnReset.setBounds(slot(0), btnY, bw, btnH);
    btnRamp.setBounds(slot(1), btnY, bw, btnH);
    btnStepped.setBounds(slot(2), btnY, bw, btnH);
    btnEvoMode.setBounds(slot(3), btnY, bw, btnH);
}

juce::Rectangle<float> EvoCurveEditor::drawArea() const
{
    auto b = getLocalBounds().toFloat();
    // Top margin clears: title row (~18px) + button row (~24px starting at 24, height 20) + gap
    return b.reduced(4.f, 0.f).withTrimmedTop(50.f).withTrimmedBottom(4.f);
}

float EvoCurveEditor::pointToPixelX(int idx) const {
    auto da = drawArea();
    return da.getX() + (float)idx / (float)(EVO_POINTS - 1) * da.getWidth();
}

float EvoCurveEditor::pointToPixelY(float val) const {
    auto da = drawArea();
    return da.getBottom() - val * da.getHeight();
}

int EvoCurveEditor::xToPointIndex(float px) const {
    auto da = drawArea();
    float t = (px - da.getX()) / da.getWidth();
    return juce::jlimit(0, EVO_POINTS - 1, (int)std::round(t * (float)(EVO_POINTS - 1)));
}

float EvoCurveEditor::yToVal(float py) const {
    auto da = drawArea();
    float v = (da.getBottom() - py) / da.getHeight();
    return juce::jlimit(0.f, 1.f, v);
}

void EvoCurveEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto da = drawArea();

    juce::ColourGradient bg(juce::Colour(0xff0d0520), b.getTopLeft(),
        juce::Colour(0xff051030), b.getBottomRight(), false);
    g.setGradientFill(bg); g.fillRoundedRectangle(b, 6.f);
    g.setColour(juce::Colour(0xff6040a0).withAlpha(0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.f, 1.f);

    // Title — its own row at the very top, well clear of the FLAT/RAMP/STEP
    // buttons (which sit in their own row below it). No more hidden text.
    g.setFont(PhizmoLookAndFeel::panelFont());
    g.setColour(juce::Colour(PhizmoLookAndFeel::col_accent1).withAlpha(0.85f));
    g.drawText(title, b.reduced(6.f, 2.f).withHeight(16.f).toNearestInt(),
        juce::Justification::centredLeft);

    // Grid
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 0; i <= 4; ++i) {
        float y = pointToPixelY((float)i / 4.f);
        g.drawHorizontalLine((int)y, da.getX(), da.getRight());
    }
    for (int i = 0; i < EVO_POINTS; i += 8) {
        float x = pointToPixelX(i);
        g.drawVerticalLine((int)x, da.getY(), da.getBottom());
    }

    bool stepped = (proc.apvts.getRawParameterValue(steppedParamID()) &&
        proc.apvts.getRawParameterValue(steppedParamID())->load() > 0.5f);

    // Fill
    juce::Path fillPath;
    fillPath.startNewSubPath(pointToPixelX(0), da.getBottom());
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        if (stepped && i > 0) {
            float py = pointToPixelY(proc.getCurvePoint(i - 1, osc));
            fillPath.lineTo(x, py);
        }
        fillPath.lineTo(x, y);
    }
    fillPath.lineTo(pointToPixelX(EVO_POINTS - 1), da.getBottom());
    fillPath.closeSubPath();
    juce::ColourGradient fillGrad(juce::Colour(PhizmoLookAndFeel::col_accent1).withAlpha(0.18f), 0.f, da.getY(),
        juce::Colour(PhizmoLookAndFeel::col_accent2).withAlpha(0.04f), 0.f, da.getBottom(), false);
    g.setGradientFill(fillGrad); g.fillPath(fillPath);

    // Line
    juce::Path linePath;
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        if (i == 0) linePath.startNewSubPath(x, y);
        else {
            if (stepped) { float py = pointToPixelY(proc.getCurvePoint(i - 1, osc)); linePath.lineTo(x, py); }
            linePath.lineTo(x, y);
        }
    }
    juce::ColourGradient lineGrad(juce::Colour(PhizmoLookAndFeel::col_accent1), da.getTopLeft(),
        juce::Colour(PhizmoLookAndFeel::col_accent2), da.getTopRight(), false);
    g.setGradientFill(lineGrad);
    g.strokePath(linePath, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Point handles
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        bool hovered = (i == dragIndex);
        g.setColour(hovered ? juce::Colour(PhizmoLookAndFeel::col_accent1) : juce::Colours::white.withAlpha(0.55f));
        g.fillEllipse(x - 3.5f, y - 3.5f, 7.f, 7.f);
        if (hovered) {
            g.setColour(juce::Colour(PhizmoLookAndFeel::col_accent1).withAlpha(0.6f));
            g.drawEllipse(x - 5.f, y - 5.f, 10.f, 10.f, 1.5f);
        }
    }

    // Playheads. In NORM/RAND/LOCK each sounding note on the focused Sound gets
    // its own bar (see below). In FIX there is instead a single perpetual master
    // playhead that is always drawn — even with nothing playing — which every
    // FIX note rides, so we draw just that one.
    const int editSnd = proc.getEditSound();

    // The published scan phase is the raw phase, before the per-osc curve phase
    // offset the audio path folds in (evoPhaseOff for A, evoBPhaseOff for B).
    // Reapply it (wrapped as the DSP does) so each dot tracks the audible frame.
    float phaseOff = 0.f;
    if (auto* off = proc.apvts.getRawParameterValue(osc == 0 ? "evoPhaseOff" : "evoBPhaseOff"))
        phaseOff = off->load();

    int evoMode = 0;
    if (auto* mp = proc.apvts.getRawParameterValue(modeParamID()))
        evoMode = juce::jlimit(0, 3, (int)std::round(mp->load()));

    auto drawPlayhead = [&](float ph, float lineAlpha, float dotR)
        {
            ph -= std::floor(ph);                 // wrap into [0,1)
            float phX = da.getX() + ph * da.getWidth();
            float phY = pointToPixelY(proc.evalCurve(ph, osc));
            g.setColour(juce::Colours::white.withAlpha(lineAlpha));
            g.drawVerticalLine((int)phX, da.getY(), da.getBottom());
            g.setColour(juce::Colour(PhizmoLookAndFeel::col_accent1));
            g.fillEllipse(phX - dotR, phY - dotR, dotR * 2.f, dotR * 2.f);
        };

    if (evoMode == 3)   // FIX — one always-present master playhead
    {
        drawPlayhead(proc.getEvoFixPhase(editSnd, osc) + phaseOff, 0.4f, 4.5f);
    }
    else                // NORM / RAND / LOCK — one bar per sounding note
    {
        for (int vi = 0; vi < proc.getEvoPlayheadCount(); ++vi)
        {
            if (proc.getEvoPlayheadSound(vi) != editSnd) continue;   // silent (-1) or other Sound
            drawPlayhead(proc.getEvoPlayheadScan(vi, osc) + phaseOff, 0.28f, 4.f);
        }
    }

    // Min/max labels
    g.setFont(PhizmoLookAndFeel::font(PhizmoLookAndFeel::kBaseTextPx - 1.f, false));
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawText("1.0", juce::Rectangle<int>((int)da.getX() - 2, (int)da.getY(), 26, 12), juce::Justification::centredRight);
    g.drawText("0.0", juce::Rectangle<int>((int)da.getX() - 2, (int)da.getBottom() - 12, 26, 12), juce::Justification::centredRight);
}

void EvoCurveEditor::handleDrag(const juce::MouseEvent& e) {
    int idx = xToPointIndex((float)e.x);
    float val = yToVal((float)e.y);
    dragIndex = idx;
    proc.setCurvePoint(idx, val, osc);
    repaint();
}

void EvoCurveEditor::mouseDown(const juce::MouseEvent& e) { handleDrag(e); }
void EvoCurveEditor::mouseDrag(const juce::MouseEvent& e) { handleDrag(e); }
void EvoCurveEditor::mouseUp(const juce::MouseEvent&) { dragIndex = -1; repaint(); }

//==============================================================================
// WavetableDisplay
//==============================================================================
WavetableDisplay::WavetableDisplay(PhizmoAudioProcessor& p, int slot, const juce::String& lbl)
    : proc(p), slotIndex(slot), slotLabel(lbl) {
    startTimerHz(30);
}
WavetableDisplay::~WavetableDisplay() { stopTimer(); }

bool WavetableDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    // 2.0: .wav is still treated as a wavetable and loaded as before. Any other
    // audio format is treated as a sample and Transwavified instead, so the
    // existing behaviour is never reinterpreted.
    for (auto& f : files)
        if (juce::File(f).hasFileExtension("wav;aif;aiff;flac;ogg;mp3;m4a")) return true;
    return false;
}

void WavetableDisplay::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void WavetableDisplay::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void WavetableDisplay::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (auto& f : files)
    {
        juce::File file(f);
        if (!file.existsAsFile()) continue;

        int cs = getCycleSize ? getCycleSize() : 2048;
        if (cs < 16) cs = 2048;

        if (file.hasFileExtension("wav"))
        {
            // Unchanged 1.x path: a .wav is a wavetable.
            proc.loadWavetable(file, cs, slotIndex);
            break;
        }
        if (file.hasFileExtension("aif;aiff;flac;ogg;mp3;m4a"))
        {
            // 2.0: anything else audio gets Transwavified.
            const int frames = (int)(proc.apvts.getRawParameterValue("genFrames")
                ? proc.apvts.getRawParameterValue("genFrames")->load() : 32.f);
            const bool slice = proc.apvts.getRawParameterValue("genUnpitched")
                && proc.apvts.getRawParameterValue("genUnpitched")->load() > 0.5f;
            juce::String status;
            proc.transwavifyFile(slotIndex, file, frames, cs, slice, status);
            break;
        }
    }
}

void WavetableDisplay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient bg(juce::Colour(0xff0d0520), b.getTopLeft(), juce::Colour(0xff051030), b.getBottomRight(), false);
    g.setGradientFill(bg); g.fillRoundedRectangle(b, 6.f);

    // Both oscillators are always active; highlight by mix amount
    float mix = proc.apvts.getRawParameterValue("oscMix") ?
        proc.apvts.getRawParameterValue("oscMix")->load() : 0.5f;
    bool isActive = ((slotIndex % 2) == 0) ? (mix <= 0.5f) : (mix > 0.5f);
    juce::Colour bc = isActive ? juce::Colour(0xffff6ec7) : juce::Colour(0xff6040a0);
    g.setColour(bc.withAlpha(isActive ? 0.9f : 0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.f, isActive ? 1.5f : 1.f);

    juce::Rectangle<float> badge(b.getX() + 6.f, b.getY() + 5.f, 58.f, 15.f);
    juce::ColourGradient bg2(isActive ? juce::Colour(0xffff6ec7) : juce::Colour(0xff6040a0), badge.getTopLeft(),
        isActive ? juce::Colour(0xff6ec6ff) : juce::Colour(0xff2a1060), badge.getBottomRight(), false);
    g.setGradientFill(bg2); g.fillRoundedRectangle(badge, 3.f);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(PhizmoLookAndFeel::panelFont());
    g.drawText("S" + juce::String(slotIndex / 2 + 1) + " OSC" + juce::String(slotIndex % 2 + 1),
        badge.toNearestInt(), juce::Justification::centred);

    // Drag-and-drop hover highlight
    if (dragOver)
    {
        g.setColour(juce::Colour(0x44ffffff));
        g.fillRoundedRectangle(b.reduced(2.f), 6.f);
        g.setColour(juce::Colour(PhizmoLookAndFeel::col_accent1));
        g.drawRoundedRectangle(b.reduced(1.5f), 6.f, 2.f);
        g.setFont(PhizmoLookAndFeel::font(PhizmoLookAndFeel::kBaseTextPx + 1.f));
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawText("Drop .wav here", b, juce::Justification::centred);
        return;
    }

    if (!proc.isWavetableLoaded(slotIndex)) {
        g.setColour(juce::Colour(0xff6040a0).withAlpha(0.7f));
        g.setFont(PhizmoLookAndFeel::panelFont());
        g.drawText("Drop .wav  or  LOAD", b, juce::Justification::centred); return;
    }

    g.setColour(juce::Colour(0xaaff6ec7));
    g.setFont(PhizmoLookAndFeel::panelFont());
    g.drawText(proc.getWavetableName(slotIndex),
        b.withTrimmedLeft(72.f).withTrimmedRight(8.f).withTop(b.getY() + 5.f).withHeight(15.f).toNearestInt(),
        juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xaa6ec6ff));
    g.setFont(PhizmoLookAndFeel::font(PhizmoLookAndFeel::kBaseTextPx - 1.f, false));
    juce::String info = juce::String(proc.getNumFrames(slotIndex)) + " fr  " +
        juce::String(proc.getCycleSamples(slotIndex)) + " smp";
    g.drawText(info, b.reduced(4.f, 5.f).withHeight(14.f).toNearestInt(), juce::Justification::topRight);

    int nf = proc.getNumFrames(slotIndex);
    int w = (int)b.getWidth();
    if (nf > 0 && w > 0) {
        juce::Path wp;
        float midY = b.getCentreY(), halfH = b.getHeight() * 0.38f;
        float fp = proc.getCurrentEvoFramePos(slotIndex);   // each osc reads its OWN curve
        float fi = fp * (float)(nf - 1);
        for (int px = 0; px < w; ++px) {
            double ph = (double)px / (double)(w - 1);
            float s = proc.sampleFrameNearest(slotIndex, fi, ph);
            float y = midY - s * halfH;
            if (px == 0) wp.startNewSubPath((float)px + b.getX(), y);
            else         wp.lineTo((float)px + b.getX(), y);
        }
        if (isActive) {
            juce::ColourGradient wg(juce::Colour(0xffff6ec7), b.getTopLeft(), juce::Colour(0xff6ec6ff), b.getBottomRight(), false);
            g.setGradientFill(wg);
        }
        else g.setColour(juce::Colour(0xff6040a0).withAlpha(0.6f));
        g.strokePath(wp, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}