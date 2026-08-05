#include "PluginEditor.h"

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
    // Reserve 13px at the bottom for the label drawn by LKnob::paint
    auto bounds = juce::Rectangle<float>((float)x, (float)y,
        (float)width, (float)(height - 13)).reduced(8.0f);
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

void LKnob::resized() {
    slider.setBounds(getLocalBounds().withTrimmedBottom(13));
}

void LKnob::paint(juce::Graphics& g) {
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
    g.setColour(Pal::textDim);
    g.drawText(labelText,
        getLocalBounds().removeFromBottom(13),
        juce::Justification::centred, false);
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
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&laf);

    // ---- Populate combo boxes ----
    auto fillWave = [](juce::ComboBox& cb) {
        cb.addItem("Sine", 1);
        cb.addItem("Tri", 2);
        cb.addItem("Saw", 3);
        cb.addItem("Square", 4);
        cb.addItem("Noise", 5);
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

    // ---- Randomizer button ----
    randomBtn.setButtonText("Randomize");
    randomBtn.setLookAndFeel(&laf);
    addAndMakeVisible(randomBtn);

    // IDs excluded from randomisation
    const juce::StringArray skipIDs{
        PID::FRES, PID::AVOL, PID::HWET, PID::HTIM, PID::HRAT, PID::GLID
    };

    randomBtn.onClick = [this, skipIDs]() {
        juce::Random rng;
        auto& state = proc.apvts;
        for (auto* param : state.processor.getParameters()) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(param);
            if (p == nullptr) continue;
            if (skipIDs.contains(p->getParameterID())) continue;
            // Generate a random normalised value and set it
            float norm = rng.nextFloat();
            p->setValueNotifyingHost(norm);
        }
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

    // ---- Save / Load buttons ----
    saveBtn.setLookAndFeel(&laf);
    loadBtn.setLookAndFeel(&laf);
    addAndMakeVisible(saveBtn);
    addAndMakeVisible(loadBtn);

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
                syncAllUIFromAPVTS();
            });
        };
    setSize(804, 586);
}

AlphaBetaAudioProcessorEditor::~AlphaBetaAudioProcessorEditor() {
    saveBtn.setLookAndFeel(nullptr);
    loadBtn.setLookAndFeel(nullptr);
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

    // ---- Randomizer button: row 4, col 0 ----
    {
        auto r = cell(0, 4);
        randomBtn.setBounds(r.withSizeKeepingCentre(r.getWidth() - 12, 32));
    }

    // ---- Col 0: OSC waveform combos / hWet ----
    placeCB(o1WaveA, 0, 0);
    placeCB(o1WaveB, 0, 1);
    placeCB(o2WaveA, 0, 2);
    placeCB(o2WaveB, 0, 3);
    hWet.setBounds(knobArea(0, 5));

    // ---- Col 1: OSC octave combos / hTime ----
    placeCB(o1OctA, 1, 0);
    placeCB(o1OctB, 1, 1);
    placeCB(o2OctA, 1, 2);
    placeCB(o2OctB, 1, 3);
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
        g.drawText("Simple Synthesizer with a smooth filter inspired by the old LinPlug Alpha 3 VST",
            juce::Rectangle<int>(140, 0, 420, TITLE_H), juce::Justification::centredLeft);
    }
}