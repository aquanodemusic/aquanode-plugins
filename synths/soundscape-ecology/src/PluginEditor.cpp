/*
  ==============================================================================

    PluginEditor.cpp
    Soundscape Ecology — interface implementation.

    Everything is drawn: no image assets. The background gradient responds to
    the temperature and time-of-day controls, so the panel warms and cools with
    the sound rather than sitting behind it as decoration.

  ==============================================================================
*/

#include "PluginEditor.h"

using namespace soundeco;

//==============================================================================
// GlassPanel
//==============================================================================
void GlassPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float corner = 14.0f;

    // frosted body
    g.setColour (Palette::glass());
    g.fillRoundedRectangle (r, corner);

    // soft vertical sheen so panels read as glass rather than flat fills
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.14f), r.getCentreX(), r.getY(),
                                juce::Colours::white.withAlpha (0.02f), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r, corner);

    // edge
    g.setColour (Palette::glassEdge());
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

    if (title.isNotEmpty())
    {
        auto header = r.removeFromTop (30.0f).reduced (14.0f, 6.0f);

        // small accent mark, like a leaf vein
        auto mark = header.removeFromLeft (14.0f);
        g.setColour (accent.withAlpha (0.85f));
        juce::Path p;
        p.startNewSubPath (mark.getX(), mark.getCentreY() + 4.0f);
        p.quadraticTo (mark.getCentreX(), mark.getCentreY() - 7.0f,
                       mark.getRight(), mark.getCentreY() + 3.0f);
        p.quadraticTo (mark.getCentreX(), mark.getCentreY() + 5.0f,
                       mark.getX(), mark.getCentreY() + 4.0f);
        g.fillPath (p);

        g.setColour (Palette::text());
        g.setFont (juce::Font (juce::FontOptions (14.0f)).withExtraKerningFactor (0.12f));
        g.drawText (title.toUpperCase(), header.translated (6.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// EcoLookAndFeel
//==============================================================================
EcoLookAndFeel::EcoLookAndFeel()
{
    setColour (juce::Label::textColourId, Palette::text());
    setColour (juce::ComboBox::textColourId, Palette::text());
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha (0.12f));
    setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.25f));
    setColour (juce::ComboBox::arrowColourId, Palette::textDim());
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf01e2a1c));
    setColour (juce::PopupMenu::textColourId, Palette::text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::canopy().withAlpha (0.55f));
    setColour (juce::Slider::textBoxTextColourId, Palette::text());
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::Font EcoLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (12.5f));
}

juce::Font EcoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (14.0f));
}

void EcoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                       float pos, float startAngle, float endAngle,
                                       juce::Slider& s)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // outer arc track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 1.5f, radius - 1.5f, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.strokePath (track, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // luminous value arc
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - 1.5f, radius - 1.5f, 0.0f,
                         startAngle, angle, true);
    auto glow = s.isEnabled() ? accent.brighter (0.35f) : juce::Colours::grey;
    g.setColour (glow.withAlpha (0.95f));
    g.strokePath (value, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // glass cap
    auto cap = bounds.reduced (radius * 0.24f);
    juce::ColourGradient body (juce::Colours::white.withAlpha (0.30f), cap.getX(), cap.getY(),
                               juce::Colours::black.withAlpha (0.30f), cap.getX(), cap.getBottom(), false);
    g.setGradientFill (body);
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.32f));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    // pointer
    juce::Path pointer;
    const float pw = 2.0f;
    pointer.addRoundedRectangle (-pw * 0.5f, -radius * 0.72f, pw, radius * 0.42f, pw * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void EcoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                       float pos, float minPos, float maxPos,
                                       juce::Slider::SliderStyle, juce::Slider& s)
{
    juce::ignoreUnused (minPos, maxPos);
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float barH = juce::jmin (7.0f, r.getHeight());
    auto track = r.withSizeKeepingCentre (r.getWidth(), barH);

    g.setColour (juce::Colours::black.withAlpha (0.26f));
    g.fillRoundedRectangle (track, barH * 0.5f);

    auto filled = track.withWidth (juce::jmax (barH, pos - track.getX()));
    auto glow = s.isEnabled() ? accent.brighter (0.30f) : juce::Colours::grey;
    g.setColour (glow.withAlpha (0.92f));
    g.fillRoundedRectangle (filled, barH * 0.5f);

    // handle
    const float hw = 4.0f;
    auto handle = juce::Rectangle<float> (hw, r.getHeight() * 0.82f)
                    .withCentre ({ juce::jlimit (track.getX() + hw, track.getRight() - hw, pos),
                                   track.getCentreY() });
    g.setColour (juce::Colours::white.withAlpha (0.94f));
    g.fillRoundedRectangle (handle, hw * 0.5f);
}

void EcoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.13f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.26f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void EcoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                       bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto pill = r.removeFromLeft (juce::jmin (34.0f, r.getWidth())).withSizeKeepingCentre (30.0f, 16.0f);

    g.setColour (b.getToggleState() ? accent.withAlpha (0.75f)
                                    : juce::Colours::white.withAlpha (0.16f));
    g.fillRoundedRectangle (pill, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.45f : 0.28f));
    g.drawRoundedRectangle (pill, 8.0f, 1.0f);

    auto dot = juce::Rectangle<float> (13.0f, 13.0f)
                 .withCentre ({ b.getToggleState() ? pill.getRight() - 8.5f : pill.getX() + 8.5f,
                                pill.getCentreY() });
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.fillEllipse (dot);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (Palette::textDim());
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (b.getButtonText(), r.translated (4.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// EcoKnob
//==============================================================================
EcoKnob::EcoKnob()
{
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);
    rebuildStyle();
}

void EcoKnob::rebuildStyle()
{
    slider.setSliderStyle (horizontal ? juce::Slider::LinearHorizontal
                                      : juce::Slider::RotaryHorizontalVerticalDrag);
    resized();
    repaint();
}

void EcoKnob::setLabel (const juce::String& t)
{
    caption = t;
    // Hover help. Two-second delay is set on the TooltipWindow in the editor.
    auto help = describeControl (t);
    slider.setTooltip (help.isNotEmpty() ? t + " — " + help : t);
    setTooltip (slider.getTooltip());
    repaint();
}

void EcoKnob::resized()
{
    auto r = getLocalBounds();

    if (horizontal)
    {
        // label on the left, bar on the right
        const int labelW = juce::jlimit (52, 96, r.getWidth() * 45 / 100);
        r.removeFromLeft (labelW);
        slider.setBounds (r.reduced (2, 3));
    }
    else
    {
        auto labelArea = r.removeFromBottom (14);
        juce::ignoreUnused (labelArea);
        const int d = juce::jmin (r.getWidth(), r.getHeight());
        slider.setBounds (juce::Rectangle<int> (d, d).withCentre (r.getCentre()));
    }
}

void EcoKnob::paint (juce::Graphics& g)
{
    g.setColour (Palette::text().withAlpha (0.86f));

    if (horizontal)
    {
        const int labelW = juce::jlimit (52, 96, getWidth() * 45 / 100);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (caption, getLocalBounds().removeFromLeft (labelW).reduced (2, 0),
                          juce::Justification::centredLeft, 1, 0.85f);
    }
    else
    {
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawFittedText (caption, getLocalBounds().removeFromBottom (14),
                          juce::Justification::centred, 1, 0.8f);
    }
}

//==============================================================================
// Plain-language hover help.
//==============================================================================
juce::String soundeco::describeControl (const juce::String& label)
{
    static const std::map<juce::String, juce::String> help =
    {
        // --- global / header ---
        { "Temperature",  "Ambient temperature in Celsius. Insects genuinely track it: cricket chirp rate follows Dolbear's law, so this speeds up or slows the whole insect chorus" },
        { "Time of Day",  "Morphs between a preset's day and night variants, where it has both. Also darkens the background and shifts which creatures are active" },
        { "Humidity",     "Moisture in the air. Damp air absorbs high frequencies over distance, so raising this makes far-off sources duller and closer-sounding ones unchanged" },
        { "Niche",        "Krause's acoustic niche hypothesis: in a healthy habitat, species occupy separate frequency bands so their calls do not mask each other. Raising this pushes the three slots apart in the spectrum" },
        { "Weather",      "How strongly weather couples to everything: a gust raises the wind, stirs the canopy and quietens the animals at once. Rain silences insects" },
        { "Output",       "Master output trim" },

        // --- habitat ---
        { "Size",         "Size of the surrounding space. Small is a clearing, large is a valley" },
        { "Damping",      "How absorbent the surroundings are. High damping is dense foliage and snow, low is bare rock and water" },
        { "Mix",          "Balance of direct sound against the reflected habitat. Changes the sense of space, not the loudness" },
        { "Width",        "Stereo width of the final output" },

        // --- MIDI ---
        { "Release",      "How long the scene takes to fade after the last note is released" },
        { "Velocity",     "How much note velocity scales the level of the whole field" },
        { "Root",         "Which MIDI note counts as no transposition. In Gated + Pitch mode, notes above and below this shift every model's carrier" },
        { "Draw Depth",   "How strongly the drawn pitch and amplitude curves affect the sound. At zero the curves are ignored" },

        // --- articulation ---
        { "Rate",         "How often this source calls, in events per second" },
        { "Phrase",       "Syllables inside one event. Species identity lives here" },
        { "Rhythm",       "Syllable shape: 0 smooth tremolo, 1 sharp struck syllables" },
        { "Jitter",       "Irregularity of the timing — 0 is metronomic" },
        { "Hold",         "Sustain before the syllable train starts. Minmin needs this" },
        { "Attack",       "Fade-in time of each individual event" },
        { "Decay",        "Fade-out time of each individual event" },
        { "Length",       "Duration of a single event in seconds" },
        { "Level",        "Output level of this slot" },
        // --- the field ---
        { "Population",   "How many individuals are scattered in the landscape" },
        { "Spread",       "How far apart they sit — near ones stay distinct" },
        { "Distance",     "How far the nearest individual is from the listener" },
        { "Diversity",    "How much individuals differ from one another" },
        { "Synchrony",    "Chorus coupling. Around 0.3 gives waves of synchrony" },
        { "Parallax",     "How strongly near sources pan compared to far ones" },
        { "Elevation",    "Height above ground in metres. Sets the ground-reflection comb: low and near combs in the midrange, high overhead combs densely" },
        // --- crickets ---
        { "Carrier",      "Resonant frequency of the wing — field crickets sit 2-8 kHz" },
        { "Harp Q",       "Sharpness of the wing resonance. High Q gives a pure whistle" },
        { "Mirror Q",     "Sharpness of the katydid wing resonance" },
        { "Tooth Jitter", "Regularity of the file teeth. Even = pure tone, irregular = rasp" },
        { "File Taper",   "Tooth spacing narrowing along the file, keeping the tone steady" },
        { "Strike Tune",  "Detunes tooth strike rate from the resonance" },
        { "Pulses",       "Wing strokes per chirp" },
        // --- cicada ---
        { "Tymbal Freq",  "Resonance of the tymbal plate" },
        { "Tymbal Q",     "How long the tymbal rings after each rib buckles" },
        { "Ribs",         "Stiff ribs buckling in sequence per muscle contraction" },
        { "Muscle Rate",  "Tymbal muscle contractions per second — 200 to 500 in nature" },
        { "Rate Drift",   "How the muscle rate slows across a phrase" },
        { "Air Sac",      "Abdominal Helmholtz resonator tuning" },
        { "Second Band",  "A second spectral peak. Minmin-zemi has two, at 4.7 and 15 kHz" },
        { "Brightness",   "Strength of the upper modes" },
        { "Bell",         "Abdominal air sac resonance. The cicada's ringing quality" },
        { "Sub Rumble",   "Body mode below the plate. Makes huge steel sound huge" },
        // --- frog / bird ---
        { "Larynx",       "Fundamental of the vocal folds" },
        // --- anuran (researched model) ---
        { "Fold Rate",    "Vocal fold opening rate. This IS the call fundamental" },
        { "Pulse Rate",   "Arytenoid cartilages gating amplitude — a separate clock, and the main species signature" },
        { "Dominant",     "Which harmonic the head and sac radiate most strongly" },
        { "Radiator",     "How sharply the dominant is emphasised. The sac is a radiator, not a cavity resonator" },
        { "Fibrous Mass", "Mass loading the folds. High values detune them into subharmonics — the tungara chuck" },
        { "Open Quotient","Fraction of each cycle the folds are open. Lower is buzzier" },
        { "FM Sweep",     "Fold rate drifting across the call. The tungara whine falls steeply" },
        { "Sac Inflate",  "Vocal sac inflation. Bigger sac, lower pitch" },
        { "Low F",        "Frequency at the bottom of the gesture loop" },
        { "High F",       "Frequency at the top of the gesture loop" },
        { "Gesture Phase","Phase between air-sac pressure and tension. Turns upsweeps into downsweeps" },
        { "Wander",       "Natural random variation in the gesture" },
        { "Beak Open",    "Vocal tract resonance, tracking beak opening" },
        { "Trachea",      "Length of the windpipe" },
        { "Biphonation",  "Blends the two syringeal sources." },
        { "Breath",       "Breath noise mixed in" },
        { "F0",           "Fundamental of the hoot" },
        { "Tremulant",    "Vibrato rate. Large owls sit near 6 Hz" },
        { "Trem Depth",   "Vibrato depth" },
        { "Trem Onset",   "How late in the note the tremulant appears" },
        { "2nd Harm",     "Level of the second harmonic. Owls are nearly pure sines" },
        { "Swell",        "How late the note reaches full volume" },
        // --- water / weather ---
        { "Drop Size",    "Bubble radius. A 1 mm bubble rings near 3.3 kHz" },
        { "Bubble Size",  "Bubble radius, setting the Minnaert ring frequency" },
        { "Size Spread",  "Range of bubble sizes" },
        { "Bubble Mix",   "Balance of bubble ring against impact click" },
        { "Impact",       "Sharpness of the drop striking a surface" },
        { "Canopy",       "High-frequency absorption by leaves overhead" },
        { "Density",      "How many events per second" },
        { "Chirp",        "Upward pitch sweep as a bubble collapses" },
        { "Swell Rate",   "Period of the ocean swell" },
        { "Swell Depth",  "How strongly the swell gates bubble density" },
        { "Flow",         "Water speed, driving bubble entrainment" },
        { "Turbulence",   "Untamed component of the flow" },
        { "Speed",        "Wind speed. Aeolian tones track it directly" },
        { "Gustiness",    "Slow random variation in wind speed" },
        { "Obstacle",     "Diameter of what the wind passes. Grass, twig, branch, gap" },
        { "Aeolian Q",    "Sharpness of the vortex-shedding tone" },
        { "Hiss",         "High-frequency turbulence" },
        { "Tilt",         "Overall spectral tilt" },
        { "Crackle Size", "Resonance of the bursting moisture pockets" },
        { "Crackle Rate", "Crackles per second" },
        { "Rumble",       "Low combustion roar. High values give a furnace, low a campfire" },
        { "Spit",         "Rare loud pops" },
        { "Body",         "Resonance of the surrounding hearth or pit" },
        // --- anthrophony ---
        { "Traffic",      "Density of passing vehicles" },
        { "Tyre Tone",    "Centre of the tyre-road noise band" },
        { "Ground Comb",  "Interference with the ground reflection. This is what sells a pass" },
        { "Comb Depth",   "Strength of the ground reflection" },
        { "Engine",       "Level of low engine harmonics" },
        { "Plate F0",     "Fundamental of the steel plate" },
        { "Aspect",       "Plate length against width. Controls how modes cluster" },
        { "Drive",        "Anuran: air pressure through the folds. Plate: how fast the surfaces are dragged across each other" },
        { "Roughness",    "Irregularity of the stick-slip friction" },
        { "Mode Split",   "Detuning between mode pairs, producing slow beating" },
        { "Decay Spread", "How unevenly modes decay. Real plates vary hugely" },
        { "Hardness",     "Hardness of the striking object" },
        { "Pitch",        "Nominal pitch of the bell or bar" },
        { "Strike Hard",  "Harder strikes excite the high inharmonic modes" },
        { "Beating",      "Two polarisations slightly detuned, giving slow beats" },
        { "Roar",         "Broadband layer under the bubbles. Most of surf is this" },
        { "Bed",          "Merged bed of thousands of distant drops" },
        { "Surface Pitch","Fundamental of the panel the rain is landing on" },
        { "Surface Decay","How long the panel rings after each drop" },
        { "Click",        "Sharpness of the drop striking the surface" },
        { "Break",        "How abruptly the wave collapses" },
        { "Radiation",    "High-frequency tilt as the sound leaves the body" },
        { "Rasp",         "Irregularity in the pulse train, giving a creaking edge" },
        { "Sac Q",        "Sharpness of the vocal sac resonance" },
        { "Pitch Rise",   "Depth of the pitch arch across each note. Great horned owls rise 12-17% and fall back" },
        { "Rise Time",    "Where in the note the pitch arch peaks. Measured near the middle in real hoots" },
        { "Inharmonic",   "How far the modes depart from a harmonic series. Low is a membrane (tent, canvas), high is a plate (metal roof, bell)" },
        { "Gravel",       "Broadband rush of water over stones" },
        { "Strike Pos",   "Where the bell is struck" },
        { "Housing",      "Resonance of the machine casing" },
        { "Stretch",      "Inharmonic stretching of the mode series" },
        { "Clatter",      "Irregular secondary impacts from slack in the mechanism" },
        { "Load",         "Broadband noise rising with mechanical load" },
        { "Tone",         "Overall brightness" },
        { "Resonance",    "Sharpness of the housing resonance" },
    };

    auto it = help.find (label);
    return it != help.end() ? it->second : juce::String();
}

//==============================================================================
// DrawCanvas
//==============================================================================
DrawCanvas::DrawCanvas (std::array<float, kDrawPoints>& target, juce::Colour accentColour,
                        juce::String captionText)
    : curve (target), accent (accentColour), caption (std::move (captionText))
{
}

void DrawCanvas::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (r, 8.0f);

    // centre line
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4.0f, r.getRight() - 4.0f);

    auto inner = r.reduced (4.0f);
    juce::Path p;
    for (int i = 0; i < kDrawPoints; ++i)
    {
        const float x = inner.getX() + inner.getWidth() * (float) i / (float) (kDrawPoints - 1);
        const float y = inner.getBottom() - inner.getHeight() * juce::jlimit (0.0f, 1.0f, curve[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }

    g.setColour (accent.brighter (0.4f).withAlpha (0.95f));
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    // soft fill under the curve
    auto filled = p;
    filled.lineTo (inner.getRight(), inner.getBottom());
    filled.lineTo (inner.getX(), inner.getBottom());
    filled.closeSubPath();
    g.setColour (accent.withAlpha (0.13f));
    g.fillPath (filled);

    g.setColour (Palette::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText (caption, r.reduced (7.0f, 3.0f), juce::Justification::topLeft, false);
}

void DrawCanvas::writePoint (juce::Point<float> pt)
{
    auto inner = getLocalBounds().toFloat().reduced (4.0f);
    if (inner.getWidth() < 1.0f) return;

    const float u = juce::jlimit (0.0f, 1.0f, (pt.x - inner.getX()) / inner.getWidth());
    const float v = juce::jlimit (0.0f, 1.0f, 1.0f - (pt.y - inner.getY()) / inner.getHeight());
    const int idx = juce::jlimit (0, kDrawPoints - 1, (int) std::round (u * (kDrawPoints - 1)));

    curve[(size_t) idx] = v;
    // feather into neighbours so freehand drawing stays smooth
    if (idx > 0)               curve[(size_t) idx - 1] = 0.5f * (curve[(size_t) idx - 1] + v);
    if (idx < kDrawPoints - 1) curve[(size_t) idx + 1] = 0.5f * (curve[(size_t) idx + 1] + v);

    if (onEdit) onEdit();
    repaint();
}

void DrawCanvas::mouseDown (const juce::MouseEvent& e) { writePoint (e.position); }
void DrawCanvas::mouseDrag (const juce::MouseEvent& e) { writePoint (e.position); }

void DrawCanvas::mouseDoubleClick (const juce::MouseEvent&)
{
    for (int i = 0; i < kDrawPoints; ++i) curve[(size_t) i] = 0.5f;
    if (onEdit) onEdit();
    repaint();
}

//==============================================================================
// FieldMap
//==============================================================================
FieldMap::FieldMap (SoundscapeEcologyAudioProcessor& p) : processor (p)
{
    startTimerHz (30);
    setTooltip ("Top-down view of the population. Scroll to zoom, drag to pan, "
                "click twice to reset. Dots pulse as individuals call.");
}

void FieldMap::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    zoom = juce::jlimit (0.35f, 8.0f, zoom * (1.0f + w.deltaY * 0.9f));
    repaint();
}

void FieldMap::mouseDown (const juce::MouseEvent& e) { dragStart = e.position - offset; }

void FieldMap::mouseDrag (const juce::MouseEvent& e)
{
    offset = e.position - dragStart;
    repaint();
}

void FieldMap::mouseDoubleClick (const juce::MouseEvent&)
{
    zoom = 1.0f;
    offset = { 0.0f, 0.0f };
    repaint();
}

void FieldMap::timerCallback()
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        const float a = processor.slotActivity[s].load();
        pulse[s] = juce::jmax (a, pulse[s] * 0.90f);
    }
    repaint();
}

void FieldMap::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    const auto centre = r.getCentre() + offset;

    // ---- work out the real extent of the scene, in metres --------------
    // This MIRRORS updateField() in the processor exactly. Previously the map
    // only read Population and ignored Distance and Spread, so two of the
    // three controls that actually place individuals did nothing to the
    // picture.
    float sceneFar = 8.0f;
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (processor.apvts.getRawParameterValue (pid::enabled (s))->load() < 0.5f) continue;
        const float dist = processor.apvts.getRawParameterValue (pid::distance (s))->load();
        const float sprd = processor.apvts.getRawParameterValue (pid::spread (s))->load();
        const float nearD = 1.2f + 18.0f * dist;
        sceneFar = juce::jmax (sceneFar, nearD + 4.0f + 110.0f * sprd);
    }

    const float viewR = juce::jmax (r.getWidth() * 0.30f, r.getHeight() * 0.46f) * zoom;
    const float pxPerMetre = viewR / juce::jmax (1.0f, sceneFar);

    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (4));

    // ---- distance rings, labelled in metres ----------------------------
    auto niceStep = [] (float far)
    {
        const float raw = far / 3.0f;
        const float mag = std::pow (10.0f, std::floor (std::log10 (juce::jmax (1.0f, raw))));
        const float n = raw / mag;
        return mag * (n < 1.5f ? 1.0f : n < 3.5f ? 2.0f : n < 7.5f ? 5.0f : 10.0f);
    };
    const float step = niceStep (sceneFar);

    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    for (float m = step; m <= sceneFar * 1.35f; m += step)
    {
        const float rad = m * pxPerMetre;
        if (rad < 6.0f || rad > viewR * 3.5f) continue;
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (centre), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawText (juce::String ((int) m) + " m",
                    juce::Rectangle<float> (46.0f, 12.0f)
                      .withCentre ({ centre.x, centre.y - rad }),
                    juce::Justification::centred, false);
    }

    // ---- the listener --------------------------------------------------
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (centre));
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre), 1.0f);

    // Colours follow each slot's CLASS, not its position, so the map stays
    // readable when a column is switched to a different phony.
    const juce::Colour classColours[kNumPhony] = { Palette::canopy(), Palette::water(),
                                                   Palette::bark() };
    auto slotPhony = [this] (int s)
    {
        if (auto* p = processor.apvts.getRawParameterValue (pid::phony (s)))
            return juce::jlimit (0, kNumPhony - 1, (int) p->load());
        return s;
    };
    juce::Colour accents[kNumSlots];
    for (int s = 0; s < kNumSlots; ++s) accents[s] = classColours[slotPhony (s)];

    for (int s = 0; s < kNumSlots; ++s)
    {
        if (processor.apvts.getRawParameterValue (pid::enabled (s))->load() < 0.5f) continue;

        const int pop = juce::jlimit (1, kMaxIndividuals,
            (int) processor.apvts.getRawParameterValue (pid::population (s))->load());
        const float distP = processor.apvts.getRawParameterValue (pid::distance (s))->load();
        const float sprdP = processor.apvts.getRawParameterValue (pid::spread (s))->load();
        const float para  = processor.apvts.getRawParameterValue (pid::parallax (s))->load();
        const float order = processor.chorusOrder[s].load();

        const float nearD = 1.2f + 18.0f * distP;
        const float farD  = nearD + 4.0f + 110.0f * sprdP;

        for (int i = 0; i < pop; ++i)
        {
            // identical maths to updateField(), so the picture is the truth
            const float u = (pop > 1) ? (float) i / (float) (pop - 1) : 0.5f;
            const float jitterU = std::fmod (std::sin ((float) i * 12.9898f) * 43758.5453f, 1.0f);
            const float metres = nearD + (farD - nearD)
                                 * std::pow (juce::jlimit (0.0f, 1.0f,
                                       0.5f * (u + std::abs (jitterU))), 1.7f);
            const float az = std::sin ((float) i * 7.7f) * 0.95f;

            // parallax narrows the apparent angle of distant sources, exactly
            // as FieldIndividual::place() does for panning
            const float width = 1.0f / (1.0f + metres / (14.0f * juce::jmax (0.05f, para)));
            const float ang = juce::jlimit (-1.0f, 1.0f, az * width)
                                * juce::MathConstants<float>::pi * 0.85f
                              + (float) s * juce::MathConstants<float>::twoPi / 3.0f * 0.06f;

            const float rad = metres * pxPerMetre;
            const auto pt = centre.translated (std::sin (ang) * rad, -std::cos (ang) * rad);

            const float glow = pulse[s] * (0.35f + 0.65f * order);
            const float zs = juce::jlimit (0.75f, 1.8f, std::sqrt (zoom));

            // Individuals must read clearly AT REST, not only while calling.
            // Against a saturated gradient a faint dot disappears, so each one
            // is a solid disc with a dark rim for contrast, and the call adds
            // a halo and a bright core on top rather than supplying all the
            // visibility.
            const float base = 10.0f * zs;          // was 6 px and still lost
            const float size = base + 7.0f * glow * zs;
            auto dot = juce::Rectangle<float> (size, size).withCentre (pt);

            // soft halo while calling
            if (glow > 0.01f)
            {
                auto halo = dot.expanded (size * (0.5f + 1.4f * glow));
                juce::ColourGradient hg (accents[s].withAlpha (0.42f * glow), pt.x, pt.y,
                                         accents[s].withAlpha (0.0f),
                                         pt.x + halo.getWidth() * 0.5f, pt.y, true);
                g.setGradientFill (hg);
                g.fillEllipse (halo);
            }

            // dark rim first, so the dot separates from any background
            g.setColour (juce::Colours::black.withAlpha (0.62f));
            g.fillEllipse (dot.expanded (2.4f));

            // fully opaque body, brightened hard. These must read at a glance
            // on a saturated gradient, so nothing here is translucent.
            g.setColour (accents[s].brighter (0.55f));
            g.fillEllipse (dot);

            // white outline: the single biggest legibility win against a
            // background that shares the dots' own hue
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawEllipse (dot, 1.4f);

            // bright core when calling
            if (glow > 0.02f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.45f + 0.55f * glow));
                g.fillEllipse (dot.reduced (size * (0.28f + 0.14f * (1.0f - glow))));
            }
        }
    }

    g.restoreState();

    // ---- legend, naming the controls that drive this ------------------
    auto foot = getLocalBounds().reduced (10, 6);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    auto legend = foot.removeFromTop (14);
    for (int s = 0; s < kNumSlots; ++s)
    {
        const bool on = processor.apvts.getRawParameterValue (pid::enabled (s))->load() > 0.5f;
        auto cell = legend.removeFromLeft (104);
        const char* label = phonyName (slotPhony (s));
        g.setColour (accents[s].withAlpha (on ? 0.95f : 0.25f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f)
                         .withCentre ({ (float) cell.getX() + 4.0f, (float) cell.getCentreY() }));
        g.setColour (Palette::text().withAlpha (on ? 0.80f : 0.30f));
        g.drawText (label, cell.withTrimmedLeft (14), juce::Justification::centredLeft, false);
    }

    g.setColour (Palette::textDim());
    g.drawText ("Population, Spread, Distance and Parallax place these  -  "
                "scroll to zoom, drag to pan, click twice to reset",
                foot, juce::Justification::bottomLeft, false);
    g.drawText (juce::String (zoom, 1) + "x",
                foot, juce::Justification::bottomRight, false);
}

//==============================================================================
// SlotPanel
//==============================================================================
SlotPanel::SlotPanel (SoundscapeEcologyAudioProcessor& p, int slotIndex)
    : processor (p), slot (slotIndex)
{
    addAndMakeVisible (modelBox);

    // Index and parameter value are kept IDENTICAL in both directions. The
    // guard flag stops refreshClass()'s repopulation from writing a stale
    // selection back into the parameter.
    if (auto* mp = processor.apvts.getParameter (pid::model (slot)))
    {
        modelAttachment = std::make_unique<juce::ParameterAttachment> (
            *mp,
            [this] (float v)
            {
                const juce::ScopedValueSetter<bool> guard (updatingModelBox, true);
                modelBox.setSelectedItemIndex ((int) std::round (v), juce::dontSendNotification);
                refreshKnobLabels();
            },
            nullptr);
        modelAttachment->sendInitialUpdate();
    }

    modelBox.onChange = [this]
    {
        if (updatingModelBox) return;
        const int idx = modelBox.getSelectedItemIndex();
        if (idx >= 0 && modelAttachment != nullptr)
            modelAttachment->setValueAsCompleteGesture ((float) idx);
        refreshKnobLabels();
    };

    addAndMakeVisible (enableButton);
    buttonAttachments.push_back (std::make_unique<BA> (processor.apvts, pid::enabled (slot), enableButton));

    addAndMakeVisible (randomButton);
    randomButton.setColour (juce::TextButton::buttonColourId, juce::Colours::white.withAlpha (0.12f));
    randomButton.setColour (juce::TextButton::textColourOffId, Palette::text());
    randomButton.setTooltip ("Subtly randomise this slot: nudges the physics, timing and "
                             "placement around their current values. The model, class and "
                             "population are left alone, so the scene stays recognisably itself");
    randomButton.onClick = [this] { processor.randomiseSlot (slot); };

    for (int k = 0; k < kNumModelKnobs; ++k)
    {
        physicsKnobs[(size_t) k].setHorizontal (true);
        addAndMakeVisible (physicsKnobs[(size_t) k]);
        sliderAttachments.push_back (std::make_unique<SA> (processor.apvts, pid::knob (slot, k),
                                                           physicsKnobs[(size_t) k].slider));
    }

    attach (rateKnob,      pid::rate (slot),       "Rate");
    attach (phraseKnob,    pid::phrase (slot),     "Phrase");
    attach (rhythmKnob,    pid::rhythm (slot),     "Rhythm");
    attach (jitterKnob,    pid::jitter (slot),     "Jitter");
    attach (swingKnob,     pid::swing (slot),      "Hold");
    attach (attackKnob,    pid::attack (slot),     "Attack");
    attach (decayKnob,     pid::decay (slot),      "Decay");
    attach (durationKnob,  pid::duration (slot),   "Length");
    attach (popKnob,       pid::population (slot), "Population");
    attach (spreadKnob,    pid::spread (slot),     "Spread");
    attach (distanceKnob,  pid::distance (slot),   "Distance");
    attach (diversityKnob, pid::diversity (slot),  "Diversity");
    attach (syncKnob,      pid::synchrony (slot),  "Synchrony");
    attach (parallaxKnob,  pid::parallax (slot),   "Parallax");
    attach (elevationKnob, pid::elevation (slot),  "Elevation");
    attach (levelKnob,     pid::level (slot),      "Level");

    accent = Palette::canopy();
    pitchCanvas = std::make_unique<DrawCanvas> (processor.pitchCurves[(size_t) slot], accent,
                                                "PITCH / GESTURE");
    ampCanvas   = std::make_unique<DrawCanvas> (processor.ampCurves[(size_t) slot], accent,
                                                "AMPLITUDE");
    addAndMakeVisible (*pitchCanvas);
    addAndMakeVisible (*ampCanvas);

    addAndMakeVisible (drawPitchButton);
    addAndMakeVisible (drawAmpButton);
    buttonAttachments.push_back (std::make_unique<BA> (processor.apvts, pid::drawPitchOn (slot), drawPitchButton));
    buttonAttachments.push_back (std::make_unique<BA> (processor.apvts, pid::drawAmpOn (slot), drawAmpButton));

    attach (drawDepthKnob, pid::drawDepth (slot), "Draw Depth");

    setTooltip ("Click the heading to change this slot's class: "
                "Biophony, Geophony, Anthrophony");
    refreshClass();
}

int SlotPanel::currentPhony() const
{
    if (auto* p = processor.apvts.getRawParameterValue (pid::phony (slot)))
        return juce::jlimit (0, kNumPhony - 1, (int) p->load());
    return slot;
}

juce::Rectangle<int> SlotPanel::titleArea() const
{
    return getLocalBounds().reduced (12, 8).removeFromTop (30);
}

void SlotPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! titleArea().contains (e.getPosition())) return;

    // Cycle the class. No button: the heading IS the control, which keeps the
    // panel uncluttered and makes the relationship obvious.
    if (auto* p = processor.apvts.getParameter (pid::phony (slot)))
    {
        const int next = (currentPhony() + 1) % kNumPhony;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float) next));
        p->endChangeGesture();
    }

    // A new class means a different model list, so reset the selection.
    if (auto* m = processor.apvts.getParameter (pid::model (slot)))
    {
        m->beginChangeGesture();
        m->setValueNotifyingHost (m->getNormalisableRange().convertTo0to1 (0.0f));
        m->endChangeGesture();
    }
    refreshClass();
}

void SlotPanel::mouseMove (const juce::MouseEvent& e)
{
    const bool hot = titleArea().contains (e.getPosition());
    if (hot != titleHot) { titleHot = hot; repaint(); }
    setMouseCursor (hot ? juce::MouseCursor::PointingHandCursor
                        : juce::MouseCursor::NormalCursor);
}

void SlotPanel::mouseExit (const juce::MouseEvent&)
{
    if (titleHot) { titleHot = false; repaint(); }
}

void SlotPanel::refreshClass()
{
    const int phony = currentPhony();
    const juce::Colour accents[kNumPhony] = { Palette::canopy(), Palette::water(),
                                              Palette::bark() };
    accent = accents[phony];

    int count = 0;
    auto* models = modelsForClass (phony, count);

    // Read the authoritative value from the PARAMETER, not from the box's
    // current selection: after a class change the box may still be showing an
    // index from the previous class's list. Clamp it into the new class's
    // range and write it back, so parameter and display cannot drift apart.
    int want = 0;
    if (auto* mp = processor.apvts.getRawParameterValue (pid::model (slot)))
        want = (int) mp->load();
    want = juce::jlimit (0, count - 1, want);

    {
        const juce::ScopedValueSetter<bool> guard (updatingModelBox, true);
        modelBox.clear (juce::dontSendNotification);
        for (int i = 0; i < count; ++i) modelBox.addItem (models[i].name, i + 1);
        modelBox.setSelectedItemIndex (want, juce::dontSendNotification);
    }

    if (modelAttachment != nullptr)
        modelAttachment->setValueAsCompleteGesture ((float) want);

    if (pitchCanvas) pitchCanvas->setAccent (accent);
    if (ampCanvas)   ampCanvas->setAccent (accent);

    refreshKnobLabels();
    repaint();
}

void SlotPanel::attach (EcoKnob& k, const juce::String& paramID, const juce::String& label)
{
    k.setHorizontal (true);
    k.setLabel (label);
    addAndMakeVisible (k);
    sliderAttachments.push_back (std::make_unique<SA> (processor.apvts, paramID, k.slider));
}

void SlotPanel::refreshKnobLabels()
{
    int count = 0;
    auto* models = modelsForClass (currentPhony(), count);
    const int idx = juce::jlimit (0, count - 1, modelBox.getSelectedItemIndex());
    const auto& md = models[idx];

    for (int k = 0; k < kNumModelKnobs; ++k)
    {
        const char* label = md.knobs[(size_t) k];
        physicsKnobs[(size_t) k].setLabel (label != nullptr ? juce::String (label) : juce::String());
        physicsKnobs[(size_t) k].setVisible (label != nullptr);
    }
}

void SlotPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::glass());
    g.fillRoundedRectangle (r, 14.0f);
    juce::ColourGradient sheen (accent.withAlpha (0.22f), r.getCentreX(), r.getY(),
                                juce::Colours::white.withAlpha (0.03f), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (Palette::glassEdge());
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

    // The heading doubles as the class selector: clicking it cycles
    // Biophony -> Geophony -> Anthrophony. It highlights on hover so the
    // affordance is discoverable without adding a button.
    const int phony = currentPhony();
    auto header = titleArea();
    if (titleHot)
    {
        g.setColour (accent.withAlpha (0.20f));
        g.fillRoundedRectangle (header.toFloat().expanded (5.0f, 2.0f), 6.0f);
    }
    auto line1 = header.removeFromTop (16);
    g.setColour (Palette::text());
    g.setFont (juce::Font (juce::FontOptions (16.0f)).withExtraKerningFactor (0.12f));
    g.drawText (juce::String (phonyName (phony)).toUpperCase(), line1,
                juce::Justification::centredLeft, false);

    // a small chevron, only while hovered
    if (titleHot)
    {
        const float cx = (float) line1.getX() + 8.0f
                       + g.getCurrentFont().getStringWidthFloat (
                             juce::String (phonyName (phony)).toUpperCase()) * 1.14f;
        const float cy = (float) line1.getCentreY();
        juce::Path ch;
        ch.startNewSubPath (cx, cy - 4.0f);
        ch.lineTo (cx + 5.0f, cy);
        ch.lineTo (cx, cy + 4.0f);
        g.setColour (accent.brighter (0.8f));
        g.strokePath (ch, juce::PathStrokeType (1.6f));
    }

    g.setColour (accent.brighter (0.75f).withAlpha (0.95f));
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText (phonyDescription (phony), header, juce::Justification::topLeft, false);

    // Section rules drawn from the positions COMPUTED in resized(), so they
    // can never land on top of a knob row.
    for (const auto& rule : rules)
    {
        auto line = getLocalBounds().reduced (12, 0).withY (rule.y).withHeight (11);
        g.setColour (Palette::textDim());
        g.setFont (juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.14f));
        const int textW = 96;
        g.drawText (rule.text, line.removeFromLeft (textW), juce::Justification::centredLeft, false);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine (rule.y + 5, (float) line.getX() + 4.0f, (float) line.getRight());
    }
}

void SlotPanel::resized()
{
    rules.clear();
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (36);                       // heading + subtitle

    auto top = r.removeFromTop (26);
    enableButton.setBounds (top.removeFromRight (38));
    top.removeFromRight (4);
    randomButton.setBounds (top.removeFromRight (40).reduced (0, 2));
    top.removeFromRight (4);
    modelBox.setBounds (top.reduced (0, 1));
    r.removeFromTop (6);

    // Two columns of horizontal bars. Bars buy back the width that rotary
    // labels wasted, so the type can be a legible size.
    const int rowH = 20;
    auto pair = [&r, rowH] (juce::Component* a, juce::Component* b)
    {
        auto row = r.removeFromTop (rowH);
        const int half = row.getWidth() / 2;
        if (a) a->setBounds (row.removeFromLeft (half).reduced (1, 0));
        if (b) b->setBounds (row.reduced (1, 0));
    };

    // ---- physics: 8 ----
    for (int i = 0; i < 8; i += 2)
        pair (&physicsKnobs[(size_t) i], &physicsKnobs[(size_t) i + 1]);

    rules.push_back ({ r.removeFromTop (15).getY(), "ARTICULATION" });

    pair (&rateKnob,     &phraseKnob);
    pair (&rhythmKnob,   &jitterKnob);
    pair (&swingKnob,    &durationKnob);
    pair (&attackKnob,   &decayKnob);
    pair (&levelKnob,    nullptr);

    rules.push_back ({ r.removeFromTop (15).getY(), "THE FIELD" });

    pair (&popKnob,       &spreadKnob);
    pair (&distanceKnob,  &elevationKnob);
    pair (&diversityKnob, &syncKnob);
    pair (&parallaxKnob,  nullptr);

    rules.push_back ({ r.removeFromTop (15).getY(), "CONTOUR" });

    // Toggles and Draw Depth share one row: the two switches only need a
    // third of the width each, so the depth bar fits alongside without
    // costing the canvases any height.
    auto toggles = r.removeFromTop (20);
    const int third = toggles.getWidth() / 3;
    drawPitchButton.setBounds (toggles.removeFromLeft (third).reduced (1));
    drawAmpButton.setBounds (toggles.removeFromLeft (third).reduced (1));
    drawDepthKnob.setBounds (toggles.reduced (1, 0));
    r.removeFromTop (3);

    const int canvasH = juce::jmax (18, (r.getHeight() - 4) / 2);
    pitchCanvas->setBounds (r.removeFromTop (canvasH));
    r.removeFromTop (4);
    ampCanvas->setBounds (r.removeFromTop (canvasH));
}

//==============================================================================
// Editor
//==============================================================================
SoundscapeEcologyAudioProcessorEditor::SoundscapeEcologyAudioProcessorEditor (
        SoundscapeEcologyAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      headerPanel (""), fieldPanel ("The Field"), habitatPanel ("Habitat"),
      midiPanel ("MIDI")
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (headerPanel);

    // ---- preset bar ------------------------------------------------------
    addAndMakeVisible (presetBox);
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && processor.presets != nullptr
            && idx != processor.presets->getCurrentIndex())
            processor.presets->loadByIndex (idx);
    };

    for (auto* b : { &prevButton, &nextButton, &saveButton, &loadButton })
    {
        addAndMakeVisible (b);
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::white.withAlpha (0.12f));
        b->setColour (juce::TextButton::textColourOffId, Palette::text());
    }
    prevButton.onClick = [this] { if (processor.presets) processor.presets->loadNext (-1); };
    nextButton.onClick = [this] { if (processor.presets) processor.presets->loadNext (1); };
    saveButton.onClick = [this] { doSavePreset(); };
    loadButton.onClick = [this] { doLoadPreset(); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current soundscape as an .xml preset");
    loadButton.setTooltip ("Load a preset .xml from anywhere on disk");
    presetBox.setTooltip ("Browse installed presets");

    processor.onPresetChanged = [this]
    {
        refreshPresetBox();
        // a preset can change any slot's class, so rebuild the headings too
        for (auto& s : slots) if (s) s->refreshClass();
    };
    refreshPresetBox();

    attachGlobal (temperatureKnob, pid::temperature, "Temperature");
    attachGlobal (timeOfDayKnob,   pid::timeOfDay,   "Time of Day");
    attachGlobal (humidityKnob,    pid::humidity,    "Humidity");
    attachGlobal (nicheKnob,       pid::niche,       "Niche");
    attachGlobal (weatherKnob,     pid::weather,     "Weather");
    attachGlobal (outputKnob,      pid::output,      "Output");

    // Each panel derives its own heading, colour and model list from its class
    // parameter, so nothing here needs to know which class a slot holds.
    for (int s = 0; s < kNumSlots; ++s)
    {
        slots[s] = std::make_unique<SlotPanel> (processor, s);
        addAndMakeVisible (*slots[s]);
    }

    addAndMakeVisible (fieldPanel);
    fieldPanel.setAccent (Palette::canopy());
    fieldMap = std::make_unique<FieldMap> (processor);
    addAndMakeVisible (*fieldMap);

    addAndMakeVisible (habitatPanel);
    habitatPanel.setAccent (Palette::water());
    addAndMakeVisible (midiPanel);
    midiPanel.setAccent (Palette::lightWarm());
    addAndMakeVisible (midiModeBox);
    midiModeBox.addItemList ({ "Free Run", "Gated", "Gated + Pitch" }, 1);
    midiModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                            (processor.apvts, pid::midiMode, midiModeBox);
    attachGlobal (midiAttackKnob,  pid::midiAttack,  "Attack");
    attachGlobal (midiReleaseKnob, pid::midiRelease, "Release");
    attachGlobal (midiVelKnob,     pid::midiVelSens, "Velocity");
    attachGlobal (midiPitchKnob,   pid::midiPitch,   "Pitch");
    attachGlobal (midiRootKnob,    pid::midiRoot,    "Root");

    attachGlobal (habSizeKnob, pid::habSize,    "Size");
    attachGlobal (habDampKnob, pid::habDamping, "Damping");
    attachGlobal (habMixKnob,  pid::habMix,     "Mix");
    attachGlobal (widthKnob,   pid::width,      "Width");

    // drifting petals
    juce::Random r (2026);
    for (int i = 0; i < 22; ++i)
        petals.push_back ({ r.nextFloat(), r.nextFloat(),
                            r.nextFloat() * juce::MathConstants<float>::twoPi,
                            (r.nextFloat() - 0.5f) * 0.004f,
                            0.00006f + 0.00022f * r.nextFloat(),
                            7.0f + 16.0f * r.nextFloat(),
                            0.05f + 0.14f * r.nextFloat() });

    setResizable (true, true);
    setResizeLimits (1120, 700, 1400, 900);
    setSize (1200, 700);
    startTimerHz (30);
}

SoundscapeEcologyAudioProcessorEditor::~SoundscapeEcologyAudioProcessorEditor()
{
    processor.onPresetChanged = nullptr;
    setLookAndFeel (nullptr);
}

void SoundscapeEcologyAudioProcessorEditor::refreshPresetBox()
{
    if (processor.presets == nullptr) return;
    auto names = processor.presets->getAllNames();
    const int numFactory = processor.presets->getNumFactory();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == numFactory && i < names.size())
            presetBox.addSectionHeading ("User");
        presetBox.addItem (names[i], i + 1);
    }
    presetBox.setSelectedItemIndex (processor.presets->getCurrentIndex(),
                                    juce::dontSendNotification);

    juce::ignoreUnused (numFactory);
}

void SoundscapeEcologyAudioProcessorEditor::doLoadPreset()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a Soundscape Ecology preset",
                                                       PresetManager::getPresetDirectory(),
                                                       "*.xml");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile() && processor.presets != nullptr)
                processor.presets->loadFromFile (file);
        });
}

void SoundscapeEcologyAudioProcessorEditor::doSavePreset()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save Preset",
                                                      "Name this soundscape. It is written to\n"
                                                      + PresetManager::getPresetDirectory().getFullPathName(),
                                                      juce::MessageBoxIconType::NoIcon);
    saveWindow->addTextEditor ("name", processor.presets != nullptr
                                        ? processor.presets->getCurrentName() : "Untitled");
    saveWindow->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [this] (int result)
        {
            if (result == 1 && saveWindow != nullptr && processor.presets != nullptr)
            {
                auto name = saveWindow->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                    processor.presets->saveUserPreset (name);
            }
            saveWindow.reset();
        }), false);
}

void SoundscapeEcologyAudioProcessorEditor::attachGlobal (EcoKnob& k, const juce::String& id,
                                                          const juce::String& label)
{
    k.setLabel (label);
    addAndMakeVisible (k);
    attachments.push_back (std::make_unique<SA> (processor.apvts, id, k.slider));
}

void SoundscapeEcologyAudioProcessorEditor::timerCallback()
{
    driftPhase += 0.0016f;
    if (driftPhase > juce::MathConstants<float>::twoPi) driftPhase -= juce::MathConstants<float>::twoPi;

    for (auto& pt : petals)
    {
        pt.y += pt.speed;
        pt.x += std::sin (driftPhase * 2.0f + pt.y * 9.0f) * 0.00035f;
        pt.rot += pt.spin;
        if (pt.y > 1.08f) { pt.y = -0.08f; pt.x = juce::Random::getSystemRandom().nextFloat(); }
    }
    repaint();
}

void SoundscapeEcologyAudioProcessorEditor::paintBackground (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // The gradient RESPONDS to the synthesis: it warms with temperature and
    // darkens toward night, drifting slowly enough to be noticed only
    // subliminally.
    const float temp = processor.apvts.getRawParameterValue (pid::temperature)->load();
    const float tod  = processor.apvts.getRawParameterValue (pid::timeOfDay)->load();
    const float warm = juce::jlimit (0.0f, 1.0f, (temp + 5.0f) / 47.0f);
    const float night = juce::jlimit (0.0f, 1.0f, 1.0f - tod);

    // Saturated jungle, top to bottom: bright aqua sky -> lagoon -> vivid
    // canopy -> deep jade shadow. Warmth adds sunlit new growth (yellow-green),
    // never ochre. The bottom band is wet dark green, not soil.
    auto top    = Palette::sky().interpolatedWith (Palette::water(), 0.30f + 0.30f * night)
                     .darker (night * 0.62f);
    auto upperMid = Palette::water().interpolatedWith (Palette::canopy(), 0.55f)
                     .darker (night * 0.55f);
    auto middle = Palette::canopy().interpolatedWith (Palette::lightWarm(), 0.15f + 0.35f * warm)
                     .darker (night * 0.52f);
    auto bottom = Palette::moss().interpolatedWith (Palette::loam(), 0.55f)
                     .darker (night * 0.42f);

    juce::ColourGradient grad (top, r.getWidth() * 0.15f, 0.0f,
                               bottom, r.getWidth() * 0.85f, r.getBottom(), false);
    grad.addColour (0.26, upperMid);
    grad.addColour (0.52, middle);
    grad.addColour (0.78, middle.interpolatedWith (bottom, 0.60f));
    g.setGradientFill (grad);
    g.fillAll();

    // soft light blooms, drifting
    auto bloom = [&g, &r, this] (float cx, float cy, float rad, juce::Colour c, float alpha)
    {
        const float dx = std::sin (driftPhase * 0.7f + cx * 5.0f) * 26.0f;
        const float dy = std::cos (driftPhase * 0.5f + cy * 4.0f) * 18.0f;
        const auto centre = juce::Point<float> (r.getWidth() * cx + dx, r.getHeight() * cy + dy);
        juce::ColourGradient gg (c.withAlpha (alpha), centre.x, centre.y,
                                 c.withAlpha (0.0f), centre.x + rad, centre.y, true);
        g.setGradientFill (gg);
        g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (centre));
    };

    bloom (0.20f, 0.12f, r.getWidth() * 0.34f, Palette::lightWarm(), 0.20f * (0.35f + 0.65f * tod));
    bloom (0.82f, 0.30f, r.getWidth() * 0.30f, Palette::sky(),       0.16f);
    bloom (0.55f, 0.92f, r.getWidth() * 0.38f, Palette::canopy(),    0.24f);
    bloom (0.08f, 0.62f, r.getWidth() * 0.26f, Palette::water(),     0.14f);
}

void SoundscapeEcologyAudioProcessorEditor::paintPetals (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    for (const auto& pt : petals)
    {
        juce::Path leaf;
        const float s = pt.size;
        // a simple two-arc leaf, drawn not imported
        leaf.startNewSubPath (0.0f, -s * 0.5f);
        leaf.quadraticTo (s * 0.42f, 0.0f, 0.0f, s * 0.5f);
        leaf.quadraticTo (-s * 0.42f, 0.0f, 0.0f, -s * 0.5f);
        leaf.closeSubPath();

        g.setColour (juce::Colours::white.withAlpha (pt.alpha));
        g.fillPath (leaf, juce::AffineTransform::rotation (pt.rot)
                            .translated (pt.x * r.getWidth(), pt.y * r.getHeight()));
    }
}

void SoundscapeEcologyAudioProcessorEditor::paint (juce::Graphics& g)
{
    paintBackground (g);
    paintPetals (g);

    // wordmark
    auto header = getLocalBounds().reduced (10, 0).withHeight (58);
    auto wm = header.reduced (14, 10);
    g.setColour (Palette::text());
    g.setFont (juce::Font (juce::FontOptions (18.0f)).withExtraKerningFactor (0.26f));
    g.drawText ("SOUNDSCAPE ECOLOGY", wm.removeFromTop (22),
                juce::Justification::centredLeft, false);
    g.setColour (Palette::textDim());
    g.setFont (juce::Font (juce::FontOptions (8.0f)).withExtraKerningFactor (0.18f));
    g.drawText ("BIOPHONY  -  GEOPHONY  -  ANTHROPHONY", wm,
                juce::Justification::topLeft, false);
}

void SoundscapeEcologyAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    // ---- header ----
    auto header = r.removeFromTop (58);
    headerPanel.setBounds (header);
    auto hk = header.reduced (12, 8);

    hk.removeFromLeft (232);                       // wordmark

    // global knobs on the right
    const int kw = 52;
    for (auto* k : { &outputKnob, &weatherKnob, &nicheKnob, &humidityKnob,
                     &timeOfDayKnob, &temperatureKnob })
        k->setBounds (hk.removeFromRight (kw).reduced (2, 0));

    // preset bar fills what is left in the middle
    hk.removeFromLeft (10);
    hk.removeFromRight (10);
    auto presetArea = hk.withSizeKeepingCentre (juce::jmin (460, hk.getWidth()), 28);
    prevButton.setBounds  (presetArea.removeFromLeft (54).reduced (1));
    presetArea.removeFromLeft (4);
    nextButton.setBounds  (presetArea.removeFromLeft (54).reduced (1));
    presetArea.removeFromLeft (6);
    loadButton.setBounds  (presetArea.removeFromRight (58).reduced (1));
    presetArea.removeFromRight (4);
    saveButton.setBounds  (presetArea.removeFromRight (58).reduced (1));
    presetArea.removeFromRight (6);
    presetBox.setBounds (presetArea);

    r.removeFromTop (8);

    // ---- footer ----
    auto footer = r.removeFromBottom (128);
    r.removeFromBottom (8);

    auto midiArea = footer.removeFromRight (juce::jmin (250, footer.getWidth() / 4));
    midiPanel.setBounds (midiArea);
    auto mb = midiArea.reduced (10, 0).withTrimmedTop (midiPanel.titleHeight() - 4)
                      .withTrimmedBottom (8);
    midiModeBox.setBounds (mb.removeFromTop (22).reduced (2, 0));
    mb.removeFromTop (4);
    const int mw = mb.getWidth() / 5;
    midiAttackKnob.setBounds  (mb.removeFromLeft (mw).reduced (1, 0));
    midiReleaseKnob.setBounds (mb.removeFromLeft (mw).reduced (1, 0));
    midiVelKnob.setBounds     (mb.removeFromLeft (mw).reduced (1, 0));
    midiPitchKnob.setBounds   (mb.removeFromLeft (mw).reduced (1, 0));
    midiRootKnob.setBounds    (mb.reduced (1, 0));

    footer.removeFromRight (8);

    auto habitatArea = footer.removeFromRight (juce::jmin (250, footer.getWidth() / 3));
    habitatPanel.setBounds (habitatArea);
    auto hb = habitatArea.reduced (10, 0).withTrimmedTop (habitatPanel.titleHeight())
                         .withTrimmedBottom (8);
    const int hw = hb.getWidth() / 4;
    habSizeKnob.setBounds (hb.removeFromLeft (hw).reduced (2, 0));
    habDampKnob.setBounds (hb.removeFromLeft (hw).reduced (2, 0));
    habMixKnob.setBounds  (hb.removeFromLeft (hw).reduced (2, 0));
    widthKnob.setBounds   (hb.reduced (2, 0));

    footer.removeFromRight (8);
    fieldPanel.setBounds (footer);
    fieldMap->setBounds (footer.reduced (10).withTrimmedTop (fieldPanel.titleHeight() - 4));

    // three slot columns
    const int colW = (r.getWidth() - 16) / 3;
    for (int s = 0; s < kNumSlots; ++s)
    {
        slots[s]->setBounds (r.removeFromLeft (colW));
        if (s < kNumSlots - 1) r.removeFromLeft (8);
    }
}
