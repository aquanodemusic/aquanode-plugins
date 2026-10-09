/*
    PluginEditor.h  -  83ChorusVerb UI.

    Same look as VirtualDX7's FX page - amber-on-espresso panels, captioned
    rotary knobs - just the four effect strips on their own, with nothing
    else around them.

    GPLv3.
*/
#pragma once
#include <JuceHeader.h>

#include <vector>
#include <memory>
#include "PluginProcessor.h"

namespace cv83ui {

// ============================================================================
//  Palette
// ============================================================================
namespace col {
    const juce::Colour bg        (0xff211d18);   // deep espresso
    const juce::Colour panel     (0xff2f2a22);   // panel brown
    const juce::Colour panelHi   (0xff3a342a);
    const juce::Colour edge      (0xff100d0a);
    const juce::Colour text      (0xffe8dcc8);   // warm parchment
    const juce::Colour textDim   (0xff9a8f7c);
    const juce::Colour accent    (0xffe0912f);   // amber
    const juce::Colour accentDim (0xff7a5320);
}

// ============================================================================
//  Look and feel
// ============================================================================
class DXLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DXLookAndFeel() {
        setColour (juce::Slider::textBoxTextColourId,    col::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,            col::text);
        setColour (juce::ComboBox::backgroundColourId,   col::panelHi);
        setColour (juce::ComboBox::textColourId,         col::text);
        setColour (juce::ComboBox::outlineColourId,      col::edge);
        setColour (juce::ComboBox::arrowColourId,        col::accent);
        setColour (juce::PopupMenu::backgroundColourId,  col::panel);
        setColour (juce::PopupMenu::textColourId,        col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accentDim);
        setColour (juce::TextButton::buttonColourId,     col::panelHi);
        setColour (juce::TextButton::textColourOnId,     col::text);
        setColour (juce::TextButton::textColourOffId,    col::text);
        setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
        setColour (juce::TooltipWindow::textColourId,       col::text);
        setColour (juce::TooltipWindow::outlineColourId,    col::accentDim);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        // Reserve the top/bottom text bands so the knob never sits under a label.
        auto b = juce::Rectangle<float> ((float)x,(float)y,(float)w,(float)h).reduced (6.0f, 14.0f);
        auto r  = juce::jmax (6.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.5f);
        auto cx = b.getCentreX(), cy = b.getCentreY();
        auto angle = startAngle + pos * (endAngle - startAngle);

        g.setColour (col::edge);
        g.fillEllipse (cx - r, cy - r, r*2, r*2);
        g.setColour (s.hasKeyboardFocus (false) ? col::panel.brighter (0.15f) : col::panelHi);
        g.fillEllipse (cx - r*0.86f, cy - r*0.86f, r*1.72f, r*1.72f);

        // focus ring for keyboard users
        if (s.hasKeyboardFocus (false)) {
            g.setColour (col::accent.withAlpha (0.6f));
            g.drawEllipse (cx - r, cy - r, r*2, r*2, 1.4f);
        }

        juce::Path arc;
        arc.addCentredArc (cx, cy, r*0.98f, r*0.98f, 0.0f, startAngle, angle, true);
        g.setColour (col::accent);
        g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        juce::Path p;
        p.addRoundedRectangle (-1.4f, -r*0.82f, 2.8f, r*0.5f, 1.4f);
        g.setColour (col::text);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (cx, cy));
    }

    juce::Font getLabelFont (juce::Label&) override {
        return juce::Font (juce::FontOptions (12.0f));
    }
};

// ============================================================================
//  FxKnob  -  a captioned rotary bound directly to one RangedAudioParameter,
//  showing the parameter's own current-value text (through
//  getCurrentValueAsText()/getLabel()) so a log-skewed frequency knob reads
//  correctly without the UI knowing which units it is showing.
// ============================================================================
class FxKnob : public juce::Component {
public:
    FxKnob (const juce::String& caption, juce::RangedAudioParameter& p)
        : param (p), caption_ (caption)
    {
        const auto range = p.getNormalisableRange();
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setNormalisableRange ({ (double) range.start, (double) range.end,
                                       (double) range.interval, (double) range.skew,
                                       range.symmetricSkew });
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setDoubleClickReturnValue (true, (double) range.convertFrom0to1 (p.getDefaultValue()));
        slider.setWantsKeyboardFocus (true);

        slider.setTitle (p.getName (64));
        slider.setName  (p.getName (64));
        slider.setTooltip (p.getName (64));

        slider.onValueChange = [this] { repaint(); };   // refresh the printed value
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::SliderParameterAttachment> (p, slider);
        setInterceptsMouseClicks (false, true);
    }

    void resized() override { slider.setBounds (getLocalBounds()); }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (14);
        auto bot = b.removeFromBottom (14);
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (caption_, top, juce::Justification::centred);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (valueText(), bot, juce::Justification::centred);
    }

private:
    juce::String valueText() const {
        auto txt = param.getCurrentValueAsText();
        auto unit = param.getLabel();
        return unit.isEmpty() ? txt : txt + " " + unit;
    }

    juce::RangedAudioParameter& param;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::String caption_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxKnob)
};

// ============================================================================
//  FxUnitPanel  -  one effect: header, on/off switch, and its knobs.
//
//  Painted panel-brown with an amber header band and a dark edge - the same
//  tile styling as the rest of VirtualDX7's UI.
// ============================================================================
class FxUnitPanel : public juce::Component {
public:
    // `cols` controls how the knob/choice/toggle tiles are gridded - the
    // default two-per-row matches Chorus/Delay/Phaser/Reverb.
    FxUnitPanel (const juce::String& titleText,
                 juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& enableParamId,
                 int cols = 2)
        : title_ (titleText), cols_ (juce::jmax (1, cols))
    {
        enableBtn.setButtonText ("OFF");
        enableBtn.setClickingTogglesState (true);
        enableBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
        enableBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        enableBtn.setTitle (titleText + " enable");
        enableBtn.setTooltip ("Switch the " + titleText.toLowerCase() + " in and out");
        enableBtn.onStateChange = [this] {
            enableBtn.setButtonText (enableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible (enableBtn);

        if (auto* p = apvts.getParameter (enableParamId))
            enableAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p, enableBtn);
    }

    // Adds a continuous knob bound to `paramId`.
    void addKnob (const juce::String& caption,
                  juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& paramId)
    {
        if (auto* p = apvts.getParameter (paramId)) {
            auto k = std::make_unique<FxKnob> (caption, *p);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
    }

    // Adds a captioned combo box (the phaser's stage count) in a knob-sized
    // tile, so it lines up with the rotaries around it.
    void addChoice (const juce::String& caption,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr)
            return;

        auto tile = std::make_unique<ChoiceTile> (caption, *p);
        addAndMakeVisible (*tile);
        choices.push_back (std::move (tile));
    }

    // Adds a second toggle inside the panel (the reverb's Freeze).
    void addToggle (const juce::String& caption,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr)
            return;

        auto tile = std::make_unique<ToggleTile> (caption, *p);
        addAndMakeVisible (*tile);
        toggles.push_back (std::move (tile));
    }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto hdr = b.removeFromTop (22);
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (hdr.toFloat().reduced (3, 3), 4.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (title_, hdr, juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (6);

        enableBtn.setBounds (b.removeFromTop (30).reduced (12, 2));
        b.removeFromTop (6);

        // Every control gets the same tile size, laid out two per row, so the
        // panels line up with each other however many knobs each has.
        std::vector<juce::Component*> tiles;
        for (auto& k : knobs)   tiles.push_back (k.get());
        for (auto& c : choices) tiles.push_back (c.get());
        for (auto& t : toggles) tiles.push_back (t.get());
        if (tiles.empty())
            return;

        const int cols = cols_;
        const int rows = ((int) tiles.size() + cols - 1) / cols;
        const int cw = b.getWidth() / cols;
        const int ch = b.getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < (int) tiles.size(); ++i) {
            const int r = i / cols, c = i % cols;
            tiles[(size_t) i]->setBounds (b.getX() + c*cw, b.getY() + r*ch, cw, ch);
        }
    }

private:
    // A combo box wearing a knob tile's caption band, so mixed rows stay tidy.
    struct ChoiceTile : public juce::Component {
        ChoiceTile (const juce::String& caption, juce::RangedAudioParameter& p)
            : caption_ (caption)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&p))
                box.addItemList (choice->choices, 1);
            box.setTitle (p.getName (64));
            box.setTooltip (p.getName (64));
            addAndMakeVisible (box);
            attachment = std::make_unique<juce::ComboBoxParameterAttachment> (p, box);
        }
        void paint (juce::Graphics& g) override {
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (caption_, getLocalBounds().removeFromTop (14),
                        juce::Justification::centred);
        }
        void resized() override {
            auto b = getLocalBounds();
            b.removeFromTop (14);
            b.removeFromBottom (14);
            box.setBounds (b.reduced (6, juce::jmax (0, (b.getHeight() - 28) / 2)));
        }
        juce::String caption_;
        juce::ComboBox box;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
    };

    // Same idea for a boolean (the reverb's Freeze).
    struct ToggleTile : public juce::Component {
        ToggleTile (const juce::String& caption, juce::RangedAudioParameter& p)
            : caption_ (caption)
        {
            btn.setButtonText ("OFF");
            btn.setClickingTogglesState (true);
            btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
            btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.setTitle (p.getName (64));
            btn.setTooltip (p.getName (64));
            btn.onStateChange = [this] {
                btn.setButtonText (btn.getToggleState() ? "ON" : "OFF");
            };
            addAndMakeVisible (btn);
            attachment = std::make_unique<juce::ButtonParameterAttachment> (p, btn);
        }
        void paint (juce::Graphics& g) override {
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (caption_, getLocalBounds().removeFromTop (14),
                        juce::Justification::centred);
        }
        void resized() override {
            auto b = getLocalBounds();
            b.removeFromTop (14);
            b.removeFromBottom (14);
            btn.setBounds (b.reduced (6, juce::jmax (0, (b.getHeight() - 28) / 2)));
        }
        juce::String caption_;
        juce::TextButton btn;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    juce::String title_;
    int cols_ = 2;
    juce::TextButton enableBtn;
    std::unique_ptr<juce::ButtonParameterAttachment> enableAttachment;
    std::vector<std::unique_ptr<FxKnob>>     knobs;
    std::vector<std::unique_ptr<ChoiceTile>> choices;
    std::vector<std::unique_ptr<ToggleTile>> toggles;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxUnitPanel)
};

// ============================================================================
//  FxPanel  -  the whole plugin window: four effect strips side by side, in
//  the order they run - Chorus -> Delay -> Phaser -> Reverb - plus a small
//  signal-flow caption underneath.
// ============================================================================
class FxPanel : public juce::Component {
public:
    explicit FxPanel (juce::AudioProcessorValueTreeState& apvts) {
        using namespace cv83::ids;

        auto* chorus = addUnit ("CHORUS", apvts, chorusOn);
        chorus->addKnob ("RATE",   apvts, chorusRate);
        chorus->addKnob ("DEPTH",  apvts, chorusDepth);
        chorus->addKnob ("DELAY",  apvts, chorusDelay);
        chorus->addKnob ("SPREAD", apvts, chorusSpread);
        chorus->addKnob ("MIX",    apvts, chorusMix);

        auto* delay = addUnit ("DELAY", apvts, delayOn);
        delay->addKnob ("TIME L", apvts, delayTimeL);
        delay->addKnob ("TIME R", apvts, delayTimeR);
        delay->addKnob ("FBK",    apvts, delayFb);
        delay->addKnob ("HPF",    apvts, delayHp);
        delay->addKnob ("MIX",    apvts, delayMix);

        auto* phaser = addUnit ("PHASER", apvts, phaserOn);
        phaser->addKnob   ("RATE",   apvts, phaserRate);
        phaser->addKnob   ("DEPTH",  apvts, phaserDepth);
        phaser->addKnob   ("CENTER", apvts, phaserCentre);
        phaser->addKnob   ("FBK",    apvts, phaserFb);
        phaser->addKnob   ("MIX",    apvts, phaserMix);
        phaser->addChoice ("STAGES", apvts, phaserStages);

        auto* reverb = addUnit ("REVERB", apvts, reverbOn);
        reverb->addKnob   ("SIZE",   apvts, reverbSize);
        reverb->addKnob   ("FBK",    apvts, reverbFb);
        reverb->addKnob   ("DAMP",   apvts, reverbDamp);
        reverb->addKnob   ("MOD HZ", apvts, reverbRate);
        reverb->addKnob   ("MOD DP", apvts, reverbDepth);
        reverb->addKnob   ("MIX",    apvts, reverbMix);
        reverb->addToggle ("FREEZE", apvts, reverbFreeze);
    }

    void paint (juce::Graphics& g) override {
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("IN  >  CHORUS  >  DELAY  >  PHASER  >  REVERB  >  OUT",
                    getLocalBounds().removeFromBottom (16),
                    juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromBottom (18);   // signal-flow caption

        const int gap = 8;
        const int n = (int) units.size();
        if (n == 0)
            return;

        // Four equal-width strips; the last absorbs any rounding remainder so
        // the row still fills exactly to the right edge.
        const int usableW = b.getWidth() - gap * (n - 1);
        const int w = usableW / n;

        int x = b.getX();
        for (int i = 0; i < n; ++i) {
            const bool last = (i == n - 1);
            const int ww = last ? (b.getRight() - x) : w;
            units[(size_t) i]->setBounds (x, b.getY(), ww, b.getHeight());
            x += ww + gap;
        }
    }

private:
    FxUnitPanel* addUnit (const juce::String& name,
                          juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& enableId)
    {
        auto u = std::make_unique<FxUnitPanel> (name, apvts, enableId);
        addAndMakeVisible (*u);
        auto* raw = u.get();
        units.push_back (std::move (u));
        return raw;
    }

    std::vector<std::unique_ptr<FxUnitPanel>> units;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

} // namespace cv83ui

// ============================================================================
//  juce::AudioProcessorEditor
// ============================================================================
class ChorusVerbAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit ChorusVerbAudioProcessorEditor (ChorusVerbAudioProcessor&);
    ~ChorusVerbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChorusVerbAudioProcessor& processor;
    cv83ui::DXLookAndFeel lnf;
    std::unique_ptr<cv83ui::FxPanel> fxPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusVerbAudioProcessorEditor)
};
