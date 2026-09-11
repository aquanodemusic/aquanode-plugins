#include "EuclidModule.h"

using namespace aquanode;

//==============================================================================
// tiny read-only pattern display: one dot per step, filled = pulse
//==============================================================================
class EuclidDisplay : public juce::Component,
                      private juce::Timer
{
public:
    explicit EuclidDisplay (EuclidModule& m) : module (&m)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<EuclidModule*> (module.get());
        if (m == nullptr)
            return;

        const int steps = juce::jlimit (1, EuclidModule::kMaxSteps, (int) m->getParameter ("steps"));
        const int fills = (int) m->getParameter ("fills");
        const int rotate = (int) m->getParameter ("rotate");
        const int playing = m->getDisplayStep();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        const float w = (float) getWidth() / (float) steps;
        const float r = juce::jmin (w * 0.35f, getHeight() * 0.32f);

        for (int i = 0; i < steps; ++i)
        {
            const float cx = (i + 0.5f) * w;
            const float cy = getHeight() * 0.5f;
            const bool on = EuclidModule::pulseAt (i, steps, fills, rotate);

            g.setColour (on ? juce::Colour (0xff53b06a) : juce::Colours::white.withAlpha (0.18f));
            g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);

            if (i == playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawEllipse (cx - r - 1.5f, cy - r - 1.5f, 2.0f * r + 3.0f, 2.0f * r + 3.0f, 1.2f);
            }
        }
    }

private:
    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> EuclidModule::createExtraContentComponent()
{
    return std::make_unique<EuclidDisplay> (*this);
}

//==============================================================================
void EuclidModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int steps = juce::jlimit (1, kMaxSteps, (int) param (pSteps));

    const float resetIn = inputs[1][0];
    if (resetIn > 0.5f && lastReset <= 0.5f)
    {
        stepIndex = 0;
        phase = 0.0;
    }
    lastReset = resetIn;

    if (stepIndex >= steps)
        stepIndex = 0;

    bool gateWindow;

    if (isInputConnected (0))
    {
        const float clockIn = inputs[0][0];
        if (clockIn > 0.5f && lastClock <= 0.5f)
            stepIndex = (stepIndex + 1) % steps;
        lastClock = clockIn;
        gateWindow = clockIn > 0.5f;
    }
    else
    {
        const double bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;
        const double stepSeconds = (60.0 / bpm) * seqDivisionBeats ((int) param (pDivision));
        phase += 1.0 / (stepSeconds * sampleRate);
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            stepIndex = (stepIndex + 1) % steps;
        }
        gateWindow = phase < param (pGateLen) * 0.01;
    }

    displayStep.store (stepIndex, std::memory_order_relaxed);

    const bool on = pulseAt (stepIndex, steps, (int) param (pFills), (int) param (pRotate));

    const float step = (float) (1.0 / (0.0005 * sampleRate));
    const float target = (gateWindow && on) ? 1.0f : 0.0f;
    if (gateLevel < target)      gateLevel = juce::jmin (target, gateLevel + step);
    else if (gateLevel > target) gateLevel = juce::jmax (target, gateLevel - step);

    outputs[0][0] = gateLevel;
    outputs[0][1] = gateLevel;
}

static ModuleDescriptor euclidDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.euclid";
    d.displayName = "Euclid";
    d.description =
        "Distributes Fills pulses as evenly as possible over steps - the Euclidean "
        "patterns behind many world rhythms - with rotation. Clock In takes a Clock module "
        "(unpatched, it runs from the host tempo); Gate Out feeds any drum's Trig In. Reset In "
        "restarts the pattern on a rising edge - a Gate module there restarts it whenever you "
        "play, a slower Clock keeps it bar-locked.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 12;
    d.sockets = {
        modIn  ("clockIn", "Clock In"),
        modIn  ("resetIn", "Reset In"),
        modOut ("gateOut", "Gate Out")
    };
    d.params = {
        makeRotary ("steps",  "Steps",  1.0f, 16.0f, 16.0f, 0, {}, false, 1.0f),
        makeRotary ("fills",  "Fills",  0.0f, 16.0f, 4.0f,  0, {}, false, 1.0f),
        makeRotary ("rotate", "Rotate", 0.0f, 15.0f, 0.0f,  0, {}, false, 1.0f),
        makeSteppedList ("division", "Division", seqDivisionChoices(), 8, 0),
        makeRotary ("gateLen", "Gate Len", 5.0f, 95.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (EuclidModule, euclidDescriptor)
