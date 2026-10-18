#include "DXEnvelopeModule.h"
#include "DXDisplays.h"

using namespace aquanode;
namespace dx = dxnative;

namespace
{
    int iparam (float v) { return (int) std::lround (v); }
}

void DXEnvelopeModule::voiceReset (int v)
{
    env[v] = dx::OperatorEnv();   // finished, silent
    gain[v] = gainStep[v] = 0.0f;
    ctrlLeft[v] = 0;
    noteOf[v] = 60;
    velocity[v] = 100;
}

void DXEnvelopeModule::voiceNoteOn (int v, int note, bool retrigger)
{
    juce::ignoreUnused (retrigger);
    noteOf[v] = note;
    env[v].reset (-dx::kEgRangeDb);   // every note starts from silence, as on the DX7
    env[v].keyOn();
    ctrlLeft[v] = 0;
}

double DXEnvelopeModule::voiceTailSeconds() const
{
    const float dbPerSec = dx::rateToDbPerSecond ((float) iparam (param (pR4)));
    return juce::jlimit (0.05, 30.0, (double) (dx::kEgRangeDb / juce::jmax (0.1f, dbPerSec)) + 0.05);
}

void DXEnvelopeModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    if (--ctrlLeft[v] < 0)
    {
        uint8_t rates[4], levels[4];
        for (int i = 0; i < 4; ++i)
        {
            rates[i]  = (uint8_t) juce::jlimit (0, 99, iparam (param (pR1 + i)));
            levels[i] = (uint8_t) juce::jlimit (0, 99, iparam (param (pL1 + i)));
        }
        env[v].configure (rates, levels,
                          dx::rateScaleOffset (juce::jlimit (0, 7, iparam (param (pRateScale))), noteOf[v]));
        const float egDb = env[v].tick ((float) kCtrlBlock / (float) sampleRate);

        const float db = dx::levelToDb (juce::jlimit (0, 99, iparam (param (pLevel))))
                       + dx::velocityDb (velocity[v], juce::jlimit (0, 7, iparam (param (pVelSens))))
                       + egDb;
        const float target = (db <= dx::silenceDb()) ? 0.0f : dx::dbToGain (juce::jmin (db, 0.0f));
        gainStep[v] = (target - gain[v]) / (float) kCtrlBlock;
        ctrlLeft[v] = kCtrlBlock - 1;
    }

    gain[v] = juce::jlimit (0.0f, 1.0f, gain[v] + gainStep[v]);
    outputs[0] = { gain[v], gain[v] };
}

std::unique_ptr<juce::Component> DXEnvelopeModule::createExtraContentComponent()
{
    return std::make_unique<dxui::EnvelopeView> (*this);
}

//==============================================================================
static ModuleDescriptor dxEnvelopeDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.dxenv";
    d.displayName = "DX Envelope";
    d.description =
        "A DX7 operator's envelope on its own: four rates and four levels with the DX7's log-domain "
        "segments and snappy attack, plus output level, rate scaling and velocity sensitivity. Env Out is "
        "a 0..1 gain, so it drops into any generator's Env In for a DX-style pluck or swell.";
    d.section = ModuleSection::Utility;
    d.family = ModuleFamily::DX;
    d.sidebarOrder = 42;
    d.sockets = { modOut ("envOut", "Env Out") };
    d.params = {
        makeRotary ("r1", "R1", 0.0f, 99.0f, 95.0f, 0, {}, false, 1.0f),
        makeRotary ("r2", "R2", 0.0f, 99.0f, 40.0f, 0, {}, false, 1.0f),
        makeRotary ("r3", "R3", 0.0f, 99.0f, 30.0f, 0, {}, false, 1.0f),
        makeRotary ("r4", "R4", 0.0f, 99.0f, 60.0f, 0, {}, false, 1.0f),
        makeRotary ("l1", "L1", 0.0f, 99.0f, 99.0f, 1, {}, false, 1.0f),
        makeRotary ("l2", "L2", 0.0f, 99.0f, 92.0f, 1, {}, false, 1.0f),
        makeRotary ("l3", "L3", 0.0f, 99.0f, 85.0f, 1, {}, false, 1.0f),
        makeRotary ("l4", "L4", 0.0f, 99.0f, 0.0f, 1, {}, false, 1.0f),
        makeRotary ("level",     "Level",    0.0f, 99.0f, 99.0f, 2, {}, false, 1.0f),
        makeRotary ("rateScale", "Rate Scl", 0.0f, 7.0f, 0.0f, 2, {}, false, 1.0f),
        makeRotary ("velSens",   "Vel Sens", 0.0f, 7.0f, 0.0f, 2, {}, false, 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DXEnvelopeModule, dxEnvelopeDescriptor)
