#include "StepSeqModule.h"

using namespace aquanode;

//==============================================================================
// step display / editor - top area: pitch bars (drag, -12..+12 semitones,
// double-click = 0), bottom row: gate cells (click toggles)
//==============================================================================
class StepSeqDisplay : public juce::Component,
                       private juce::Timer
{
public:
    explicit StepSeqDisplay (StepSeqModule& m) : module (&m)
    {
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<StepSeqModule*> (module.get());
        if (m == nullptr)
            return;

        const int numSteps = juce::jlimit (1, StepSeqModule::kMaxSteps,
                                           (int) m->getParameter ("steps"));
        const int playing = m->getDisplayStep();
        const float w = (float) getWidth() / (float) StepSeqModule::kMaxSteps;
        const float barsH = (float) getHeight() - gateRowH - 2.0f;
        const float midY = barsH * 0.5f;

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        for (int i = 0; i < StepSeqModule::kMaxSteps; ++i)
        {
            const float x = i * w;
            const bool inRange = i < numSteps;
            const bool isPlaying = inRange && i == playing;
            const float pitch = m->getParameter ("s" + juce::String (i));   // -12..12
            const bool gateOn = m->getParameter ("g" + juce::String (i)) > 0.5f;

            // pitch bar (from centre line)
            const float extent = juce::jlimit (-1.0f, 1.0f, pitch / 12.0f) * (midY - 3.0f);
            juce::Rectangle<float> bar (x + 2.0f,
                                        extent >= 0.0f ? midY - extent : midY,
                                        w - 4.0f,
                                        juce::jmax (2.0f, std::abs (extent)));

            auto barColour = inRange ? juce::Colour (0xffd970b0) : juce::Colours::darkgrey;
            g.setColour (barColour.withAlpha (isPlaying ? 1.0f : (inRange ? 0.75f : 0.3f)));
            g.fillRect (bar);

            // centre line tick
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (x + 1.0f, midY - 0.5f, w - 2.0f, 1.0f);

            // gate cell
            juce::Rectangle<float> cell (x + 2.0f, barsH + 2.0f, w - 4.0f, gateRowH - 2.0f);
            g.setColour (gateOn ? juce::Colour (0xff53b06a).withAlpha (inRange ? 0.9f : 0.35f)
                                : juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (cell, 2.0f);

            if (isPlaying)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRect (juce::Rectangle<float> (x + 0.5f, 0.5f, w - 1.0f,
                                                    (float) getHeight() - 1.0f), 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStep = stepAt (e.x);
        if (e.y >= getHeight() - (int) gateRowH)   // gate row: toggle
        {
            if (auto* m = module.get())
            {
                const auto id = "g" + juce::String (dragStep);
                m->setParameter (id, m->getParameter (id) > 0.5f ? 0.0f : 1.0f);
            }
            dragStep = -1;   // no pitch dragging from the gate row
            repaint();
            return;
        }
        applyPitchDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStep >= 0)
        {
            dragStep = stepAt (e.x);   // allow painting across steps
            applyPitchDrag (e);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < getHeight() - (int) gateRowH)
        {
            if (auto* m = module.get())
                m->setParameter ("s" + juce::String (stepAt (e.x)), 0.0f);
            repaint();
        }
    }

private:
    static constexpr float gateRowH = 18.0f;

    int stepAt (int x) const
    {
        return juce::jlimit (0, StepSeqModule::kMaxSteps - 1,
                             x * StepSeqModule::kMaxSteps / juce::jmax (1, getWidth()));
    }

    void applyPitchDrag (const juce::MouseEvent& e)
    {
        auto* m = module.get();
        if (m == nullptr || dragStep < 0)
            return;

        const float barsH = (float) getHeight() - gateRowH - 2.0f;
        const float midY = barsH * 0.5f;
        const float norm = juce::jlimit (-1.0f, 1.0f, (midY - (float) e.y) / (midY - 3.0f));
        m->setParameter ("s" + juce::String (dragStep), std::round (norm * 12.0f));
        repaint();
    }

    void timerCallback() override { repaint(); }   // playhead + external changes

    juce::WeakReference<SynthModule> module;
    int dragStep { -1 };
};

std::unique_ptr<juce::Component> StepSeqModule::createExtraContentComponent()
{
    return std::make_unique<StepSeqDisplay> (*this);
}

//==============================================================================
void StepSeqModule::advance (int numSteps, int direction)
{
    switch (direction)
    {
        case 0:  stepIndex = (stepIndex + 1) % numSteps; break;              // Forward
        case 1:  stepIndex = (stepIndex + numSteps - 1) % numSteps; break;   // Backward
        case 2:                                                              // Pendulum
            if (numSteps < 2) { stepIndex = 0; break; }
            if (pendulumUp)
            {
                if (++stepIndex >= numSteps - 1) { stepIndex = numSteps - 1; pendulumUp = false; }
            }
            else
            {
                if (--stepIndex <= 0) { stepIndex = 0; pendulumUp = true; }
            }
            break;
        default: stepIndex = random.nextInt (numSteps); break;               // Random
    }
}

void StepSeqModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int numSteps = juce::jlimit (1, kMaxSteps, (int) param (pSteps));
    const int direction = (int) param (pDirection);
    const double gateLen = param (pGateLen) * 0.01;

    // reset edge -> back to the start
    const float resetIn = inputs[1][0];
    if (resetIn > 0.5f && lastReset <= 0.5f)
    {
        stepIndex = 0;
        pendulumUp = true;
        phase = 0.0;
    }
    lastReset = resetIn;

    if (stepIndex >= numSteps)
        stepIndex = 0;

    bool gateWindow;

    if (isInputConnected (0))
    {
        // external clock: advance on rising edges, gate mirrors the clock
        const float clockIn = inputs[0][0];
        if (clockIn > 0.5f && lastClock <= 0.5f)
            advance (numSteps, direction);
        lastClock = clockIn;
        clockHigh = clockIn > 0.5f;
        gateWindow = clockHigh;
    }
    else
    {
        // internal clock from host tempo
        const double bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;
        const double stepSeconds = (60.0 / bpm) * seqDivisionBeats ((int) param (pDivision));
        phase += 1.0 / (stepSeconds * sampleRate);
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            advance (numSteps, direction);
        }
        gateWindow = phase < gateLen;
    }

    displayStep.store (stepIndex, std::memory_order_relaxed);

    const float pitchSemis = param (4 + stepIndex);                    // s0..s15
    const bool stepGate = param (4 + kMaxSteps + stepIndex) > 0.5f;    // g0..g15

    // 0.5 ms anti-click ramp on the gate
    const float step = (float) (1.0 / (0.0005 * sampleRate));
    const float target = (gateWindow && stepGate) ? 1.0f : 0.0f;
    if (gateLevel < target)      gateLevel = juce::jmin (target, gateLevel + step);
    else if (gateLevel > target) gateLevel = juce::jmax (target, gateLevel - step);

    const float pitchOut = pitchSemis / 60.0f;   // KeyTrack-compatible scaling
    outputs[0][0] = pitchOut;
    outputs[0][1] = pitchOut;
    outputs[1][0] = gateLevel;
    outputs[1][1] = gateLevel;
}

static ModuleDescriptor stepSeqDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.stepseq";
    d.displayName = "Step Seq";
    d.description =
        "A 16-step pitch/gate sequencer, edited right in the module - drag for pitch, "
        "bottom row for gates, click twice to reset a step. Pitch Out uses the KeyTrack "
        "semitones/60 scaling, so it drives filter cutoffs and Pluck / Modal Drum / True Comb "
        "pitch inputs 1:1; Clock In takes a Clock module (unpatched, it runs from the host "
        "tempo).";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 11;
    d.sockets = {
        modIn  ("clockIn", "Clock In"),
        modIn  ("resetIn", "Reset In"),
        modOut ("pitchOut", "Pitch Out"),
        modOut ("gateOut", "Gate Out")
    };
    d.params = {
        makeRotary ("steps", "Steps", 1.0f, 16.0f, 16.0f, 0, {}, false, 1.0f),
        makeSteppedList ("division", "Division", seqDivisionChoices(), 8, 0),
        makeCombo  ("direction", "Direction", { "Forward", "Backward", "Pendulum", "Random" }, 0, 0, 2),
        makeRotary ("gateLen", "Gate Len", 5.0f, 95.0f, 50.0f, 0, "%")
    };
    for (int i = 0; i < StepSeqModule::kMaxSteps; ++i)
        d.params.push_back (makeRotary ("s" + juce::String (i), "S" + juce::String (i + 1),
                                        -12.0f, 12.0f, 0.0f, 9, {}, false, 1.0f).hide());
    for (int i = 0; i < StepSeqModule::kMaxSteps; ++i)
        d.params.push_back (makeRotary ("g" + juce::String (i), "G" + juce::String (i + 1),
                                        0.0f, 1.0f, 1.0f, 9, {}, false, 1.0f).hide());
    return d;
}

AQUANODE_REGISTER_MODULE (StepSeqModule, stepSeqDescriptor)
