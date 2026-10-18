#include "WindModule.h"

using namespace aquanode;

namespace
{
    // per instrument: the period it sounds at is periodPerDelay x the loop
    // delay + periodOffset samples (measured - the jet and the loop's filters
    // add their own delay), so the delay for a pitch is worked back from that;
    // then the jet's share of the delay, the pipe's loss filter, the breath
    // noise and the output level
    struct Voicing { double periodPerDelay, periodOffset, jetRatio; float lossPole, noise, outGain; };
    const Voicing kVoicing[3] = {
        { 0.65969, 1.125, 0.32, 0.70f, 0.10f, 0.55f },   // flute: the jet-driven pipe sounds at 1.5 x its length
        { 0.66604, 1.808, 0.34, 0.80f, 0.22f, 0.65f },   // pan flute: a slower jet, a duller pipe, more air
        { 2.0,     1.0,   0.0,  0.0f,  0.06f, 0.75f }    // clarinet: the cylindrical bore is half the length
    };

    inline float whiteNoise (juce::uint32& state)
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
}

WindModule::WindModule()
{
    for (auto& v : voices)
    {
        v.bore.assign (kBoreSize, 0.0f);
        v.jet.assign (kJetSize, 0.0f);
    }
}

void WindModule::reset()
{
    pool.resetAll();
    glide.resetAll();
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void WindModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    gateOn[v] = false;
    velocity[v] = 1.0f;
    auto& s = voices[v];
    std::fill (s.bore.begin(), s.bore.end(), 0.0f);
    std::fill (s.jet.begin(), s.jet.end(), 0.0f);
    s.boreWrite = s.jetWrite = 0;
    s.loop = s.lastBore = s.dcIn = s.dcOut = s.outLp = s.outDcIn = s.outDcOut = s.breath = 0.0f;
    s.vibPhase = 0.0;
    s.sinceOn = 0.0;
    s.noise = 0x1234567u + (juce::uint32) v * 7919u;
}

void WindModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    gateOn[v] = true;
    if (! retrigger)
    {
        voices[v].sinceOn = 0.0;
        voices[v].vibPhase = 0.0;
    }
}

void WindModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

float WindModule::readDelay (const std::vector<float>& buf, int write, double delay, int size) const
{
    double rp = (double) write - delay;
    while (rp < 0.0)
        rp += size;
    const int i0 = (int) rp % size;
    const int i1 = (i0 + 1) % size;
    const float frac = (float) (rp - std::floor (rp));
    return buf[(size_t) i0] + frac * (buf[(size_t) i1] - buf[(size_t) i0]);
}

void WindModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    outputs[0][0] = outputs[0][1] = 0.0f;
    if (pool.isSilent (v))
        return;

    auto& s = voices[v];
    const int inst = juce::jlimit (0, 2, (int) std::lround (param (pInstrument)));
    const auto& tune = kVoicing[inst];
    const float dt = (float) (1.0 / sampleRate);
    s.sinceOn += dt;

    // ---- the breath ---------------------------------------------------------
    float env;
    if (isInputConnected (1))
    {
        env = inputs[1][0] * velocity[v];
    }
    else
    {
        // rises over Attack, falls over Release (exponential, ~99 % in the time)
        const float attack = juce::jmax (1.0f, param (pAttack)) * 0.001f;
        const float release = juce::jmax (1.0f, param (pRelease)) * 0.001f;
        // (velocity only nudges it: the pipe speaks in a narrow pressure window)
        const float target = gateOn[v] ? 0.9f + 0.1f * velocity[v] : 0.0f;
        const float coef = 1.0f - std::exp (-4.6f * dt / (gateOn[v] ? attack : release));
        s.breath += (target - s.breath) * coef;
        env = s.breath;
    }

    // vibrato: fades in over 300 ms once its delay has passed
    const float vibDepth = param (pVibrato) * 0.01f;
    const float vibIn = juce::jlimit (0.0f, 1.0f, (float) ((s.sinceOn - param (pVibDelay) * 0.001) / 0.3));
    s.vibPhase += param (pVibRate) / sampleRate;
    if (s.vibPhase >= 1.0) s.vibPhase -= 1.0;
    const float vib = vibDepth * vibIn * (float) std::sin (juce::MathConstants<double>::twoPi * s.vibPhase);

    // Breath 0..100 % spans the pipe's playing range: just speaking .. pushed
    const float amount = juce::jlimit (0.0f, 1.0f, (param (pBreath) + param (pBreathMod) * inputs[0][0]) * 0.01f);
    const float pressure = inst == clarinet ? 0.62f + 0.5f * amount : 0.93f + 0.25f * amount;
    float breath = pressure * env * (1.0f + 0.08f * vib);
    const float noiseAmt = tune.noise * (0.3f + 1.7f * param (pNoise) * 0.01f);
    breath += breath * whiteNoise (s.noise) * noiseAmt;

    // ---- the pipe -----------------------------------------------------------
    const double semis = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate) + pitchBendSemitones();
    const double f0 = midiNoteToHz (semis) * (1.0 + 0.003 * vib);
    const double boreDelay = juce::jlimit (4.0, (double) kBoreSize - 4.0,
                                           (sampleRate / f0 - tune.periodOffset) / tune.periodPerDelay);
    const float tone = param (pTone) * 0.01f;
    const float boreOut = readDelay (s.bore, s.boreWrite, boreDelay, kBoreSize);

    float into;
    if (inst == clarinet)
    {
        // a one-zero low-pass on the reflection (the bore's losses)
        const float reflected = -0.95f * 0.5f * (boreOut + s.lastBore);
        s.lastBore = boreOut;
        const float diff = reflected - breath;
        // the reed: closes as the pressure across it grows
        const float slope = -0.3f;
        const float reed = juce::jlimit (-1.0f, 1.0f, 0.7f + slope * diff);
        into = breath + diff * reed;
    }
    else
    {
        // the pipe's loss filter (one pole) and a DC blocker, on the reflection
        s.loop = (1.0f - tune.lossPole) * boreOut + tune.lossPole * s.loop;
        const float lossy = -s.loop;
        s.dcOut = lossy - s.dcIn + 0.995f * s.dcOut;
        s.dcIn = lossy;
        const float reflected = s.dcOut;

        // the jet: crosses the mouthpiece (its delay), then flips in or out
        const double jetDelay = juce::jlimit (2.0, (double) kJetSize - 4.0, boreDelay * tune.jetRatio);
        s.jet[(size_t) s.jetWrite] = breath - 0.5f * reflected;
        const float jetOut = readDelay (s.jet, s.jetWrite, jetDelay, kJetSize);
        if (++s.jetWrite >= kJetSize) s.jetWrite = 0;
        const float flip = juce::jlimit (-1.0f, 1.0f, jetOut * (jetOut * jetOut - 1.0f));
        into = flip + 0.5f * reflected;
    }

    s.bore[(size_t) s.boreWrite] = juce::jlimit (-4.0f, 4.0f, into);
    if (++s.boreWrite >= kBoreSize) s.boreWrite = 0;

    // a DC blocker and a gentle low-pass on the way out, then a soft limit
    const float ac = boreOut - s.outDcIn + 0.997f * s.outDcOut;
    s.outDcIn = boreOut;
    s.outDcOut = ac;
    // Tone: the bell's brightness (a one-pole from ~1.5 kHz up to open)
    const float bright = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                          * (float) (1500.0 * std::pow (12.0, (double) tone)) / (float) sampleRate);
    s.outLp += bright * (ac - s.outLp);
    const float out = std::tanh (s.outLp * tune.outGain) * param (pVolume) * voiceGain;
    outputs[0][0] = outputs[0][1] = std::isfinite (out) ? out : 0.0f;
}

//==============================================================================
static ModuleDescriptor windDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.wind";
    d.displayName = "Wind";
    d.description =
        "Blown pipes, physically modelled: Flute (an air jet over an open pipe), Pan Flute (a slower jet "
        "over a duller pipe - hollow and airy) and Clarinet (a reed over a cylindrical bore). Breath is the playing "
        "control: it rises over Attack, falls over Release, velocity sets how hard it starts, and Breath In "
        "adds to it (mod wheel or aftertouch from Midi Controls) - more breath, a louder, "
        "fuller note. Tone runs from soft to bright. Noise is the air around "
        "the tone, Vibrato fades in after its Delay. Env In, patched, replaces the breath envelope.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 15;
    d.sockets = {
        modIn    ("breathIn",  "Breath In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",     "Volume",     0.0f, 1.0f, 0.7f, 0),
        makeCombo  ("instrument", "Instrument", { "Flute", "Pan Flute", "Clarinet" }, 0, 0, 3),
        makeRotary ("breath",     "Breath",     0.0f, 150.0f, 85.0f, 0, "%"),
        makeRotary ("tone",       "Tone",       0.0f, 100.0f, 50.0f, 0, "%"),

        makeRotary ("noise",      "Noise",      0.0f, 100.0f, 25.0f, 1, "%"),
        makeRotary ("vibrato",    "Vibrato",    0.0f, 100.0f, 20.0f, 1, "%"),
        makeRotary ("vibRate",    "Vib Rate",   1.0f, 9.0f, 5.0f, 1, "Hz"),
        makeRotary ("vibDelay",   "Vib Delay",  0.0f, 2000.0f, 350.0f, 1, "ms"),

        makeRotary ("attack",     "Attack",     1.0f, 2000.0f, 60.0f, 2, "ms", true),
        makeRotary ("release",    "Release",    1.0f, 3000.0f, 120.0f, 2, "ms", true),
        makeRotary ("breathMod",  "Breath Mod", -100.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("voices",     "Voices",     1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",      "Glide",      0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (WindModule, windDescriptor)
