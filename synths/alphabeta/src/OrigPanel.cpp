#include "PluginEditor.h"
#include "AlphaOrigPreset.h"
#include "OrigPresets.h"
#include "OrigRandomData.h"

// ================================================================
//  EngineSwitch
// ================================================================
EngineSwitch::EngineSwitch(juce::RangedAudioParameter& p, std::function<void(int)> onChange)
    : att(p, [this](float v) {
          value = juce::roundToInt(v);
          repaint();
          if (changed) changed(value);
      }),
      changed(std::move(onChange))
{
    setTooltip("NATIVE: the AlphaBeta engine.  ORIGINAL: a model of the LinPlug FreeAlpha 3.3 "
               "engine, built from measurements of the original plugin. Each engine keeps its own patch.");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    att.sendInitialUpdate();
}

void EngineSwitch::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(Pal::gt(0xFF0B0E14));
    g.fillRoundedRectangle(r, 5.0f);
    const char* labels[2] = { "NATIVE", "ORIGINAL" };
    for (int i = 0; i < 2; ++i) {
        auto seg = r.withWidth(r.getWidth() * 0.5f).translated(r.getWidth() * 0.5f * (float)i, 0.0f).reduced(2.0f);
        const bool on = value == i;
        if (on) {
            juce::ColourGradient body(Pal::blue, seg.getX(), seg.getY(), Pal::blueDark, seg.getX(), seg.getBottom(), false);
            g.setGradientFill(body);
            g.fillRoundedRectangle(seg, 4.0f);
        }
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold)));
        g.setColour(on ? juce::Colours::white : Pal::textBright.withAlpha(0.7f));
        g.drawText(labels[i], seg, juce::Justification::centred, false);
    }
    g.setColour(Pal::blueDark.withAlpha(0.9f));
    g.drawRoundedRectangle(r, 5.0f, 1.2f);
}

void EngineSwitch::mouseDown(const juce::MouseEvent& e) {
    const int v = e.x < getWidth() / 2 ? 0 : 1;
    if (v != value) att.setValueAsCompleteGesture((float)v);
}

// ================================================================
//  OrigPanel
// ================================================================
namespace {
    CrtConfig origMatrixConfig() {
        CrtConfig c;
        c.numSlots = AlphaOrig::kNumRows;
        c.srcID = OrigP::rowSrcID; c.amtID = OrigP::rowAmtID; c.dstID = OrigP::rowDstID;
        c.srcNames.clear(); c.dstNames.clear();
        for (int i = 0; i < AlphaOrig::NUM_SRC; ++i) c.srcNames.add(AlphaOrig::kSrcNames[i]);
        for (int i = 0; i < AlphaOrig::NUM_DST; ++i) c.dstNames.add(AlphaOrig::kDstNames[i]);
        c.srcSeparators = { 1, 3, 7, 9 };
        c.dstSeparators = { 1, 4, 7, 9, 11, 19 };
        c.formatAmount = [](float amt, int d) {
            using namespace AlphaOrig;
            if (d == DST_OSC1_PITCH || d == DST_OSC2_PITCH || d == DST_MAIN_PITCH) {
                const float semis = amt * 0.01f * 48.18f;           // measured: 100 % = 48.18 semitones
                return (semis < 0.0f ? "-" : "+") + juce::String(std::abs(semis), 2) + "st";
            }
            return (amt < 0.0f ? "-" : "+") + juce::String(std::abs(amt), 2);
        };
        c.title = "MATRIX  (ORIGINAL)";
        return c;
    }
}

OrigPanel::OrigPanel(AlphaBetaAudioProcessor& p, int gw, const int* colWidths, int numCols)
    : proc(p), gridW(gw), cw(colWidths, colWidths + numCols), crt(p, origMatrixConfig())
{
    setOpaque(true);
    auto& a = proc.apvts;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    auto combo = [&](juce::ComboBox& cb, const juce::String& id, const char* const* names, int n) {
        for (int i = 0; i < n; ++i) cb.addItem(names[i], i + 1);
        addAndMakeVisible(cb);
        cAtt.push_back(std::make_unique<CA>(a, id, cb));
    };
    auto knob = [&](LKnob& k, const juce::String& id) {
        addAndMakeVisible(k);
        k.slider.setPopupDisplayEnabled(true, true, this, 1200);     // shows the value in the original's units
        sAtt.push_back(std::make_unique<SA>(a, id, k.slider));
        if (auto* prm = a.getParameter(id)) k.slider.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
    };

    for (int o = 0; o < 2; ++o)
        for (int ab = 0; ab < 2; ++ab) {
            combo(wave[o][ab], OrigP::waveID(o, ab), AlphaOrigData::kWaveNames, AlphaOrigData::kNumWaves);
            combo(range[o][ab], OrigP::rangeID(o, ab), AlphaOrig::kRangeNames, 5);
        }
    combo(fType, OrigP::FTYP, AlphaOrig::kFilterNames, 5);
    combo(glideMode, OrigP::GMODE, AlphaOrig::kGlideModeNames, 4);
    combo(voices, OrigP::VOICES, AlphaOrig::kVoicesNames, 13);
    combo(lfoWave, OrigP::LWAVE, AlphaOrig::kLfoWaveNames, 6);
    combo(lfoSync, OrigP::LSYNC, AlphaOrig::kSyncNames, AlphaOrig::kNumSync);

    knob(mix1, OrigP::mixID(0)); knob(mix2, OrigP::mixID(1));
    knob(balance, OrigP::BAL); knob(detune, OrigP::DET); knob(spread, OrigP::SPR); knob(drive, OrigP::DRV);
    knob(cutoff, OrigP::FCUT); knob(reso, OrigP::FRES); knob(envDepth, OrigP::FDEP);
    LKnob* fk[5] = { &fA, &fD, &fS, &fF, &fR };
    LKnob* ak[5] = { &aA, &aD, &aS, &aF, &aR };
    for (int st = 0; st < 5; ++st) { knob(*ak[st], OrigP::envID(0, st)); knob(*fk[st], OrigP::envID(1, st)); }
    knob(vol, OrigP::VOL); knob(vel, OrigP::VEL); knob(glideTime, OrigP::GTIME); knob(glideBend, OrigP::GBEND);
    knob(chWet, OrigP::CWET); knob(chTime, OrigP::CTIM); knob(chRate, OrigP::CRAT);
    knob(lfoRate, OrigP::LRATE); knob(lfoAtt, OrigP::LATT);
    addAndMakeVisible(crt);

    // ---- preset browser (the user's presets, grouped by category) ----
    {
        juce::String lastCat;
        for (int i = 0; i < OrigPresets::kNumPresets; ++i) {
            const auto& e = OrigPresets::kBank[i];
            if (lastCat != e.category) { presetBox.addSectionHeading(e.category); lastCat = e.category; }
            presetBox.addItem(e.name, i + 1);
        }
        presetBox.setTextWhenNothingSelected("- presets -");
        presetBox.setTooltip("Presets for the original engine");
        presetBox.onChange = [this]() { if (presetBox.getSelectedId() > 0) loadBankPreset(presetBox.getSelectedId() - 1); };
        const int last = (int)proc.apvts.state.getProperty("origPresetIndex", -1);
        if (last >= 0 && last < OrigPresets::kNumPresets && proc.getPatchName() == OrigPresets::kBank[last].name)
            presetBox.setSelectedId(last + 1, juce::dontSendNotification);
        prevBtn.onClick = [this]() {
            int id = presetBox.getSelectedId(); id = id <= 1 ? OrigPresets::kNumPresets : id - 1;
            presetBox.setSelectedId(id, juce::sendNotificationSync);
        };
        nextBtn.onClick = [this]() {
            int id = presetBox.getSelectedId(); id = id >= OrigPresets::kNumPresets ? 1 : id + 1;
            presetBox.setSelectedId(id, juce::sendNotificationSync);
        };
        prevBtn.setTooltip("Previous preset"); nextBtn.setTooltip("Next preset");
        addAndMakeVisible(presetBox); addAndMakeVisible(prevBtn); addAndMakeVisible(nextBtn);
    }

    mix1.slider.setTooltip("Osc 1: crossfade from wave A to wave B");
    mix2.slider.setTooltip("Osc 2: crossfade from wave A to wave B");
    balance.slider.setTooltip("Level balance between oscillator 1 and oscillator 2");
    detune.slider.setTooltip("Detunes osc 1 up and osc 2 down");
    spread.slider.setTooltip("Adds detuned copies of each oscillator (4 extra for osc 1, 2 for osc 2)");
    envDepth.slider.setTooltip("Filter envelope amount: negative values close the filter, positive values open it");
    glideBend.slider.setTooltip("Glide mode BEND: each note slides in from this interval");
    fType.setTooltip("LP24+ is the original's 'alternative' 24 dB low-pass");
    lfoSync.setTooltip("Off = free running at Rate, otherwise synced to the host tempo");
}

OrigPanel::~OrigPanel() = default;

juce::Rectangle<int> OrigPanel::cell(int col, int row) const {
    int x = 0;
    for (int c = 0; c < col; ++c) x += cw[(size_t)c];
    const int rowH = getHeight() / 6;
    return { x, row * rowH, cw[(size_t)col], rowH };
}

juce::Rectangle<int> OrigPanel::comboBounds(int col, int row) const {
    auto r = cell(col, row);
    const int top = (row % 2 == 0) ? 28 : 16;
    return { r.getX() + 4, r.getY() + top, r.getWidth() - 8, 22 };
}

void OrigPanel::placeCB(juce::ComboBox& cb, int col, int row) {
    auto r = cell(col, row);
    cb.setBounds(r.withSizeKeepingCentre(r.getWidth() - 8, 22));
}

void OrigPanel::resized() {
    // Oscillators: same places as the native wave / octave choosers
    for (int o = 0; o < 2; ++o)
        for (int ab = 0; ab < 2; ++ab) {
            wave[o][ab].setBounds(comboBounds(0, o * 2 + ab));
            range[o][ab].setBounds(comboBounds(1, o * 2 + ab));
        }
    placeCB(voices, 0, 4);
    placeCB(glideMode, 1, 4);
    mix1.setBounds(cell(2, 0));      balance.setBounds(cell(3, 0));
    detune.setBounds(cell(2, 1));    drive.setBounds(cell(3, 1));
    mix2.setBounds(cell(2, 2));      glideTime.setBounds(cell(3, 2));
    spread.setBounds(cell(2, 3));    glideBend.setBounds(cell(3, 3));
    chWet.setBounds(cell(0, 5));     chTime.setBounds(cell(1, 5));    chRate.setBounds(cell(2, 5));

    // Filter
    cutoff.setBounds(cell(5, 0));    reso.setBounds(cell(6, 0));
    placeCB(fType, 5, 1);            envDepth.setBounds(cell(6, 1));
    fA.setBounds(cell(5, 2));        fD.setBounds(cell(6, 2));
    fS.setBounds(cell(5, 3));        fR.setBounds(cell(6, 3));
    fF.setBounds(cell(5, 4));

    // Amp
    vol.setBounds(cell(8, 0));       vel.setBounds(cell(9, 0));
    aA.setBounds(cell(8, 2));        aD.setBounds(cell(9, 2));
    aS.setBounds(cell(8, 3));        aR.setBounds(cell(9, 3));
    aF.setBounds(cell(8, 4));

    infoBox = cell(5, 5).getUnion(cell(9, 5)).reduced(8, 10);
    {
        auto r = infoBox.reduced(10, 0);
        auto left = r.removeFromLeft(r.getWidth() * 11 / 20);
        left = left.withSizeKeepingCentre(left.getWidth(), 22).translated(0, 6);
        nextBtn.setBounds(left.removeFromRight(26));
        left.removeFromRight(4);
        prevBtn.setBounds(left.removeFromRight(26));
        left.removeFromRight(6);
        presetBox.setBounds(left);
        r.removeFromLeft(12);
        infoText = r;
    }

    // Right panel: matrix on top, the single LFO below
    auto panel = juce::Rectangle<int>(gridW, 0, getWidth() - gridW, getHeight()).reduced(8, 6);
    crt.setBounds(panel.removeFromTop(panel.getHeight() - 172));
    panel.removeFromTop(8);
    lfoBox = panel.reduced(3, 0);
    auto r = lfoBox.reduced(8, 5);
    r.removeFromTop(24);
    auto left = r.removeFromLeft(r.getWidth() / 2 - 4);
    left.removeFromTop(4);
    lfoWave.setBounds(left.removeFromTop(22));
    left.removeFromTop(12);
    lfoSync.setBounds(left.removeFromTop(22));
    r.removeFromLeft(8);
    auto knobs = r.withHeight(juce::jmin(r.getHeight(), r.getWidth() / 2 + 14));
    lfoRate.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2));
    lfoAtt.setBounds(knobs);
}

void OrigPanel::drawComboLabel(juce::Graphics& g, const juce::ComboBox& cb, const juce::String& text) const {
    auto b = cb.getBounds().toFloat();
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
    g.setColour(Pal::textDim);
    g.drawText(text, b.withY(b.getBottom() + 3.0f).withHeight(12.0f), juce::Justification::centredTop, false);
}

void OrigPanel::paint(juce::Graphics& g) {
    const int W = getWidth(), H = getHeight();
    g.fillAll(Pal::bg);

    // filter and amp sections as rounded cards (the preset card sits below them)
    const juce::Rectangle<int> cards[] = {
        cell(5, 0).getUnion(cell(6, 4)),   // filter
        cell(8, 0).getUnion(cell(9, 4)),   // amp
        cell(2, 0).getUnion(cell(3, 3)),   // mixer knobs
        cell(0, 4).getUnion(cell(3, 5)),   // voices / glide / chorus row
    };
    for (auto& r : cards) {
        auto box = r.reduced(3, 5).toFloat();
        g.setColour(Pal::bgDark.withAlpha(0.35f));
        g.fillRoundedRectangle(box, 7.0f);
        g.setColour(Pal::blueDark.withAlpha(0.35f));
        g.drawRoundedRectangle(box.reduced(0.5f), 7.0f, 1.0f);
    }

    // oscillator boxes
    for (int o = 0; o < 2; ++o) {
        auto box = cell(0, o * 2).getUnion(cell(1, o * 2 + 1)).reduced(3, 5).toFloat();
        g.setColour(Pal::bgDark.withAlpha(0.55f));
        g.fillRoundedRectangle(box, 7.0f);
        g.setColour(Pal::blueDark.withAlpha(0.45f));
        g.drawRoundedRectangle(box.reduced(0.5f), 7.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold)));
        g.setColour(Pal::blueDark);
        g.drawText("OSC " + juce::String(o + 1), box.reduced(8.0f, 5.0f).withHeight(14.0f), juce::Justification::centredLeft, false);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain)));
        g.setColour(Pal::textDim);
        g.drawText("WAVE A / B        RANGE", box.reduced(8.0f, 5.0f).withHeight(14.0f), juce::Justification::centredRight, false);
    }
    drawComboLabel(g, voices, "Voices");
    drawComboLabel(g, glideMode, "Glide Mode");
    drawComboLabel(g, fType, "Filter Type");

    // info strip: preset browser + a short note (opaque, it spans a separator column)
    {
        auto b = infoBox.toFloat();
        g.setColour(Pal::bgDark.withAlpha(0.35f));
        g.fillRoundedRectangle(b, 7.0f);
        g.setColour(Pal::blueDark.withAlpha(0.35f));
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold)));
        g.setColour(Pal::blueDark);
        g.drawText("PRESETS", presetBox.getBounds().translated(0, -17).withHeight(14).toFloat(), juce::Justification::centredLeft, false);
        auto t = infoText.toFloat().reduced(0.0f, 6.0f);
        g.drawText("ORIGINAL ENGINE", t.removeFromTop(15.0f), juce::Justification::centredLeft, false);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
        g.setColour(Pal::textDim);
        g.drawFittedText("Model of the LinPlug FreeAlpha 3.3 engine. IMPORT loads .fxp presets exactly.",
                         t.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }

    // right panel LFO box
    {
        auto b = lfoBox.toFloat();
        g.setColour(Pal::bgDark);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(Pal::blueDark.withAlpha(0.35f));
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold)));
        g.setColour(Pal::blueDark);
        g.drawText("LFO 1", b.reduced(8.0f, 5.0f).withHeight(20.0f), juce::Justification::centredLeft, false);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
        g.setColour(Pal::textDim);
        auto w = lfoWave.getBounds().toFloat(), s = lfoSync.getBounds().toFloat();
        g.drawText("Wave", w.withY(w.getBottom() + 1.0f).withHeight(11.0f), juce::Justification::centredTop, false);
        g.drawText("Sync", s.withY(s.getBottom() + 1.0f).withHeight(11.0f), juce::Justification::centredTop, false);
    }
    juce::ignoreUnused(W, H);
}

// ----------------------------------------------------------------
//  Presets and randomizer
// ----------------------------------------------------------------
void OrigPanel::loadBankPreset(int index) {
    if (index < 0 || index >= OrigPresets::kNumPresets) return;
    const auto& e = OrigPresets::kBank[index];
    auto r = AlphaOrig::readFxp(e.data, (size_t)e.size);
    if (!r.ok) { crt.showStatus("PRESET ERROR"); return; }
    OrigP::applyPatch(proc.apvts, r.patch);
    proc.setPatchName(e.name);
    proc.apvts.state.setProperty("origPresetIndex", index, nullptr);
    if (presetBox.getSelectedId() != index + 1) presetBox.setSelectedId(index + 1, juce::dontSendNotification);
    crt.showStatus(juce::String(e.category).toUpperCase() + ": " + juce::String(e.name).toUpperCase());
}

juce::StringArray OrigPanel::randomModeNames() {
    juce::StringArray s{ "Anything" };
    for (int i = 0; i < OrigRandom::kNumCats; ++i) s.add(OrigRandom::kCatNames[i]);
    s.add("Mutate current");
    s.add("Blend two presets");
    return s;
}

namespace {
    // parameter IDs of each randomizer group, in the order of OrigRandomData.h
    struct GroupDef { std::vector<juce::String> ids; std::vector<bool> continuous; };
    const std::vector<GroupDef>& groupDefs() {
        static const std::vector<GroupDef> g = [] {
            using namespace OrigP;
            std::vector<GroupDef> v;
            v.push_back({ { waveID(0,0), rangeID(0,0), waveID(0,1), rangeID(0,1), mixID(0), waveID(1,0), rangeID(1,0), waveID(1,1), rangeID(1,1), mixID(1), BAL, DET, SPR },
                          { false, false, false, false, true, false, false, false, false, true, true, true, true } });
            v.push_back({ { FTYP, FCUT, FRES, FDEP, DRV }, { false, true, true, true, true } });
            v.push_back({ { envID(0,0), envID(0,1), envID(0,2), envID(0,3), envID(0,4), VEL }, { true, true, true, true, true, true } });
            v.push_back({ { envID(1,0), envID(1,1), envID(1,2), envID(1,3), envID(1,4) }, { true, true, true, true, true } });
            v.push_back({ { CWET, CTIM, CRAT }, { true, true, true } });
            v.push_back({ { GMODE, GTIME, GBEND, VOICES }, { false, true, false, false } });
            v.push_back({ { LWAVE, LRATE, LSYNC, LATT }, { false, true, false, true } });
            v.push_back({ { VOL }, { true } });     // + matrix rows
            return v;
        }();
        return g;
    }
    const float* const* poolsFor(int group) {
        static const float* const* p[8] = { OrigRandom::kPools_osc, OrigRandom::kPools_filter, OrigRandom::kPools_aenv, OrigRandom::kPools_fenv,
                                            OrigRandom::kPools_chorus, OrigRandom::kPools_glide, OrigRandom::kPools_lfo, OrigRandom::kPools_matrix };
        return p[group];
    }
    const int* countsFor(int group) {
        static const int* c[8] = { OrigRandom::kCounts_osc, OrigRandom::kCounts_filter, OrigRandom::kCounts_aenv, OrigRandom::kCounts_fenv,
                                   OrigRandom::kCounts_chorus, OrigRandom::kCounts_glide, OrigRandom::kCounts_lfo, OrigRandom::kCounts_matrix };
        return c[group];
    }
    int widthFor(int group) {
        static const int w[8] = { OrigRandom::kW_osc, OrigRandom::kW_filter, OrigRandom::kW_aenv, OrigRandom::kW_fenv,
                                  OrigRandom::kW_chorus, OrigRandom::kW_glide, OrigRandom::kW_lfo, OrigRandom::kW_matrix };
        return w[group];
    }
}

juce::String OrigPanel::randomize(juce::Random& rng, int mode) {
    auto& a = proc.apvts;
    auto set = [&](const juce::String& id, float real) {
        if (auto* prm = a.getParameter(id)) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(real));
            prm->endChangeGesture();
        }
    };
    auto get = [&](const juce::String& id) { return a.getRawParameterValue(id)->load(); };
    presetBox.setSelectedId(0, juce::dontSendNotification);
    static const char* words[] = { "Glass", "Tide", "Velvet", "Quartz", "Haze", "Drift", "Cathode", "Liquid", "Orbit", "Nimbus",
                                   "Amber", "Silk", "Echo", "Frost", "Ember", "Lumen", "Hollow", "Prism", "Dune", "Coral" };
    auto word = [&]() { return juce::String(words[rng.nextInt(20)]); };

    if (mode == 7) {   // mutate the current patch
        for (int gIdx = 0; gIdx < 7; ++gIdx) {
            const auto& gd = groupDefs()[(size_t)gIdx];
            for (size_t k = 0; k < gd.ids.size(); ++k) {
                if (!gd.continuous[k]) continue;
                auto* prm = a.getParameter(gd.ids[k]);
                float n = juce::jlimit(0.0f, 1.0f, prm->getValue() + (rng.nextFloat() - 0.5f) * 0.16f);
                prm->beginChangeGesture(); prm->setValueNotifyingHost(n); prm->endChangeGesture();
            }
        }
        if (rng.nextFloat() < 0.3f) {   // sometimes a neighbouring wave
            int o = rng.nextInt(2), ab = rng.nextInt(2);
            int w = juce::roundToInt(get(OrigP::waveID(o, ab))) + (rng.nextBool() ? 1 : -1);
            set(OrigP::waveID(o, ab), (float)juce::jlimit(0, AlphaOrigData::kNumWaves - 1, w));
        }
        return proc.getPatchName().upToFirstOccurrenceOf(" *", false, false) + " *";
    }

    if (mode == 8) {   // blend two presets of the same category from the bank
        int i1 = 1 + rng.nextInt(OrigPresets::kNumPresets - 1), i2 = i1;
        for (int t = 0; t < 50 && (i2 == i1 || juce::String(OrigPresets::kBank[i2].category) != OrigPresets::kBank[i1].category); ++t)
            i2 = 1 + rng.nextInt(OrigPresets::kNumPresets - 1);
        auto A = AlphaOrig::readFxp(OrigPresets::kBank[i1].data, (size_t)OrigPresets::kBank[i1].size).patch;
        const auto B = AlphaOrig::readFxp(OrigPresets::kBank[i2].data, (size_t)OrigPresets::kBank[i2].size).patch;
        if (rng.nextBool()) { for (int o = 0; o < 2; ++o) for (int ab = 0; ab < 2; ++ab) { A.wave[o][ab] = B.wave[o][ab]; A.range[o][ab] = B.range[o][ab]; }
                              A.mix[0] = B.mix[0]; A.mix[1] = B.mix[1]; A.balance = B.balance; A.detune = B.detune; A.spread = B.spread; }
        if (rng.nextBool()) { A.filterType = B.filterType; A.cutoff = B.cutoff; A.resonance = B.resonance; A.envDepth = B.envDepth; A.drive = B.drive; }
        if (rng.nextBool()) { A.ampEnv = B.ampEnv; A.velSens = B.velSens; }
        if (rng.nextBool()) A.filEnv = B.filEnv;
        if (rng.nextBool()) { A.chorusWet = B.chorusWet; A.chorusMs = B.chorusMs; A.chorusHz = B.chorusHz; }
        if (rng.nextBool()) { A.lfoWave = B.lfoWave; A.lfoHz = B.lfoHz; A.lfoSync = B.lfoSync; A.lfoAttack = B.lfoAttack; }
        OrigP::applyPatch(a, A);
        return juce::String(OrigPresets::kBank[i1].name) + " x " + OrigPresets::kBank[i2].name;
    }

    // categories: every group drawn from the pools of one category, plus a little jitter
    const int cat = (mode >= 1 && mode <= OrigRandom::kNumCats) ? mode - 1 : rng.nextInt(OrigRandom::kNumCats);
    for (int gIdx = 0; gIdx < 8; ++gIdx) {
        const int n = countsFor(gIdx)[cat];
        if (n <= 0) continue;
        const float* row = poolsFor(gIdx)[cat] + (size_t)rng.nextInt(n) * (size_t)widthFor(gIdx);
        const auto& gd = groupDefs()[(size_t)gIdx];
        for (size_t k = 0; k < gd.ids.size(); ++k) {
            float v = row[k];
            if (gd.continuous[k] && gd.ids[k] != OrigP::VOL) {
                auto* prm = a.getParameter(gd.ids[k]);
                const float n01 = juce::jlimit(0.0f, 1.0f, prm->convertTo0to1(v) + (rng.nextFloat() - 0.5f) * 0.06f);
                v = prm->convertFrom0to1(n01);
            }
            set(gd.ids[k], v);
        }
        if (gIdx == 7) {   // matrix rows follow the volume in the matrix pool
            for (int r = 0; r < AlphaOrig::kNumRows; ++r) {
                set(OrigP::rowSrcID(r), row[1 + 3 * r]);
                set(OrigP::rowAmtID(r), row[2 + 3 * r] * 100.0f);
                set(OrigP::rowDstID(r), row[3 + 3 * r]);
            }
        }
    }
    static const char* kinds[] = { "Bass", "Keys", "Lead", "Pad", "FX", "Drum" };
    const juce::String w1 = word(); juce::String w2 = word();
    while (w2 == w1) w2 = word();
    return w1 + " " + w2 + " " + kinds[cat];
}

AlphaOrig::Patch OrigPanel::blendPatches(AlphaOrig::Patch A, const AlphaOrig::Patch& B, juce::Random& rng) {
    if (rng.nextBool()) { for (int o = 0; o < 2; ++o) for (int ab = 0; ab < 2; ++ab) { A.wave[o][ab] = B.wave[o][ab]; A.range[o][ab] = B.range[o][ab]; }
                          A.mix[0] = B.mix[0]; A.mix[1] = B.mix[1]; A.balance = B.balance; A.detune = B.detune; A.spread = B.spread; }
    if (rng.nextBool()) { A.filterType = B.filterType; A.cutoff = B.cutoff; A.resonance = B.resonance; A.envDepth = B.envDepth; A.drive = B.drive; }
    if (rng.nextBool()) { A.ampEnv = B.ampEnv; A.velSens = B.velSens; }
    if (rng.nextBool()) A.filEnv = B.filEnv;
    if (rng.nextBool()) { A.chorusWet = B.chorusWet; A.chorusMs = B.chorusMs; A.chorusHz = B.chorusHz; }
    if (rng.nextBool()) { A.lfoWave = B.lfoWave; A.lfoHz = B.lfoHz; A.lfoSync = B.lfoSync; A.lfoAttack = B.lfoAttack; }
    if (rng.nextBool()) { A.glideMode = B.glideMode; A.glideTime = B.glideTime; A.glideBend = B.glideBend; A.voices = B.voices; }
    if (rng.nextBool()) { A.rows = B.rows; A.volume = B.volume; }       // the matrix travels with its volume
    return A;
}
