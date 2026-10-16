#include "DXOperatorModule.h"
#include "DXDisplays.h"

using namespace aquanode;
namespace dx = dxnative;

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
    extFbPrev[v] = 0.0f;
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
        // Key Sync Off: the DX7's oscillators run free, so a note starts
        // wherever the oscillator happens to be - an arbitrary phase
        phase[v] = param (pKeySync) > 0.5f ? 0.0 : (double) phaseRandom.nextFloat();
        gain[v] = gainStep[v] = 0.0f;
        fb1[v] = fb2[v] = 0.0f;
        extFbPrev[v] = 0.0f;
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

juce::String DXOperatorModule::titleSuffix() const
{
    const int n = iparam (getParameter ("opNum"));
    return n > 0 ? " " + juce::String (n) : juce::String();
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
        controlUpdate (v, inputs[inAm][0]);
        ctrlLeft[v] = kCtrlBlock - 1;
    }

    // frequency: ratio x note pitch, or fixed Hz; detune on top
    const float semis = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                      + pitchBendSemitones()
                      + inputs[inPitch][0] * 60.0f;     // KeyTrack convention
    const int coarse = juce::jlimit (0, 31, iparam (param (pCoarse)));
    const int fine = juce::jlimit (0, 99, iparam (param (pFine)));
    const float detuneMul = std::exp2 (dx::detuneCents (juce::jlimit (0, 14, iparam (param (pDetune)))) / 1200.0f);
    const double hz = param (pMode) > 0.5f
                        ? (double) dx::fixedHzFromCoarseFine (coarse, fine) * detuneMul
                        : midiNoteToHz ((double) semis) * dx::ratioFromCoarseFine (coarse, fine) * detuneMul;

    // phase modulation: another DX Operator's output at full scale swings the
    // phase by the engine's 2.07 cycles; feedback averages the last two
    // outputs and scales by 2^(fb-7), as the engine does. With Fb In patched
    // the feedback comes from that operator instead of from this one.
    float mod = 0.5f * (inputs[inFm][0] + inputs[inFm][1]) * (dx::kModCycles / kOutScale);
    const int fb = juce::jlimit (0, 7, iparam (param (pFeedback)));
    const bool externalFb = isInputConnected (inFb);
    if (fb > 0)
    {
        if (externalFb)
        {
            // back to the other operator's raw (but carrier-attenuated) output
            const float x = 0.5f * (inputs[inFb][0] + inputs[inFb][1]) / kOutScale;
            mod += (x + extFbPrev[v]) * 0.5f * dx::kModCycles * std::exp2 ((float) fb - 7.0f);
            extFbPrev[v] = x;
        }
        else
        {
            mod += (fb1[v] + fb2[v]) * 0.5f * dx::kModCycles * std::exp2 ((float) fb - 7.0f);
        }
    }

    phase[v] += hz / sampleRate;
    phase[v] -= std::floor (phase[v]);

    // Out / : the OPS divides a carrier by the carrier count inside the
    // operator, so the feedback tap below reads the attenuated signal too
    const float com = 1.0f / (float) (juce::jlimit (0, 5, iparam (param (pCom))) + 1);   // "/1" .. "/6"

    gain[v] = juce::jmax (0.0f, gain[v] + gainStep[v]);
    const float s = dx::sineTable() ((float) phase[v] + mod) * gain[v] * com;
    fb2[v] = fb1[v];
    fb1[v] = s;

    const float out = s * kOutScale * voiceGain;
    outputs[0] = { out, out };
}

std::unique_ptr<juce::Component> DXOperatorModule::createExtraContentComponent()
{
    return std::make_unique<dxui::EnvelopeView> (*this);
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
        "algorithm (the FM row of Preset Generation wires them for you). Its envelope is built in (no "
        "ADSR needed). AM In and Pitch In take a DX LFO and a DX Pitch EG; Fb In takes another "
        "operator for the cross-operator feedback of algorithms 4 and 6. Out / divides a carrier "
        "by the algorithm's carrier count, as the DX7 does.";
    d.section = ModuleSection::Oscillator;
    d.family = ModuleFamily::DX;
    d.sidebarOrder = 20;
    d.sockets = {
        audioIn  ("fmIn",      "FM In"),
        audioIn  ("fbIn",      "Fb In"),
        modIn    ("amIn",      "AM In"),
        modIn    ("pitchIn",   "Pitch In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };

    juce::StringArray detune, bp, com;
    for (int i = -7; i <= 7; ++i) detune.add (i > 0 ? "+" + juce::String (i) : juce::String (i));
    for (int i = 0; i < 100; ++i) bp.add (midiNoteName (21 + i));   // BP 0 = A-1
    for (int i = 1; i <= 6; ++i) com.add ("/" + juce::String (i));
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
        makeRotary ("glide",  "Glide",  0.0f, 1000.0f, 0.0f, 5, "ms").visibleWhen ("voices", 1.0f),
        makeSteppedList ("com", "Out /", com, 0, 5),
        makeCombo ("keySync", "Key Sync", { "Free Run", "Key Sync" }, 1, 5, 2),

        // which operator of a generated patch this is (titles it "DX Operator 3")
        makeRotary ("opNum", "Op", 0.0f, 6.0f, 0.0f, 6, {}, false, 1.0f).hide().noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DXOperatorModule, dxOperatorDescriptor)
