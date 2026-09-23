#include "PluginEditor.h"
#include "Alpha3Import.h"
#include "RandomPatch.h"

// ================================================================
//  AlphaLAF – constructor
// ================================================================
AlphaLAF::AlphaLAF() {
    // ComboBox colours
    setColour(juce::ComboBox::backgroundColourId, Pal::bg);
    setColour(juce::ComboBox::textColourId, Pal::text);
    setColour(juce::ComboBox::arrowColourId, Pal::blue);
    setColour(juce::ComboBox::outlineColourId, Pal::track);
    setColour(juce::ComboBox::focusedOutlineColourId, Pal::blue);
    // Popup menu colours
    setColour(juce::PopupMenu::backgroundColourId,
        juce::Colour(0xFF1C2030));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFCDD2DA));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Pal::blueDark);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

// ================================================================
//  AlphaLAF – rotary slider
// ================================================================
void AlphaLAF::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos, float startAngle, float endAngle,
    juce::Slider& /*slider*/)
{
    // The slider bounds are the knob area only (labels are laid out by the owner)
    auto bounds = juce::Rectangle<float>((float)x, (float)y,
        (float)width, (float)height).reduced(8.0f);
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    float angle = startAngle + sliderPos * (endAngle - startAngle);

    // ---- Drop shadow ----
    g.setColour(juce::Colour(0x50000000));
    g.fillEllipse(cx - radius + 1.5f, cy - radius + 2.5f,
        radius * 2.0f, radius * 2.0f);

    // ---- Knob body (radial gradient: top-left bright → bottom-right dark) ----
    juce::ColourGradient body(
        Pal::knobTop, cx - radius * 0.4f, cy - radius * 0.5f,
        Pal::knobBot, cx + radius * 0.5f, cy + radius * 0.6f, false);
    body.addColour(0.45, juce::Colour(0xFFB0B8C4));
    body.addColour(0.70, juce::Colour(0xFF747D8A));
    g.setGradientFill(body);
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // ---- Inner specular highlight (top-left) ----
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.fillEllipse(cx - radius * 0.7f, cy - radius * 0.75f,
        radius * 0.9f, radius * 0.65f);

    // ---- Outer rim ----
    g.setColour(Pal::knobRim);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.4f);

    // ---- Track arc (dark background ring) ----
    float arcR = radius + 4.0f;
    {
        juce::Path tr;
        tr.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(Pal::track);
        g.strokePath(tr, juce::PathStrokeType(3.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- Value arc (watery blue gradient) ----
    if (sliderPos > 0.001f) {
        juce::Path va;
        va.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, angle, true);

        // Multi-stop gradient for the "wet water" look
        juce::ColourGradient arcG(
            Pal::blueDark, cx - arcR, cy,
            Pal::blueBright, cx + arcR, cy, false);
        arcG.addColour(0.4, Pal::blue);
        arcG.addColour(0.75, Pal::blueMid);
        g.setGradientFill(arcG);
        g.strokePath(va, juce::PathStrokeType(3.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bright end-cap dot
        float dotX = cx + std::sin(angle) * arcR;
        float dotY = cy - std::cos(angle) * arcR;
        g.setColour(Pal::blueBright);
        g.fillEllipse(dotX - 2.8f, dotY - 2.8f, 5.6f, 5.6f);
        // Faint glow around the dot
        g.setColour(Pal::blueGlow);
        g.fillEllipse(dotX - 5.5f, dotY - 5.5f, 11.0f, 11.0f);
    }

    // ---- Indicator line ----
    {
        float sinA = std::sin(angle), cosA = std::cos(angle);
        float li = radius * 0.25f, lo = radius * 0.80f;
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawLine(cx + sinA * li, cy - cosA * li,
            cx + sinA * lo, cy - cosA * lo, 2.0f);
    }

    // ---- Centre dot ----
    g.setColour(Pal::blue);
    g.fillEllipse(cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
}

// ================================================================
//  AlphaLAF – combo box
// ================================================================
void AlphaLAF::drawComboBox(juce::Graphics& g, int width, int height,
    bool isDown, int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
    juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0.5f, 0.5f,
        (float)width - 1.0f, (float)height - 1.0f);

    // Body: subtle top-to-bottom gradient
    juce::ColourGradient bg(
        isDown ? juce::Colour(0xFFC8CDDA) : juce::Colour(0xFFD6DAE4), 0.f, 0.f,
        isDown ? juce::Colour(0xFFB8BDC8) : juce::Colour(0xFFC0C5D0), 0.f, (float)height,
        false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, 4.0f);

    // Border
    bool active = box.isPopupActive();
    g.setColour(active ? Pal::blue : juce::Colour(0xFF7A8090));
    g.drawRoundedRectangle(r, 4.0f, active ? 1.8f : 1.1f);

    // Arrow triangle
    float ax = (float)width - 13.0f;
    float ay = (float)height * 0.5f;
    juce::Path arr;
    arr.addTriangle(ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour(active ? Pal::blueBright : Pal::blue);
    g.fillPath(arr);
}

void AlphaLAF::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(5, 0, box.getWidth() - 24, box.getHeight());
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Font AlphaLAF::getComboBoxFont(juce::ComboBox&) {
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);
}

juce::Font AlphaLAF::getLabelFont(juce::Label&) {
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain);
}

void AlphaLAF::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.setFont(getLabelFont(label));
    g.setColour(Pal::textDim);
    g.drawText(label.getText(), label.getLocalBounds(),
        label.getJustificationType(), true);
}

void AlphaLAF::fillTextEditorBackground(juce::Graphics& g, int w, int h,
    juce::TextEditor&) {
    g.setColour(Pal::bg);
    g.fillRect(0, 0, w, h);
}

// ================================================================
//  AlphaLAF – popup menu
// ================================================================
void AlphaLAF::drawButtonBackground(juce::Graphics& g,
    juce::Button& btn, const juce::Colour& /*bg*/,
    bool isHighlighted, bool isDown)
{
    auto r = btn.getLocalBounds().toFloat().reduced(1.0f);
    // Body
    juce::ColourGradient body(
        isDown ? Pal::blueDark : (isHighlighted ? Pal::blue : juce::Colour(0xFF3A4050)),
        r.getX(), r.getY(),
        isDown ? Pal::blue.darker(0.3f) : (isHighlighted ? Pal::blueMid : juce::Colour(0xFF252A38)),
        r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, 5.0f);

    // Border glow
    g.setColour(isHighlighted || isDown ? Pal::blue : Pal::blueDark.withAlpha(0.8f));
    g.drawRoundedRectangle(r, 5.0f, isDown ? 2.0f : 1.3f);

    // Inner top highlight
    g.setColour(juce::Colours::white.withAlpha(isDown ? 0.04f : 0.08f));
    g.drawLine(r.getX() + 6.0f, r.getY() + 1.5f,
        r.getRight() - 6.0f, r.getY() + 1.5f, 1.0f);
}

void AlphaLAF::drawButtonText(juce::Graphics& g, juce::TextButton& btn,
    bool isHighlighted, bool /*isDown*/)
{
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
    g.setColour(isHighlighted ? Pal::blueBright : Pal::blue);
    g.drawText(btn.getButtonText(), btn.getLocalBounds(),
        juce::Justification::centred, false);
}

void AlphaLAF::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.setColour(juce::Colour(0xFF1C2030));
    g.fillRect(0, 0, w, h);
    g.setColour(Pal::blueDark.withAlpha(0.6f));
    g.drawRect(0, 0, w, h, 1);
}

void AlphaLAF::drawPopupMenuItem(juce::Graphics& g,
    const juce::Rectangle<int>& area,
    bool isSeparator, bool isActive, bool isHighlighted,
    bool isTicked, bool /*hasSubMenu*/,
    const juce::String& text, const juce::String& /*shortcut*/,
    const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/)
{
    if (isSeparator) {
        g.setColour(Pal::track);
        g.fillRect(area.withHeight(1).withY(area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive) {
        juce::ColourGradient hi(Pal::blueDark, (float)area.getX(), 0.f,
            Pal::blueDark.darker(0.2f), (float)area.getRight(), 0.f, false);
        g.setGradientFill(hi);
        g.fillRect(area);
    }

    auto tc = isHighlighted ? juce::Colours::white : juce::Colour(0xFFCDD2DA);
    if (!isActive) tc = tc.withAlpha(0.35f);
    g.setColour(tc);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    g.drawText(text, area.reduced(10, 0), juce::Justification::centredLeft, true);

    if (isTicked) {
        g.setColour(Pal::blue);
        float cy = (float)area.getCentreY();
        g.fillEllipse(4.0f, cy - 3.0f, 6.0f, 6.0f);
    }
}

// ================================================================
//  LKnob
// ================================================================
LKnob::LKnob(const juce::String& lab) : labelText(lab) {
    slider.setSliderStyle(juce::Slider::Rotary);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(slider);
}

// Knob and label form one block, centred in the cell, label right under the knob
static constexpr int LABEL_H = 12;

static juce::Rectangle<int> knobBlock(juce::Rectangle<int> b, int maxKnob = 66) {
    const int k = juce::jmin(b.getWidth(), b.getHeight() - LABEL_H, maxKnob);
    return b.withSizeKeepingCentre(b.getWidth(), k + LABEL_H);
}

void LKnob::resized() {
    auto blk = knobBlock(getLocalBounds());
    auto knob = blk.removeFromTop(blk.getHeight() - LABEL_H);
    slider.setBounds(knob.withSizeKeepingCentre(knob.getHeight(), knob.getHeight()));
}

void LKnob::paint(juce::Graphics& g) {
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
    g.setColour(Pal::textDim);
    g.drawText(labelText, knobBlock(getLocalBounds()).removeFromBottom(LABEL_H),
               juce::Justification::centredTop, false);
}

// ================================================================
//  WtSlot
// ================================================================
WtSlot::WtSlot(const juce::String& t) : title(t) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour(juce::Slider::thumbColourId, Pal::blue);
    slider.setColour(juce::Slider::trackColourId, Pal::blue.withAlpha(0.7f));
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF252A38));
    slider.setTooltip("Wavetable position (scans the frames of the loaded table)");
    addAndMakeVisible(slider);
    startTimerHz(4);
}

void WtSlot::resized() {
    auto b = getLocalBounds();
    strip = b.removeFromLeft(b.getWidth() / 2 - 4);
    b.removeFromLeft(8);
    slider.setBounds(b);
}

void WtSlot::timerCallback() {
    auto n = getName ? getName() : juce::String();
    if (n != shownName) { shownName = n; repaint(); }
}

void WtSlot::paint(juce::Graphics& g) {
    auto r = strip.toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xFF252A38));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(shownName.isEmpty() ? Pal::blueDark.withAlpha(0.8f) : Pal::blue);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold)));
    auto text = strip.reduced(5, 0);
    g.setColour(Pal::textDim);
    g.drawText(title, text.removeFromLeft(10), juce::Justification::centredLeft, false);
    g.setColour(shownName.isEmpty() ? Pal::textBright : Pal::blueBright);
    g.drawFittedText(shownName.isEmpty() ? juce::String("LOAD WT") : shownName.toUpperCase(),
                     text, juce::Justification::centred, 1, 0.75f);
}

void WtSlot::mouseDown(const juce::MouseEvent& e) {
    if (!strip.contains(e.getPosition())) return;
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m, sizes;
        for (int fs : { 256, 512, 1024, 2048, 4096 })
            sizes.addItem(fs, juce::String(fs) + " samples per frame");
        m.addItem(1, "Load wavetable (auto frame size)...");
        m.addSubMenu("Load with frame size", sizes);
        m.addSeparator();
        m.addItem(2, "Clear wavetable", shownName.isNotEmpty());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
            if (r == 1 && onLoad) onLoad(0);
            if (r >= 256 && onLoad) onLoad(r);
            if (r == 2 && onClear) onClear();
        });
    } else if (onLoad) {
        onLoad(0);
    }
}

// ================================================================
//  createEditor (lives here because it needs the editor type)
// ================================================================
juce::AudioProcessorEditor* AlphaBetaAudioProcessor::createEditor() {
    return new AlphaBetaAudioProcessorEditor(*this);
}

// ================================================================
//  Editor – constructor
// ================================================================
AlphaBetaAudioProcessorEditor::AlphaBetaAudioProcessorEditor(
    AlphaBetaAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p), crt(p)
{
    setLookAndFeel(&laf);

    // ---- Populate combo boxes ----
    // Items are added in parameter order; headings don't count as items,
    // so the ComboBoxAttachment's index mapping stays correct.
    auto fillWave = [](juce::ComboBox& cb) {
        const auto names = InternalWaves::allWaveNames();
        auto heading = [](int i) -> juce::String {
            switch (i) {
            case 0: return "Basic"; case 5: return "Wavetable"; case 6: return "Squares";
            case 8: return "Organ"; case 11: return "Spectra"; case 15: return "Rich saw";
            case 19: return "Saw spectra"; case 21: return "Vintage saw"; case 24: return "Saw bass";
            default: return {};
            }
        };
        for (int i = 0; i < names.size(); ++i) {
            if (heading(i).isNotEmpty()) cb.addSectionHeading(heading(i));
            cb.addItem(names[i], i + 1);
        }
        };
    auto fillOct = [](juce::ComboBox& cb) {
        cb.addItem("-2", 1); cb.addItem("-1", 2); cb.addItem("0", 3);
        cb.addItem("+1", 4); cb.addItem("+2", 5);
        };

    fillWave(o1WaveA); fillWave(o1WaveB);
    fillWave(o2WaveA); fillWave(o2WaveB);
    fillOct(o1OctA);   fillOct(o1OctB);
    fillOct(o2OctA);   fillOct(o2OctB);

    fType.addItem("LP12", 1);
    fType.addItem("LP24", 2);
    fType.addItem("LP24+", 3);
    fType.addItem("BP", 4);
    fType.addItem("HP", 5);

    resType.addItem("Quadratic", 1);
    resType.addItem("Cubic", 2);

    // ---- Add all children ----
    auto add = [this](juce::Component& c) { addAndMakeVisible(c); };

    add(o1WaveA); add(o1OctA); add(o1WaveB); add(o1OctB);
    add(o2WaveA); add(o2OctA); add(o2WaveB); add(o2OctB);
    add(o1Morph);  add(o1Detune);
    add(o2Morph);  add(o2Detune);
    add(knobMix);  add(knobDrive); add(knobFM); add(knobSpread);
    add(fCutoff);  add(fRes);      add(fType);   add(resType);
    add(fAtt);     add(fDec);      add(fSus);   add(fRel);
    add(fFade);    add(fDepth);
    add(aVol);     add(aVel);
    add(aAtt);     add(aDec);      add(aSus);   add(aRel);
    add(aFade);
    add(hWet);     add(hTime);     add(hRate);  add(knobGlide);
    add(o1Pitch);  add(o2Pitch);
    add(knobNoise); add(knobRing); add(knobFFM); add(ffmSrc); add(knobBend);
    add(knobUnison); add(knobAnalog);
    voiceMode.addItem("Poly", 1);
    voiceMode.addItem("Mono", 2);
    voiceMode.addItem("Legato", 3);
    voiceMode.setLookAndFeel(&laf);
    voiceMode.setTooltip("Poly: 32 voices. Mono: one voice, glides on every note. Legato: glides only on overlapping notes, no envelope restart.");
    addAndMakeVisible(voiceMode);

    // ---- Wavetable slots: each wave slot (A / B) of each oscillator ----
    for (int slot = 0; slot < 4; ++slot) {
        auto& ws = wtSlots[(size_t)slot];
        add(ws);
        const int osc = slot / 2;
        const bool isA = (slot % 2) == 0;
        const juce::String waveId = osc == 0 ? (isA ? PID::O1WA : PID::O1WB) : (isA ? PID::O2WA : PID::O2WB);
        const juce::String label = "OSC " + juce::String(osc + 1) + (isA ? " A" : " B");
        ws.getName = [this, slot]() { return proc.getWavetableName(slot); };
        ws.onClear = [this, slot]() { proc.clearWavetable(slot); };
        ws.onLoad = [this, slot, waveId, label](int frameSize) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load wavetable for " + label, juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser, slot, waveId, label, frameSize](const juce::FileChooser& fc) {
                    auto f = fc.getResult();
                    if (f == juce::File{} || !f.existsAsFile()) return;
                    auto err = proc.loadWavetable(slot, f, frameSize);
                    if (err.isNotEmpty()) { crt.showStatus("WT ERROR: " + err.toUpperCase()); return; }
                    // The table replaces this wave slot's waveform
                    if (auto* prm = proc.apvts.getParameter(waveId)) {
                        prm->beginChangeGesture();
                        prm->setValueNotifyingHost(prm->convertTo0to1((float)MonoOsc::TABLE));
                        prm->endChangeGesture();
                    }
                    crt.showStatus(label + " TABLE: " + f.getFileNameWithoutExtension().toUpperCase());
                });
        };
        ws.refresh();
    }
    add(knobTune);
    add(crt);

    ffmSrc.addItem("Osc 1", 1);
    ffmSrc.addItem("Osc 2", 2);
    ffmSrc.addItem("Noise", 3);

    // ---- LFO strip ----
    for (int k = 0; k < Mod::NUM_LFOS; ++k) {
        auto& u = lfoUi[(size_t)k];
        for (int i = 0; i < Mod::lfoWaveNames().size(); ++i) u.wave.addItem(Mod::lfoWaveNames()[i], i + 1);
        for (int i = 0; i < Mod::syncNames().size(); ++i)    u.sync.addItem(Mod::syncNames()[i], i + 1);
        u.mode.addItem("Mono", 1);
        u.mode.addItem("Poly", 2);
        add(u.wave); add(u.sync); add(u.mode); add(u.rate); add(u.att);
        u.aWave = std::make_unique<CA>(proc.apvts, Mod::lfoWaveID(k), u.wave);
        u.aSync = std::make_unique<CA>(proc.apvts, Mod::lfoSyncID(k), u.sync);
        u.aMode = std::make_unique<CA>(proc.apvts, Mod::lfoModeID(k), u.mode);
        u.aRate = std::make_unique<SA>(proc.apvts, Mod::lfoRateID(k), u.rate.slider);
        u.aAtt = std::make_unique<SA>(proc.apvts, Mod::lfoAttID(k), u.att.slider);
    }

    // ---- Randomizer button ----
    randomBtn.setButtonText("RANDOM");
    randomBtn.setLookAndFeel(&laf);
    addAndMakeVisible(randomBtn);

    // IDs excluded from randomisation


    // ---- Character randomizer (AquaNova style) ----
    {
        const auto names = RandomPatch::characterNames();
        for (int i = 0; i < names.size(); ++i) {
            auto head = RandomPatch::sectionBefore(i);
            if (head.isNotEmpty()) randomType.addSectionHeading(head);
            randomType.addItem(names[i], i + 1);
        }
        randomType.setLookAndFeel(&laf);
        randomType.setTooltip("Kind of patch RANDOM creates");
        // remembered with the plugin state
        randomType.setSelectedItemIndex((int)proc.apvts.state.getProperty("randomCharacter", RandomPatch::anythingIndex()),
                                        juce::dontSendNotification);
        randomType.onChange = [this]() {
            proc.apvts.state.setProperty("randomCharacter", randomType.getSelectedItemIndex(), nullptr);
        };
        addAndMakeVisible(randomType);
    }
    randomBtn.setTooltip("Create a new patch of the chosen kind");
    randomBtn.onClick = [this]() {
        juce::Random rng;
        auto name = RandomPatch::generate(proc.apvts, randomType.getSelectedItemIndex(), rng);
        proc.setPatchName(name);
        syncAllUIFromAPVTS();
        crt.showStatus("RANDOM: " + name.toUpperCase());
    };

    // ---- Slider attachments ----
    auto& a = proc.apvts;
    att_o1m = std::make_unique<SA>(a, PID::O1MRP, o1Morph.slider);
    att_o1d = std::make_unique<SA>(a, PID::O1DET, o1Detune.slider);
    att_o2m = std::make_unique<SA>(a, PID::O2MRP, o2Morph.slider);
    att_o2d = std::make_unique<SA>(a, PID::O2DET, o2Detune.slider);
    att_mix = std::make_unique<SA>(a, PID::MIX, knobMix.slider);
    att_drv = std::make_unique<SA>(a, PID::DRV, knobDrive.slider);
    att_fm = std::make_unique<SA>(a, PID::FM, knobFM.slider);
    att_spr = std::make_unique<SA>(a, PID::SPR, knobSpread.slider);
    att_fcut = std::make_unique<SA>(a, PID::FCUT, fCutoff.slider);
    att_fres = std::make_unique<SA>(a, PID::FRES, fRes.slider);
    att_fatt = std::make_unique<SA>(a, PID::FATT, fAtt.slider);
    att_fdec = std::make_unique<SA>(a, PID::FDEC, fDec.slider);
    att_fsus = std::make_unique<SA>(a, PID::FSUS, fSus.slider);
    att_frel = std::make_unique<SA>(a, PID::FREL, fRel.slider);
    att_ffad = std::make_unique<SA>(a, PID::FFAD, fFade.slider);
    att_fdep = std::make_unique<SA>(a, PID::FDEP, fDepth.slider);
    att_avol = std::make_unique<SA>(a, PID::AVOL, aVol.slider);
    att_avel = std::make_unique<SA>(a, PID::AVEL, aVel.slider);
    att_aatt = std::make_unique<SA>(a, PID::AATT, aAtt.slider);
    att_adec = std::make_unique<SA>(a, PID::ADEC, aDec.slider);
    att_asus = std::make_unique<SA>(a, PID::ASUS, aSus.slider);
    att_arel = std::make_unique<SA>(a, PID::AREL, aRel.slider);
    att_afad = std::make_unique<SA>(a, PID::AFAD, aFade.slider);
    att_hwet = std::make_unique<SA>(a, PID::HWET, hWet.slider);
    att_htim = std::make_unique<SA>(a, PID::HTIM, hTime.slider);
    att_hrat = std::make_unique<SA>(a, PID::HRAT, hRate.slider);
    att_glide = std::make_unique<SA>(a, PID::GLID, knobGlide.slider);
    att_o1pit = std::make_unique<SA>(a, PID::O1PIT, o1Pitch.slider);
    att_o2pit = std::make_unique<SA>(a, PID::O2PIT, o2Pitch.slider);
    att_noise = std::make_unique<SA>(a, PID::NOISE, knobNoise.slider);
    att_ring = std::make_unique<SA>(a, PID::RING, knobRing.slider);
    att_ffm = std::make_unique<SA>(a, PID::FFM, knobFFM.slider);
    att_bend = std::make_unique<SA>(a, PID::BEND, knobBend.slider);
    att_uni = std::make_unique<SA>(a, PID::UNI, knobUnison.slider);
    att_analog = std::make_unique<SA>(a, PID::ANALOG, knobAnalog.slider);
    att_wtp[0] = std::make_unique<SA>(a, PID::O1WTP, wtSlots[0].slider);
    att_wtp[1] = std::make_unique<SA>(a, PID::O1BWTP, wtSlots[1].slider);
    att_wtp[2] = std::make_unique<SA>(a, PID::O2WTP, wtSlots[2].slider);
    att_wtp[3] = std::make_unique<SA>(a, PID::O2BWTP, wtSlots[3].slider);
    att_tune = std::make_unique<SA>(a, PID::TUNE, knobTune.slider);

    // ---- ComboBox attachments ----
    att_o1wa = std::make_unique<CA>(a, PID::O1WA, o1WaveA);
    att_o1oa = std::make_unique<CA>(a, PID::O1OCA, o1OctA);
    att_o1wb = std::make_unique<CA>(a, PID::O1WB, o1WaveB);
    att_o1ob = std::make_unique<CA>(a, PID::O1OCB, o1OctB);
    att_o2wa = std::make_unique<CA>(a, PID::O2WA, o2WaveA);
    att_o2oa = std::make_unique<CA>(a, PID::O2OCA, o2OctA);
    att_o2wb = std::make_unique<CA>(a, PID::O2WB, o2WaveB);
    att_o2ob = std::make_unique<CA>(a, PID::O2OCB, o2OctB);
    att_ftyp = std::make_unique<CA>(a, PID::FTYP, fType);
    att_rtyp = std::make_unique<CA>(a, PID::RTYP, resType);
    att_ffms = std::make_unique<CA>(a, PID::FFMS, ffmSrc);
    att_vmode = std::make_unique<CA>(a, PID::VMODE, voiceMode);

    // ---- Save / Load buttons ----
    saveBtn.setLookAndFeel(&laf);
    loadBtn.setLookAndFeel(&laf);
    importBtn.setLookAndFeel(&laf);
    addAndMakeVisible(saveBtn);
    addAndMakeVisible(loadBtn);
    addAndMakeVisible(importBtn);

    importBtn.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Import LinPlug Alpha 3 preset", juce::File(), "*.fxp;*.fxb");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File{} || !f.existsAsFile()) return;
                auto res = Alpha3Import::importFile(f, proc.apvts);
                if (res.ok) {
                    proc.setPatchName(res.patchName);
                    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                   .getChildFile("AlphaBeta");
                    dir.createDirectory();
                    dir.getChildFile("Alpha3 import report.txt").replaceWithText(res.report);
                    syncAllUIFromAPVTS();
                }
                crt.showStatus(res.summary);
            });
        };

    saveBtn.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save preset", juce::File::getSpecialLocation(
                juce::File::userDocumentsDirectory).getChildFile("AlphaBeta"),
            "*.abpreset");
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File{}) return;
                auto file = f.withFileExtension(".abpreset");
                proc.setPatchName(file.getFileNameWithoutExtension());
                juce::MemoryBlock data;
                proc.getStateInformation(data);
                file.replaceWithData(data.getData(), data.getSize());
            });
        };

    loadBtn.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Load preset", juce::File::getSpecialLocation(
                juce::File::userDocumentsDirectory).getChildFile("AlphaBeta"),
            "*.abpreset");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File{} || !f.existsAsFile()) return;
                juce::MemoryBlock data;
                f.loadFileAsData(data);
                proc.setStateInformation(data.getData(), (int)data.getSize());
                if (proc.getPatchName() == "init") proc.setPatchName(f.getFileNameWithoutExtension());
                syncAllUIFromAPVTS();
                crt.showStatus("LOADED " + f.getFileNameWithoutExtension().toUpperCase());
            });
        };
    setSize(GRID_W + PANEL_W, 586);
}

AlphaBetaAudioProcessorEditor::~AlphaBetaAudioProcessorEditor() {
    saveBtn.setLookAndFeel(nullptr);
    loadBtn.setLookAndFeel(nullptr);
    importBtn.setLookAndFeel(nullptr);
    voiceMode.setLookAndFeel(nullptr);
    randomType.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// ================================================================
//  syncAllUIFromAPVTS – force all controls to reflect current state
// ================================================================
void AlphaBetaAudioProcessorEditor::syncAllUIFromAPVTS()
{
    auto& a = proc.apvts;

    auto getV = [&](const juce::String& id) -> double {
        return (double)*a.getRawParameterValue(id);
        };
    auto getI = [&](const juce::String& id) -> int {
        return (int)*a.getRawParameterValue(id);
        };

    // Sliders
    o1Morph.slider.setValue(getV(PID::O1MRP), juce::sendNotificationAsync);
    o1Detune.slider.setValue(getV(PID::O1DET), juce::sendNotificationAsync);
    o2Morph.slider.setValue(getV(PID::O2MRP), juce::sendNotificationAsync);
    o2Detune.slider.setValue(getV(PID::O2DET), juce::sendNotificationAsync);
    knobMix.slider.setValue(getV(PID::MIX), juce::sendNotificationAsync);
    knobDrive.slider.setValue(getV(PID::DRV), juce::sendNotificationAsync);
    knobFM.slider.setValue(getV(PID::FM), juce::sendNotificationAsync);
    knobSpread.slider.setValue(getV(PID::SPR), juce::sendNotificationAsync);
    fCutoff.slider.setValue(getV(PID::FCUT), juce::sendNotificationAsync);
    fRes.slider.setValue(getV(PID::FRES), juce::sendNotificationAsync);
    fAtt.slider.setValue(getV(PID::FATT), juce::sendNotificationAsync);
    fDec.slider.setValue(getV(PID::FDEC), juce::sendNotificationAsync);
    fSus.slider.setValue(getV(PID::FSUS), juce::sendNotificationAsync);
    fRel.slider.setValue(getV(PID::FREL), juce::sendNotificationAsync);
    fFade.slider.setValue(getV(PID::FFAD), juce::sendNotificationAsync);
    fDepth.slider.setValue(getV(PID::FDEP), juce::sendNotificationAsync);
    aVol.slider.setValue(getV(PID::AVOL), juce::sendNotificationAsync);
    aVel.slider.setValue(getV(PID::AVEL), juce::sendNotificationAsync);
    aAtt.slider.setValue(getV(PID::AATT), juce::sendNotificationAsync);
    aDec.slider.setValue(getV(PID::ADEC), juce::sendNotificationAsync);
    aSus.slider.setValue(getV(PID::ASUS), juce::sendNotificationAsync);
    aRel.slider.setValue(getV(PID::AREL), juce::sendNotificationAsync);
    aFade.slider.setValue(getV(PID::AFAD), juce::sendNotificationAsync);
    hWet.slider.setValue(getV(PID::HWET), juce::sendNotificationAsync);
    hTime.slider.setValue(getV(PID::HTIM), juce::sendNotificationAsync);
    hRate.slider.setValue(getV(PID::HRAT), juce::sendNotificationAsync);
    knobGlide.slider.setValue(getV(PID::GLID), juce::sendNotificationAsync);
    o1Pitch.slider.setValue(getV(PID::O1PIT), juce::sendNotificationAsync);
    o2Pitch.slider.setValue(getV(PID::O2PIT), juce::sendNotificationAsync);

    // ComboBoxes (setSelectedItemIndex is 0-based, matching the raw int value)
    o1WaveA.setSelectedItemIndex(getI(PID::O1WA), juce::sendNotificationAsync);
    o1OctA.setSelectedItemIndex(getI(PID::O1OCA), juce::sendNotificationAsync);
    o1WaveB.setSelectedItemIndex(getI(PID::O1WB), juce::sendNotificationAsync);
    o1OctB.setSelectedItemIndex(getI(PID::O1OCB), juce::sendNotificationAsync);
    o2WaveA.setSelectedItemIndex(getI(PID::O2WA), juce::sendNotificationAsync);
    o2OctA.setSelectedItemIndex(getI(PID::O2OCA), juce::sendNotificationAsync);
    o2WaveB.setSelectedItemIndex(getI(PID::O2WB), juce::sendNotificationAsync);
    o2OctB.setSelectedItemIndex(getI(PID::O2OCB), juce::sendNotificationAsync);
    fType.setSelectedItemIndex(getI(PID::FTYP), juce::sendNotificationAsync);
    resType.setSelectedItemIndex(getI(PID::RTYP), juce::sendNotificationAsync);
    ffmSrc.setSelectedItemIndex(getI(PID::FFMS), juce::sendNotificationAsync);
    knobNoise.slider.setValue(getV(PID::NOISE), juce::sendNotificationAsync);
    knobRing.slider.setValue(getV(PID::RING), juce::sendNotificationAsync);
    knobFFM.slider.setValue(getV(PID::FFM), juce::sendNotificationAsync);
    knobBend.slider.setValue(getV(PID::BEND), juce::sendNotificationAsync);
    knobUnison.slider.setValue(getV(PID::UNI), juce::sendNotificationAsync);
    knobAnalog.slider.setValue(getV(PID::ANALOG), juce::sendNotificationAsync);
    voiceMode.setSelectedItemIndex(getI(PID::VMODE), juce::sendNotificationAsync);
    wtSlots[0].slider.setValue(getV(PID::O1WTP), juce::sendNotificationAsync);
    wtSlots[1].slider.setValue(getV(PID::O1BWTP), juce::sendNotificationAsync);
    wtSlots[2].slider.setValue(getV(PID::O2WTP), juce::sendNotificationAsync);
    wtSlots[3].slider.setValue(getV(PID::O2BWTP), juce::sendNotificationAsync);
    knobTune.slider.setValue(getV(PID::TUNE), juce::sendNotificationAsync);
    for (int k = 0; k < Mod::NUM_LFOS; ++k) {
        auto& u = lfoUi[(size_t)k];
        u.wave.setSelectedItemIndex(getI(Mod::lfoWaveID(k)), juce::sendNotificationAsync);
        u.sync.setSelectedItemIndex(getI(Mod::lfoSyncID(k)), juce::sendNotificationAsync);
        u.mode.setSelectedItemIndex(getI(Mod::lfoModeID(k)), juce::sendNotificationAsync);
        u.rate.slider.setValue(getV(Mod::lfoRateID(k)), juce::sendNotificationAsync);
        u.att.slider.setValue(getV(Mod::lfoAttID(k)), juce::sendNotificationAsync);
    }
}

// ================================================================
//  Layout helpers
// ================================================================
juce::Rectangle<int>
AlphaBetaAudioProcessorEditor::cell(int col, int row) const {
    int x = 0;
    for (int c = 0; c < col; ++c) x += CW[c];
    const int rowH = (getHeight() - TITLE_H) / ROWS;
    return { x, TITLE_H + row * rowH, CW[col], rowH };
}

juce::Rectangle<int>
AlphaBetaAudioProcessorEditor::knobArea(int col, int row) const {
    return cell(col, row); // LKnob fills the cell; it trims 16px itself
}

// Oscillator choosers: wave A sits low in its row, wave B high in its row,
// so each oscillator's pair reads as one group inside its box
juce::Rectangle<int> AlphaBetaAudioProcessorEditor::oscComboBounds(int col, int row) const {
    auto r = cell(col, row);
    const int top = (row % 2 == 0) ? 28 : 16;
    return { r.getX() + 4, r.getY() + top, r.getWidth() - 8, 22 };
}

// Wavetable strip + position slider under the wave / octave choosers
juce::Rectangle<int> AlphaBetaAudioProcessorEditor::wtRowBounds(int osc, int ab) const {
    auto wave = oscComboBounds(0, osc * 2 + ab);
    auto oct = oscComboBounds(1, osc * 2 + ab);
    return { wave.getX(), wave.getBottom() + 5, oct.getRight() - wave.getX(), 16 };
}

void AlphaBetaAudioProcessorEditor::placeOscCB(juce::ComboBox& cb, int col, int row) {
    cb.setBounds(oscComboBounds(col, row));
}

void AlphaBetaAudioProcessorEditor::placeCB(juce::ComboBox& cb,
    int col, int row) {
    auto r = cell(col, row);
    cb.setBounds(r.withSizeKeepingCentre(r.getWidth() - 8, 22));
}

// ================================================================
//  resized
// ================================================================
void AlphaBetaAudioProcessorEditor::resized() {
    // ---- Save / Load in title bar, top-right ----
    const int btnW = 52, btnH = 20, btnY = (TITLE_H - btnH) / 2;
    saveBtn.setBounds(getWidth() - btnW * 2 - 14, btnY, btnW, btnH);
    loadBtn.setBounds(getWidth() - btnW - 7, btnY, btnW, btnH);
    importBtn.setBounds(getWidth() - btnW * 2 - 14 - 72 - 7, btnY, 72, btnH);

    // ---- New knobs in the free grid cells ----
    knobRing.setBounds(knobArea(2, 4));
    knobNoise.setBounds(knobArea(3, 4));
    knobFFM.setBounds(knobArea(5, 5));
    placeCB(ffmSrc, 6, 5);
    knobBend.setBounds(knobArea(8, 1));
    knobUnison.setBounds(knobArea(9, 1));
    knobAnalog.setBounds(knobArea(9, 4));
    placeCB(voiceMode, 0, 4);

    // ---- Right panel: CRT matrix on top, three LFOs below ----
    {
        auto panel = juce::Rectangle<int>(GRID_W, TITLE_H, PANEL_W, getHeight() - TITLE_H).reduced(8, 6);
        crt.setBounds(panel.removeFromTop(panel.getHeight() - 172));
        panel.removeFromTop(8);
        const int colW = panel.getWidth() / Mod::NUM_LFOS;
        for (int k = 0; k < Mod::NUM_LFOS; ++k) {
            auto box = panel.withX(panel.getX() + k * colW).withWidth(colW).reduced(3, 0);
            lfoBoxes[(size_t)k] = box;
            auto& u = lfoUi[(size_t)k];
            auto r = box.reduced(5, 5);
            auto top = r.removeFromTop(20);
            u.mode.setBounds(top.removeFromRight(54));
            r.removeFromTop(5);
            u.wave.setBounds(r.removeFromTop(22));
            r.removeFromTop(4);
            u.sync.setBounds(r.removeFromTop(22));
            r.removeFromTop(4);
            // knob + label: square knob area plus the 13 px label strip
            auto knobs = r.removeFromTop(juce::jmin(r.getHeight(), r.getWidth() / 2 + 14));
            u.rate.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2));
            u.att.setBounds(knobs);
        }
    }

    // ---- Randomizer button: title bar, left of IMPORT ----
    randomBtn.setBounds(getWidth() - btnW * 2 - 14 - 72 - 7 - 72 - 7, btnY, 72, btnH);
    randomType.setBounds(randomBtn.getX() - 7 - 132, btnY, 132, btnH);

    // ---- Wavetable slots: row 4 of the oscillator columns ----
    for (int slot = 0; slot < 4; ++slot)
        wtSlots[(size_t)slot].setBounds(wtRowBounds(slot / 2, slot % 2));
    knobTune.setBounds(knobArea(1, 4));

    // ---- Col 0: OSC waveform combos / hWet ----
    placeOscCB(o1WaveA, 0, 0);
    placeOscCB(o1WaveB, 0, 1);
    placeOscCB(o2WaveA, 0, 2);
    placeOscCB(o2WaveB, 0, 3);
    hWet.setBounds(knobArea(0, 5));

    // ---- Col 1: OSC octave combos / hTime ----
    placeOscCB(o1OctA, 1, 0);
    placeOscCB(o1OctB, 1, 1);
    placeOscCB(o2OctA, 1, 2);
    placeOscCB(o2OctB, 1, 3);
    hTime.setBounds(knobArea(1, 5));

    // ---- Col 2: Morph / Detune knobs / hRate ----
    o1Morph.setBounds(knobArea(2, 0));
    o1Detune.setBounds(knobArea(2, 1));
    o2Morph.setBounds(knobArea(2, 2));
    o2Detune.setBounds(knobArea(2, 3));
    hRate.setBounds(knobArea(2, 5));

    // ---- Col 3: Mix column / Glide ----
    knobMix.setBounds(knobArea(3, 0));
    knobDrive.setBounds(knobArea(3, 1));
    knobFM.setBounds(knobArea(3, 2));
    knobSpread.setBounds(knobArea(3, 3));
    knobGlide.setBounds(knobArea(3, 5));

    // ---- Col 4: separator (22 px) – no controls ----

    // ---- Col 5: Filter left column ----
    fCutoff.setBounds(knobArea(5, 0));
    placeCB(fType, 5, 1);
    fAtt.setBounds(knobArea(5, 2));
    fSus.setBounds(knobArea(5, 3));
    fFade.setBounds(knobArea(5, 4));

    // ---- Col 6: Filter right column ----
    fRes.setBounds(knobArea(6, 0));
    placeCB(resType, 6, 1);   // resonance curve selector
    fDec.setBounds(knobArea(6, 2));
    fRel.setBounds(knobArea(6, 3));
    fDepth.setBounds(knobArea(6, 4));

    // ---- Col 7: separator (22 px) – no controls ----

    // ---- Col 8: Amp left column ----
    aVol.setBounds(knobArea(8, 0));
    // row 1: intentionally empty
    aAtt.setBounds(knobArea(8, 2));
    aSus.setBounds(knobArea(8, 3));
    aFade.setBounds(knobArea(8, 4));

    // ---- Col 9: Amp right column ----
    aVel.setBounds(knobArea(9, 0));
    // row 1: intentionally empty
    aDec.setBounds(knobArea(9, 2));
    aRel.setBounds(knobArea(9, 3));
    // rows 4,5: pitch knobs
    o1Pitch.setBounds(knobArea(8, 5));
    o2Pitch.setBounds(knobArea(9, 5));
}

// ================================================================
//  paint
// ================================================================
void AlphaBetaAudioProcessorEditor::paint(juce::Graphics& g) {
    const int W = getWidth();
    const int H = getHeight();
    // ---- Background ----
    g.setColour(Pal::bg);
    g.fillRect(0, TITLE_H, W, H - TITLE_H);

    // ---- Separator columns (cols 4 and 7) ----
    {
        int x4 = CW[0] + CW[1] + CW[2] + CW[3];
        int x7 = x4 + CW[4] + CW[5] + CW[6];
        g.setColour(Pal::sepBg);
        g.fillRect(x4, TITLE_H, CW[4], H - TITLE_H);
        g.fillRect(x7, TITLE_H, CW[7], H - TITLE_H);

        // Thin blue accent lines on the inner edge of each spacer
        g.setColour(Pal::blueDark.withAlpha(0.25f));
        g.fillRect(x4, TITLE_H, 1, H - TITLE_H);
        g.fillRect(x4 + CW[4] - 1, TITLE_H, 1, H - TITLE_H);
        g.fillRect(x7, TITLE_H, 1, H - TITLE_H);
        g.fillRect(x7 + CW[7] - 1, TITLE_H, 1, H - TITLE_H);
    }

    // ---- Oscillator boxes: each osc has two waves (A / B) per voice ----
    for (int o = 0; o < 2; ++o) {
        auto box = cell(0, o * 2).getUnion(cell(1, o * 2 + 1)).reduced(3, 5).toFloat();
        g.setColour(Pal::bgDark.withAlpha(0.55f));
        g.fillRoundedRectangle(box, 7.0f);
        g.setColour(Pal::blueDark.withAlpha(0.45f));
        g.drawRoundedRectangle(box.reduced(0.5f), 7.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold)));
        g.setColour(Pal::blueDark);
        g.drawText("OSC " + juce::String(o + 1), box.reduced(8.0f, 5.0f).withHeight(14.0f),
                   juce::Justification::centredLeft, false);
    }

    // ---- Label under the voice mode chooser ----
    {
        auto cb = voiceMode.getBounds().toFloat();
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
        g.setColour(Pal::textDim);
        g.drawText("Voice Mode", cb.withY(cb.getBottom() + 3.0f).withHeight(12.0f), juce::Justification::centredTop, false);
    }

    // ---- Right panel: divider and LFO boxes ----
    {
        g.setColour(Pal::sepBg);
        g.fillRect(GRID_W, TITLE_H, 4, H - TITLE_H);
        g.setColour(Pal::blueDark.withAlpha(0.25f));
        g.fillRect(GRID_W, TITLE_H, 1, H - TITLE_H);
        g.fillRect(GRID_W + 3, TITLE_H, 1, H - TITLE_H);

        for (int k = 0; k < Mod::NUM_LFOS; ++k) {
            auto b = lfoBoxes[(size_t)k].toFloat();
            g.setColour(Pal::bgDark);
            g.fillRoundedRectangle(b, 6.0f);
            g.setColour(Pal::blueDark.withAlpha(0.35f));
            g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold)));
            g.setColour(Pal::blueDark);
            g.drawText("LFO " + juce::String(k + 1), b.reduced(7.0f, 5.0f).withHeight(20.0f),
                       juce::Justification::centredLeft, false);
        }
    }

    // ---- Title bar ----
    {
        // Dark base
        g.setColour(Pal::titleBg);
        g.fillRect(0, 0, W, TITLE_H);

        // Watery blue gradient accent strip at the very bottom of the title
        juce::ColourGradient titleLine(
            Pal::blue.withAlpha(0.9f), 0.f, (float)TITLE_H,
            Pal::blueBright.withAlpha(0.5f), (float)W, (float)TITLE_H, false);
        g.setGradientFill(titleLine);
        g.fillRect(0, TITLE_H - 2, W, 2);

        // Plugin name
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
        g.setColour(Pal::blue);
        g.drawText("ALPHA", juce::Rectangle<int>(10, 0, 72, TITLE_H), juce::Justification::centredLeft);
        g.setColour(Pal::blueBright);
        g.drawText("BETA", juce::Rectangle<int>(57, 0, 55, TITLE_H), juce::Justification::centredLeft);

        // Subtitle
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
        g.setColour(Pal::textBright.withAlpha(0.6f));
        g.drawText("Subtractive Synthesizer with a smooth filter inspired by the LinPlug Alpha 3 VST",
            juce::Rectangle<int>(140, 0, 420, TITLE_H), juce::Justification::centredLeft);
    }
}