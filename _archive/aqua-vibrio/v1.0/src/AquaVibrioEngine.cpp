#include "AquaVibrioEngine.h"

namespace aquavibrio
{

using namespace aquanode;
using scaling::unit;
using scaling::bipolar;

    // Module parameters by name, allocation free: a parameter's index depends
    // only on the module TYPE, so each call site resolves it once (a static,
    // looked up on the first call - prepare() makes that call, off the audio
    // thread) and writes by index from then on.
    #define SETP(module, id, value) \
        do { static const int setpIndex = (module).paramIndex (id); \
             (module).setParameterAt (setpIndex, (value)); } while (false)

namespace
{
    // x*|x|: a bipolar amount curve with fine control around the centre,
    // which is where modulation depths are almost always set.
    inline float curve (float x) { return x * std::abs (x); }

    inline float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }

    // One unit of matrix amount times one unit of source moves the destination
    // knob by half its travel - the same depth an amount of +63 has on the
    // hardware.
    constexpr float kMatrixFullScale = 64.0f / 127.0f;

    // The clock lists, as fractions of a whole note, in the order the choice
    // lists present them.
    constexpr float kLfoClock[] = { 0.0f, 1.f/64, 1.f/32, 1.f/16, 1.f/8, 1.f/4, 1.f/2, 3.f/64, 3.f/32,
                                    3.f/16, 3.f/8, 1.f/24, 1.f/12, 1.f/6, 1.f/3, 2.f/3, 3.f/4,
                                    1.f, 2.f, 4.f, 8.f, 16.f };
    constexpr float kDelayClock[] = { 0.0f, 1.f/64, 1.f/32, 1.f/16, 1.f/8, 1.f/4, 1.f/2, 3.f/64, 3.f/32,
                                      3.f/16, 3.f/8, 1.f/24, 1.f/12, 1.f/6, 1.f/3, 2.f/3, 3.f/4 };
    constexpr float kArpClock[] = { 1.f/16, 1.f/128, 1.f/64, 1.f/32, 1.f/16, 1.f/8, 1.f/4, 3.f/128,
                                    3.f/64, 3.f/32, 3.f/16, 1.f/48, 1.f/24, 1.f/12, 1.f/6, 1.f/3,
                                    3.f/8, 1.f/2 };

    template <size_t N>
    inline float listValue (const float (&list)[N], int index)
    {
        return list[(size_t) juce::jlimit (0, (int) N - 1, index)];
    }
}

//==============================================================================
Engine::Engine()
{
    auto& factory = ModuleFactory::instance();

    osc1 = factory.createInstance ("osc.virus");
    osc2 = factory.createInstance ("osc.virus");
    osc3 = factory.createInstance ("osc.virus");
    subOsc = factory.createInstance ("osc.basic");
    noise = factory.createInstance ("osc.noise");

    filter1 = factory.createInstance ("filter.aquafilter");
    filter2 = factory.createInstance ("filter.aquafilter");

    envFilter = factory.createInstance ("util.virusenv");
    envAmp = factory.createInstance ("util.virusenv");
    env3 = factory.createInstance ("util.virusenv");
    env4 = factory.createInstance ("util.virusenv");
    lfo1 = factory.createInstance ("util.viruslfo");
    lfo2 = factory.createInstance ("util.viruslfo");
    lfo3 = factory.createInstance ("util.viruslfo");

    distortion = factory.createInstance ("fx.virusdistortion");
    phaser = factory.createInstance ("fx.phaser");
    chorus = factory.createInstance ("fx.chorus");
    delay = factory.createInstance ("fx.adelaysr");
    reverb = factory.createInstance ("fx.reverb");
    tapeWobble = factory.createInstance ("fx.tapewobble");
    vowelBank = factory.createInstance ("filter.formant");
    combBank = factory.createInstance ("filter.combfilter");
    vocoder = factory.createInstance ("fx.vocode");
    granulator = factory.createInstance ("fx.granulation");
    envFollow = factory.createInstance ("util.envfollow");
    stereoWidth = factory.createInstance ("fx.stereowidth");

    // A module type that failed to register is almost always a .cpp missing
    // from the build. Say so loudly - in every build configuration - instead
    // of crashing on a null pointer in the audio thread later.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(),
                     filter1.get(), filter2.get(), envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                     lfo1.get(), lfo2.get(), lfo3.get(), distortion.get(), phaser.get(), chorus.get(),
                     delay.get(), reverb.get(), tapeWobble.get(), vowelBank.get(), combBank.get(),
                     vocoder.get(), envFollow.get(), stereoWidth.get(), granulator.get() })
    {
        if (m == nullptr)
        {
            juce::Logger::writeToLog ("FATAL: Aqua Vibrio could not create one of its modules - "
                                      "a module .cpp is probably missing from the build.");
            jassertfalse;
            std::abort();
        }
    }

    lfoTyped = { static_cast<VirusLfoModule*> (lfo1.get()),
                 static_cast<VirusLfoModule*> (lfo2.get()),
                 static_cast<VirusLfoModule*> (lfo3.get()) };
    ampEnvTyped = static_cast<VirusEnvModule*> (envAmp.get());

    const auto oscIdx = [] (SynthModule& m)
    {
        return OscIdx { m.paramIndex ("fmRatio"), m.paramIndex ("wave"), m.paramIndex ("shape"),
                        m.paramIndex ("pulseWidth"), m.paramIndex ("sync"), m.paramIndex ("syncRatio"),
                        m.paramIndex ("detune"), m.paramIndex ("spread"), m.paramIndex ("unison"),
                        m.paramIndex ("mode"), m.paramIndex ("formant"), m.paramIndex ("volume") };
    };
    o1 = oscIdx (*osc1); o2 = oscIdx (*osc2); o3 = oscIdx (*osc3);
    subRatio = subOsc->paramIndex ("fmRatio");

    f1 = { filter1->paramIndex ("cutoff"), filter1->paramIndex ("resonance"), filter1->paramIndex ("type") };
    f2 = { filter2->paramIndex ("cutoff"), filter2->paramIndex ("resonance"), filter2->paramIndex ("type") };

    eIdx = { envAmp->paramIndex ("attack"), envAmp->paramIndex ("decay"), envAmp->paramIndex ("sustain"),
             envAmp->paramIndex ("release"), envAmp->paramIndex ("sustainTime") };
    lIdx = { lfo1->paramIndex ("rate"), lfo1->paramIndex ("symmetry") };

    distDrive = distortion->paramIndex ("drive");
    phaserWet = phaser->paramIndex ("dryWet");
    chorusWet = chorus->paramIndex ("dryWet");
    combFreq = combBank->paramIndex ("freq");
    vowelPos = vowelBank->paramIndex ("vowel");

    // Things that never change: the engine owns voice allocation, gating and
    // glide, so the modules are told to stay out of the way.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get() })
    {
        m->setParameter ("voices", (float) kMaxVoices);
        m->setParameter ("glide", 0.0f);
    }

    for (auto* m : { osc1.get(), osc2.get(), osc3.get() })
    {
        m->setParameter ("volume", 1.0f);
        m->setParameter ("drift", 0.5f);          // a whisper of movement, not a chorus
        m->setParameter ("syncEnvAmount", 0.0f);
        m->setParameter ("syncOut", 0.0f);
    }

    subOsc->setParameter ("volume", 1.0f);
    noise->setParameter ("type", 0.0f);           // white; Noise Colour is our own filter
    noise->setParameter ("level", 1.0f);

    filter1->setParameter ("drive", 0.0f);
    filter2->setParameter ("drive", 0.0f);
    filter1->setParameter ("resCurve", 0.0f);     // quadratic: resonance spread over the whole knob
    filter2->setParameter ("resCurve", 0.0f);
    filter1->setParameter ("modDepth", 0.0f);
    filter2->setParameter ("modDepth", 0.0f);

    tapeWobble->setParameter ("hiss", 0.0f);
    tapeWobble->setParameter ("dropouts", 0.0f);
    tapeWobble->setParameter ("dryWet", 100.0f);
    delay->setParameter ("dryWet", 100.0f);       // delay and reverb are sends: we mix them in
    reverb->setParameter ("dryWet", 100.0f);
    vowelBank->setParameter ("dryWet", 100.0f);
    combBank->setParameter ("dryWet", 100.0f);
    combBank->setParameter ("blend", 100.0f);
    distortion->setParameter ("level", 1.0f);

    freqShifter.setMix (1.0f);

    controllers.fill (0.0f);
    controllers[7] = 1.0f;    // channel volume rests at full
    controllers[8] = 0.5f;    // balance rests centred
    controllers[11] = 1.0f;   // expression rests at full

    monoStack.reserve (128);
    buildMatrixTables();
}

//==============================================================================
// Matrix tables are built by NAME from the same choice lists the panel shows,
// so a destination always does what its label says.
void Engine::buildMatrixTables()
{
    destForByte.fill (Dest::None);
    srcForByte.fill (Src::Off);

    static const std::pair<const char*, Dest> destNames[] =
    {
        { "Patch Volume", Dest::PatchVolume }, { "Panorama", Dest::Panorama }, { "Transpose", Dest::Transpose },
        { "Osc 1 Shape/Index", Dest::Osc1Shape }, { "Osc 1 Pulse Width", Dest::Osc1Pw },
        { "Osc 1 Pitch", Dest::Osc1Pitch }, { "Osc 1 Wavetable Index", Dest::Osc1WtIndex },
        { "Osc 2 Shape/Index", Dest::Osc2Shape }, { "Osc 2 Pulse Width", Dest::Osc2Pw },
        { "Osc 2 Pitch", Dest::Osc2Pitch }, { "Osc 2 Detune", Dest::Osc2Detune },
        { "Osc 2 FM Amount", Dest::Osc2Fm }, { "FiltEnv > Osc 2 Pitch", Dest::FiltEnvOsc2Pitch },
        { "FiltEnv>FM/Sync", Dest::FiltEnvFmSync }, { "Osc 2 Wavetable Index", Dest::Osc2WtIndex },
        { "Osc Balance", Dest::OscBalance }, { "Sub Osc Volume", Dest::SubVolume },
        { "Osc Volume", Dest::OscVolume }, { "Noise Volume", Dest::NoiseVolume },
        { "Ring Modulator", Dest::RingMod }, { "Osc 3 Volume", Dest::Osc3Volume },
        { "Osc 3 Pitch", Dest::Osc3Pitch }, { "Noise Color", Dest::NoiseColor },
        { "Punch Intensity", Dest::Punch },
        { "Filter 1 Cutoff", Dest::Cutoff1 }, { "Filter 2 Cutoff", Dest::Cutoff2 },
        { "Filter 1 Resonance", Dest::Reso1 }, { "Filter 2 Resonance", Dest::Reso2 },
        { "Filter 1 Env Amount", Dest::Filter1EnvAmt }, { "Filter 2 Env Amount", Dest::Filter2EnvAmt },
        { "Filter Balance", Dest::FilterBalance },
        { "Filter Env Attack", Dest::FiltAttack }, { "Filter Env Decay", Dest::FiltDecay },
        { "Filter Env Sustain", Dest::FiltSustain }, { "Filter Env Slope", Dest::FiltSlope },
        { "Filter Env Release", Dest::FiltRelease },
        { "Amp Env Attack", Dest::AmpAttack }, { "Amp Env Decay", Dest::AmpDecay },
        { "Amp Env Sustain", Dest::AmpSustain }, { "Amp Env Slope", Dest::AmpSlope },
        { "Amp Env Release", Dest::AmpRelease },
        { "LFO 1 Rate", Dest::Lfo1Rate }, { "LFO 1 Contour", Dest::Lfo1Contour },
        { "LFO 1>Osc 1 Pitch", Dest::Lfo1Osc1 }, { "LFO 1>Osc 2 Pitch", Dest::Lfo1Osc2 },
        { "LFO 1>Pulse Width", Dest::Lfo1Pw }, { "LFO 1>Resonance", Dest::Lfo1Reso },
        { "LFO 1>Filter Gain", Dest::Lfo1FiltGain }, { "LFO 1 Assign Amt", Dest::Lfo1Amt },
        { "LFO 2 Rate", Dest::Lfo2Rate }, { "LFO 2 Contour", Dest::Lfo2Contour },
        { "LFO 2>Shape", Dest::Lfo2Shape }, { "LFO 2>FM Amount", Dest::Lfo2Fm },
        { "LFO 2>Cutoff 1", Dest::Lfo2Cutoff1 }, { "LFO 2>Cutoff 2", Dest::Lfo2Cutoff2 },
        { "LFO 2>Panorama", Dest::Lfo2Pan }, { "LFO 2 Assign Amt", Dest::Lfo2Amt },
        { "LFO 3 Rate", Dest::Lfo3Rate }, { "LFO 3 Assign Amt", Dest::Lfo3Amt },
        { "Chorus Mix", Dest::ChorusMix }, { "Delay Send", Dest::DelaySend },
        { "Reverb Send", Dest::ReverbSend }, { "Phaser Mix", Dest::PhaserMix },
        { "Distortion Intensity", Dest::DistIntensity }, { "Filterbank Freq", Dest::BankFreq },
        { "Filterbank Reso", Dest::BankReso }
    };

    const auto& dests = choicesFor ("modmatrixDest");
    for (int i = 0; i < juce::jmin (128, dests.size()); ++i)
        for (const auto& [name, d] : destNames)
            if (dests[i] == name)
                destForByte[(size_t) i] = d;

    static const std::pair<const char*, Src> srcNames[] =
    {
        { "Pitch Bend", Src::PitchBend }, { "Chan Pressure", Src::ChanPressure }, { "Mod Wheel", Src::ModWheel },
        { "Breath", Src::Breath }, { "Controller 3", Src::Controller3 }, { "Foot Pedal", Src::FootPedal },
        { "Data Entry", Src::DataEntry }, { "Balance", Src::Balance }, { "Controller 9", Src::Controller9 },
        { "Expression", Src::Expression }, { "Controller 12", Src::Controller12 },
        { "Controller 13", Src::Controller13 }, { "Controller 14", Src::Controller14 },
        { "Controller 15", Src::Controller15 }, { "Controller 16", Src::Controller16 },
        { "Hold Pedal", Src::HoldPedal }, { "Portamento Sw", Src::PortamentoSwitch },
        { "Sost Pedal", Src::SostenutoPedal }, { "Amp Envelope", Src::AmpEnv },
        { "Filter Envelope", Src::FilterEnv }, { "Envelope 3", Src::Env3 }, { "Envelope 4", Src::Env4 },
        { "LFO 1 bipolar", Src::Lfo1Bi }, { "LFO 2 bipolar", Src::Lfo2Bi }, { "LFO 3 bipolar", Src::Lfo3Bi },
        { "LFO 1 unipolar", Src::Lfo1Uni }, { "LFO 2 unipolar", Src::Lfo2Uni }, { "LFO 3 unipolar", Src::Lfo3Uni },
        { "Velocity On", Src::VelocityOn }, { "Velocity Off", Src::VelocityOff },
        { "Key Follow", Src::KeyFollow }, { "Random", Src::RandomPerNote },
        { "1% constant", Src::Const1 }, { "10% constant", Src::Const10 }
    };

    const auto& sources = choicesFor ("modmatrixSource");
    for (int i = 0; i < juce::jmin (128, sources.size()); ++i)
        for (const auto& [name, s] : srcNames)
            if (sources[i] == name)
                srcForByte[(size_t) i] = s;
}

void Engine::bindTo (juce::AudioProcessorValueTreeState& apvts)
{
    bindings.clear();
    for (const auto& p : allParameters())
        bindings[std::string_view (p.id)] = apvts.getRawParameterValue (p.id);

    const auto ptr = [&apvts] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    // Matrix slots: slots 2 and 3 name their first destination "Destination1"
    // and "Amount1", the others just "Destination" and "Amount".
    for (int s = 0; s < 6; ++s)
    {
        const juce::String n ("Assign" + juce::String (s + 1));
        auto& sp = slotPointers[(size_t) s];
        sp.src = ptr (n + "Source");
        sp.dest[0] = ptr (n + "Destination1") != nullptr ? ptr (n + "Destination1") : ptr (n + "Destination");
        sp.amt[0]  = ptr (n + "Amount1") != nullptr ? ptr (n + "Amount1") : ptr (n + "Amount");
        sp.dest[1] = ptr (n + "Destination2"); sp.amt[1] = ptr (n + "Amount2");
        sp.dest[2] = ptr (n + "Destination3"); sp.amt[2] = ptr (n + "Amount3");
    }

    for (int i = 0; i < 32; ++i)
    {
        const juce::String n ("Step" + juce::String (i + 1));
        stepPointers[(size_t) i] = { ptr (n + "Length"), ptr (n + "Velocity"), ptr (n + "Bitfield") };
    }

    bound = true;
}

void Engine::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(),
                     filter1.get(), filter2.get(), envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                     lfo1.get(), lfo2.get(), lfo3.get(), distortion.get(), phaser.get(), chorus.get(),
                     delay.get(), reverb.get(), tapeWobble.get(), vowelBank.get(), combBank.get(),
                     vocoder.get(), envFollow.get(), stereoWidth.get() })
        m->prepare (newSampleRate);

    freqShifter.prepare (newSampleRate);
    granulator->prepare (newSampleRate);
    punchCoef = (float) std::exp (-1.0 / (0.012 * newSampleRate));   // punch time constant 12 ms (gone by ~40 ms)

    // Resolve every cached parameter index now, on the message thread, so
    // the audio thread never does it.
    updateParameters();
    reset();
}

void Engine::reset()
{
    for (auto* m : { distortion.get(), phaser.get(), chorus.get(), delay.get(), reverb.get(),
                     tapeWobble.get(), vowelBank.get(), combBank.get(), vocoder.get(), envFollow.get(),
                     stereoWidth.get(), granulator.get() })
        m->reset();

    freqShifter.reset();
    granulator->reset();
    granIdle = 1 << 30;

    // Every source of randomness restarts from a fixed seed, so rendering the
    // same MIDI twice gives the same audio.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(), lfo1.get(), lfo2.get(), lfo3.get() })
        m->reset();
    random.setSeed (0xa9a);
    arp.rng.setSeed (0xa4b);

    for (int v = 0; v < kMaxVoices; ++v)
        for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(),
                         filter1.get(), filter2.get(), envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                         lfo1.get(), lfo2.get(), lfo3.get() })
            m->voiceReset (v);

    for (auto& v : voices)
        v = Voice {};

    for (auto* q : { &eqLow, &eqMid, &eqHigh, &charA, &charB, &bankPeak, &distPre, &distPost })
        q->reset();
    for (auto& s : bankStages)   s.reset();
    for (auto& s : bankStagesHp) s.reset();
    bankBpLp.reset();
    lastPeakHz = -1.0f;
    smoothedBankFreq = -1.0f;
    delayIdle = reverbIdle = 1 << 30;
    delayColour.reset();

    arp = Arp {};
    arp.rng.setSeed (0xa4b);
    arpWasRunning = false;
    monoStack.clear();
    keyIsDown.fill (false);
    sustainPedal = false;
    holdLatched = false;
    lastVoice = -1;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
}

void Engine::readMatrix()
{
    for (int s = 0; s < 6; ++s)
    {
        const auto& sp = slotPointers[(size_t) s];
        auto& slot = matrix[(size_t) s];

        const int srcByte = juce::roundToInt (load (sp.src, 0.0f));
        slot.source = srcByte >= 0 && srcByte < 128 ? srcForByte[(size_t) srcByte] : Src::Off;

        for (int d = 0; d < 3; ++d)
        {
            const int destByte = juce::roundToInt (load (sp.dest[d], 0.0f));
            slot.dest[(size_t) d] = destByte >= 0 && destByte < 128 ? destForByte[(size_t) destByte] : Dest::None;
            slot.amount[(size_t) d] = bipolar (load (sp.amt[d], 64.0f)) * kMatrixFullScale;
        }
    }
}

//==============================================================================
// Once per block: every knob into musical units, and every module parameter
// that does not change from voice to voice written into its module.
//==============================================================================
void Engine::updateParameters()
{
    if (! bound)
        return;

    const auto r = [this] (const char* id, float fallback) { return raw (id, fallback); };
    const auto k = [&r] (const char* id, float fallback) { return unit (r (id, fallback)); };
    const auto bi = [&r] (const char* id, float fallback) { return bipolar (r (id, fallback)); };
    const auto i = [&r] (const char* id, int fallback) { return juce::roundToInt (r (id, (float) fallback)); };

    // Tempo: the host's when it has one, otherwise the patch's own clock.
    tempoBpm = hostTempoValid ? hostTempo : (double) scaling::tempoBpm (r ("ClockTempo", 57));

    //== oscillators ==========================================================
    b.osc1Mode = i ("Osc1Mode", 0);
    b.osc2Mode = i ("Osc2Mode", 0);
    b.osc3Mode = i ("Osc3Mode", 0);

    b.osc1Semi = r ("Osc1Semitone", 64) - 64.0f;
    b.osc2Semi = r ("Osc2Semitone", 64) - 64.0f;
    b.osc3Semi = r ("Osc3Semitone", 64) - 64.0f;

    // Fine detune is a gentle upward offset: squared so the first half of the
    // knob is the slow-beating range, the end reaches a full semitone.
    b.osc2DetuneCents = scaling::fineDetuneCents (r ("Osc2Detune", 32));
    b.osc3DetuneCents = scaling::fineDetuneCents (r ("Osc3Detune", 32));

    // Key follow: 96 is ordinary keyboard tracking.
    b.osc1Track = r ("Osc1Keyfollow", 96) / 96.0f;
    b.osc2Track = r ("Osc2Keyfollow", 96) / 96.0f;

    b.osc1Shape = k ("Osc1Shape", 64);
    b.osc2Shape = k ("Osc2Shape", 64);
    b.osc1ShapeVel = bi ("Osc1ShapeVelocity", 64);
    b.osc2ShapeVel = bi ("Osc2ShapeVelocity", 64);
    b.osc1Pw = k ("Osc1Pulsewidth", 0);
    b.osc2Pw = k ("Osc2Pulsewidth", 0);
    b.pwVel = bi ("PulsewidthVelocity", 64);

    b.osc1Wave = (float) juce::jlimit (0, 63, i ("Osc1WaveSelect", 0));
    b.osc2Wave = (float) juce::jlimit (0, 63, i ("Osc2WaveSelect", 0));

    // Wavetables: the oscillator's 64-wave table is walked as 100 overlapping
    // tables of eight neighbouring waves each. Table 0 starts from the sine.
    const auto tableBase = [] (int table)
    {
        table = juce::jlimit (0, 99, table);
        return table == 0 ? 0.0f : (float) (2 + (table * 7) % 54);
    };
    b.osc1WtBase = tableBase (i ("Osc1WavetableWavetableselect", 0));
    b.osc2WtBase = tableBase (i ("Osc2WavetableWavetableselect", 0));
    b.osc1WtIdx = k ("Osc1WavetableWavetableindex", 0);
    b.osc2WtIdx = k ("Osc2WavetableWavetableindex", 0);
    b.osc1Interp = k ("Osc1WavetableInterpolation", 0);
    b.osc2Interp = k ("Osc2WavetableInterpolation", 0);
    b.osc1FormantSpread = k ("Osc1WavetableSync", 0);
    b.osc2FormantSpread = k ("Osc2WavetableSync", 0);

    b.osc2Sync = r ("Osc2Sync", 0) > 0.5f;
    b.syncFreq = k ("Osc2HypersawCrossoscsyncfreq", 0);
    b.syncEnv = bi ("Osc2HsawFiltEnvSyncFreq", 64);
    b.osc2EnvPitch = bi ("Osc2FiltEnvAmt", 64);

    b.fmAmount = k ("Osc2FmAmount", 0);
    b.fmEnv = bi ("FmFiltEnvAmt", 64);
    b.fmVel = bi ("FmAmountVelocity", 64);
    b.fmMode = i ("OscFmMode", 0);

    b.balance = k ("OscBalance", 64);
    b.oscVolume = bi ("OscMainvolume", 64);
    b.subVolume = k ("SuboscillatorVolume", 0);
    b.noiseVolume = k ("NoiseVolume", 0);
    b.ringVolume = k ("RingmodulatorVolume", 0);
    b.osc3Volume = k ("Osc3Volume", 64);
    b.noiseColour = bi ("NoiseColor", 64);

    SETP (*subOsc, "waveform", i ("SuboscillatorShape", 0) == 0 ? 3.0f : 1.0f);

    // Stacks. HyperSaw always stacks (3..9 saws with Density); the Wavetable
    // models stack from Density above zero; Unison adds copies to any model.
    const int unisonCount = juce::jlimit (1, VirusOscModule::kMaxUnison, i ("UnisonMode", 0) + 1);
    const float unisonCents = scaling::unisonCents (r ("UnisonDetune", 48));

    const auto setupStack = [&] (SynthModule& m, const OscIdx& ix, int mode, const char* densityId,
                                 const char* spreadId)
    {
        const float density = k (densityId, 0);
        int stack = 1;
        if (mode == 1)      stack = 3 + juce::roundToInt (density * 6.0f);
        else if (mode >= 2) stack = 1 + juce::roundToInt (density * 8.0f);
        stack = juce::jlimit (1, VirusOscModule::kMaxUnison, juce::jmax (stack, unisonCount));

        const float localCents = mode >= 1 ? scaling::localDetuneCents (r (spreadId, 64)) : 0.0f;
        m.setParameterAt (ix.unison, (float) stack);
        m.setParameterAt (ix.detune, juce::jmax (localCents, unisonCount > 1 ? unisonCents : 0.0f));
        m.setParameterAt (ix.spread, stack > 1 ? k ("UnisonPanSpread", 127) : 0.0f);
        m.setParameterAt (ix.mode, (float) (mode == 0 ? 0 : (mode == 1 ? 1 : 2)));
    };

    setupStack (*osc1, o1, b.osc1Mode, "Osc1HypersawDensity", "Osc1HypersawDetunespread");
    setupStack (*osc2, o2, b.osc2Mode, "Osc2HypersawDensity", "Osc2HypersawDetunespread");

    // Formant shift (Wavetable models): below the centre the spectrum slides
    // down, above it slides up; the pitch stays.
    osc1->setParameterAt (o1.formant, std::pow (2.0f, bi ("Osc1WavetableFormantshift", 64) * 1.5f));
    osc2->setParameterAt (o2.formant, std::pow (2.0f, bi ("Osc2WavetableFormantshift", 64) * 1.5f));

    const float initPhase = k ("OscInitPhase", 0);
    for (auto* m : { osc1.get(), osc2.get(), osc3.get() })
        SETP (*m, "initPhase", initPhase);

    // Oscillator 3: Off, Slave (a copy of oscillator 2), Saw, Pulse, Sine,
    // Triangle, then Waves 3..64 of the table.
    osc3->setParameterAt (o3.unison, 1.0f);
    osc3->setParameterAt (o3.detune, 0.0f);
    osc3->setParameterAt (o3.mode, 0.0f);
    osc3->setParameterAt (o3.sync, 0.0f);
    osc3->setParameterAt (o3.pw, 0.5f);
    osc3->setParameterAt (o3.formant, 1.0f);
    switch (b.osc3Mode)
    {
        case 2:  osc3->setParameterAt (o3.wave, 0); osc3->setParameterAt (o3.shape, 0.5f); break;  // saw
        case 3:  osc3->setParameterAt (o3.wave, 0); osc3->setParameterAt (o3.shape, 1.0f); break;  // pulse
        case 4:  osc3->setParameterAt (o3.wave, 0); osc3->setParameterAt (o3.shape, 0.0f); break;  // sine
        case 5:  osc3->setParameterAt (o3.wave, 1); osc3->setParameterAt (o3.shape, 0.0f); break;  // triangle
        default:
            if (b.osc3Mode >= 6)
            {
                osc3->setParameterAt (o3.wave, (float) juce::jlimit (0, 63, b.osc3Mode - 4));
                osc3->setParameterAt (o3.shape, 0.0f);
            }
            break;   // Slave is set per voice from oscillator 2
    }

    //== filters ==============================================================
    b.cut1 = k ("Cutoff", 127);
    b.cut2 = k ("Cutoff2", 64);
    b.res1 = k ("Filter1Resonance", 0);
    b.res2 = k ("Filter2Resonance", 0);
    b.env1 = k ("Filter1EnvAmt", 0);
    b.env2 = k ("Filter2EnvAmt", 0);
    b.env1Vel = bi ("Flt1EnvamtVelocity", 64);
    b.env2Vel = bi ("Flt2EnvamtVelocity", 64);
    b.res1Vel = bi ("Resonance1Velocity", 64);
    b.res2Vel = bi ("Resonance2Velocity", 64);
    b.key1 = bi ("Filter1Keyfollow", 64);
    b.key2 = bi ("Filter2Keyfollow", 64);
    b.keyBase = (float) i ("FilterKeytrackBase", 60);        // a MIDI note: C-1 = 0
    b.pol1 = i ("Filter1EnvPolarity", 1) == 0 ? -1.0f : 1.0f;
    b.pol2 = i ("Filter2EnvPolarity", 1) == 0 ? -1.0f : 1.0f;
    b.link = i ("Filter2CutoffLink", 1) > 0;
    b.linkOffset = bi ("OffsetForFilterlink", 64) * 0.5f;
    b.routing = juce::jlimit (0, 3, i ("FilterRouting", 0));
    b.filterBalance = k ("FilterBalance", 64);
    b.satCurve = juce::jlimit (0, 14, i ("SaturationCurve", 0));

    // Filter modes. The digital modes are 12 dB state-variable filters, which
    // stack to 24 dB in serial routing exactly as the hardware's do; the
    // Analog modes are the ladder.
    const auto filterType = [] (int mode)
    {
        switch (mode)
        {
            case 0:  return 5;   // Low Pass   -> SVF LP
            case 1:  return 4;   // High Pass  -> SVF HP
            case 2:  return 3;   // Band Pass  -> SVF BP
            case 3:  return 6;   // Band Stop  -> SVF Notch
            case 4:
            case 5:  return 0;   // Analog 1/2 pole -> ladder LP12
            case 6:  return 1;   // Analog 3 pole   -> ladder LP24
            default: return 2;   // Analog 4 pole   -> ladder LP24, driven
        }
    };
    b.type1 = filterType (i ("Filter1Mode", 0));
    b.type2 = filterType (i ("Filter2Mode", 0));
    filter1->setParameterAt (f1.type, (float) b.type1);
    filter2->setParameterAt (f2.type, (float) b.type2);

    //== envelopes ============================================================
    b.fA = r ("FilterEnvAttack", 0);   b.fD = r ("FilterEnvDecay", 46);
    b.fS = r ("FilterEnvSustain", 0);  b.fSlope = r ("FilterEnvSustainTime", 64);
    b.fR = r ("FilterEnvRelease", 40);
    b.aA = r ("AmpEnvAttack", 0);      b.aD = r ("AmpEnvDecay", 127);
    b.aS = r ("AmpEnvSustain", 127);   b.aSlope = r ("AmpEnvSustainTime", 64);
    b.aR = r ("AmpEnvRelease", 4);

    b.ampMs[0] = scaling::envMs (b.aA); b.ampMs[1] = scaling::envMs (b.aD); b.ampMs[2] = scaling::envMs (b.aR);
    b.filtMs[0] = scaling::envMs (b.fA); b.filtMs[1] = scaling::envMs (b.fD); b.filtMs[2] = scaling::envMs (b.fR);

    const auto setEnv = [&] (SynthModule& m, const char* a, const char* d, const char* s,
                             const char* slope, const char* rel)
    {
        m.setParameterAt (eIdx.attack, scaling::envMs (r (a, 20)));
        m.setParameterAt (eIdx.decay, scaling::envMs (r (d, 70)));
        m.setParameterAt (eIdx.sustain, k (s, 64));
        m.setParameterAt (eIdx.slope, bi (slope, 64));
        m.setParameterAt (eIdx.release, scaling::envMs (r (rel, 70)));
    };
    setEnv (*env3, "Envelope3Attack", "Envelope3Decay", "Envelope3Sustain", "Envelope3SustainTime", "Envelope3Release");
    setEnv (*env4, "Envelope4Attack", "Envelope4Decay", "Envelope4Sustain", "Envelope4SustainTime", "Envelope4Release");

    //== LFOs =================================================================
    const char* rateIds[] = { "Lfo1Rate", "Lfo2Rate", "Lfo3Rate" };
    const char* clockIds[] = { "Lfo1Clock", "Lfo2Clock", "Lfo3Clock" };
    const char* keyIds[] = { "Lfo1Keyfollow", "Lfo2Keyfollow", "Lfo3Keyfollow" };
    const char* shapeIds[] = { "Lfo1Shape", "Lfo2Shape", "Lfo3Shape" };
    const char* modeIds[] = { "Lfo1Mode", "Lfo2Mode", "Lfo3Mode" };
    SynthModule* lfos[] = { lfo1.get(), lfo2.get(), lfo3.get() };

    for (int n = 0; n < 3; ++n)
    {
        b.lfoRateByte[n] = r (rateIds[n], 48);

        // Clocked: the rate is one cycle per note value at the current tempo.
        const float wholeNotes = listValue (kLfoClock, i (clockIds[n], 0));
        b.lfoClockHz[n] = wholeNotes > 0.0f ? (float) (tempoBpm / 60.0 / (wholeNotes * 4.0)) : 0.0f;

        // Key follow: at full, the rate doubles every octave up the keyboard.
        b.lfoKeyFollow[n] = k (keyIds[n], 0);

        SETP (*lfos[n], "shape", (float) juce::jlimit (0, 67, i (shapeIds[n], 1)));
        SETP (*lfos[n], "mode", i (modeIds[n], 0) > 0 ? 1.0f : 0.0f);
        SETP (*lfos[n], "phaseSpread", k ("UnisonLfoPhase", 64));
        b.lfoHz[n] = b.lfoClockHz[n] > 0.0f ? b.lfoClockHz[n] : scaling::lfoRateHz (b.lfoRateByte[n]);
        lfos[n]->setParameterAt (lIdx.rate, b.lfoHz[n]);
    }

    b.lfoContour[0] = bi ("Lfo1Symmetry", 64);
    b.lfoContour[1] = bi ("Lfo2Symmetry", 64);
    lfo3->setParameterAt (lIdx.symmetry, 0.0f);

    SETP (*lfo1, "envMode", i ("Lfo1EnvMode", 0) > 0 ? 1.0f : 0.0f);
    SETP (*lfo2, "envMode", i ("Lfo2EnvMode", 0) > 0 ? 1.0f : 0.0f);
    SETP (*lfo3, "envMode", 0.0f);

    // Keytrigger 0 is free running; above that, every note restarts the LFO
    // at that point of its cycle.
    const int kt1 = i ("Lfo1Keytrigger", 0), kt2 = i ("Lfo2Keytrigger", 0);
    SETP (*lfo1, "keytrigger", kt1 == 0 ? -1.0f : (float) (kt1 - 1) / 126.0f);
    SETP (*lfo2, "keytrigger", kt2 == 0 ? -1.0f : (float) (kt2 - 1) / 126.0f);
    SETP (*lfo3, "keytrigger", 0.0f);   // LFO 3 is a per-note vibrato: always from the top

    b.lfo1Osc1 = bi ("Osc1Lfo1Amount", 64);
    b.lfo1Osc2 = bi ("Osc2Lfo1Amount", 64);
    b.lfo1Pw = bi ("PwLfo1Amount", 64);
    b.lfo1Reso = bi ("ResoLfo1Amount", 64);
    b.lfo1Gain = bi ("FiltgainLfo1Amount", 64);
    b.lfo1Assign = bi ("Lfo1AssignAmount", 64);
    b.lfo1Dest = i ("Lfo1AssignDest", 0);

    b.lfo2Shape = bi ("ShapeLfo2Amount", 64);
    b.lfo2Fm = bi ("FmLfo2Amount", 64);
    b.lfo2Cut1 = bi ("Cutoff1Lfo2Amount", 64);
    b.lfo2Cut2 = bi ("Cutoff2Lfo2Amount", 64);
    b.lfo2Pan = bi ("PanLfo2Amount", 64);
    b.lfo2Assign = bi ("Lfo2AssignAmount", 64);
    b.lfo2Dest = i ("Lfo2AssignDest", 0);

    b.lfo3Amount = k ("OscLfo3Amount", 0);
    b.lfo3Dest = i ("Lfo3Destination", 1);
    b.lfo3FadeSeconds = scaling::lfo3FadeSec (r ("Lfo3FadeInTime", 0));

    //== amp and common =======================================================
    b.patchVolume = k ("PatchVolume", 100);
    b.pan = bi ("Panorama", 64);
    b.panVel = bi ("PanoramaVelocity", 64);
    b.ampVel = bi ("AmpVelocity", 64);
    b.punch = k ("PunchIntensity", 0);
    b.transpose = r ("Transpose", 64) - 64.0f;
    b.bendUp = r ("BenderRangeUp", 66) - 64.0f;
    b.bendDown = 64.0f - r ("BenderRangeDown", 62);
    b.bendExp = i ("BenderScale", 1) > 0;
    b.keyMode = juce::jlimit (0, 5, i ("KeyMode", 0));

    // Portamento: 0 is off, then from a couple of milliseconds to about 3 s.
    const float porta = r ("PortamentoTime", 0);
    b.glideOn = porta > 0.5f;
    const float glideMs = scaling::portamentoMs (porta);
    b.glideCoef = (float) std::exp (-4.6 / (glideMs * 0.001 * sampleRate));

    //== arpeggiator ==========================================================
    b.arpMode = juce::jlimit (0, 7, i ("ArpMode", 0));
    b.arpPattern = juce::jlimit (0, 63, i ("ArpPatternSelct", 1));
    b.arpOctaves = juce::jlimit (1, 4, i ("ArpOctaveRange", 0) + 1);
    b.arpStepBeats = listValue (kArpClock, i ("ArpClock", 4)) * 4.0f;
    b.arpGate = scaling::arpGate (r ("ArpNoteLength", 64));
    b.arpSwing = scaling::arpSwing (r ("ArpSwing", 0));
    b.arpHold = i ("ArpHoldEnable", 0) > 0;

    userPatternLength = juce::jlimit (1, 32, i ("ArpeggiatorUserpatternlength", 15) + 1);
    for (int s = 0; s < 32; ++s)
    {
        const auto& sp = stepPointers[(size_t) s];
        stepOn[(size_t) s] = load (sp.on, 1.0f) > 0.5f;
        stepVelocity[(size_t) s] = unit (load (sp.velocity, 100.0f));
        stepLength[(size_t) s] = unit (load (sp.length, 64.0f));
    }

    //== effects ==============================================================
    b.inputMode = i ("InputMode", 0);
    b.inputSelect = i ("InputSelect", 1);
    b.inputRing = k ("InputRingmodulator", 0);
    b.followerMode = i ("InputFollowerMode", 0);

    SETP (*envFollow, "attack", 0.1f + 150.0f * k ("InputFollowerAttack", 64) * k ("InputFollowerAttack", 64));
    SETP (*envFollow, "release", 1.0f + 1500.0f * k ("InputFollowerRelease", 64) * k ("InputFollowerRelease", 64));
    SETP (*envFollow, "gain", 1.0f + 7.0f * k ("InputFollowerLevel", 0));

    // Distortion
    b.distCurve = juce::jlimit (0, 25, i ("DistortionCurve", 0));
    b.distDrive = k ("DistortionIntensity", 0);
    b.distMix = k ("PatchDistortionMix", 127);
    SETP (*distortion, "curve", (float) b.distCurve);
    SETP (*distortion, "tone", k ("PatchDistortionTone127", 64));   // the filter-type curves use it directly
    {
        // Treble Booster: a high shelf INTO the distortion, so the top end
        // gets driven harder (the classic pedal trick). Tone: a tilt AFTER
        // it - below the centre darker, above brighter.
        using T = Biquad::Type;
        distPre.set (T::HighShelf, sampleRate, 2500.0f, 0.7f, 15.0f * k ("PatchDistortionTrebleBooster", 0));
        const float tone = bi ("PatchDistortionTone127", 64);
        distPost.set (T::HighShelf, sampleRate, 1200.0f, 0.6f, 12.0f * tone);
        distToneOn = std::abs (tone) > 0.01f;
    }
    SETP (*distortion, "highCut", k ("PatchDistortionHighCut", 127));
    SETP (*distortion, "dryWet", 100.0f);   // the engine does the blend

    // Filter bank: one Frequency knob serves every type.
    b.bankType = juce::jlimit (0, 11, i ("FilterBankType", 0));
    b.bankFreq = k ("FilterBankFrequency", 64);
    b.bankReso = k ("FilterBankResonance", 64);
    b.bankMix = k ("FilterBankMix", 127);
    b.bankPoles = k ("FilterBankPoles", 0);
    b.bankSlope = k ("FilterBankSlope", 0);
    b.bankStereo = bi ("FilterBankStereoPhase", 64);
    SETP (*vowelBank, "resonance", 40.0f + 58.0f * b.bankReso);
    SETP (*vowelBank, "size", b.bankStereo * 6.0f);
    SETP (*combBank, "feedback", b.bankReso * 97.0f);

    // Vocoder
    b.vocoderMode = i ("VocoderMode", 0);
    b.vocoderBalance = k ("VocoderBalance", 64);
    const int bands = i ("VocoderBands", 31);
    SETP (*vocoder, "bands", bands < 11 ? 0.0f : (bands < 22 ? 1.0f : 2.0f));
    SETP (*vocoder, "attack", 0.1f + 99.0f * k ("VocoderAttack", 0) * k ("VocoderAttack", 0));
    SETP (*vocoder, "release", 1.0f + 499.0f * k ("VocoderRelease", 46) * k ("VocoderRelease", 46));
    SETP (*vocoder, "bright", k ("VocoderSpectralBalance", 64) * 100.0f);
    const float carrierHz = scaling::vocoderCentreHz (r ("VocoderCarrierCenterFrequency", 64));
    const float modHz = i ("VocoderLink", 1) > 0
                          ? carrierHz * std::pow (2.0f, bi ("VocoderModulatorFrequencyOffset", 64) * 2.0f)
                          : scaling::vocoderCentreHz (r ("VocoderModulatorCenterFrequency", 64));
    SETP (*vocoder, "carrierCentre", carrierHz);
    SETP (*vocoder, "modCentre", juce::jlimit (100.0f, 5000.0f, modHz));
    SETP (*vocoder, "carrierSpread", 55.0f + 45.0f * k ("CarrierFrequencySpread", 0));   // even the minimum spans a voice
    SETP (*vocoder, "modSpread", 55.0f + 45.0f * k ("VocoderModulatorFrequencySpread", 127));
    SETP (*vocoder, "carrierQ", 0.2f + 5.8f * k ("VocoderCarrierQFactor", 64));
    SETP (*vocoder, "modQ", 0.2f + 5.8f * k ("VocoderModulatorQFactor", 64));
    SETP (*vocoder, "level", 4.0f);

    // Character: type, intensity and tune (the old Analog Boost pair).
    b.charType = juce::jlimit (0, 8, i ("CharacterType", 0));
    b.charAmount = k ("BassIntensity", 0);
    {
        const float tune = k ("BassTune", 32);
        const float a = b.charAmount;
        using T = Biquad::Type;
        switch (b.charType)
        {
            case 0:  // Analog Boost: a low shelf, Tune sets where
                charA.set (T::LowShelf, sampleRate, 40.0f * std::pow (6.0f, tune), 0.7f, a * 12.0f);
                charB.set (T::Peak, sampleRate, 1000.0f, 0.7f, 0.0f); break;
            case 1: case 2: case 3:  // Vintage: saturation, darkened progressively
                charA.set (T::LowPass, sampleRate, (18000.0f / (float) b.charType) * (0.5f + tune), 0.6f, 0.0f);
                charB.set (T::LowShelf, sampleRate, 120.0f, 0.7f, a * 3.0f); break;
            case 4:  // Pad Opener: air on top
                charA.set (T::HighShelf, sampleRate, 2000.0f * std::pow (5.0f, tune), 0.7f, a * 9.0f);
                charB.set (T::Peak, sampleRate, 300.0f, 0.8f, -a * 3.0f); break;
            case 5:  // Lead Enhancer: presence
                charA.set (T::Peak, sampleRate, 600.0f * std::pow (6.0f, tune), 1.1f, a * 8.0f);
                charB.set (T::LowShelf, sampleRate, 150.0f, 0.7f, -a * 3.0f); break;
            case 6:  // Bass Enhancer: weight plus a little harmonic drive
                charA.set (T::LowShelf, sampleRate, 50.0f * std::pow (4.0f, tune), 0.8f, a * 9.0f);
                charB.set (T::Peak, sampleRate, 800.0f, 1.0f, a * 2.0f); break;
            case 7:  // Stereo Widener: flat EQ, the width does the work
                charA.set (T::Peak, sampleRate, 1000.0f, 0.7f, 0.0f);
                charB.set (T::Peak, sampleRate, 1000.0f, 0.7f, 0.0f); break;
            default: // Speaker Cabinet: band-limited, boxy
                charA.set (T::LowPass, sampleRate, 2000.0f + 5000.0f * tune * (1.0f - 0.5f * a), 0.9f, 0.0f);
                charB.set (T::HighPass, sampleRate, 70.0f + 150.0f * a, 0.8f, 0.0f); break;
        }
        SETP (*stereoWidth, "width", b.charType == 7 ? 100.0f + 100.0f * a : 100.0f);
        SETP (*stereoWidth, "bassMono", b.charType == 7 ? 60.0f + 200.0f * tune : 0.0f);
    }

    // EQ: low shelf, mid bell, high shelf.
    {
        const float lg = scaling::eqGainDb (r ("LoweqGain", 64)), mg = scaling::eqGainDb (r ("MideqGain", 64)),
                    hg = scaling::eqGainDb (r ("HigheqGain", 64));
        b.eqOn = std::abs (lg) > 0.01f || std::abs (mg) > 0.01f || std::abs (hg) > 0.01f;
        using T = Biquad::Type;
        eqLow.set (T::LowShelf, sampleRate, scaling::eqLowHz (r ("LoweqFrequency", 5)), 0.7f, lg);
        eqMid.set (T::Peak, sampleRate, scaling::eqMidHz (r ("MideqFrequency", 84)),
                   scaling::eqQ (r ("MideqQFactor", 32)), mg);
        eqHigh.set (T::HighShelf, sampleRate, scaling::eqHighHz (r ("HigheqFrequency", 0)), 0.7f, hg);
    }

    // Phaser: Mode is the number of stages. Its mix tops out at an even blend
    // of dry and shifted signal, which is where the notches are deepest.
    b.phaserMix = k ("PhaserMix", 0);
    SETP (*phaser, "stages", (float) juce::jlimit (0, 5, i ("PhaserMode", 3)));
    SETP (*phaser, "rate", scaling::phaserRateHz (r ("PhaserRate", 36)));
    SETP (*phaser, "depth", k ("PhaserDepth", 112) * 100.0f);
    SETP (*phaser, "centre", scaling::phaserCentreHz (r ("PhaserFrequency", 64)));
    SETP (*phaser, "feedback", k ("PhaserFeedback", 64) * 95.0f);
    SETP (*phaser, "spread", k ("PhaserSpread", 127) * 100.0f);

    // Chorus: Type picks the flavour; Mix, Rate, Depth, Delay and LFO Shape
    // mean the same thing for all of them.
    b.chorusType = juce::jlimit (0, 6, i ("ChorusType", 1));
    b.chorusMix = k ("ChorusMix", 0);
    {
        const int t = b.chorusType;
        const float rate = scaling::chorusRateHz (r ("ChorusRate", 64));
        SETP (*chorus, "rate", t == 6 ? juce::jmax (0.7f, rate * 2.0f) : rate);
        SETP (*chorus, "depth", k ("ChorusDepth", 16) * 100.0f);
        SETP (*chorus, "baseDelay", scaling::chorusDelayMs (r ("ChorusDelay", 64)));
        SETP (*chorus, "spread", t == 5 ? 0.0f : 100.0f);
        SETP (*chorus, "voices", t == 3 ? 4.0f : (t == 4 ? 3.0f : (t == 5 ? 1.0f : 2.0f)));
        SETP (*chorus, "lfoShape", (float) juce::jlimit (0, 3, t == 2 ? 1 : i ("ChorusLfoShape", 1)));
    }

    // Delay: a send. Clocked, the time is a note value at the current tempo.
    b.delayMode = i ("DelayMode", 1);
    b.delaySend = k ("DelaySend", 0);
    b.delayColour = bi ("DelayColor", 64);
    {
        float ms = scaling::delayMs (r ("DelayTime", 64));
        const float clock = listValue (kDelayClock, i ("DelayClock", 0));
        if (clock > 0.0f)
            ms = (float) (60000.0 / tempoBpm * clock * 4.0);
        SETP (*delay, "time", juce::jlimit (1.0f, 5000.0f, ms));
        SETP (*delay, "tapVolume", k ("DelayFeedback", 0) * 95.0f);
        SETP (*delay, "modRate", scaling::delayModHz (r ("DlyRateRevDecay", 16)));
        SETP (*delay, "modDepth", k ("DlyDepth", 12) * 100.0f);

        // Right-channel time as a ratio of the left, for the ping-pong and
        // pattern modes; the module takes it as an offset from 1.
        float right = 1.0f;
        int moduleMode = 1;
        switch (b.delayMode)
        {
            case 2: right = 0.5f;   moduleMode = 2; break;   // Ping Pong 2:1
            case 3: right = 0.75f;  moduleMode = 2; break;   // 4:3
            case 4: right = 0.5f;   moduleMode = 3; break;   // 4:1 (as far as the module reaches)
            case 5: right = 0.875f; moduleMode = 2; break;   // 8:7
            default:
                if (b.delayMode >= 6)
                {
                    static const int patterns[][2] = { {1,1},{2,1},{3,1},{4,1},{5,1},{2,3},{2,5},{3,2},{3,3},
                                                       {3,4},{3,5},{4,3},{4,5},{5,2},{5,3},{5,4},{5,5} };
                    const auto& pt = patterns[juce::jlimit (0, 16, b.delayMode - 6)];
                    right = (float) pt[1] / (float) pt[0];
                }
                break;
        }
        SETP (*delay, "ratio", juce::jlimit (-0.5f, 0.5f, right - 1.0f));
        SETP (*delay, "mode", (float) moduleMode);

        const int type = i ("DelayType", 0);    // Classic, Tape Clocked, Tape Free, Tape Doppler
        const float wow = k ("DelayTapeDelayModulation", 12);
        SETP (*tapeWobble, "wow", type == 0 ? 0.0f : (type == 3 ? 40.0f : 10.0f) + 60.0f * wow);
        SETP (*tapeWobble, "flutter", type == 0 ? 0.0f : 10.0f + 30.0f * wow);
        SETP (*tapeWobble, "drive", type == 0 ? 0.0f : 25.0f);
        b.delayTape = type != 0;
    }

    // Reverb: a send. Mode Off silences it; the Feedback modes let the tail
    // feed back into itself.
    {
        const int mode = i ("ReverbMode", 1);
        b.reverbOn = mode > 0;
        b.reverbSend = k ("ReverbSend", 0);
        const int type = juce::jlimit (0, 3, i ("ReverbType", 0));
        // Ambience, Small Room, Large Room, Hall. The underlying reverb gets
        // its tail mostly from room size, so Time feeds both.
        static const float rooms[] = { 25.0f, 50.0f, 75.0f, 100.0f };
        const float t = k ("ReverbTime", 64);
        SETP (*reverb, "roomSize", rooms[type] * (0.4f + 0.6f * t));
        SETP (*reverb, "decay", 0.3f + 19.7f * t * std::sqrt (t));
        SETP (*reverb, "damping", k ("ReverbDamping", 10) * 100.0f);
        SETP (*reverb, "preDelay", scaling::reverbPredelayMs (r ("ReverbPredelay", 20)));
        SETP (*reverb, "colour", bi ("ReverbColor", 64));
        SETP (*reverb, "feedback", mode >= 2 ? k ("ReverbFeedback", 64) * 90.0f : 0.0f);
    }

    b.secondOut = juce::jmax (0.0f, bi ("SecondOutputBalance", 0));

    // Granulator. Its own dry/wet stays at 100 %: the engine blends it in, so
    // the cloud can fade away cleanly when the mix is closed.
    granMix = k ("Atomizer", 0);
    SETP (*granulator, "grains",    1.0f + 23.0f * k ("GranulatorGrains", 40));
    SETP (*granulator, "size",      5.0f * std::pow (100.0f, k ("GranulatorSize", 48)));      // 5 .. 500 ms
    SETP (*granulator, "position",  k ("GranulatorPosition", 0) * 100.0f);
    SETP (*granulator, "spray",     k ("GranulatorSpray", 12) * 100.0f);
    SETP (*granulator, "window",    2.0f);     // Position scans the last two seconds
    SETP (*granulator, "freeze",    i ("GranulatorFreeze", 0) > 0 ? 1.0f : 0.0f);
    SETP (*granulator, "pitch",     bi ("GranulatorPitch", 64) * 24.0f);
    SETP (*granulator, "pitchDisp", k ("GranulatorPitchDisp", 0) * 100.0f);
    SETP (*granulator, "stereo",    k ("GranulatorStereo", 38) * 100.0f);
    SETP (*granulator, "dryWet",    100.0f);

    readMatrix();

    // With the arp switched off mid-phrase, its notes must not hang.
    const bool arpRunning = b.arpMode > 0;
    if (arpWasRunning && ! arpRunning)
    {
        arpReleaseSounding();
        arp.numHeld = 0;
        arp.latched = false;
    }
    arpWasRunning = arpRunning;
}

//==============================================================================
// Voices
//==============================================================================
int Engine::allocateVoice()
{
    for (int v = 0; v < kPolyVoices; ++v)
        if (! voices[(size_t) v].active)
            return v;

    // Steal: the oldest voice that is already releasing, else the oldest.
    int best = -1;
    juce::uint64 bestAge = std::numeric_limits<juce::uint64>::max();

    for (int v = 0; v < kPolyVoices; ++v)
    {
        const auto& voice = voices[(size_t) v];
        if (! voice.held && ! voice.sustained && voice.age < bestAge)
        {
            bestAge = voice.age;
            best = v;
        }
    }

    if (best >= 0)
        return best;

    for (int v = 0; v < kPolyVoices; ++v)
        if (voices[(size_t) v].age < bestAge)
        {
            bestAge = voices[(size_t) v].age;
            best = v;
        }

    return juce::jmax (0, best);
}

void Engine::startVoice (int v, int note, float velocity, bool retriggerEnvelopes, bool glide)
{
    auto& voice = voices[(size_t) v];
    const bool wasActive = voice.active;

    if (! wasActive)
    {
        const auto keep = voice.age;
        voice = Voice {};
        voice.age = keep;

        for (auto* m : { filter1.get(), filter2.get() })
            m->voiceReset (v);
    }

    // Portamento starts from the last note played (poly) or from wherever this
    // voice's pitch currently is (mono legato).
    voice.glideNote = glide ? (wasActive ? voice.glideNote : lastNote) : (float) note;

    voice.note = note;
    voice.velocity = velocity;
    voice.held = true;
    voice.sustained = false;
    voice.active = true;
    voice.age = ++ageCounter;

    const bool envelopes = retriggerEnvelopes || ! wasActive;

    if (envelopes)
    {
        voice.random = random.nextFloat() * 2.0f - 1.0f;
        voice.punch = 1.0f;
        voice.lfo3Fade = b.lfo3FadeSeconds > 0.0f ? 0.0f : 1.0f;
    }

    // Sound sources: 'retrigger' when the voice is still sounding keeps the
    // oscillator phases running, so a stolen or legato voice does not click.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get() })
    {
        m->voiceNoteOn (v, note, wasActive);
        m->voiceVelocity (v, 1.0f);   // velocity is applied at the amplifier, once
    }

    if (envelopes)
        for (auto* m : { envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                         lfo1.get(), lfo2.get(), lfo3.get() })
        {
            m->voiceNoteOn (v, note, wasActive);
            m->voiceVelocity (v, velocity);
        }

    lastNote = (float) note;
    lastVoice = v;
    lastVelocity = velocity;
}

void Engine::releaseVoice (int v, float releaseVelocity)
{
    auto& voice = voices[(size_t) v];
    if (! voice.active || ! voice.held)
        return;

    voice.held = false;
    voice.releaseVelocity = releaseVelocity;

    if (sustainPedal)
    {
        voice.sustained = true;
        return;
    }

    // Only the envelopes are told: the oscillators keep running until the
    // amplifier envelope has finished, so every release time is heard in full.
    for (auto* m : { envFilter.get(), envAmp.get(), env3.get(), env4.get() })
        m->voiceNoteOff (v);
}

// The voice layer: what a note means depends on Key Mode.
//   Poly           one voice per note
//   Mono 1         multi trigger, portamento always
//   Mono 2         legato (envelopes carry on), portamento always
//   Mono 3         multi trigger, portamento only when playing legato
//   Mono 4         legato, portamento only when playing legato
//   Hold           poly, and notes keep sounding until the next chord
void Engine::keyDown (int note, float velocity)
{
    const int mode = b.keyMode;

    if (mode >= 1 && mode <= 4)
    {
        const bool legato = ! monoStack.empty();
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        if (monoStack.size() < monoStack.capacity())
            monoStack.push_back (note);

        const bool singleTrigger = mode == 2 || mode == 4;
        const bool glide = b.glideOn && (mode <= 2 || legato);
        startVoice (0, note, velocity, ! (legato && singleTrigger), glide);
        return;
    }

    // Poly: a note that is still ringing (released or on the pedal) is
    // reused rather than doubled.
    for (int v = 0; v < kPolyVoices; ++v)
        if (voices[(size_t) v].active && voices[(size_t) v].note == note)
        {
            startVoice (v, note, velocity, true, b.glideOn);
            return;
        }

    startVoice (allocateVoice(), note, velocity, true, b.glideOn);
}

void Engine::keyUp (int note, float releaseVelocity)
{
    const int mode = b.keyMode;

    if (mode >= 1 && mode <= 4)
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());

        auto& voice = voices[0];
        if (! voice.active || voice.note != note)
            return;

        if (! monoStack.empty())
        {
            // fall back to the previous key still held - always legato
            const bool singleTrigger = mode == 2 || mode == 4;
            startVoice (0, monoStack.back(), voice.velocity, ! singleTrigger, b.glideOn);
        }
        else
        {
            releaseVoice (0, releaseVelocity);
        }
        return;
    }

    for (int v = 0; v < kPolyVoices; ++v)
        if (voices[(size_t) v].active && voices[(size_t) v].held && voices[(size_t) v].note == note)
            releaseVoice (v, releaseVelocity);
}

// Public note entry points: MIDI (or the on-screen keyboard) lands here.
void Engine::noteOn (int note, float velocity)
{
    note = juce::jlimit (0, 127, note);
    lastVelocity = velocity;

    if (b.arpMode > 0)
    {
        keyIsDown[(size_t) note] = true;
        arpKeyDown (note);
        return;
    }

    // Hold mode: the first key of a new chord lets the previous one go.
    if (b.keyMode == 5 && holdLatched)
    {
        bool anyDown = false;
        for (bool d : keyIsDown) anyDown = anyDown || d;

        if (! anyDown)
        {
            for (int v = 0; v < kPolyVoices; ++v)
                releaseVoice (v, 0.0f);
            holdLatched = false;
        }
    }

    keyIsDown[(size_t) note] = true;
    keyDown (note, velocity);
}

void Engine::noteOff (int note, float releaseVelocity)
{
    note = juce::jlimit (0, 127, note);
    keyIsDown[(size_t) note] = false;

    if (b.arpMode > 0)
    {
        arpKeyUp (note);
        return;
    }

    if (b.keyMode == 5)
    {
        bool anyDown = false;
        for (bool d : keyIsDown) anyDown = anyDown || d;
        if (! anyDown)
            holdLatched = true;
        return;
    }

    keyUp (note, releaseVelocity);
}

void Engine::allNotesOff()
{
    arp.numHeld = 0;
    arp.latched = false;
    arpReleaseSounding();
    monoStack.clear();
    keyIsDown.fill (false);
    holdLatched = false;
    sustainPedal = false;

    for (int v = 0; v < kPolyVoices; ++v)
    {
        voices[(size_t) v].sustained = false;
        releaseVoice (v, 0.0f);
    }
}

//==============================================================================
// Arpeggiator
//==============================================================================
void Engine::arpKeyDown (int note)
{
    if (arp.latched)
    {
        // Hold: a fresh chord after letting go replaces the held one.
        arp.numHeld = 0;
        arp.latched = false;
    }

    for (int n = 0; n < arp.numHeld; ++n)
        if (arp.held[(size_t) n] == note)
            return;

    if (arp.numHeld < (int) arp.held.size())
        arp.held[(size_t) arp.numHeld++] = note;

    if (arp.numHeld == 1 && arp.numSounding == 0)
    {
        // first key: play straight away rather than a step later
        arp.samplesToNextStep = 0.0;
        arp.step = -1;
        arp.patternStep = 0;
        arp.goingUp = true;
    }
}

void Engine::arpKeyUp (int note)
{
    if (b.arpHold)
    {
        bool anyDown = false;
        for (bool d : keyIsDown) anyDown = anyDown || d;
        if (! anyDown)
            arp.latched = true;
        return;
    }

    for (int n = 0; n < arp.numHeld; ++n)
        if (arp.held[(size_t) n] == note)
        {
            for (int m = n; m < arp.numHeld - 1; ++m)
                arp.held[(size_t) m] = arp.held[(size_t) m + 1];
            --arp.numHeld;
            break;
        }

    if (arp.numHeld == 0)
        arpReleaseSounding();
}

void Engine::arpReleaseSounding()
{
    for (int n = 0; n < arp.numSounding; ++n)
        keyUp (arp.sounding[(size_t) n], 0.0f);
    arp.numSounding = 0;
    arp.samplesToGateOff = -1.0;
}

int Engine::arpBuildSequence (int* seq) const
{
    int notes[64];
    const int count = arp.numHeld;
    for (int n = 0; n < count; ++n)
        notes[n] = arp.held[(size_t) n];

    if (b.arpMode != 4)   // everything but As Played runs in pitch order
        std::sort (notes, notes + count);

    int total = 0;
    for (int o = 0; o < b.arpOctaves; ++o)
        for (int n = 0; n < count; ++n)
            if (notes[n] + 12 * o <= 127 && total < 256)
                seq[total++] = notes[n] + 12 * o;

    return total;
}

// Patterns: 0 plays the 32-step user grid, 1 plays every step, and 2..63 are
// fixed rhythms of their own - each one a distinct, repeatable 16-step groove.
bool Engine::arpStepIsOn (int patternStep, float& velocityScale, float& lengthScale) const
{
    velocityScale = 1.0f;
    lengthScale = 1.0f;

    if (b.arpPattern == 0)
    {
        const int s = patternStep % userPatternLength;
        velocityScale = juce::jmax (0.05f, stepVelocity[(size_t) s]);
        lengthScale = 0.1f + 1.8f * stepLength[(size_t) s];
        return stepOn[(size_t) s];
    }

    if (b.arpPattern == 1)
        return true;

    const int s = patternStep % 16;
    if (s == 0)
        return true;

    auto h = (juce::uint32) (b.arpPattern * 2654435761u) ^ (juce::uint32) (s * 40503u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;

    const float density = 0.45f + 0.4f * (float) ((b.arpPattern * 7) % 10) / 10.0f;
    velocityScale = (h & 0x300u) == 0 ? 1.0f : 0.72f;          // accents
    lengthScale = (h & 0xC00u) == 0 ? 0.45f : 1.0f;            // the odd staccato
    return (float) (h & 0xFFu) / 255.0f < density;
}

void Engine::arpAdvance()
{
    if (arp.numHeld == 0)
    {
        if (arp.numSounding > 0)
            arpReleaseSounding();
        return;
    }

    if (arp.samplesToGateOff >= 0.0 && (arp.samplesToGateOff -= 1.0) <= 0.0)
    {
        for (int n = 0; n < arp.numSounding; ++n)
            keyUp (arp.sounding[(size_t) n], 0.0f);
        arp.numSounding = 0;
        arp.samplesToGateOff = -1.0;
    }

    if ((arp.samplesToNextStep -= 1.0) > 0.0)
        return;

    double stepSamples = 60.0 / juce::jmax (20.0, tempoBpm) * (double) b.arpStepBeats * sampleRate;
    stepSamples *= (arp.patternStep % 2 == 0) ? 1.0 + b.arpSwing : 1.0 - b.arpSwing;
    stepSamples = juce::jmax (16.0, stepSamples);
    arp.samplesToNextStep += stepSamples;

    for (int n = 0; n < arp.numSounding; ++n)
        keyUp (arp.sounding[(size_t) n], 0.0f);
    arp.numSounding = 0;

    float velScale, lenScale;
    const bool on = arpStepIsOn (arp.patternStep, velScale, lenScale);
    ++arp.patternStep;

    if (! on)
        return;

    int seq[256];
    const int n = arpBuildSequence (seq);
    if (n <= 0)
        return;

    const float velocity = juce::jlimit (0.05f, 1.0f, lastVelocity * velScale);

    if (b.arpMode == 6)
    {
        // Chord: every held key at once, climbing through the octaves
        arp.step = (arp.step + 1) % b.arpOctaves;
        int sorted[64];
        const int count = arp.numHeld;
        for (int k = 0; k < count; ++k) sorted[k] = arp.held[(size_t) k];
        std::sort (sorted, sorted + count);

        for (int k = 0; k < count && arp.numSounding < (int) arp.sounding.size(); ++k)
        {
            const int note = juce::jmin (127, sorted[k] + 12 * arp.step);
            keyDown (note, velocity);
            arp.sounding[(size_t) arp.numSounding++] = note;
        }
    }
    else
    {
        switch (b.arpMode)
        {
            case 2:  arp.step = arp.step <= 0 ? n - 1 : arp.step - 1; break;   // Down
            case 3:                                                             // Up & Down
                if (n == 1) { arp.step = 0; break; }
                if (arp.goingUp) { if (++arp.step >= n - 1) { arp.step = n - 1; arp.goingUp = false; } }
                else             { if (--arp.step <= 0)     { arp.step = 0;     arp.goingUp = true; } }
                break;
            case 5:  arp.step = arp.rng.nextInt (n); break;                      // Random
            default: arp.step = (arp.step + 1) % n; break;                       // Up, As Played, Arp>Matrix
        }

        const int note = seq[juce::jlimit (0, n - 1, arp.step)];
        keyDown (note, velocity);
        arp.sounding[0] = note;
        arp.numSounding = 1;
    }

    arp.samplesToGateOff = juce::jmax (8.0, stepSamples * (double) juce::jmin (1.0f, b.arpGate * lenScale));
}

//==============================================================================
// Per-voice saturation, between the filters. Its drive comes from Osc Volume:
// centre is a moderate drive, clockwise pushes it harder - the way the
// hardware uses the mixer level as the saturation input gain.
//==============================================================================
static float saturate (int curveId, float x, int ch, float amount01, float noteHz, double sr,
                       float* hold, int& holdCount, OnePole& filt)
{
    const float d = std::pow (2.0f, amount01 * 5.0f - 1.0f);   // 0.5 .. 16, 2.8 at centre

    switch (curveId)
    {
        case 1:  return std::tanh (x * d * 0.35f) / std::tanh (0.35f * d + 0.2f);   // Light
        case 2:  return std::tanh (x * d * 0.7f) / std::tanh (0.7f * d);            // Soft
        case 3:  { const float y = juce::jlimit (-1.5f, 1.5f, x * d * 0.7f);         // Middle
                   return (y - y * y * y / 6.75f) * 0.9f; }
        case 4:  return juce::jlimit (-1.0f, 1.0f, x * d);                           // Hard
        case 5:  { const float y = juce::jlimit (-1.0f, 1.0f, x * d * 2.0f);         // Digital
                   return std::round (y * 8.0f) / 8.0f; }
        case 6:  return std::sin (juce::jlimit (-6.0f, 6.0f, x * d) * 1.2f);         // Wave Shaper
        case 7:  return std::abs (std::tanh (x * d * 0.8f)) * 1.6f - 0.5f;           // Rectifier
        case 8:  { const float steps = std::pow (2.0f, 1.0f + (1.0f - amount01) * 11.0f); // Bit Reducer
                   return std::round (x * steps) / steps; }
        case 9:                                                                      // Rate Reducer
        case 10:                                                                     // Rate + Follow
        {
            const int period = curveId == 9
                ? 1 + (int) (amount01 * amount01 * 40.0f)
                : juce::jmax (1, (int) (sr / juce::jmax (20.0f, noteHz) * (0.02f + 0.3f * amount01)));
            if (ch == 0 && ++holdCount >= period) holdCount = 0;
            if (holdCount == 0) hold[ch] = x;
            return hold[ch];
        }
        case 11: case 12:                                                            // Low Pass (+ Follow)
        case 13: case 14:                                                            // High Pass (+ Follow)
        {
            float hz = 100.0f * std::pow (190.0f, amount01);
            if (curveId == 12 || curveId == 14)
                hz *= noteHz / 261.6f;
            const float lp = filt.lp (x, OnePole::coef (hz, sr), ch);
            return curveId <= 12 ? lp : x - lp;
        }
        default: return x;
    }
}

void Engine::renderVoice (int v, float& outL, float& outR, ModArray& mod)
{
    auto& voice = voices[(size_t) v];
    const ModArray& pm = voice.prevMod;
    const float vel = voice.velocity;
    const float note = (float) voice.note;

    // Portamento
    voice.glideNote = b.glideOn ? note + (voice.glideNote - note) * b.glideCoef : note;
    const float glideOffset = voice.glideNote - note;

    //-- envelopes (matrix offsets are one sample late, which nobody can hear)
    const auto envParams = [&] (SynthModule& m, const float* blockMs, float A, float D, float S, float Sl, float R,
                                Dest dA, Dest dD, Dest dS, Dest dSl, Dest dR)
    {
        const auto t = [&] (int n, float byte, Dest d)
        {
            const float mv = pm[(size_t) d];
            return mv == 0.0f ? blockMs[n] : scaling::envMs (byte + mv * 127.0f);
        };
        m.setParameterAt (eIdx.attack, t (0, A, dA));
        m.setParameterAt (eIdx.decay, t (1, D, dD));
        m.setParameterAt (eIdx.sustain, clamp01 (unit (S) + pm[(size_t) dS]));
        m.setParameterAt (eIdx.slope, juce::jlimit (-1.0f, 1.0f, bipolar (Sl) + 2.0f * pm[(size_t) dSl]));
        m.setParameterAt (eIdx.release, t (2, R, dR));
    };

    envParams (*envAmp, b.ampMs, b.aA, b.aD, b.aS, b.aSlope, b.aR,
               Dest::AmpAttack, Dest::AmpDecay, Dest::AmpSustain, Dest::AmpSlope, Dest::AmpRelease);
    envParams (*envFilter, b.filtMs, b.fA, b.fD, b.fS, b.fSlope, b.fR,
               Dest::FiltAttack, Dest::FiltDecay, Dest::FiltSustain, Dest::FiltSlope, Dest::FiltRelease);

    envAmp->processVoiceSample (v, in.data(), out.data());     const float ampEnv = out[0][0];
    envFilter->processVoiceSample (v, in.data(), out.data());  const float filterEnvRaw = out[0][0];
    env3->processVoiceSample (v, in.data(), out.data());       const float env3Value = out[0][0];
    env4->processVoiceSample (v, in.data(), out.data());       const float env4Value = out[0][0];

    // With the Input Follower on, the filter envelope follows the audio input.
    const float filtEnv = b.followerMode > 0 ? juce::jlimit (0.0f, 1.0f, followerValue) : filterEnvRaw;

    //-- LFOs
    const Dest rateDest[] = { Dest::Lfo1Rate, Dest::Lfo2Rate, Dest::Lfo3Rate };
    float lfo[3];
    const float octavesFromC = (note - 60.0f) / 12.0f;

    for (int n = 0; n < 3; ++n)
    {
        float hz = b.lfoHz[n];
        const float octaves = pm[(size_t) rateDest[n]] * 9.97f + b.lfoKeyFollow[n] * octavesFromC;
        if (octaves != 0.0f)
            hz *= std::exp2 (octaves);

        auto& m = *lfoTyped[(size_t) n];
        m.setParameterAt (lIdx.rate, hz);
        if (n < 2)
            m.setParameterAt (lIdx.symmetry, 0.95f * juce::jlimit (-1.0f, 1.0f, b.lfoContour[n]
                                 + 2.0f * pm[(size_t) (n == 0 ? Dest::Lfo1Contour : Dest::Lfo2Contour)]));
        m.processVoiceSample (v, in.data(), out.data());
        lfo[n] = out[0][0];
    }

    if (voice.lfo3Fade < 1.0f)
    {
        voice.lfo3Fade = juce::jmin (1.0f, voice.lfo3Fade + (float) (1.0 / (juce::jmax (0.02f, b.lfo3FadeSeconds) * sampleRate)));
        lfo[2] *= voice.lfo3Fade;
    }

    //-- modulation matrix
    std::array<float, (size_t) Src::Count> src {};
    src[(size_t) Src::PitchBend] = pitchBend;
    src[(size_t) Src::ChanPressure] = channelPressure;
    src[(size_t) Src::ModWheel] = controllers[1];
    src[(size_t) Src::Breath] = controllers[2];
    src[(size_t) Src::Controller3] = controllers[3];
    src[(size_t) Src::FootPedal] = controllers[4];
    src[(size_t) Src::DataEntry] = controllers[6];
    src[(size_t) Src::Balance] = controllers[8];
    src[(size_t) Src::Controller9] = controllers[9];
    src[(size_t) Src::Expression] = controllers[11];
    for (int c = 12; c <= 16; ++c)
        src[(size_t) Src::Controller12 + (size_t) (c - 12)] = controllers[(size_t) c];
    src[(size_t) Src::HoldPedal] = controllers[64];
    src[(size_t) Src::PortamentoSwitch] = controllers[65];
    src[(size_t) Src::SostenutoPedal] = controllers[66];
    src[(size_t) Src::AmpEnv] = ampEnv;
    src[(size_t) Src::FilterEnv] = filtEnv;
    src[(size_t) Src::Env3] = env3Value;
    src[(size_t) Src::Env4] = env4Value;
    src[(size_t) Src::Lfo1Bi] = lfo[0];
    src[(size_t) Src::Lfo2Bi] = lfo[1];
    src[(size_t) Src::Lfo3Bi] = lfo[2];
    src[(size_t) Src::Lfo1Uni] = lfo[0] * 0.5f + 0.5f;
    src[(size_t) Src::Lfo2Uni] = lfo[1] * 0.5f + 0.5f;
    src[(size_t) Src::Lfo3Uni] = lfo[2] * 0.5f + 0.5f;
    src[(size_t) Src::VelocityOn] = vel;
    src[(size_t) Src::VelocityOff] = voice.releaseVelocity;
    src[(size_t) Src::KeyFollow] = (note - 60.0f) / 64.0f;
    src[(size_t) Src::RandomPerNote] = voice.random;
    src[(size_t) Src::Const1] = 0.01f;
    src[(size_t) Src::Const10] = 0.10f;

    mod.fill (0.0f);
    for (const auto& slot : matrix)
    {
        if (slot.source == Src::Off)
            continue;
        const float s = src[(size_t) slot.source];
        for (int d = 0; d < 3; ++d)
            mod[(size_t) slot.dest[(size_t) d]] += s * slot.amount[(size_t) d];
    }

    // LFO 1 and 2 each have an Assign route of their own into the same list.
    if (b.lfo1Dest > 0)
        mod[(size_t) destForByte[(size_t) juce::jlimit (0, 127, b.lfo1Dest)]]
            += lfo[0] * juce::jlimit (-1.0f, 1.0f, b.lfo1Assign + 2.0f * mod[(size_t) Dest::Lfo1Amt]) * kMatrixFullScale;
    if (b.lfo2Dest > 0)
        mod[(size_t) destForByte[(size_t) juce::jlimit (0, 127, b.lfo2Dest)]]
            += lfo[1] * juce::jlimit (-1.0f, 1.0f, b.lfo2Assign + 2.0f * mod[(size_t) Dest::Lfo2Amt]) * kMatrixFullScale;
    mod[(size_t) Dest::None] = 0.0f;

    const auto m = [&mod] (Dest d) { return mod[(size_t) d]; };
    const auto amt = [&m] (float knob, Dest d) { return juce::jlimit (-1.0f, 1.0f, knob + 2.0f * m (d)); };

    // LFO 3: its own short destination list, as a vibrato/PWM source.
    const float lfo3Amt = clamp01 (b.lfo3Amount + m (Dest::Lfo3Amt));
    const float lfo3Pitch = lfo[2] * lfo3Amt * lfo3Amt * 12.0f;
    const float lfo3Pw = lfo[2] * lfo3Amt * 0.5f;
    const int l3 = b.lfo3Dest;

    //-- pitch
    const float pb = b.bendExp ? pitchBend * std::abs (pitchBend) : pitchBend;
    const float bend = pb >= 0.0f ? pb * b.bendUp : pb * b.bendDown;
    const float common = b.transpose + m (Dest::Transpose) * 96.0f + bend + glideOffset;
    const float fromMiddle = note - 60.0f;

    const float semis1 = b.osc1Semi + fromMiddle * (b.osc1Track - 1.0f) + common
                       + lfo[0] * curve (amt (b.lfo1Osc1, Dest::Lfo1Osc1)) * 24.0f
                       + (l3 == 0 || l3 == 1 ? lfo3Pitch : 0.0f)
                       + m (Dest::Osc1Pitch) * 96.0f;

    const float detune2 = clamp01 (std::sqrt (b.osc2DetuneCents / 100.0f) + m (Dest::Osc2Detune));
    const float semis2 = b.osc2Semi + detune2 * detune2 + fromMiddle * (b.osc2Track - 1.0f) + common
                       + lfo[0] * curve (amt (b.lfo1Osc2, Dest::Lfo1Osc2)) * 24.0f
                       + (l3 == 1 || l3 == 2 ? lfo3Pitch : 0.0f)
                       + m (Dest::Osc2Pitch) * 96.0f
                       + filtEnv * curve (amt (b.osc2EnvPitch, Dest::FiltEnvOsc2Pitch)) * 48.0f;

    const float r1 = std::exp2 (semis1 / 12.0f);
    const float r2 = std::exp2 (semis2 / 12.0f);

    //-- wave, shape, pulse width
    const float lfo2Shape = lfo[1] * curve (amt (b.lfo2Shape, Dest::Lfo2Shape)) * 0.5f;
    const float lfo1Pw = lfo[0] * curve (amt (b.lfo1Pw, Dest::Lfo1Pw)) * 0.5f;
    const bool wt1 = b.osc1Mode >= 2, wt2 = b.osc2Mode >= 2;

    const auto wavePos = [] (bool wt, float classic, float base, float index, float interp)
    {
        if (! wt)
            return classic;
        float pos = clamp01 (index) * 7.0f;
        if (interp > 0.0f)
        {
            // Interpolation turned up steps through the waves instead of
            // gliding between them.
            const float whole = std::floor (pos), frac = pos - whole;
            pos = whole + clamp01 ((frac - 0.5f) / juce::jmax (0.02f, 1.0f - interp) + 0.5f);
        }
        return juce::jmin (63.0f, base + pos);
    };

    const float shape1 = clamp01 (b.osc1Shape + b.osc1ShapeVel * vel * 0.5f + lfo2Shape + (wt1 ? 0.0f : m (Dest::Osc1Shape)));
    const float shape2 = clamp01 (b.osc2Shape + b.osc2ShapeVel * vel * 0.5f + lfo2Shape + (wt2 ? 0.0f : m (Dest::Osc2Shape)));
    const float pw1 = 0.5f - 0.48f * clamp01 (b.osc1Pw + m (Dest::Osc1Pw) + b.pwVel * vel * 0.5f + lfo1Pw
                                              + (l3 == 3 || l3 == 4 ? lfo3Pw : 0.0f));
    const float pw2 = 0.5f - 0.48f * clamp01 (b.osc2Pw + m (Dest::Osc2Pw) + b.pwVel * vel * 0.5f + lfo1Pw
                                              + (l3 == 4 || l3 == 5 ? lfo3Pw : 0.0f));
    const float wave1 = wavePos (wt1, b.osc1Wave, b.osc1WtBase, b.osc1WtIdx + m (Dest::Osc1WtIndex) + (wt1 ? m (Dest::Osc1Shape) : 0.0f), b.osc1Interp);
    const float wave2 = wavePos (wt2, b.osc2Wave, b.osc2WtBase, b.osc2WtIdx + m (Dest::Osc2WtIndex) + (wt2 ? m (Dest::Osc2Shape) : 0.0f), b.osc2Interp);

    //-- oscillator 1 (Formant Spread in the wavetable models is a pitch-
    //   preserving sync: the slave runs faster, the reset keeps the pitch)
    const float spread1 = wt1 ? 1.0f + 3.0f * b.osc1FormantSpread : 1.0f;
    osc1->setParameterAt (o1.ratio, r1 * spread1);
    osc1->setParameterAt (o1.wave, wave1);
    osc1->setParameterAt (o1.shape, shape1);
    osc1->setParameterAt (o1.pw, pw1);
    osc1->setParameterAt (o1.sync, spread1 > 1.001f ? 1.0f : 0.0f);
    osc1->setParameterAt (o1.syncRatio, 1.0f / spread1);

    in[0] = { 0.0f, 0.0f }; in[1] = { 0.0f, 0.0f }; in[2] = { 0.0f, 0.0f };
    osc1->processVoiceSample (v, in.data(), out.data());
    const float o1L = out[0][0], o1R = out[0][1];

    //-- oscillator 2: FM from oscillator 1, hard sync to oscillator 1,
    //   Sync Frequency (plus the filter envelope) as a pitch-preserving sync.
    const float fmKnob = clamp01 (b.fmAmount + m (Dest::Osc2Fm) + b.fmVel * vel * 0.5f
                                  + lfo[1] * curve (amt (b.lfo2Fm, Dest::Lfo2Fm)) * 0.5f
                                  + filtEnv * curve (amt (b.fmEnv, Dest::FiltEnvFmSync)) * 0.5f);
    const float fmIndex = fmKnob * fmKnob * 6.0f;

    float fmSource = 0.0f;
    if (fmIndex > 0.0f)
    {
        const float o1 = (o1L + o1R) * 0.5f;
        switch (b.fmMode)
        {
            case 0:  fmSource = o1 * 0.5f + 0.5f; break;                          // Pos Triangle
            case 3:  fmSource = random.nextFloat() * 2.0f - 1.0f; break;          // Noise
            case 4:  fmSource = inputL; break;
            case 5:  fmSource = (inputL + inputR) * 0.5f; break;
            case 6:  fmSource = inputR; break;
            default: fmSource = o1; break;                                        // Triangle, Wave
        }
    }

    const float syncOctaves = juce::jlimit (0.0f, 4.5f, b.syncFreq * 4.5f
                               + filtEnv * curve (amt (b.syncEnv, Dest::FiltEnvFmSync)) * 4.5f
                               + (l3 == 6 ? (lfo[2] * 0.5f + 0.5f) * lfo3Amt * 3.0f : 0.0f));
    const float spread2 = wt2 ? 1.0f + 3.0f * b.osc2FormantSpread : 1.0f;
    const float slave2 = r2 * std::exp2 (syncOctaves) * spread2;
    const float master2 = b.osc2Sync ? r1 : r2;
    const bool sync2 = b.osc2Sync || slave2 > r2 * 1.001f;

    osc2->setParameterAt (o2.ratio, slave2);
    osc2->setParameterAt (o2.wave, wave2);
    osc2->setParameterAt (o2.shape, shape2);
    osc2->setParameterAt (o2.pw, pw2);
    osc2->setParameterAt (o2.sync, sync2 ? 1.0f : 0.0f);
    osc2->setParameterAt (o2.syncRatio, master2 / slave2);
    osc2->setInputConnected (0, fmIndex > 0.0f);

    in[0] = { fmSource * fmIndex, 0.0f };
    osc2->processVoiceSample (v, in.data(), out.data());
    const float o2L = out[0][0], o2R = out[0][1];
    in[0] = { 0.0f, 0.0f };

    //-- oscillator 3, sub, noise, ring
    float o3L = 0.0f, o3R = 0.0f;
    const float osc3Level = clamp01 (b.osc3Volume + m (Dest::Osc3Volume));
    if (b.osc3Mode > 0 && osc3Level > 0.0f)
    {
        const float detune3 = b.osc3DetuneCents / 100.0f;
        if (b.osc3Mode == 1)   // Slave: oscillator 2's sound, its own detune
        {
            osc3->setParameterAt (o3.ratio, r2 * std::exp2 ((detune3 + m (Dest::Osc3Pitch) * 96.0f) / 12.0f));
            osc3->setParameterAt (o3.wave, wt2 ? wave2 : b.osc2Wave);
            osc3->setParameterAt (o3.shape, shape2);
            osc3->setParameterAt (o3.pw, pw2);
        }
        else
        {
            osc3->setParameterAt (o3.ratio, std::exp2 ((b.osc3Semi + detune3 + common + m (Dest::Osc3Pitch) * 96.0f) / 12.0f));
        }
        osc3->processVoiceSample (v, in.data(), out.data());
        o3L = out[0][0] * osc3Level;
        o3R = out[0][1] * osc3Level;
    }

    float subL = 0.0f, subR = 0.0f;
    const float subLevel = clamp01 (b.subVolume + m (Dest::SubVolume));
    if (subLevel > 0.0f)
    {
        subOsc->setParameterAt (subRatio, 0.5f * r1);   // an octave under oscillator 1, wherever it goes
        subOsc->processVoiceSample (v, in.data(), out.data());
        subL = out[0][0] * subLevel;
        subR = out[0][1] * subLevel;
    }

    float nL = 0.0f, nR = 0.0f;
    const float noiseLevel = clamp01 (b.noiseVolume + m (Dest::NoiseVolume));
    if (noiseLevel > 0.0f)
    {
        noise->processVoiceSample (v, in.data(), out.data());
        const float colour = juce::jlimit (-1.0f, 1.0f, b.noiseColour + 2.0f * m (Dest::NoiseColor));
        const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float) sampleRate;

        for (int ch = 0; ch < 2; ++ch)
        {
            float x = out[0][(size_t) ch];
            if (colour < 0.0f)        // darker: low pass sweeping down to ~70 Hz
            {
                const float w = 18000.0f * std::exp2 (colour * 8.0f) * twoPiOverSr;
                voice.noiseLp[ch] += (x - voice.noiseLp[ch]) * (w / (1.0f + w));
                x = voice.noiseLp[ch];
            }
            else if (colour > 0.0f)   // brighter: high pass sweeping up to ~14 kHz
            {
                const float w = 20.0f * std::exp2 (colour * 9.5f) * twoPiOverSr;
                voice.noiseHp[ch] += (x - voice.noiseHp[ch]) * (w / (1.0f + w));
                x -= voice.noiseHp[ch];
            }
            (ch == 0 ? nL : nR) = x * noiseLevel * 0.7f;
        }
    }

    const float ringLevel = clamp01 (b.ringVolume + m (Dest::RingMod));

    //-- mixer
    const float bal = clamp01 (b.balance + m (Dest::OscBalance));
    const float g1 = juce::jmin (1.0f, 2.0f * (1.0f - bal));
    const float g2 = juce::jmin (1.0f, 2.0f * bal);

    // Split routing sends oscillator 1 (and the sub) to filter 1, the rest to
    // filter 2; every other routing sums the two halves.
    float aL = o1L * g1 + subL, aR = o1R * g1 + subR;
    float bL = o2L * g2 + o3L + o1L * o2L * ringLevel;
    float bR = o2R * g2 + o3R + o1R * o2R * ringLevel;

    if (b.inputMode == 1)   // Dynamic: the audio input played through each voice
    {
        aL += inputL; aR += inputR;
    }

    // Osc Volume: below the centre it fades the section out; above it drives
    // the saturation stage (or, with saturation off, adds up to half again).
    const float ov = juce::jlimit (-1.0f, 1.0f, b.oscVolume + 2.0f * m (Dest::OscVolume));
    const float satOn = b.satCurve > 0;
    const float pre = (ov < 0.0f ? 1.0f + ov : 1.0f + (satOn ? 0.0f : ov * 0.5f)) * 0.5f;   // 0.5: filter headroom
    aL *= pre; aR *= pre; bL *= pre; bR *= pre;

    // Noise has its own level and joins after Osc Volume, so pulling the
    // oscillators down to zero leaves pure noise into the filters.
    bL += nL * 0.5f; bR += nR * 0.5f;

    //-- filters
    const float keySemis = note - b.keyBase;
    constexpr float kFractionPerSemitone = 12.9f / 127.0f / 12.0f;   // one semitone of cutoff, in knob units
    const float lfo2Cut1 = lfo[1] * curve (amt (b.lfo2Cut1, Dest::Lfo2Cutoff1)) * 0.5f;
    const float lfo2Cut2 = lfo[1] * curve (amt (b.lfo2Cut2, Dest::Lfo2Cutoff2)) * 0.5f;

    const float cut1 = b.cut1 + m (Dest::Cutoff1)
                     + filtEnv * b.pol1 * (b.env1 + m (Dest::Filter1EnvAmt) + b.env1Vel * vel * 0.5f)
                     + lfo2Cut1 + keySemis * b.key1 * kFractionPerSemitone;

    const float cut2 = (b.link ? cut1 + b.linkOffset
                               : b.cut2 + lfo2Cut2 + keySemis * b.key2 * kFractionPerSemitone)
                     + m (Dest::Cutoff2)
                     + filtEnv * b.pol2 * (b.env2 + m (Dest::Filter2EnvAmt) + b.env2Vel * vel * 0.5f);

    const float lfo1Reso = lfo[0] * curve (amt (b.lfo1Reso, Dest::Lfo1Reso)) * 0.5f;
    const float res1 = clamp01 (b.res1 + m (Dest::Reso1) + b.res1Vel * vel * 0.5f + lfo1Reso);
    const float res2 = clamp01 (b.res2 + m (Dest::Reso2) + b.res2Vel * vel * 0.5f + lfo1Reso);

    filter1->setParameterAt (f1.cutoff, scaling::cutoffHz (cut1));
    filter1->setParameterAt (f1.resonance, res1 * 0.97f);
    filter2->setParameterAt (f2.cutoff, scaling::cutoffHz (cut2));
    filter2->setParameterAt (f2.resonance, res2 * 0.97f);

    // High resonance boosts the level at the peak; take some of it back so a
    // resonance sweep doesn't jump out of the mix.
    const float reso1Trim = 1.0f - 0.4f * res1 * res1;
    const float reso2Trim = 1.0f - 0.4f * res2 * res2;

    const float fbal = clamp01 (b.filterBalance + m (Dest::FilterBalance));
    const float noteHz = 440.0f * std::exp2 ((voice.glideNote - 69.0f) / 12.0f);
    const float satAmount = (ov + 1.0f) * 0.5f;

    const auto sat = [&] (float& l, float& r)
    {
        if (! satOn) return;
        l = saturate (b.satCurve, l, 0, satAmount, noteHz, sampleRate, voice.satHold, voice.satCount, voice.satFilter);
        r = saturate (b.satCurve, r, 1, satAmount, noteHz, sampleRate, voice.satHold, voice.satCount, voice.satFilter);
    };

    float yL, yR;

    if (b.routing <= 1)   // Serial 4 / Serial 6
    {
        in[0] = { aL + bL, aR + bR };
        filter1->processVoiceSample (v, in.data(), out.data());
        float s1L = out[0][0] * reso1Trim, s1R = out[0][1] * reso1Trim;
        sat (s1L, s1R);

        in[0] = { s1L, s1R };
        filter2->processVoiceSample (v, in.data(), out.data());
        out[0][0] *= reso2Trim; out[0][1] *= reso2Trim;

        // Balance below the centre fades filter 2 back out of the chain.
        const float through = clamp01 (fbal * 2.0f);
        yL = s1L + (out[0][0] - s1L) * through;
        yR = s1R + (out[0][1] - s1R) * through;
    }
    else
    {
        const bool split = b.routing == 3;
        in[0] = { split ? aL : aL + bL, split ? aR : aR + bR };
        filter1->processVoiceSample (v, in.data(), out.data());
        const float p1L = out[0][0] * reso1Trim, p1R = out[0][1] * reso1Trim;

        in[0] = { split ? bL : aL + bL, split ? bR : aR + bR };
        filter2->processVoiceSample (v, in.data(), out.data());
        out[0][0] *= reso2Trim; out[0][1] *= reso2Trim;

        const float w1 = juce::jmin (1.0f, 2.0f * (1.0f - fbal)), w2 = juce::jmin (1.0f, 2.0f * fbal);
        yL = p1L * w1 + out[0][0] * w2;
        yR = p1R * w1 + out[0][1] * w2;
        sat (yL, yR);
    }
    in[0] = { 0.0f, 0.0f };

    //-- amplifier
    const float velGain = juce::jlimit (0.0f, 2.0f, 1.0f + b.ampVel * (vel - 1.0f));

    voice.punch *= punchCoef;
    // Punch: a short level spike at the start of every note - the "thump"
    // on a bass or pluck. Up to about +7 dB, dying away over about 30 ms.
    const float punchGain = 1.0f + clamp01 (b.punch + m (Dest::Punch)) * voice.punch * 1.25f;

    const float lfoGain = juce::jlimit (0.0f, 2.0f, 1.0f + lfo[0] * curve (amt (b.lfo1Gain, Dest::Lfo1FiltGain)));
    const float volMod = juce::jlimit (0.0f, 2.0f, 1.0f + 2.0f * m (Dest::PatchVolume));
    const float gain = ampEnv * velGain * punchGain * lfoGain * volMod * 2.0f;   // 2: undo the filter headroom

    const float pan = juce::jlimit (-1.0f, 1.0f, b.pan + 2.0f * m (Dest::Panorama) + b.panVel * vel
                                                 + lfo[1] * curve (amt (b.lfo2Pan, Dest::Lfo2Pan)));
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    outL = yL * gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    outR = yR * gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    voice.prevMod = mod;
}

//==============================================================================
// The Filter Bank: one Frequency, one Resonance and one Mix knob, and a Type
// that decides what they drive.
//==============================================================================
void Engine::processFilterBank (float& l, float& r, float freqMod, float resoMod)
{
    const float target = clamp01 (b.bankFreq + freqMod);
    smoothedBankFreq = smoothedBankFreq < 0.0f ? target : smoothedBankFreq + (target - smoothedBankFreq) * 0.002f;
    const float f = smoothedBankFreq;
    const float reso = clamp01 (b.bankReso + resoMod);
    const float dryL = l, dryR = r;
    float wetL = l, wetR = r;

    switch (b.bankType)
    {
        case 1:   // Ring Modulator: a sine carrier from 20 Hz to 20 kHz
        {
            const double hz = 20.0 * std::exp2 ((double) f * 9.97);
            bankRingPhase += hz / sampleRate;
            bankRingPhase -= std::floor (bankRingPhase);
            const double ph = bankRingPhase * juce::MathConstants<double>::twoPi;
            wetL = l * (float) std::sin (ph);
            wetR = r * (float) std::sin (ph + (double) b.bankStereo * juce::MathConstants<double>::pi);
            break;
        }

        case 2:   // Frequency Shifter: centre is no shift, either side up to 2.5 kHz
        {
            const float x = (f - 0.5f) * 2.0f;
            freqShifter.setShift (x * std::abs (x) * 2500.0f);
            freqShifter.process (wetL, wetR);
            break;
        }

        case 3:   // Vowel Filter
            vowelBank->setParameterAt (vowelPos, f * 100.0f);
            in[0] = { l, r };
            vowelBank->processSample (in.data(), out.data());
            wetL = out[0][0]; wetR = out[0][1];
            break;

        case 4:   // Comb Filter: 20 Hz .. 8 kHz
            combBank->setParameterAt (combFreq, 20.0f * std::exp2 (f * 8.64f));
            in[0] = { l, r };
            combBank->processSample (in.data(), out.data());
            wetL = out[0][0]; wetR = out[0][1];
            break;

        default:  // 5..8 XFade (1, 2, 4, 6 poles), 9..11 VariSlope LP / HP / BP
        {
            const float hzL = scaling::cutoffHz (f);
            const float hzR = hzL * std::exp2 (b.bankStereo);
            const float GL = OnePole::coef (hzL, sampleRate), GR = OnePole::coef (hzR, sampleRate);

            float poles;
            if (b.bankType <= 8)
            {
                static const float xfadePoles[] = { 1.0f, 2.0f, 4.0f, 6.0f };
                poles = xfadePoles[b.bankType - 5];
            }
            else
            {
                poles = 1.0f + 5.0f * b.bankPoles;
            }

            const int whole = juce::jlimit (1, 6, (int) std::ceil (poles));
            const float frac = poles - std::floor (poles);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = ch == 0 ? l : r;
                const float G = ch == 0 ? GL : GR;
                float lp = x, hp = x, lpPrev = x, hpPrev = x;

                for (int s = 0; s < whole; ++s)
                {
                    lpPrev = lp; hpPrev = hp;
                    lp = bankStages[(size_t) s].lp (lp, G, ch);
                    hp = hp - bankStagesHp[(size_t) s].lp (hp, G, ch);
                }

                // fractional pole counts crossfade the last stage in
                if (b.bankType >= 9 && frac > 0.0f)
                {
                    lp = lpPrev + (lp - lpPrev) * frac;
                    hp = hpPrev + (hp - hpPrev) * frac;
                }

                float y;
                if (b.bankType <= 8)       y = lp + (hp - lp) * b.bankSlope;   // XFade: Slope morphs LP -> HP
                else if (b.bankType == 9)  y = lp;
                else if (b.bankType == 10) y = hp;
                else                       y = bankBpLp.lp (hp, G, ch);         // BP: high pass into low pass

                (ch == 0 ? wetL : wetR) = y;
            }

            // Resonance: a band pass peak at the cutoff on top of the slope.
            if (reso > 0.01f)
            {
                if (std::abs (hzL - lastPeakHz) > hzL * 0.01f || std::abs (reso - lastPeakReso) > 0.01f)
                {
                    bankPeak.set (Biquad::Type::BandPass, sampleRate, hzL, 0.7f + reso * 12.0f, 0.0f);
                    lastPeakHz = hzL; lastPeakReso = reso;
                }
                wetL += bankPeak.process (l, 0) * reso * 2.0f;
                wetR += bankPeak.process (r, 1) * reso * 2.0f;
            }
            break;
        }
    }

    l = dryL + (wetL - dryL) * b.bankMix;
    r = dryR + (wetR - dryR) * b.bankMix;
}

//==============================================================================
void Engine::renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    updateParameters();

    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(), filter1.get(), filter2.get(),
                     distortion.get(), phaser.get(), chorus.get(), delay.get(), reverb.get(), tapeWobble.get(),
                     vowelBank.get(), combBank.get(), vocoder.get(), envFollow.get(), stereoWidth.get(),
                     granulator.get() })
        m->blockStart();

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    const int numSamples = buffer.getNumSamples();

    const float patchGain = b.patchVolume * controllers[7] * 0.35f;   // 0.35: headroom for chords
    const float balanceTilt = (controllers[8] - 0.5f) * 2.0f;
    const bool arpRunning = b.arpMode > 0;
    ModArray scratch {};

    const auto renderOne = [&] (int index)
    {
        // Audio input, for the Input section, vocoder, follower and FM.
        const float rawL = left[index], rawR = right[index];
        switch (b.inputSelect)
        {
            case 0:  inputL = inputR = rawL; break;
            case 2:  inputL = inputR = rawR; break;
            default: inputL = rawL; inputR = rawR; break;
        }

        if (b.followerMode > 0)
        {
            const float x = b.followerMode == 1 ? rawL : (b.followerMode == 3 ? rawR : (rawL + rawR) * 0.5f);
            in[0] = { x, x };
            envFollow->setInputConnected (0, true);
            envFollow->processSample (in.data(), out.data());
            followerValue = out[0][0];
        }

        for (auto* l : lfoTyped)
            l->advanceShared();

        if (arpRunning)
            arpAdvance();

        float l = 0.0f, r = 0.0f;

        for (int v = 0; v < kPolyVoices; ++v)
        {
            auto& voice = voices[(size_t) v];
            if (! voice.active)
                continue;

            float vl, vr;
            renderVoice (v, vl, vr, scratch);
            l += vl;
            r += vr;

            if (! voice.held && ! voice.sustained && ampEnvTyped->isIdle (v))
                voice.active = false;
        }

        l *= patchGain;
        r *= patchGain;

        // Global destinations follow the newest voice's matrix.
        const ModArray* gm = lastVoice >= 0 && voices[(size_t) lastVoice].active
                               ? &voices[(size_t) lastVoice].prevMod : nullptr;
        const auto g = [gm] (Dest d) { return gm != nullptr ? (*gm)[(size_t) d] : 0.0f; };

        //-- Input section
        if (b.inputMode == 2) { l += inputL; r += inputR; }                   // Static: straight in
        if (b.inputRing > 0.0f)
        {
            const float mono = (inputL + inputR) * 0.5f;
            l += (l * mono * 2.0f - l) * b.inputRing;
            r += (r * mono * 2.0f - r) * b.inputRing;
        }

        const auto run = [this] (SynthModule& mdl, float& a, float& c)
        {
            in[0] = { a, c };
            mdl.processSample (in.data(), out.data());
            a = out[0][0]; c = out[0][1];
        };

        //-- Distortion
        if (b.distCurve > 0)
        {
            const float drive = clamp01 (b.distDrive + g (Dest::DistIntensity));
            distortion->setParameterAt (distDrive, 1.0f + 30.0f * drive * drive);
            const float trim = 1.0f / (1.0f + drive * 1.5f);   // keep the level roughly steady
            float dl = distPre.process (l, 0), dr = distPre.process (r, 1);
            run (*distortion, dl, dr);
            if (distToneOn)
            {
                dl = distPost.process (dl, 0);
                dr = distPost.process (dr, 1);
            }
            l += (dl * trim - l) * b.distMix;   // Mix blends clean and distorted
            r += (dr * trim - r) * b.distMix;
        }

        //-- Filter Bank
        if (b.bankType > 0 && b.bankMix > 0.0f)
            processFilterBank (l, r, g (Dest::BankFreq), g (Dest::BankReso));

        //-- Vocoder
        if (b.vocoderMode > 0)
        {
            const float inMono = (inputL + inputR) * 0.5f;
            float carrierL = l, carrierR = r, modulator = inMono;

            if (b.vocoderMode == 3)             // Noise carrier
                carrierL = carrierR = (random.nextFloat() * 2.0f - 1.0f) * 0.3f;
            else if (b.vocoderMode >= 4)        // Input carrier, synth modulator
            {
                carrierL = carrierR = b.vocoderMode == 4 ? inputL : (b.vocoderMode == 6 ? inputR : inMono);
                modulator = (l + r) * 0.5f;
            }

            in[0] = { carrierL, carrierR };
            in[1] = { modulator, modulator };
            vocoder->processSample (in.data(), out.data());
            in[1] = { 0.0f, 0.0f };

            // Each band is a slice of carrier times a slice of modulator
            // envelope - two small numbers - so the sum needs real makeup
            // gain to sit at the level of the dry synth.
            constexpr float kVocoderMakeup = 18.0f;
            l = l + (out[0][0] * kVocoderMakeup - l) * b.vocoderBalance;
            r = r + (out[0][1] * kVocoderMakeup - r) * b.vocoderBalance;
        }

        //-- Character
        if (b.charAmount > 0.0f)
        {
            const float a = b.charAmount;
            if ((b.charType >= 1 && b.charType <= 3) || b.charType == 6)
            {
                const float d = 1.0f + a * (b.charType == 6 ? 2.0f : 1.5f * (float) b.charType);
                l = std::tanh (l * d) / d * (1.0f + 0.3f * a);
                r = std::tanh (r * d) / d * (1.0f + 0.3f * a);
            }

            float cl = charB.process (charA.process (l, 0), 0);
            float cr = charB.process (charA.process (r, 1), 1);

            if (b.charType == 1 || b.charType == 2 || b.charType == 3 || b.charType == 8)
            {
                // the filtering types blend in with intensity
                cl = l + (cl - l) * a;
                cr = r + (cr - r) * a;
            }
            l = cl; r = cr;

            if (b.charType == 7)
                run (*stereoWidth, l, r);
        }

        //-- EQ
        if (b.eqOn)
        {
            l = eqHigh.process (eqMid.process (eqLow.process (l, 0), 0), 0);
            r = eqHigh.process (eqMid.process (eqLow.process (r, 1), 1), 1);
        }

        //-- Phaser
        const float phaserMix = clamp01 (b.phaserMix + g (Dest::PhaserMix));
        if (phaserMix > 0.0f)
        {
            phaser->setParameterAt (phaserWet, phaserMix * 50.0f);
            run (*phaser, l, r);
        }

        //-- Chorus
        const float chorusMix = clamp01 (b.chorusMix + g (Dest::ChorusMix));
        if (b.chorusType > 0 && chorusMix > 0.0f)
        {
            chorus->setParameterAt (chorusWet, chorusMix * (b.chorusType == 5 ? 100.0f : 50.0f));
            run (*chorus, l, r);
        }

        //-- Delay (send). Keeps running for a while after the send closes so
        //   the echoes die away instead of stopping.
        const float delaySend = clamp01 (b.delaySend + g (Dest::DelaySend));
        if (b.delayMode > 0)
        {
            delayIdle = delaySend > 0.0f ? 0 : delayIdle + 1;
            if (delayIdle < (int) (sampleRate * 12.0))
            {
                float dl = l * delaySend, dr = r * delaySend;
                run (*delay, dl, dr);
                if (b.delayTape)
                    run (*tapeWobble, dl, dr);

                if (b.delayColour != 0.0f)
                {
                    // Colour: darker below the centre, thinner above it.
                    const float c = b.delayColour;
                    const float hz = c < 0.0f ? 20000.0f * std::exp2 (c * 5.3f) : 20.0f * std::exp2 (c * 7.3f);
                    const float G = OnePole::coef (hz, sampleRate);
                    const float lpL = delayColour.lp (dl, G, 0), lpR = delayColour.lp (dr, G, 1);
                    dl = c < 0.0f ? lpL : dl - lpL;
                    dr = c < 0.0f ? lpR : dr - lpR;
                }
                l += dl; r += dr;
                if (getenv("DBG") && index == 0) fprintf (stderr, "time=%.1f wet=%.1f tap=%.1f mode=%.0f ratio=%.2f dl=%.4f\n", delay->getParameter ("time"), delay->getParameter ("dryWet"), delay->getParameter ("tapVolume"), delay->getParameter ("mode"), delay->getParameter ("ratio"), dl);
            }
        }

        //-- Reverb (send)
        const float reverbSend = clamp01 (b.reverbSend + g (Dest::ReverbSend));
        if (b.reverbOn)
        {
            reverbIdle = reverbSend > 0.0f ? 0 : reverbIdle + 1;
            if (reverbIdle < (int) (sampleRate * 20.0))
            {
                float wl = l * reverbSend, wr = r * reverbSend;
                run (*reverb, wl, wr);
                l += wl * 0.45f; r += wr * 0.45f;   // the reverb's return runs hot
            }
        }

        // Granulator. Like the sends, it keeps running for a while after the
        // mix is closed so its last grains ring out instead of stopping dead.
        if (granMix > 0.0f || granIdle < (int) (sampleRate * 4.0))
        {
            granIdle = granMix > 0.0f ? 0 : granIdle + 1;
            float gl = l, gr = r;
            run (*granulator, gl, gr);
            l += (gl - l) * granMix;
            r += (gr - r) * granMix;
        }

        // AC couple at about 5 Hz, as a hardware output stage would.
        constexpr float dcR = 0.9993f;
        const float yl = l - dcX[0] + dcR * dcY[0];
        const float yr = r - dcX[1] + dcR * dcY[1];
        dcX[0] = l; dcY[0] = yl; dcX[1] = r; dcY[1] = yr;
        l = yl; r = yr;

        // Output stage: linear to -2 dB, then a gentle knee that never
        // passes full scale.
        const auto softLimit = [] (float x)
        {
            constexpr float t = 0.8f;
            if (x > t)  return t + (1.0f - t) * std::tanh ((x - t) / (1.0f - t));
            if (x < -t) return -t + (1.0f - t) * std::tanh ((x + t) / (1.0f - t));
            return x;
        };

        l = softLimit (l);   // last, so nothing after it can push past full scale
        r = softLimit (r);


        if (secondOut != nullptr)
        {
            secondOut[0][index] = l * b.secondOut;
            secondOut[1][index] = r * b.secondOut;
            l *= 1.0f - b.secondOut;
            r *= 1.0f - b.secondOut;
        }

        if (balanceTilt != 0.0f)
        {
            l *= juce::jlimit (0.0f, 1.0f, 1.0f - balanceTilt);
            r *= juce::jlimit (0.0f, 1.0f, 1.0f + balanceTilt);
        }

        left[index] = l;
        right[index] = r;
    };

    int sample = 0;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int eventSample = juce::jlimit (0, numSamples, meta.samplePosition);

        for (; sample < eventSample; ++sample)
            renderOne (sample);

        if (msg.isNoteOn())
            noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            noteOff (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            allNotesOff();
        else if (msg.isPitchWheel())
            pitchBend = ((float) msg.getPitchWheelValue() - 8192.0f) / 8192.0f;
        else if (msg.isChannelPressure())
            channelPressure = (float) msg.getChannelPressureValue() / 127.0f;
        else if (msg.isAftertouch())
            channelPressure = (float) msg.getAfterTouchValue() / 127.0f;
        else if (msg.isController())
        {
            const int cc = juce::jlimit (0, 127, msg.getControllerNumber());
            controllers[(size_t) cc] = (float) msg.getControllerValue() / 127.0f;

            if (cc == 64)   // sustain pedal
            {
                const bool down = msg.getControllerValue() >= 64;
                if (sustainPedal && ! down)
                {
                    sustainPedal = false;
                    for (int v = 0; v < kPolyVoices; ++v)
                        if (voices[(size_t) v].sustained)
                        {
                            voices[(size_t) v].sustained = false;
                            voices[(size_t) v].held = true;   // so releaseVoice lets it go
                            releaseVoice (v, 0.0f);
                        }
                }
                sustainPedal = down;
            }
        }
    }

    for (; sample < numSamples; ++sample)
        renderOne (sample);
}

} // namespace aquavibrio
