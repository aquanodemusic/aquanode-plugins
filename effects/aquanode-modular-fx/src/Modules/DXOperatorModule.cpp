#include "DXOperatorModule.h"

using namespace aquanode;
namespace dx = vdx7native;

namespace
{
    int iparam (float v) { return (int) std::lround (v); }
}

//==============================================================================
void DXOperatorModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    env[v].reset (-dx::kEgRangeDb);
    env[v] = dx::OperatorEnv();          // finished, silent
    phase[v] = 0.0;
    gain[v] = gainStep[v] = 0.0f;
    fb1[v] = fb2[v] = 0.0f;
    ctrlLeft[v] = 0;
    noteOf[v] = 60;
    velocity[v] = 100;
}

void DXOperatorModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    noteOf[v] = note;

    // Every note starts its envelope from silence, exactly like the native
    // engine's NoteState::start(). A fresh voice also gets the engine's
    // oscillator key sync (phase 0) and an empty feedback memory; a held
    // voice struck again keeps its phase and lets the gain glide down over
    // one control block instead of jumping, so the restart cannot click.
    env[v].reset (-dx::kEgRangeDb);
    if (! retrigger)
    {
        phase[v] = 0.0;
        gain[v] = gainStep[v] = 0.0f;
        fb1[v] = fb2[v] = 0.0f;
    }
    env[v].keyOn();
    ctrlLeft[v] = 0;   // evaluate the envelope on the very first sample
}

void DXOperatorModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    env[v].keyOff();
}

double DXOperatorModule::voiceTailSeconds() const
{
    // R4 carries the note from wherever it is down to silence: the whole
    // 96 dB range at R4's speed (rate scaling only ever makes it quicker)
    const float dbPerSec = dx::rateToDbPerSecond ((float) iparam (param (pR4)));
    return juce::jlimit (0.05, 30.0, (double) (dx::kEgRangeDb / juce::jmax (0.1f, dbPerSec)) + 0.05);
}

void DXOperatorModule::envelopeSettings (uint8_t rates[4], uint8_t levels[4]) const
{
    for (int i = 0; i < 4; ++i)
    {
        rates[i]  = (uint8_t) juce::jlimit (0, 99, iparam (getParameter ("r" + juce::String (i + 1))));
        levels[i] = (uint8_t) juce::jlimit (0, 99, iparam (getParameter ("l" + juce::String (i + 1))));
    }
}

//==============================================================================
// control rate: the engine's per-block evaluation, for one operator
void DXOperatorModule::controlUpdate (int v, float amIn)
{
    const int note = noteOf[v];

    uint8_t rates[4], levels[4];
    for (int i = 0; i < 4; ++i)
    {
        rates[i]  = (uint8_t) juce::jlimit (0, 99, iparam (param (pR1 + i)));
        levels[i] = (uint8_t) juce::jlimit (0, 99, iparam (param (pL1 + i)));
    }
    env[v].configure (rates, levels, dx::rateScaleOffset (juce::jlimit (0, 7, iparam (param (pRateScale))), note));
    const float egDb = env[v].tick ((float) kCtrlBlock / (float) sampleRate);

    // output level + keyboard scaling in the DX7's level units (clipped at
    // full scale), then velocity, envelope and amplitude modulation in dB
    const int scaled = std::min (127, dx::scaleOutLevel (iparam (param (pLevel)))
                                 + dx::keyScaleUnits (note,
                                                      juce::jlimit (0, 99, iparam (param (pBreakPoint))),
                                                      juce::jlimit (0, 99, iparam (param (pLeftDepth))),
                                                      juce::jlimit (0, 99, iparam (param (pRightDepth))),
                                                      juce::jlimit (0, 3, iparam (param (pLeftCurve))),
                                                      juce::jlimit (0, 3, iparam (param (pRightCurve)))));
    const float staticDb = dx::unitsToDb (scaled)
                         + dx::velocityDb (velocity[v], juce::jlimit (0, 7, iparam (param (pVelSens))));
    const float db = staticDb + egDb
                   - juce::jlimit (0.0f, 1.0f, amIn) * dx::amsDb (juce::jlimit (0, 3, iparam (param (pAms))));

    const float target = (db <= dx::silenceDb()) ? 0.0f : dx::dbToGain (std::min (db, 6.0f));
    gainStep[v] = (target - gain[v]) / (float) kCtrlBlock;
}

void DXOperatorModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0] = { 0.0f, 0.0f };
        return;
    }

    if (--ctrlLeft[v] < 0)
    {
        controlUpdate (v, inputs[1][0]);
        ctrlLeft[v] = kCtrlBlock - 1;
    }

    // frequency: ratio x note pitch, or fixed Hz; detune on top
    const float semis = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                      + pitchBendSemitones();
    const int coarse = juce::jlimit (0, 31, iparam (param (pCoarse)));
    const int fine = juce::jlimit (0, 99, iparam (param (pFine)));
    const float detuneMul = std::exp2 (dx::detuneCents (juce::jlimit (0, 14, iparam (param (pDetune)))) / 1200.0f);
    const double hz = param (pMode) > 0.5f
                        ? (double) dx::fixedHzFromCoarseFine (coarse, fine) * detuneMul
                        : midiNoteToHz ((double) semis) * dx::ratioFromCoarseFine (coarse, fine) * detuneMul;

    // phase modulation: another DX Operator's output at full scale swings the
    // phase by the engine's 2.07 cycles; self-feedback averages the last two
    // outputs and scales by 2^(fb-7), as the engine does
    float mod = 0.5f * (inputs[0][0] + inputs[0][1]) * (dx::kModCycles / kOutScale);
    const int fb = juce::jlimit (0, 7, iparam (param (pFeedback)));
    if (fb > 0)
        mod += (fb1[v] + fb2[v]) * 0.5f * dx::kModCycles * std::exp2 ((float) fb - 7.0f);

    phase[v] += hz / sampleRate;
    phase[v] -= std::floor (phase[v]);

    gain[v] = juce::jmax (0.0f, gain[v] + gainStep[v]);
    const float s = dx::sineTable() ((float) phase[v] + mod) * gain[v];
    fb2[v] = fb1[v];
    fb1[v] = s;

    const float out = s * kOutScale * voiceGain;
    outputs[0] = { out, out };
}

//==============================================================================
// the envelope display: the operator's envelope for a note held at middle C
// until it settles, then released at the dashed line - a simple take on the
// Virtual DX7 envelope view
//==============================================================================
class DXEnvelopeView : public juce::Component, private juce::Timer
{
public:
    explicit DXEnvelopeView (DXOperatorModule& m) : module (&m) { startTimerHz (10); timerCallback(); }

    void paint (juce::Graphics& g) override
    {
        const auto bezel = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff101410));
        g.fillRoundedRectangle (bezel, 4.0f);

        const auto plot = bezel.reduced (6.0f, 6.0f).withTrimmedBottom (9.0f);
        const auto phosphor = juce::Colour (0xff9fe870);

        // 24 dB grid lines (32 DX7 level steps)
        g.setColour (phosphor.withAlpha (0.08f));
        for (float db : { -24.0f, -48.0f, -72.0f })
            g.drawHorizontalLine ((int) yFor (db, plot), plot.getX(), plot.getRight());

        if (points.empty() || totalSecs <= 0.0f)
            return;

        // key-off marker
        const float kx = plot.getX() + plot.getWidth() * keyOffSecs / totalSecs;
        juce::Path line, dashed;
        line.startNewSubPath (kx, plot.getY());
        line.lineTo (kx, plot.getBottom());
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (phosphor.withAlpha (0.35f));
        g.fillPath (dashed);

        juce::Path curve;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (points.size() - 1);
            const float y = yFor (points[i], plot);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (phosphor.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (3.0f));   // glow
        g.setColour (phosphor);
        g.strokePath (curve, juce::PathStrokeType (1.2f));

        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (phosphor.withAlpha (0.55f));
        const float lx = juce::jlimit (plot.getX(), plot.getRight() - 44.0f, kx - 22.0f);
        g.drawText ("KEY OFF", juce::Rectangle<float> (lx, plot.getY() - 1.0f, 44.0f, 9.0f),
                    juce::Justification::centred, false);
        g.drawText (juce::String (totalSecs, totalSecs < 10.0f ? 2 : 1) + " s",
                    bezel.withTrimmedRight (6.0f).withTrimmedTop (bezel.getHeight() - 11.0f),
                    juce::Justification::centredRight, false);
    }

private:
    static float yFor (float db, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db + vdx7native::kEgRangeDb) / vdx7native::kEgRangeDb);
        return plot.getBottom() - t * plot.getHeight();
    }

    void timerCallback() override
    {
        auto* op = dynamic_cast<DXOperatorModule*> (module.get());
        if (op == nullptr)
            return;

        uint8_t r[4], l[4];
        op->envelopeSettings (r, l);
        const int rs = juce::jlimit (0, 7, (int) std::lround (op->getParameter ("rateScale")));
        juce::String key;
        for (int i = 0; i < 4; ++i) key << (int) r[i] << ',' << (int) l[i] << ',';
        key << rs;
        if (key == lastKey)
            return;
        lastKey = key;

        // Run the engine's own envelope: hold until it has settled (or 10 s),
        // then release until it has finished - or settled, since a patch with
        // L4 above 0 holds its release level forever, as on a DX7.
        vdx7native::OperatorEnv e;
        e.configure (r, l, vdx7native::rateScaleOffset (rs, 60));
        e.reset (-vdx7native::kEgRangeDb);
        e.keyOn();

        constexpr float dt = 0.0005f;              // fine enough for a 12 ms R=99 segment
        constexpr int settledSteps = 160;          // 80 ms without change
        trace.clear();
        auto run = [&] (bool releasing)
        {
            float prev = 1.0e9f;
            int still = 0;
            for (int i = 0; i < 20000; ++i)
            {
                if (releasing && e.finished())
                    break;
                const float db = e.tick (dt);
                trace.push_back (db);
                still = std::abs (db - prev) < 1.0e-4f ? still + 1 : 0;
                prev = db;
                if (i > 100 && still > settledSteps)
                    break;
            }
        };
        run (false);
        keyOffSecs = (float) trace.size() * dt;
        e.keyOff();
        run (true);
        trace.push_back (e.levelDb());
        totalSecs = (float) trace.size() * dt;

        resample();
    }

    // Fit the trace to the display width by LINEAR INTERPOLATION between the
    // simulated points: picking the nearest point made fast segments (a 12 ms
    // R4=99 release on a 90 ms plot) come out as a staircase.
    void resample()
    {
        const int w = juce::jmax (32, getWidth() - 12);
        points.assign ((size_t) w, -vdx7native::kEgRangeDb);
        if (trace.size() < 2)
            return;

        const double last = (double) (trace.size() - 1);
        for (int x = 0; x < w; ++x)
        {
            const double pos = last * (double) x / (double) (w - 1);
            const auto i0 = (size_t) pos;
            const auto i1 = juce::jmin (trace.size() - 1, i0 + 1);
            const float f = (float) (pos - (double) i0);
            points[(size_t) x] = trace[i0] + (trace[i1] - trace[i0]) * f;
        }
        repaint();
    }

    void resized() override { resample(); }

    juce::WeakReference<SynthModule> module;
    std::vector<float> trace, points;
    float keyOffSecs { 0.0f }, totalSecs { 0.0f };
    juce::String lastKey;
};

std::unique_ptr<juce::Component> DXOperatorModule::createExtraContentComponent()
{
    return std::make_unique<DXEnvelopeView> (*this);
}

//==============================================================================
static ModuleDescriptor dxOperatorDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.dxop";
    d.displayName = "DX Operator";
    d.description =
        "One operator of a FM engine closely modelled of a real DX7, with all 21 of a DX7 operator's "
        "controls and the engine's own level, scaling and envelope behaviour. Patch one's Audio "
        "Out into another's FM In to make it the modulator - stacking them builds any DX "
        "algorithm. Its envelope is built in (no ADSR needed); AM In takes an LFO for the DX7's "
        "amplitude modulation, scaled by Amplitude.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 20;
    d.sockets = {
        audioIn  ("fmIn",      "FM In"),
        modIn    ("amIn",      "AM In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };

    juce::StringArray detune, bp;
    for (int i = -7; i <= 7; ++i) detune.add (i > 0 ? "+" + juce::String (i) : juce::String (i));
    for (int i = 0; i < 100; ++i) bp.add (midiNoteName (21 + i));   // BP 0 = A-1
    const juce::StringArray curves { "-LIN", "-EXP", "+EXP", "+LIN" };

    d.params = {
        makeCombo ("mode", "Mode", { "Ratio", "Fixed" }, 0, 0, 2),
        makeRotary ("coarse", "Coarse", 0.0f, 31.0f, 1.0f, 0, {}, false, 1.0f),
        makeRotary ("fine",   "Fine",   0.0f, 99.0f, 0.0f, 0, {}, false, 1.0f),
        makeSteppedList ("detune", "Detune", detune, 7, 0),

        makeRotary ("level",     "Level",     0.0f, 99.0f, 99.0f, 1, {}, false, 1.0f),
        makeRotary ("feedback",  "Feedback",  0.0f, 7.0f, 0.0f, 1, {}, false, 1.0f),
        makeRotary ("velSens",   "Vel Sens",  0.0f, 7.0f, 0.0f, 1, {}, false, 1.0f),
        makeRotary ("ams",       "AMS",       0.0f, 3.0f, 0.0f, 1, {}, false, 1.0f),
        makeRotary ("rateScale", "Rate Scl",  0.0f, 7.0f, 0.0f, 1, {}, false, 1.0f),

        makeRotary ("r1", "R1", 0.0f, 99.0f, 99.0f, 2, {}, false, 1.0f),
        makeRotary ("r2", "R2", 0.0f, 99.0f, 99.0f, 2, {}, false, 1.0f),
        makeRotary ("r3", "R3", 0.0f, 99.0f, 99.0f, 2, {}, false, 1.0f),
        makeRotary ("r4", "R4", 0.0f, 99.0f, 99.0f, 2, {}, false, 1.0f),
        makeRotary ("l1", "L1", 0.0f, 99.0f, 99.0f, 3, {}, false, 1.0f),
        makeRotary ("l2", "L2", 0.0f, 99.0f, 99.0f, 3, {}, false, 1.0f),
        makeRotary ("l3", "L3", 0.0f, 99.0f, 99.0f, 3, {}, false, 1.0f),
        makeRotary ("l4", "L4", 0.0f, 99.0f, 0.0f, 3, {}, false, 1.0f),

        makeSteppedList ("breakPoint", "Break Pt", bp, 39, 4),
        makeRotary ("leftDepth",  "L Depth", 0.0f, 99.0f, 0.0f, 4, {}, false, 1.0f),
        makeRotary ("rightDepth", "R Depth", 0.0f, 99.0f, 0.0f, 4, {}, false, 1.0f),
        makeSteppedList ("leftCurve",  "L Curve", curves, 0, 4),
        makeSteppedList ("rightCurve", "R Curve", curves, 0, 4),

        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 5, {}, false, 1.0f).noMod(),
        makeRotary ("glide",  "Glide",  0.0f, 1000.0f, 0.0f, 5, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DXOperatorModule, dxOperatorDescriptor)
