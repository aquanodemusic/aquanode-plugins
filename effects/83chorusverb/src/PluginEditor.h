/*
    PluginEditor.h  -  83ChorusVerb UI.

    The very same FX page VirtualDX7 has - amber-on-espresso effect units,
    captioned rotary knobs in rows of three, and under them each effect's
    phosphor-green scope - just the four effect units on their own, with
    nothing else around them. The classes below (the scopes, FxKnob,
    FxUnitPanel) are VirtualDX7's, with only the namespace and the
    parameter IDs changed, so the two plugins look and read alike.

    The window is laid out once at a fixed design size and then scaled as a
    whole to the window's size (as VirtualDX7 does), so knobs, type and scopes
    all grow and shrink together.

    GPLv3.
*/
#pragma once
#include <JuceHeader.h>

#include <vector>
#include <memory>
#include <algorithm>
#include <array>
#include <atomic>
#include <complex>
#include <functional>
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
    const juce::Colour lcdBg     (0xff1a462d);   // backlit green, at the glass's edges
    const juce::Colour lcdLit    (0xff23583a);   // ... and its brighter middle
    const juce::Colour lcdOn     (0xff7dffae);   // phosphor green
}

// The green glass of the LCD and the scopes, lit from behind like a real
// backlit display: a little brighter in the middle, falling off to the edges.
inline void fillBacklitGlass (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius)
{
    const auto c = r.getCentre();
    juce::ColourGradient light (col::lcdLit, c.x, c.y,
                                col::lcdBg, r.getX(), r.getY(), true);
    g.setGradientFill (light);
    g.fillRoundedRectangle (r, cornerRadius);
}

// ============================================================================
//  drawGlass  -  the phosphor-green "glass" the LCD, the scopes and the
//  readouts all share: a dark edge, the green-black glass, and a small dim
//  caption in its top-left corner. Returns the area left for content.
// ============================================================================
inline juce::Rectangle<float> drawGlass (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& caption)
{
    g.setColour (col::edge);
    g.fillRoundedRectangle (r, 5.0f);
    auto in = r.reduced (2.5f);
    fillBacklitGlass (g, in, 3.5f);
    g.setColour (col::lcdOn.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (caption, in.reduced (6.0f, 2.0f).removeFromTop (10.0f), juce::Justification::centredLeft, false);
    return in.reduced (6.0f, 2.0f).withTrimmedTop (11.0f);
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
//  FxKnob  -  a captioned rotary bound straight to one APVTS parameter.
//
//  It takes its range from the parameter, so a log-skewed frequency knob
//  moves correctly without the UI knowing which units it is showing, and
//  prints the value rounded for reading (see formatted()). The whole tile is
//  grabbable; the caption and value are painted behind the knob.
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

    // Optional: the value the effect really uses, when something else caps
    // it (the delay's Feedback without Self-Feedback). The knob still turns
    // through its whole range; the printed value is the one that is heard.
    std::function<float (float)> shownValue;

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

    // Rounded for reading, not for storing: the parameter keeps its full
    // precision. Two decimals below 10, one below 100, none above - never
    // finer than the knob's own step (a 0.1 ms knob reads "7.0 ms"); percent
    // as whole percents; kHz and seconds once Hz and ms reach four digits.
    static juce::String formatted (float v, const juce::String& unit, float step = 0.0f) {
        if (unit == "%")                  return juce::String (juce::roundToInt (v)) + " %";
        if (unit == "Hz" && v >= 999.5f)  return formatted (v * 0.001f, "kHz", step * 0.001f);
        if (unit == "ms" && v >= 999.5f)  return formatted (v * 0.001f, "s",   step * 0.001f);
        const float a = std::abs (v);
        int decimals = a >= 99.95f ? 0 : a >= 9.995f ? 1 : 2;   // judged after rounding: 99.99 reads "100"
        if (step > 0.0f)
            decimals = juce::jmin (decimals, juce::jmax (0, (int) std::ceil (-std::log10 (step) - 1.0e-4f)));
        const auto num = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return unit.isEmpty() ? num : num + " " + unit;
    }

private:
    juce::String valueText() const {
        const auto  unit = param.getLabel();
        const float raw  = param.convertFrom0to1 (param.getValue());
        const float v    = shownValue ? shownValue (raw) : raw;
        if (dynamic_cast<const juce::AudioParameterFloat*> (&param) != nullptr)
            return formatted (v, unit, param.getNormalisableRange().interval);

        const auto txt = param.getCurrentValueAsText();   // whole numbers and choices
        return unit.isEmpty() ? txt : txt + " " + unit;
    }

    juce::RangedAudioParameter& param;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::String caption_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxKnob)
};

// ============================================================================
//  FX scopes  -  the green glass under each effect's knobs.
//
//  Each one watches its unit's parameters (read from the APVTS atomics on a
//  slow timer, repainting only when something moved) and dims while its
//  unit is switched off, like the LFO scope does when the LFO touches
//  nothing. Chorus and Phaser are drawn from the effect's formulas; Delay and
//  Reverb actually run a private copy of the effect (cv83::Delay /
//  cv83::Reverb, the same classes the audio thread uses) on a single click
//  and draw what comes out, on a background thread so a turning knob never
//  waits for it.
// ============================================================================
inline juce::String fxNumber (float v, int decimals) {
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}
inline juce::String fxHz (float hz) {
    if (hz >= 1000.0f) return fxNumber (hz * 0.001f, hz < 10000.0f ? 2 : 1) + " kHz";
    return fxNumber (hz, hz < 10.0f ? 2 : hz < 100.0f ? 1 : 0) + " Hz";
}
inline juce::String fxSeconds (float s) {
    if (s < 1.0f) return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
    return fxNumber (s, s < 10.0f ? 2 : 1) + " s";
}
// Text in a glass's top-right corner, opposite its caption.
inline void drawGlassCorner (juce::Graphics& g, juce::Rectangle<float> glass, const juce::String& text) {
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (col::lcdOn.withAlpha (0.8f));
    g.drawText (text, glass.reduced (2.5f).reduced (6.0f, 2.0f).removeFromTop (10.0f),
                juce::Justification::centredRight, false);
}

class FxScope : public juce::Component, private juce::Timer {
public:
    FxScope (juce::AudioProcessorValueTreeState& apvts, const char* enableId,
             std::initializer_list<const char*> paramIds)
        : processor_ (apvts.processor), enable_ (apvts.getRawParameterValue (enableId))
    {
        for (auto* id : paramIds)
            params_.push_back (apvts.getRawParameterValue (id));
        values_.assign (params_.size(), 0.0f);
        readValues();                     // real values from the start: paint may come before the first tick
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }
    ~FxScope() override { stopTimer(); }

protected:
    float value (size_t i) const { return i < values_.size() ? values_[i] : 0.0f; }
    bool  unitOn() const { return on_; }
    float lit (float alpha) const { return on_ ? alpha : alpha * 0.4f; }   // dim while bypassed
    double hostRate() const { return rate_; }       // the rate the effects really run at

    virtual void valuesChanged() {}               // message thread, just before the repaint
    virtual bool collectResults() { return false; }   // true: something new to draw

private:
    // Copies the parameters in; true if any of them moved. A value that
    // isn't a number (never expected) is taken as 0, so nothing downstream
    // ever draws at a NaN coordinate.
    bool readValues() {
        bool changed = false;
        const double sr = processor_.getSampleRate();
        const double rate = sr > 0.0 ? sr : 48000.0;   // before the host has prepared us
        if (! juce::exactlyEqual (rate, rate_)) { rate_ = rate; changed = true; }
        const bool on = enable_ != nullptr && enable_->load (std::memory_order_relaxed) >= 0.5f;
        if (on != on_) { on_ = on; changed = true; }
        for (size_t i = 0; i < params_.size(); ++i) {
            float v = params_[i] != nullptr ? params_[i]->load (std::memory_order_relaxed) : 0.0f;
            if (! std::isfinite (v)) v = 0.0f;
            if (! juce::exactlyEqual (v, values_[i])) { values_[i] = v; changed = true; }
        }
        return changed;
    }

    void timerCallback() override {
        if (! isShowing()) return;
        const bool changed = readValues() || first_;
        first_ = false;
        if (changed) valuesChanged();
        if (collectResults() || changed) repaint();
    }

    juce::AudioProcessor& processor_;
    std::atomic<float>* enable_ = nullptr;
    std::vector<std::atomic<float>*> params_;
    std::vector<float> values_;
    double rate_ = 0.0;
    bool on_ = false, first_ = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxScope)
};

// A small background thread for the scopes that render audio to draw
// themselves. Only the newest request matters: asking again replaces the
// queued job, and a running job polls `stale()` so it can give up as soon as
// a newer one is waiting.
class ScopeWorker : private juce::Thread {
public:
    using Job = std::function<void (const std::function<bool()>& stale)>;

    explicit ScopeWorker (const juce::String& name) : juce::Thread (name) {
        startThread (juce::Thread::Priority::low);
    }
    ~ScopeWorker() override {
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    void request (Job job) {
        {
            const juce::ScopedLock sl (lock_);
            pending_ = std::move (job);
            ++generation_;
        }
        notify();
    }

private:
    void run() override {
        while (! threadShouldExit()) {
            Job job;
            int gen = 0;
            {
                const juce::ScopedLock sl (lock_);
                std::swap (job, pending_);
                gen = generation_.load();
            }
            if (! job) { wait (-1); continue; }
            job ([this, gen] { return threadShouldExit() || generation_.load() != gen; });
        }
    }

    juce::CriticalSection lock_;
    Job pending_;
    std::atomic<int> generation_ { 0 };
};

// ---------------------------------------------------------------------------
//  Chorus: the delay time each channel's LFO sweeps, over two LFO cycles, on
//  a log scale of 0.5..40 ms so Delay moves the band, Depth widens it and
//  Spread slides the right channel (the dimmer trace) out of phase. With
//  Unison, each voice gets its own trace, spread evenly round the cycle. The
//  bottom line gives the swept range and the detune it causes, which is what
//  the ear hears: a delay that changes over time bends the pitch.
// ---------------------------------------------------------------------------
class ChorusScope : public FxScope {
public:
    explicit ChorusScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, cv83::ids::chorusOn, { cv83::ids::chorusRate, cv83::ids::chorusDepth,
                                                cv83::ids::chorusDelay, cv83::ids::chorusSpread,
                                                cv83::ids::chorusVoices })
    {
        setTitle ("Chorus delay sweep");
    }

    void paint (juce::Graphics& g) override {
        const float rate   = juce::jmax (0.001f, value (0));
        const float depth  = value (1) * 0.01f;
        const float baseMs = value (2);
        const float spread = value (3) * 0.01f;
        const int   voices = juce::jlimit (1, cv83::Chorus::maxVoices, juce::roundToInt (value (4)));

        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Chorus  L / R");
        drawGlassCorner (g, r, (voices > 1 ? juce::String (voices) + " voices  " : juce::String()) + fxHz (rate));
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);

        constexpr float lo = 0.5f, hi = 40.0f;
        auto yFor = [&] (float ms) {
            const float u = std::log (juce::jlimit (lo, hi, ms) / lo) / std::log (hi / lo);
            return plot.getBottom() - u * plot.getHeight();
        };

        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (int ms : { 1, 2, 5, 10, 20 }) {
            const float y = yFor ((float) ms);
            g.setColour (col::lcdOn.withAlpha (0.07f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (col::lcdOn.withAlpha (0.32f));
            g.drawText (juce::String (ms) + (ms == 20 ? " ms" : ""),
                        juce::Rectangle<float> (plot.getX(), y - 9.0f, 30.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }

        {   // the base delay, which the sweep is centred on
            juce::Path line, dashed;
            line.startNewSubPath (plot.getX(), yFor (baseMs));
            line.lineTo (plot.getRight(), yFor (baseMs));
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (col::lcdOn.withAlpha (lit (0.25f)));
            g.fillPath (dashed);
        }

        auto delayAt = [&] (double cycles) {
            const float lfo = (float) std::sin (cycles * juce::MathConstants<double>::twoPi);
            return juce::jmax (0.5f, baseMs * (1.0f + 0.9f * depth * lfo));   // FxChain's Chorus
        };
        constexpr float kCycles = 2.0f;
        const int n = juce::jmax (2, (int) plot.getWidth() * 2);
        auto trace = [&] (double phase) {
            juce::Path p;
            for (int i = 0; i <= n; ++i) {
                const float u = (float) i / (float) n;
                const float x = plot.getX() + u * plot.getWidth(), y = yFor (delayAt (u * kCycles + phase));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        // Right channel first (dimmer, up to 180 degrees behind), then the left
        // on top; each unison voice a fraction of a cycle further on.
        for (int v = voices - 1; v >= 0; --v) {
            const double at = (double) v / (double) voices;
            g.setColour (col::lcdOn.withAlpha (lit (v == 0 ? 0.45f : 0.3f)));
            g.strokePath (trace (at + spread * 0.5), juce::PathStrokeType (1.1f));
        }
        for (int v = voices - 1; v >= 0; --v) {
            const auto left = trace ((double) v / (double) voices);
            g.setColour (col::lcdOn.withAlpha (lit (0.12f)));
            g.strokePath (left, juce::PathStrokeType (3.5f));
            g.setColour (col::lcdOn.withAlpha (lit (v == 0 ? 1.0f : 0.7f)));
            g.strokePath (left, juce::PathStrokeType (1.3f));
        }

        // Swept range, and the pitch wobble: the fastest the delay changes is
        // base * 0.9 * depth * 2 pi * rate (seconds per second), and a delay
        // shrinking at s seconds per second plays back 1 + s times faster.
        const float minMs  = juce::jmax (0.5f, baseMs * (1.0f - 0.9f * depth));
        const float maxMs  = baseMs * (1.0f + 0.9f * depth);
        const float slope  = baseMs * 0.001f * 0.9f * depth * juce::MathConstants<float>::twoPi * rate;
        const float cents  = 1200.0f * std::log2 (1.0f + slope);
        const juce::String pm (juce::CharPointer_UTF8 ("\xc2\xb1"));
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText (depth <= 0.0f ? fxNumber (baseMs, 1) + " ms"
                                  : fxNumber (minMs, 1) + dash + fxNumber (maxMs, 1) + " ms",
                    bottom, juce::Justification::centredLeft, false);
        g.drawText (cents < 0.5f ? juce::String ("no detune")
                                 : cents < 100.0f ? "detune " + pm + juce::String (juce::roundToInt (cents)) + " ct"
                                                  : "detune " + pm + fxNumber (cents / 100.0f, 1) + " st",
                    bottom, juce::Justification::centredRight, false);
    }
};

// ---------------------------------------------------------------------------
//  Delay: the echoes of a single click, left channel above the line and
//  right below, on a 48 dB scale. Rendered by a private cv83::Delay at the
//  host's sample rate, so feedback, the high-pass in the loop (each repeat a
//  little thinner) and a runaway look exactly like they sound. The first bar
//  is the dry click, so Mix is the balance between it and the echoes. The
//  corner counts the repeats until the echoes are 60 dB down, worked out
//  from the loop's gain at its least damped frequency - "runaway" when that
//  gain is 1 or more. That takes Allow Self-Feedback (without it Feedback
//  stops at 1.00, where the high-pass keeps the loop just below 1), and then
//  happens a little above a Feedback of 1 rather than exactly at it.
// ---------------------------------------------------------------------------
class DelayScope : public FxScope {
public:
    explicit DelayScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, cv83::ids::delayOn, { cv83::ids::delayTimeL, cv83::ids::delayTimeR,
                                               cv83::ids::delayFb, cv83::ids::delayHp,
                                               cv83::ids::delayMix, cv83::ids::delaySelfFb })
    {
        setTitle ("Delay echoes");
    }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Delay  echoes");
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);
        const float cy = std::round (plot.getCentreY()) + 0.5f, half = plot.getHeight() * 0.5f - 1.0f;

        g.setColour (col::lcdOn.withAlpha (0.14f));
        g.drawHorizontalLine ((int) cy, plot.getX(), plot.getRight());
        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        g.setColour (col::lcdOn.withAlpha (0.32f));
        g.drawText ("L", plot.withHeight (9.0f), juce::Justification::centredRight, false);
        g.drawText ("R", plot.withTrimmedTop (plot.getHeight() - 9.0f), juce::Justification::centredRight, false);

        if (shown_.window <= 0.0f)
            return;

        drawGlassCorner (g, r, shown_.echoes < 0   ? juce::String ("runaway")
                             : shown_.echoes == 1  ? juce::String ("1 echo")
                             : shown_.echoes > 999 ? juce::String ("999+ echoes")
                                                   : juce::String (shown_.echoes) + " echoes");

        const float mix = value (4) * 0.01f, dry = 1.0f - mix;
        float peak = dry;
        for (auto& e : shown_.left)  peak = juce::jmax (peak, e.level * mix);
        for (auto& e : shown_.right) peak = juce::jmax (peak, e.level * mix);
        if (peak <= 0.0f)
            return;

        constexpr float kRangeDb = 48.0f;
        auto heightFor = [&] (float amp) {
            if (amp <= 0.0f) return 0.0f;
            return juce::jlimit (0.0f, 1.0f, 1.0f + 20.0f * std::log10 (amp / peak) / kRangeDb) * half;
        };
        auto bar = [&] (float x, float h, bool up, float alpha) {
            if (h < 0.5f) return;
            const float y = up ? cy - h : cy;
            g.setColour (col::lcdOn.withAlpha (lit (alpha * 0.15f)));
            g.fillRect (x - 1.5f, y, 3.0f, h);
            g.setColour (col::lcdOn.withAlpha (lit (alpha)));
            g.fillRect (x - 0.6f, y, 1.2f, h);
        };

        if (dry > 0.0f) {                              // the click itself, dimmer than its echoes
            const float h = heightFor (dry);
            g.setColour (col::lcdOn.withAlpha (lit (0.4f)));
            g.fillRect (juce::Rectangle<float> (plot.getX() + 0.5f, cy - h, 1.5f, 2.0f * h));
            g.setColour (col::lcdOn.withAlpha (0.32f));
            g.setFont (juce::Font (juce::FontOptions (8.0f)));
            g.drawText ("dry", juce::Rectangle<float> (plot.getX() + 4.0f, cy - h, 24.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }

        // One bar per echo, at the time it lands; where a short delay puts
        // several echoes into one pixel, the loudest of them.
        const int w = juce::jmax (1, (int) plot.getWidth());
        std::vector<float> up ((size_t) w, 0.0f), down ((size_t) w, 0.0f);
        auto place = [&] (const std::vector<Echo>& echoes, std::vector<float>& cols) {
            for (auto& e : echoes) {
                const int px = juce::roundToInt (e.time / shown_.window * (float) w);
                if (px >= 1 && px < w) cols[(size_t) px] = juce::jmax (cols[(size_t) px], e.level);
            }
        };
        place (shown_.left, up);
        place (shown_.right, down);
        for (int px = 1; px < w; ++px) {
            const float x = plot.getX() + (float) px + 0.5f;
            bar (x, heightFor (up[(size_t) px] * mix),   true,  1.0f);
            bar (x, heightFor (down[(size_t) px] * mix), false, 0.75f);
        }

        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText ("0", bottom, juce::Justification::centredLeft, false);
        g.drawText (fxSeconds (shown_.window), bottom, juce::Justification::centredRight, false);
    }

protected:
    void valuesChanged() override {
        // The feedback the delay really gets: without Self-Feedback it stops at 1.00.
        const float fb = value (5) >= 0.5f ? value (2) : juce::jmin (1.0f, value (2));
        const Settings s { value (0), value (1), fb, value (3), hostRate() };
        if (s == requested_) return;                   // Mix only changes the drawing
        requested_ = s;
        worker_.request ([this, s] (const std::function<bool()>& stale) {
            Echoes e;
            if (! render (s, e, stale)) return;
            const juce::SpinLock::ScopedLockType sl (lock_);
            ready_ = std::move (e);
            fresh_ = true;
        });
    }

    bool collectResults() override {
        if (! fresh_.exchange (false)) return false;
        const juce::SpinLock::ScopedLockType sl (lock_);
        shown_ = std::move (ready_);
        return true;
    }

private:
    struct Settings {
        float timeL = -1, timeR = -1, feedback = 0, highPass = 0;
        double rate = 0;
        bool operator== (const Settings& o) const {
            return juce::exactlyEqual (timeL, o.timeL) && juce::exactlyEqual (timeR, o.timeR)
                && juce::exactlyEqual (feedback, o.feedback) && juce::exactlyEqual (highPass, o.highPass)
                && juce::exactlyEqual (rate, o.rate);
        }
    };
    struct Echo { float time, level; };   // seconds after the click, level as a click amplitude
    struct Echoes {
        float window = 0.0f;
        int   echoes = 0;                  // repeats until 60 dB down; -1 = runaway
        std::vector<Echo> left, right;
    };

    // The feedback loop's gain at its least damped frequency: Feedback, times
    // the one-pole high-pass, times the linear interpolation that reads the
    // line between two samples (which dulls the top end unless the delay
    // time lands on a whole sample). At or above 1 the echoes never die away
    // - some band grows on every pass until the line's clamp holds it.
    static float loopGain (float feedback, float highPass, float timeMs, double sr) {
        const double d    = timeMs * 0.001 * sr;
        const double frac = d - std::floor (d);
        const double c    = std::exp (-juce::MathConstants<double>::twoPi * highPass / sr);
        double best = 0.0;
        constexpr int kSteps = 512;
        for (int i = 1; i <= kSteps; ++i) {
            const auto zi = std::polar (1.0, -juce::MathConstants<double>::pi * i / kSteps);   // z^-1
            const double hp  = std::abs (c * (1.0 - zi) / (1.0 - c * zi));
            const double lin = std::abs ((1.0 - frac) + frac * zi);
            best = juce::jmax (best, hp * lin);
        }
        return (float) (std::abs (feedback) * best);
    }

    static bool render (const Settings& s, Echoes& out, const std::function<bool()>& stale) {
        const double sr = s.rate;
        constexpr int kBlock = 512;
        constexpr float kClamp = 4.0f;              // FxChain's Delay clamps its line to +-4

        // How many repeats it takes to fall 60 dB, from the loop gain of
        // the slower-dying side.
        const float loop = juce::jmax (loopGain (s.feedback, s.highPass, s.timeL, sr),
                                       loopGain (s.feedback, s.highPass, s.timeR, sr));
        if (loop >= 1.0f)        out.echoes = -1;
        else if (loop < 1.0e-4f) out.echoes = 1;
        else out.echoes = 1 + (int) juce::jmin (1.0e6f, std::floor (std::log (0.001f) / std::log (loop)));

        // Look at ten repeats of the longer side at most (fewer if it dies
        // sooner), and never more than eight seconds.
        const float longest = juce::jmax (s.timeL, s.timeR) * 0.001f;
        const int repeats = out.echoes < 0 ? 10 : juce::jlimit (1, 10, out.echoes);
        out.window = juce::jlimit (0.3f, 8.0f, longest * ((float) repeats + 0.35f));

        cv83::Delay fx;
        fx.setEnabled (true);
        fx.setTimeL (s.timeL);
        fx.setTimeR (s.timeR);
        fx.setFeedback (s.feedback);
        fx.setHighPass (s.highPass);
        fx.setDryWet (100.0f);
        fx.prepare (sr, kBlock);

        // A second of silence first: the delay time glides to its setting and
        // the mix ramps fully wet, as they would have long before anyone listens.
        std::array<float, kBlock> l {}, rr {};
        for (int done = 0; done < (int) sr; done += kBlock) {
            l.fill (0.0f); rr.fill (0.0f);
            fx.process (l.data(), rr.data(), kBlock);
        }

        // Each echo's level: the energy within a millisecond of where it
        // lands (so a click the interpolation has smeared over two samples
        // still counts in full), as a click amplitude, and never more than the
        // clamp - which is what a runaway settles into.
        const double dL = s.timeL * 0.001 * sr, dR = s.timeR * 0.001 * sr;
        const int total = juce::jmax (kBlock, (int) (out.window * sr));
        const double reach = juce::jmax (1.0, 0.001 * sr);
        const int nL = (int) std::floor ((total - 1 - reach) / dL);   // echoes rendered in full
        const int nR = (int) std::floor ((total - 1 - reach) / dR);
        std::vector<double> eL ((size_t) juce::jmax (0, nL) + 2, 0.0), eR ((size_t) juce::jmax (0, nR) + 2, 0.0);
        auto collect = [reach] (std::vector<double>& e, double d, int i, float x) {
            const double k = std::round (i / d);
            if (k >= 1.0 && k < (double) e.size() && std::abs (i - k * d) <= reach)
                e[(size_t) k] += (double) x * x;
        };
        for (int pos = 0; pos < total; pos += kBlock) {
            if (stale()) return false;
            l.fill (0.0f); rr.fill (0.0f);
            if (pos == 0) l[0] = rr[0] = 1.0f;
            const int n = juce::jmin (kBlock, total - pos);
            fx.process (l.data(), rr.data(), n);
            for (int i = 0; i < n; ++i) {
                collect (eL, dL, pos + i, l[(size_t) i]);
                collect (eR, dR, pos + i, rr[(size_t) i]);
            }
        }
        auto toEchoes = [&] (const std::vector<double>& e, double d, int count, std::vector<Echo>& dst) {
            dst.clear();
            for (int k = 1; k <= count && k < (int) e.size(); ++k)
                dst.push_back ({ (float) (k * d / sr), juce::jmin (kClamp, (float) std::sqrt (e[(size_t) k])) });
        };
        toEchoes (eL, dL, nL, out.left);
        toEchoes (eR, dR, nR, out.right);
        return true;
    }

    Settings requested_;
    Echoes shown_, ready_;
    juce::SpinLock lock_;
    std::atomic<bool> fresh_ { false };
    ScopeWorker worker_ { "Delay scope" };   // last: stopped before the rest goes
};

// ---------------------------------------------------------------------------
//  Phaser: the frequency response of the whole unit (dry and wet at the
//  current Mix), 20 Hz..20 kHz. The bright curve is the response with the
//  sweep at Center, the two faint ones the ends of the sweep, and the shaded
//  band the range the notches travel. Worked out from FxChain's Phaser: N
//  first-order allpasses with a one-sample feedback loop around them, the
//  wet being (dry + chain) / 2. Stages sets the number of notches, N / 2.
// ---------------------------------------------------------------------------
class PhaserScope : public FxScope {
public:
    explicit PhaserScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, cv83::ids::phaserOn, { cv83::ids::phaserRate, cv83::ids::phaserDepth,
                                                cv83::ids::phaserCentre, cv83::ids::phaserFb,
                                                cv83::ids::phaserMix, cv83::ids::phaserStages })
    {
        setTitle ("Phaser frequency response");
    }

    void paint (juce::Graphics& g) override {
        static constexpr int kStages[] = { 2, 4, 6, 8, 10, 12 };
        const float rate   = value (0);
        const float depth  = value (1) * 0.01f;
        const float centre = value (2);
        const float fb     = value (3) * 0.01f * 0.9f;
        const float mix    = value (4) * 0.01f;
        const int   stages = kStages[juce::jlimit (0, 5, juce::roundToInt (value (5)))];

        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Phaser  response");
        drawGlassCorner (g, r, juce::String (stages) + " stages  " + fxHz (rate));
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);

        const float sr = (float) hostRate();
        const float fLo = 20.0f, fHi = juce::jmin (20000.0f, sr * 0.49f);
        constexpr float dbTop = 12.0f, dbBottom = -24.0f;
        auto xFor = [&] (float f) {
            return plot.getX() + std::log (f / fLo) / std::log (fHi / fLo) * plot.getWidth();
        };
        auto yFor = [&] (float db) {
            return plot.getY() + (dbTop - juce::jlimit (dbBottom, dbTop, db)) / (dbTop - dbBottom) * plot.getHeight();
        };

        const float fMin = juce::jlimit (30.0f, sr * 0.45f, centre * std::pow (2.0f, -2.0f * depth));
        const float fMax = juce::jlimit (30.0f, sr * 0.45f, centre * std::pow (2.0f,  2.0f * depth));

        g.setColour (col::lcdOn.withAlpha (lit (0.08f)));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (fMin), plot.getY(),
                                                                juce::jmax (xFor (fMax), xFor (fMin) + 1.0f),
                                                                plot.getBottom()));
        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (float f : { 100.0f, 1000.0f, 10000.0f }) {
            const float x = xFor (f);
            g.setColour (col::lcdOn.withAlpha (0.07f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (col::lcdOn.withAlpha (0.32f));
            g.drawText (f < 1000.0f ? "100" : f < 10000.0f ? "1k" : "10k",
                        juce::Rectangle<float> (x + 2.0f, plot.getBottom() - 9.0f, 24.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (col::lcdOn.withAlpha (0.14f));
        g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), plot.getX(), plot.getRight());
        g.setColour (col::lcdOn.withAlpha (0.32f));
        g.drawText ("0 dB", juce::Rectangle<float> (plot.getRight() - 30.0f, yFor (0.0f) - 9.0f, 30.0f, 9.0f),
                    juce::Justification::centredRight, false);

        const int w = juce::jmax (1, (int) plot.getWidth());
        auto response = [&] (float fc) {
            const float wt = std::tan (juce::MathConstants<float>::pi * fc / sr);
            const float a  = (wt - 1.0f) / (wt + 1.0f);
            juce::Path p;
            for (int px = 0; px <= w; ++px) {
                const float f = fLo * std::pow (fHi / fLo, (float) px / (float) w);
                const auto  zi = std::polar (1.0f, -juce::MathConstants<float>::twoPi * f / sr);   // z^-1
                const auto  ap = (a + zi) / (1.0f + a * zi);
                std::complex<float> chain (1.0f, 0.0f);
                for (int k = 0; k < stages; ++k) chain *= ap;
                const auto wet = 0.5f * (1.0f + chain / (1.0f - fb * zi * chain));
                const float mag = std::abs ((1.0f - mix) + mix * wet);
                const float y = yFor (20.0f * std::log10 (juce::jmax (1.0e-6f, mag)));
                const float x = plot.getX() + (float) px;
                if (px == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };

        if (depth > 0.0f) {
            g.setColour (col::lcdOn.withAlpha (lit (0.3f)));
            g.strokePath (response (fMin), juce::PathStrokeType (1.0f));
            g.strokePath (response (fMax), juce::PathStrokeType (1.0f));
        }
        const auto main = response (juce::jlimit (30.0f, sr * 0.45f, centre));
        g.setColour (col::lcdOn.withAlpha (lit (0.12f)));
        g.strokePath (main, juce::PathStrokeType (3.5f));
        g.setColour (col::lcdOn.withAlpha (lit (1.0f)));
        g.strokePath (main, juce::PathStrokeType (1.3f));

        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText (depth <= 0.0f ? "fixed at " + fxHz (centre)
                                  : "sweep " + fxHz (fMin) + " " + dash + " " + fxHz (fMax),
                    bottom, juce::Justification::centredLeft, false);
        g.drawText (juce::String (stages / 2) + (stages == 2 ? " notch" : " notches"),
                    bottom, juce::Justification::centredRight, false);
    }
};

// ---------------------------------------------------------------------------
//  Reverb: the spectrum of the tail - a short burst of noise (10 ms, the same
//  noise every time so the picture holds still) through a private
//  cv83::Reverb at the host's sample rate, and the whole tail it leaves
//  averaged into one spectrum (Hann-windowed FFTs, left and right summed),
//  20 Hz to 20 kHz on a 48 dB scale. A burst that
//  short has a lumpy spectrum of its own, which would pass straight through
//  into the picture, so each band is divided by the burst's own energy in
//  that band: what is left is the colour the reverb adds. Damping shows as
//  the top end falling away; Size and Feedback as how much tail there is to
//  hear, with its RT60
//  (the time it takes to fall 60 dB, measured from a separate click render
//  with Schroeder's backward integration) in the corner. With Freeze on, the
//  tank holds whatever is in it and takes no new input, so the picture dims
//  and says so.
// ---------------------------------------------------------------------------
class ReverbScope : public FxScope {
public:
    explicit ReverbScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, cv83::ids::reverbOn, { cv83::ids::reverbSize, cv83::ids::reverbFb,
                                                cv83::ids::reverbDamp, cv83::ids::reverbRate,
                                                cv83::ids::reverbDepth, cv83::ids::reverbFreeze })
    {
        setTitle ("Reverb tail spectrum");
    }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Spectrogram");
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);
        const bool frozen = value (5) >= 0.5f;

        if (shown_.frames.empty() || shown_.burst.size() < 2)
            return;

        drawGlassCorner (g, r, frozen ? juce::String ("FROZEN") : "RT60 " + fxSeconds (shown_.rt60));

        const float sr = (float) shown_.rate;
        const float fLo = 0.0f, fHi = juce::jmin (20000.0f, sr * 0.49f);
        const float binHz = sr / (float) kFftSize;
        const int nb = (int) shown_.burst.size();

        // Spectrogram: time left to right, frequency (log) bottom to top,
        // level as phosphor brightness on a 60 dB scale. Each cell is the
        // tail's power in a sixth-octave band over the burst's own power there.
        const int w = juce::jmax (2, (int) plot.getWidth()), h = juce::jmax (2, (int) plot.getHeight());
        const int nf = (int) shown_.frames.size();
        // Each row reads only the bins inside its own pixel (so the
        // reassigned detail survives); the burst correction uses a smooth
        // sixth-octave average of the burst, scaled to the same bin count.
        std::vector<int> b0 ((size_t) h), b1 ((size_t) h);
        std::vector<double> src ((size_t) h, 0.0);
        // Linear frequency: every row spans the same number of Hz, so no
        // row is narrower than an FFT bin (on a log axis the bass rows were,
        // and showed as blocky steps).
        const float rowHz = (fHi - fLo) / (float) (h - 1);
        for (int py = 0; py < h; ++py) {
            const float f  = fLo + (fHi - fLo) * (1.0f - (float) py / (float) (h - 1));
            const float lo = f - 0.5f * rowHz, hi = f + 0.5f * rowHz;
            b0[(size_t) py] = juce::jlimit (1, nb - 1, juce::roundToInt (lo / binHz));
            b1[(size_t) py] = juce::jlimit (b0[(size_t) py], nb - 1, juce::roundToInt (hi / binHz));
            const int s0 = juce::jlimit (1, nb - 1, (int) std::floor (juce::jmin (f * 0.944f, f - 1.5f * binHz) / binHz));
            const int s1 = juce::jlimit (s0, nb - 1, (int) std::ceil (juce::jmax (f * 1.059f, f + 1.5f * binHz) / binHz));
            double avg = 0.0;
            for (int b = s0; b <= s1; ++b) avg += shown_.burst[(size_t) b];
            src[(size_t) py] = avg / (double) (s1 - s0 + 1) * (double) (b1[(size_t) py] - b0[(size_t) py] + 1);
        }
        std::vector<float> db ((size_t) (w * h), -300.0f);
        float top = -300.0f;
        for (int px = 0; px < w; ++px) {
            const auto& fr = shown_.frames[(size_t) juce::jlimit (0, nf - 1, px * nf / w)];
            for (int py = 0; py < h; ++py) {
                double e = 0.0;
                for (int b = b0[(size_t) py]; b <= b1[(size_t) py]; ++b) e += fr[(size_t) b];
                const float d = e > 1.0e-30 && src[(size_t) py] > 1.0e-30 ? (float) (10.0 * std::log10 (e / src[(size_t) py])) : -300.0f;
                db[(size_t) (py * w + px)] = d;
                top = juce::jmax (top, d);
            }
        }
        juce::Image img (juce::Image::ARGB, w, h, true);
        const float dim = frozen ? 0.35f : 1.0f;
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                const float t = juce::jlimit (0.0f, 1.0f, 1.0f + (db[(size_t) (py * w + px)] - top) / 60.0f);
                img.setPixelAt (px, py, col::lcdOn.withAlpha (lit (t * t * 0.95f * dim)));
            }
        g.drawImage (img, plot);

        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (float f : { 5000.0f, 10000.0f, 15000.0f }) {
            if (f >= fHi) continue;
            const float y = plot.getY() + (1.0f - (f - fLo) / (fHi - fLo)) * plot.getHeight();
            g.setColour (col::lcdOn.withAlpha (0.12f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (col::lcdOn.withAlpha (0.5f));
            g.drawText (juce::String ((int) (f / 1000.0f)) + "k",
                        juce::Rectangle<float> (plot.getX() + 2.0f, y - 9.0f, 24.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        if (frozen) {
            const juce::String note ("tail held, input muted");
            const juce::Font font (juce::FontOptions (9.0f));
            juce::GlyphArrangement ga;
            ga.addLineOfText (font, note, 0.0f, 0.0f);
            const auto box = plot.withSizeKeepingCentre (ga.getBoundingBox (0, -1, true).getWidth() + 12.0f, 14.0f);
            g.setColour (col::lcdBg.withAlpha (0.9f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (col::lcdOn.withAlpha (0.35f));
            g.drawRoundedRectangle (box, 3.0f, 0.8f);
            g.setFont (font);
            g.setColour (col::lcdOn.withAlpha (0.9f));
            g.drawText (note, box, juce::Justification::centred, false);
        }
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText ("0", bottom, juce::Justification::centredLeft, false);
        g.drawText (fxSeconds (shown_.window), bottom, juce::Justification::centredRight, false);
    }

protected:
    void valuesChanged() override {
        const Settings s { value (0), value (1), value (2), value (3), value (4), hostRate() };
        if (s == requested_) return;                   // Freeze only changes the drawing
        requested_ = s;
        worker_.request ([this, s] (const std::function<bool()>& stale) {
            Response resp;
            if (! render (s, resp, stale)) return;
            const juce::SpinLock::ScopedLockType sl (lock_);
            ready_ = std::move (resp);
            fresh_ = true;
        });
    }

    bool collectResults() override {
        if (! fresh_.exchange (false)) return false;
        const juce::SpinLock::ScopedLockType sl (lock_);
        shown_ = std::move (ready_);
        return true;
    }

private:
    static constexpr int kFftOrder = 11, kFftSize = 1 << kFftOrder;   // 2048 points

    struct Settings {
        float size = -1, feedback = 0, damping = 0, modRate = 0, modDepth = 0;
        double rate = 0;
        bool operator== (const Settings& o) const {
            return juce::exactlyEqual (size, o.size) && juce::exactlyEqual (feedback, o.feedback)
                && juce::exactlyEqual (damping, o.damping) && juce::exactlyEqual (modRate, o.modRate)
                && juce::exactlyEqual (modDepth, o.modDepth) && juce::exactlyEqual (rate, o.rate);
        }
    };
    struct Response {
        float  window = 0.0f, rt60 = 0.0f;   // seconds of tail analysed; its RT60
        double rate = 48000.0;
        std::vector<std::vector<float>> frames;   // the tail's power per FFT bin, one frame per time step
        std::vector<float> burst;            // the noise burst's own power per bin
    };

    static std::unique_ptr<cv83::Reverb> makeReverb (const Settings& s, double sr, int block) {
        auto fx = std::make_unique<cv83::Reverb>();
        fx->setEnabled (true);
        fx->setSize (s.size);
        fx->setFeedback (s.feedback);
        fx->setDamping (s.damping);
        fx->setModRate (s.modRate);
        fx->setModDepth (s.modDepth * (float) (sr / 48000.0));   // given in samples at 48 kHz
        fx->setFreeze (false);
        fx->setDryWet (100.0f);
        fx->prepare (sr, block);
        std::vector<float> l ((size_t) block, 0.0f), r ((size_t) block, 0.0f);
        for (int done = 0; done < (int) (sr * 0.06); done += block) {   // let the mix ramp fully wet
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            fx->process (l.data(), r.data(), block);
        }
        return fx;
    }

    // RT60 from a click, rendered at a lower sample rate for long tails
    // (which leaves the envelope of a click the same). Returns -1 if stale.
    static float measureRt60 (const Settings& s, const std::function<bool()>& stale) {
        constexpr int kBlock = 512, kSlice = 32;

        // A first guess, to know how much to render: the FDN's matrix keeps
        // energy, so each trip round the lines (52 ms at Size 1 on average)
        // loses just the feedback gain. Damping only makes it shorter, and the
        // measurement takes care of that.
        const float loop  = juce::jlimit (0.0f, 0.98f, s.feedback * 0.8f);   // FxChain's Reverb
        const float trip  = 0.0522f * s.size;
        const float guess = loop > 0.001f ? 60.0f / (-20.0f * std::log10 (loop)) * trip : trip * 1.5f;
        const float length = juce::jlimit (0.25f, 30.0f, guess * 1.25f + 0.08f * s.size + 0.05f);
        const double sr = juce::jlimit (8000.0, 32000.0, 400000.0 / (double) length);
        auto fx = makeReverb (s, sr, kBlock);

        std::array<float, kBlock> l {}, rr {};
        const int total = juce::jmax (kBlock, (int) (length * sr));
        const int slices = (total + kSlice - 1) / kSlice;
        std::vector<double> e ((size_t) slices, 0.0);
        for (int pos = 0; pos < total; pos += kBlock) {
            if (stale()) return -1.0f;
            l.fill (0.0f); rr.fill (0.0f);
            if (pos == 0) l[0] = rr[0] = 1.0f;
            const int n = juce::jmin (kBlock, total - pos);
            fx->process (l.data(), rr.data(), n);
            for (int i = 0; i < n; ++i)
                e[(size_t) ((pos + i) / kSlice)] += (double) l[(size_t) i] * l[(size_t) i] + (double) rr[(size_t) i] * rr[(size_t) i];
        }

        // Schroeder: the energy still to come from each moment on, in dB; RT60
        // from its slope between -5 dB and -35 dB (or wherever it has got to
        // 60% of the way through the render, for very long tails).
        std::vector<double> edc ((size_t) slices + 1, 0.0);
        for (int k = slices - 1; k >= 0; --k) edc[(size_t) k] = edc[(size_t) k + 1] + e[(size_t) k];
        const double sliceSec = kSlice / sr;
        float rt60 = guess;
        if (edc[0] > 0.0) {
            auto dbAt = [&] (int k) { return 10.0 * std::log10 (juce::jmax (1.0e-30, edc[(size_t) k] / edc[0])); };
            int k5 = -1, kEnd = -1;
            const int limit = (int) (slices * 0.6);
            for (int k = 0; k < slices; ++k) {
                if (k5 < 0 && dbAt (k) <= -5.0) k5 = k;
                if (k5 >= 0 && (dbAt (k) <= -35.0 || k >= limit)) { kEnd = k; break; }
            }
            if (k5 >= 0 && kEnd > k5 && dbAt (kEnd) < -10.0)
                rt60 = (float) (-60.0 / ((dbAt (kEnd) + 5.0) / ((kEnd - k5) * sliceSec)));
        }
        return rt60;
    }

    // In-place radix-2 FFT.
    static void fft (std::vector<std::complex<float>>& x) {
        const size_t n = x.size();
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1) {
            const auto step = std::polar (1.0f, -juce::MathConstants<float>::twoPi / (float) len);
            for (size_t i = 0; i < n; i += len) {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k) {
                    const auto a = x[i + k], b = x[i + k + len / 2] * w;
                    x[i + k] = a + b;
                    x[i + k + len / 2] = a - b;
                    w *= step;
                }
            }
        }
    }

    static bool render (const Settings& s, Response& out, const std::function<bool()>& stale) {
        out.rt60 = measureRt60 (s, stale);
        if (out.rt60 < 0.0f) return false;

        // The noise burst and its tail, at the host's rate so the spectrum
        // reaches 20 kHz. As much tail as the RT60, within 0.25..4 s.
        constexpr int kBlock = 512;
        const double sr = s.rate;
        out.rate = sr;
        out.window = juce::jlimit (0.25f, 4.0f, out.rt60);
        auto fx = makeReverb (s, sr, kBlock);

        const int burst = (int) (0.010 * sr);
        const int total = juce::jmax (kFftSize, (int) (out.window * sr) + burst);
        std::vector<float> left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);
        uint32_t seed = 0x2545F491u;                   // the same noise every render
        for (int i = 0; i < burst; ++i) {
            seed = seed * 1664525u + 1013904223u;
            left[(size_t) i] = right[(size_t) i] = (float) (seed >> 8) / 8388608.0f - 1.0f;
        }
        std::vector<std::complex<float>> buf ((size_t) kFftSize);
        for (int i = 0; i < kFftSize; ++i) buf[(size_t) i] = { i < burst ? left[(size_t) i] : 0.0f, 0.0f };
        fft (buf);
        out.burst.resize ((size_t) kFftSize / 2 + 1);
        for (size_t k = 0; k < out.burst.size(); ++k) out.burst[k] = std::norm (buf[k]);

        for (int pos = 0; pos < total; pos += kBlock) {
            if (stale()) return false;
            fx->process (left.data() + pos, right.data() + pos, juce::jmin (kBlock, total - pos));
        }

        // Short-time FFTs (Hann), about 160 across the tail, left and right
        // summed - sharpened by reassignment: each bin's energy is moved to
        // the frequency (phase derivative, from an FFT with the window's time
        // derivative) and the time (group delay, from an FFT with the
        // time-ramped window) it really belongs to, instead of smearing over
        // the bin's width and the frame's length. Fixed, reasonable settings:
        // Hann window, bins more than 60 dB under the frame's level are left
        // where they are, shifts limited to 4 bins and 2 hops.
        std::vector<float> winH ((size_t) kFftSize), winDH ((size_t) kFftSize), winTH ((size_t) kFftSize);
        for (int i = 0; i < kFftSize; ++i) {
            const double ph = juce::MathConstants<double>::twoPi * i / kFftSize;
            winH[(size_t) i]  = (float) (0.5 * (1.0 - std::cos (ph)));
            winDH[(size_t) i] = (float) (juce::MathConstants<double>::pi / kFftSize * std::sin (ph));   // d/dn of h
            winTH[(size_t) i] = (float) (i - kFftSize / 2) * winH[(size_t) i];
        }
        const int hop = juce::jmax (64, (total - kFftSize) / 160);
        const int numFrames = (total - kFftSize) / hop + 1;
        const size_t nbins = (size_t) kFftSize / 2 + 1;
        out.frames.assign ((size_t) juce::jmax (1, numFrames), std::vector<float> (nbins, 0.0f));
        std::vector<std::complex<float>> bH ((size_t) kFftSize), bD ((size_t) kFftSize), bT ((size_t) kFftSize);
        constexpr float kThreshDb = -60.0f, kMaxBins = 4.0f, kMaxHops = 2.0f;
        for (int f = 0; f < numFrames; ++f) {
            if (stale()) return false;
            const int start = f * hop;
            for (const auto* ch : { &left, &right }) {
                for (int i = 0; i < kFftSize; ++i) {
                    const float x = (*ch)[(size_t) (start + i)];
                    bH[(size_t) i] = { x * winH[(size_t) i], 0.0f };
                    bD[(size_t) i] = { x * winDH[(size_t) i], 0.0f };
                    bT[(size_t) i] = { x * winTH[(size_t) i], 0.0f };
                }
                fft (bH); fft (bD); fft (bT);
                double sum = 0.0;
                for (size_t k = 1; k + 1 < nbins; ++k) sum += std::norm (bH[k]);
                const float floorPow = (float) (sum / (double) nbins) * std::pow (10.0f, kThreshDb / 10.0f);
                for (size_t k = 1; k + 1 < nbins; ++k) {
                    const float pw = std::norm (bH[k]);
                    if (pw <= 0.0f) continue;
                    size_t tk = k; int tf = f;
                    if (pw > floorPow) {
                        const auto den = bH[k] + std::complex<float> (1.0e-9f, 0.0f);
                        const float dBins = -(float) kFftSize / juce::MathConstants<float>::twoPi * (bD[k] / den).imag();
                        const float dHops = (bT[k] / den).real() / (float) hop;
                        if (std::abs (dBins) <= kMaxBins)
                            tk = (size_t) juce::jlimit (1, (int) nbins - 2, juce::roundToInt ((float) k + dBins));
                        if (std::abs (dHops) <= kMaxHops)
                            tf = juce::jlimit (0, numFrames - 1, juce::roundToInt ((float) f + dHops));
                    }
                    out.frames[(size_t) tf][tk] += pw;
                }
            }
        }
        return true;
    }

    Settings requested_;
    Response shown_, ready_;
    juce::SpinLock lock_;
    std::atomic<bool> fresh_ { false };
    ScopeWorker worker_ { "Reverb scope" };   // last: stopped before the rest goes
};

// ============================================================================
//  FxUnitPanel  -  one effect: header, on/off switch, its knobs, its scope.
//
//  Painted like VirtualDX7's module cards (panel brown, amber header band,
//  dark edge). The knobs sit in rows of three; a short last row starts at
//  the left. A unit's second switch (the reverb's Freeze) shares the top row
//  with its ON/OFF, which leaves the lower half free for the scope.
// ============================================================================
class FxUnitPanel : public juce::Component {
public:
    // `widthWeight` is how FxPanel divides its width between units - 1.0 is
    // a normal unit's share, smaller values ask for proportionally less.
    FxUnitPanel (const juce::String& titleText,
                 juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& enableParamId,
                 float widthWeight = 1.0f)
        : title_ (titleText), weight_ (widthWeight)
    {
        enableBtn.setButtonText ("OFF");
        enableBtn.setClickingTogglesState (true);
        enableBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
        enableBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        enableBtn.setTitle (titleText + " enable");
        enableBtn.setTooltip ("Change the " + titleText.toLowerCase() + " in and out");
        enableBtn.onStateChange = [this] {
            enableBtn.setButtonText (enableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible (enableBtn);

        if (auto* p = apvts.getParameter (enableParamId))
            enableAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p, enableBtn);
    }

    float widthWeight() const { return weight_; }

    // A switch in the knob grid, captioned like a knob, with a line of text
    // underneath where a knob prints its value (the delay's Self-Feedback,
    // sitting right under the Feedback knob it unlocks).
    struct SwitchTile : public juce::Component {
        SwitchTile (const juce::String& caption, juce::RangedAudioParameter& p,
                    std::function<juce::String (bool)> footerText)
            : caption_ (caption), footer_ (std::move (footerText))
        {
            btn.setButtonText ("OFF");
            btn.setClickingTogglesState (true);
            btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
            btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.setTitle (p.getName (64));
            btn.setTooltip (p.getName (64));
            btn.onStateChange = [this] {
                const bool on = btn.getToggleState();
                btn.setButtonText (on ? "ON" : "OFF");
                if (on != lastOn_) { lastOn_ = on; repaint(); if (onChange) onChange(); }
            };
            addAndMakeVisible (btn);
            attachment = std::make_unique<juce::ButtonParameterAttachment> (p, btn);
        }
        void paint (juce::Graphics& g) override {
            auto b = getLocalBounds();
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (caption_, b.removeFromTop (14), juce::Justification::centred);
            if (footer_) {
                g.setColour (col::text);
                g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
                g.drawText (footer_ (btn.getToggleState()), b.removeFromBottom (14), juce::Justification::centred);
            }
        }
        void resized() override {
            auto b = getLocalBounds();
            b.removeFromTop (14);
            b.removeFromBottom (14);
            btn.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth() - 12, 56), 24));
        }
        std::function<void()> onChange;   // after the switch has flipped
        juce::TextButton btn;
    private:
        juce::String caption_;
        std::function<juce::String (bool)> footer_;
        bool lastOn_ = false;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    // Adds a knob bound to `paramId` (a continuous, whole-number or choice
    // parameter - the phaser's Stages is a knob too).
    FxKnob* addKnob (const juce::String& caption,
                     juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr) return nullptr;
        auto k = std::make_unique<FxKnob> (caption, *p);
        auto* raw = k.get();
        addAndMakeVisible (*k);
        tiles.push_back (std::move (k));
        return raw;
    }

    // Adds a SwitchTile to the knob grid.
    SwitchTile* addSwitch (const juce::String& caption,
                           juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& paramId,
                           std::function<juce::String (bool)> footerText)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr) return nullptr;
        auto t = std::make_unique<SwitchTile> (caption, *p, std::move (footerText));
        auto* raw = t.get();
        addAndMakeVisible (*t);
        tiles.push_back (std::move (t));
        return raw;
    }

    // Adds a second switch next to ON/OFF (the reverb's Freeze).
    void addToggle (const juce::String& caption,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramId)
    {
        if (auto* p = apvts.getParameter (paramId)) {
            auto t = std::make_unique<Toggle> (caption, *p);
            addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
        }
    }

    void setScope (std::unique_ptr<juce::Component> s) {
        scope = std::move (s);
        if (scope != nullptr) addAndMakeVisible (*scope);
        resized();
    }

    // Shows or hides the green display under the knobs. A hidden scope is
    // not showing, so its timer does nothing and its renders never start.
    void setScopeVisible (bool shouldShow) {
        if (scope != nullptr && scope->isVisible() != shouldShow) {
            scope->setVisible (shouldShow);
            resized();
        }
    }

    // Height the unit needs for its header, switches and knob rows alone.
    static int heightWithoutScope (int numTiles) {
        constexpr int kCols = 3, kRowH = 68;
        return 24 + 6 + 30 + 6 + ((numTiles + kCols - 1) / kCols) * kRowH + 6;
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

        auto top = b.removeFromTop (30).reduced (12, 2);
        if (toggles.empty()) {
            enableBtn.setBounds (top);
        } else {
            const int n = 1 + (int) toggles.size(), gap = 6;
            const int w = (top.getWidth() - gap * (n - 1)) / n;
            enableBtn.setBounds (top.removeFromLeft (w));
            for (auto& t : toggles) {
                top.removeFromLeft (gap);
                t->setBounds (top.removeFromLeft (w));
            }
        }
        b.removeFromTop (6);

        constexpr int kCols = 3, kRowH = 68;
        const int n = (int) tiles.size();
        if (n > 0) {
            const int rows = (n + kCols - 1) / kCols;
            const int cw = b.getWidth() / kCols;
            for (int i = 0; i < n; ++i) {
                const int row = i / kCols, c = i % kCols;
                tiles[(size_t) i]->setBounds (b.getX() + c * cw, b.getY() + row * kRowH, cw, kRowH);
            }
            b.removeFromTop (rows * kRowH);
        }

        if (scope != nullptr && scope->isVisible()) {
            b.removeFromTop (6);
            scope->setBounds (b);
        }
    }

private:
    // A captioned switch that lights green while on, like ON/OFF.
    struct Toggle : public juce::TextButton {
        Toggle (const juce::String& caption, juce::RangedAudioParameter& p) {
            setButtonText (caption);
            setClickingTogglesState (true);
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
            setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            setTitle (p.getName (64));
            setTooltip (p.getName (64));
            attachment = std::make_unique<juce::ButtonParameterAttachment> (p, *this);
        }
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    juce::String title_;
    float weight_ = 1.0f;
    juce::TextButton enableBtn;
    std::unique_ptr<juce::ButtonParameterAttachment> enableAttachment;
    std::vector<std::unique_ptr<juce::Component>> tiles;   // knobs and switches, in grid order
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::unique_ptr<juce::Component> scope;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxUnitPanel)
};

// ============================================================================
//  FxPanel  -  the four effect units side by side, in the order they run
//  (Chorus -> Delay -> Phaser -> Reverb), plus a footer with a signal-flow
//  caption, so the fixed routing is visible rather than something you have
//  to guess at, and the SHOW VISUALIZERS switch. Knob for knob the same as
//  VirtualDX7's FX page; the green displays are there too, but hidden until
//  that switch turns them on.
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
        chorus->addKnob ("UNISON", apvts, chorusVoices);
        chorus->addKnob ("MIX",    apvts, chorusMix);
        chorus->setScope (std::make_unique<ChorusScope> (apvts));

        auto* delay = addUnit ("DELAY", apvts, delayOn);
        delay->addKnob ("TIME L", apvts, delayTimeL);
        delay->addKnob ("TIME R", apvts, delayTimeR);
        auto* fbKnob = delay->addKnob ("FBK", apvts, delayFb);
        delay->addKnob ("HPF",    apvts, delayHp);
        delay->addKnob ("MIX",    apvts, delayMix);
        // Allow Self-Feedback, right under the Feedback knob it unlocks. Off,
        // Feedback stops at 1.00 however far the knob turns (and the knob
        // prints the 1.00 that is actually used); on, the rest of its range
        // is live and the echoes can grow into self-oscillation.
        auto* selfFb = delay->addSwitch ("SELF-FB", apvts, delaySelfFb,
                                         [] (bool on) { return juce::String (on ? "cap at 1.20" : "cap at 1.00"); });
        if (fbKnob != nullptr && selfFb != nullptr) {
            selfFb->btn.setTooltip ("Allow Self-Feedback: lets FBK go above 1.00, where the echoes grow instead of fading");
            fbKnob->shownValue = [selfFb] (float v) { return selfFb->btn.getToggleState() ? v : juce::jmin (1.0f, v); };
            selfFb->onChange = [fbKnob] { fbKnob->repaint(); };
        }
        delay->setScope (std::make_unique<DelayScope> (apvts));

        auto* phaser = addUnit ("PHASER", apvts, phaserOn);
        phaser->addKnob ("RATE",   apvts, phaserRate);
        phaser->addKnob ("DEPTH",  apvts, phaserDepth);
        phaser->addKnob ("CENTER", apvts, phaserCentre);
        phaser->addKnob ("FBK",    apvts, phaserFb);
        phaser->addKnob ("STAGES", apvts, phaserStages);
        phaser->addKnob ("MIX",    apvts, phaserMix);
        phaser->setScope (std::make_unique<PhaserScope> (apvts));

        auto* reverb = addUnit ("REVERB", apvts, reverbOn);
        reverb->addToggle ("FREEZE", apvts, reverbFreeze);
        reverb->addKnob ("SIZE",   apvts, reverbSize);
        reverb->addKnob ("FBK",    apvts, reverbFb);
        reverb->addKnob ("DAMP",   apvts, reverbDamp);
        reverb->addKnob ("MOD HZ", apvts, reverbRate);
        reverb->addKnob ("MOD DP", apvts, reverbDepth);
        reverb->addKnob ("MIX",    apvts, reverbMix);
        reverb->setScope (std::make_unique<ReverbScope> (apvts));

        // Lit amber while the displays are shown, like VirtualDX7's FX button.
        // The editor decides what a click does (the window changes height).
        scopesButton.setButtonText ("SHOW VISUALIZERS");
        scopesButton.setColour (juce::TextButton::buttonOnColourId, col::accent);
        scopesButton.setColour (juce::TextButton::textColourOnId,   col::bg);
        scopesButton.setTitle ("Show visualizers");
        scopesButton.setTooltip ("Show or hide the green displays under each effect's knobs");
        addAndMakeVisible (scopesButton);
    }

    static constexpr int kFooterH = 30;   // signal-flow caption and the switch

    // How tall the panel has to be with the displays hidden: the tallest unit
    // without its scope, plus the footer.
    static int heightWithoutScopes() { return FxUnitPanel::heightWithoutScope (6) + kFooterH; }

    void setScopesVisible (bool shouldShow) {
        for (auto& u : units) u->setScopeVisible (shouldShow);
        scopesButton.setToggleState (shouldShow, juce::dontSendNotification);
    }

    juce::TextButton scopesButton;

    void paint (juce::Graphics& g) override {
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("IN  >  CHORUS  >  DELAY  >  PHASER  >  REVERB  >  OUT",
                    footerRow(), juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromBottom (kFooterH);   // signal-flow caption + SHOW VISUALIZERS
        scopesButton.setBounds (footerRow().withTrimmedRight (16)   // clear of the window's resize grip
                                           .removeFromRight (150).reduced (0, 1));

        const int gap = 8;
        const int n = (int) units.size();
        if (n == 0)
            return;

        // Four equal units; the last absorbs any rounding remainder so the
        // row still fills exactly to the right edge.
        const int w = (b.getWidth() - gap * (n - 1)) / n;
        int x = b.getX();
        for (int i = 0; i < n; ++i) {
            const int ww = (i == n - 1) ? (b.getRight() - x) : w;
            units[(size_t) i]->setBounds (x, b.getY(), ww, b.getHeight());
            x += ww + gap;
        }
    }

private:
    juce::Rectangle<int> footerRow() const { return getLocalBounds().removeFromBottom (kFooterH).withTrimmedTop (4); }

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

    // The design size everything is laid out at; the window scales it. Its
    // height depends on whether the visualizers are shown: the knobs alone
    // make a slim rack strip, the displays add the lower half.
    static constexpr int kDesignW      = 1195;
    static constexpr int kDesignHFull  = 505;
    static int designHCompact() { return 2 * kMargin + cv83ui::FxPanel::heightWithoutScopes(); }
    int designH() const { return showScopes_ ? kDesignHFull : designHCompact(); }

private:
    static constexpr int kMargin = 10;
    void setVisualizersShown (bool shown);
    void applyConstraints();             // aspect ratio + size limits for the current height

    ChorusVerbAudioProcessor& processor;
    cv83ui::DXLookAndFeel lnf;
    juce::TooltipWindow   tooltips { this, 600 };
    juce::Component       content;   // holds the FX page at the design size
    std::unique_ptr<cv83ui::FxPanel> fxPanel;
    bool showScopes_ = false;        // remembered with the plugin's state

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusVerbAudioProcessorEditor)
};
