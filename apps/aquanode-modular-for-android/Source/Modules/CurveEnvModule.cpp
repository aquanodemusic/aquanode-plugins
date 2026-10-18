#include "CurveEnvModule.h"

using namespace aquanode;

void CurveEnvModule::voiceNoteOn (int v, int, bool retrigger)
{
    if (retrigger && param (pMode) > 0.5f && stage[v] != Stage::Idle && stage[v] != Stage::Release)
        return;   // legato: the running envelope carries on

    // restart from wherever the level is, so a re-struck voice never clicks
    from[v] = level[v];
    t[v] = 0.0;
    stage[v] = param (pDelay) > 0.05f ? Stage::Delay : Stage::Attack;
}

void CurveEnvModule::voiceNoteOff (int v)
{
    if (stage[v] == Stage::Idle || stage[v] == Stage::Release)
        return;
    from[v] = level[v];
    t[v] = 0.0;
    stage[v] = Stage::Release;
}

void CurveEnvModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    const double dt = 1.0 / sampleRate;
    const float sustain = juce::jlimit (0.0f, 1.0f, param (pSustain) * 0.01f);

    switch (stage[v])
    {
        case Stage::Delay:
            t[v] += dt;
            if (t[v] >= param (pDelay) * 0.001)
            {
                t[v] = 0.0;
                stage[v] = Stage::Attack;
            }
            break;

        case Stage::Attack:
        {
            t[v] += dt;
            const float x = (float) (t[v] / juce::jmax (1.0e-4, param (pAttack) * 0.001));
            if (x >= 1.0f)
            {
                level[v] = 1.0f;
                t[v] = 0.0;
                stage[v] = param (pHold) > 0.05f ? Stage::Hold : Stage::Decay;
            }
            else
                level[v] = from[v] + (1.0f - from[v]) * curve (x, param (pACurve));
            break;
        }

        case Stage::Hold:
            level[v] = 1.0f;
            t[v] += dt;
            if (t[v] >= param (pHold) * 0.001)
            {
                t[v] = 0.0;
                stage[v] = Stage::Decay;
            }
            break;

        case Stage::Decay:
        {
            t[v] += dt;
            const float x = (float) (t[v] / juce::jmax (1.0e-3, param (pDecay) * 0.001));
            if (x >= 1.0f)
            {
                level[v] = sustain;
                stage[v] = Stage::Sustain;
            }
            else
                level[v] = sustain + (1.0f - sustain) * (1.0f - curve (x, param (pDCurve)));
            break;
        }

        case Stage::Sustain:
            level[v] = sustain;
            break;

        case Stage::Release:
        {
            t[v] += dt;
            const float x = (float) (t[v] / juce::jmax (1.0e-3, param (pRelease) * 0.001));
            if (x >= 1.0f)
            {
                level[v] = 0.0f;
                stage[v] = Stage::Idle;
            }
            else
                level[v] = from[v] * (1.0f - curve (x, param (pRCurve)));
            break;
        }

        case Stage::Idle:
            level[v] = 0.0f;
            break;
    }

    const float velScale = 1.0f - param (pVelocity) * 0.01f * (1.0f - vel[v]);
    const float out = level[v] * velScale * param (pAmount) * 0.01f;
    outputs[0][0] = out;
    outputs[0][1] = out;
    shown.store (level[v], std::memory_order_relaxed);
}

//==============================================================================
// the shape: delay / attack / hold / decay / sustain / release, to scale
class CurveEnvDisplay : public juce::Component, private juce::Timer
{
public:
    explicit CurveEnvDisplay (CurveEnvModule& m) : module (&m)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<CurveEnvModule*> (module.get());
        if (m == nullptr)
            return;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 4.0f);
        r = r.reduced (6.0f, 6.0f);

        const float D = m->getParameter ("delay"), A = m->getParameter ("attack"), H = m->getParameter ("hold");
        const float Dc = m->getParameter ("decay"), S = m->getParameter ("sustain") * 0.01f, R = m->getParameter ("release");
        const float ca = m->getParameter ("aCurve"), cd = m->getParameter ("dCurve"), cr = m->getParameter ("rCurve");

        // times on a square-root scale so short and long segments both read
        auto w = [] (float ms) { return std::sqrt (juce::jmax (0.0f, ms)); };
        const float sustainW = juce::jmax (8.0f, 0.18f * (w (A) + w (Dc) + w (R)));
        const float total = w (D) + w (A) + w (H) + w (Dc) + sustainW + w (R);
        const float sx = r.getWidth() / juce::jmax (1.0f, total);
        auto Y = [&] (float lvl) { return r.getBottom() - lvl * r.getHeight(); };

        juce::Path p;
        float x = r.getX();
        p.startNewSubPath (x, Y (0.0f));
        x += w (D) * sx;
        p.lineTo (x, Y (0.0f));
        const int steps = 24;
        const float xa = x, wa = w (A) * sx;
        for (int i = 1; i <= steps; ++i)
            p.lineTo (xa + wa * (float) i / steps, Y (CurveEnvModule::curve ((float) i / steps, ca)));
        x = xa + wa + w (H) * sx;
        p.lineTo (x, Y (1.0f));
        const float xd = x, wdd = w (Dc) * sx;
        for (int i = 1; i <= steps; ++i)
            p.lineTo (xd + wdd * (float) i / steps, Y (S + (1.0f - S) * (1.0f - CurveEnvModule::curve ((float) i / steps, cd))));
        x = xd + wdd + sustainW * sx;
        p.lineTo (x, Y (S));
        const float xr = x, wr = w (R) * sx;
        for (int i = 1; i <= steps; ++i)
            p.lineTo (xr + wr * (float) i / steps, Y (S * (1.0f - CurveEnvModule::curve ((float) i / steps, cr))));

        juce::Path fill (p);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();
        const auto c = juce::Colour (0xff7dd3fc);
        g.setColour (c.withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        const float lvl = m->displayLevel();
        if (lvl > 0.001f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (r.getX(), Y (lvl) - 0.5f, r.getWidth(), 1.0f));
        }
    }

private:
    void timerCallback() override { repaint(); }
    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> CurveEnvModule::createExtraContentComponent()
{
    return std::make_unique<CurveEnvDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor curveEnvDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.curveenv";
    d.displayName = "Curve Env";
    d.description =
        "A per-voice DAHDSR envelope with a curve on attack, decay and release (0 = straight, negative = "
        "fast then slow, positive = slow then fast). Amount scales and flips the output, so it can drive "
        "an amplitude, a cutoff or a Pitch In (20% = an octave there); a strongly negative decay curve "
        "is the classic kick pitch drop. Velocity sets how much key velocity scales it.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 2;
    d.sockets = { modOut ("envOut", "Env Out") };
    d.params = {
        makeRotary ("delay",   "Delay",   0.0f, 4000.0f, 0.0f, 0, "ms"),
        makeRotary ("attack",  "Attack",  0.1f, 20000.0f, 5.0f, 0, "ms", true),
        makeRotary ("hold",    "Hold",    0.0f, 4000.0f, 0.0f, 0, "ms"),
        makeRotary ("decay",   "Decay",   1.0f, 20000.0f, 400.0f, 0, "ms", true),
        makeRotary ("sustain", "Sustain", 0.0f, 100.0f, 70.0f, 0, "%"),

        makeRotary ("release", "Release", 1.0f, 20000.0f, 300.0f, 1, "ms", true),
        makeRotary ("aCurve",  "A Curve", -15.0f, 15.0f, 0.0f, 1),
        makeRotary ("dCurve",  "D Curve", -15.0f, 15.0f, -2.0f, 1),
        makeRotary ("rCurve",  "R Curve", -15.0f, 15.0f, -2.0f, 1),
        makeRotary ("amount",  "Amount",  -100.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),

        makeRotary ("velocity", "Velocity", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f),
        makeCombo ("mode", "Mode", { "Retrigger", "Legato" }, 0, 2, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CurveEnvModule, curveEnvDescriptor)
