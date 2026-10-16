#include "AquaVibrioOriginalEngine.h"
#include "Aquanode/OscillatorModule.h"

namespace aquavibrio
{
namespace original
{

using namespace aquanode;
using scaling::unit;

// Output calibration: a single full saw at Patch Volume 100 comes out at the
// level the reference renders it (-32.5 dB RMS, both channels).
static constexpr float kOutputTrim = 0.355f;

// The reverb's return level (lines up the measured tail against the dry note).
static constexpr float kReverbReturn = 1.04f;

// Ring modulator level (oscillator 1 times oscillator 2).
static constexpr float kRingGain = 2.79f;

// Oscillators 1 and 2 against the reference (a saw sits 0.3 dB low there).
static constexpr float kOscTrim = 0.966f;

// Oscillator 3 against oscillators 1 and 2 (measured, every mode alike).
static constexpr float kOsc3Gain = 1.50f;

// Split routing: each filter's channel against Parallel's (measured +9.9 dB).
static constexpr float kSplitGain = 2.26f;

// Osc 3 Slave: each of the doubled pair against a lone oscillator 2 (measured:
// about level), and the least detune between them, in semitones.
static constexpr float kSlaveLevel = 1.03f;
static constexpr float kSlaveMinSpread = 0.02f;

// White noise at full volume, neutral colour (measured, ~1 dB under a saw).
static constexpr float kNoiseGain = 0.79f;

// An oscillator pitch destination moves a semitone per knob byte (measured:
// LFO 2 at +11 % moves oscillator 1, 2 or 3 by 7 semitones).
static constexpr float kPitchModSemis = 127.0f;

// LFO 1 -> oscillator pitch at full amount, in semitones (the reference's depth).
static constexpr float kLfoPitchSemis = 9.5f;
using scaling::bipolar;

    // Module parameters by name, allocation free: a parameter's index depends
    // only on the module TYPE, so each call site resolves it once (a static,
    // looked up on the first call - prepare() makes that call, off the audio
    // thread) and writes by index from then on. Every effect chain holds the
    // same module types, so one call site serves all of them.
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
// Effect chains
//==============================================================================
void Engine::FxChain::createModules()
{
    auto& factory = ModuleFactory::instance();

    distortion = factory.createInstance ("fx.vibriodistortion");
    phaser = factory.createInstance ("fx.phaser");
    chorus = factory.createInstance ("fx.chorus");
    reverb = factory.createInstance ("fx.reverb");
    tapeWobble = factory.createInstance ("fx.tapewobble");
    vowelBank = factory.createInstance ("filter.formant");
    combBank = factory.createInstance ("filter.combfilter");
    vocoder = factory.createInstance ("fx.vocode");
    granulator = factory.createInstance ("fx.granulation");
    stereoWidth = factory.createInstance ("fx.stereowidth");

    for (auto* m : { distortion.get(), phaser.get(), chorus.get(), reverb.get(), tapeWobble.get(),
                     vowelBank.get(), combBank.get(), vocoder.get(), granulator.get(), stereoWidth.get() })
    {
        if (m == nullptr)
        {
            juce::Logger::writeToLog ("FATAL: Aqua Vibrio could not create one of its effect modules - "
                                      "a module .cpp is probably missing from the build.");
            jassertfalse;
            std::abort();
        }
    }

    // Things that never change: the engine does every blend itself.
    tapeWobble->setParameter ("hiss", 0.0f);
    tapeWobble->setParameter ("dropouts", 0.0f);
    tapeWobble->setParameter ("dryWet", 100.0f);
    reverb->setParameter ("dryWet", 100.0f);      // a send: the engine mixes it in
    vowelBank->setParameter ("dryWet", 100.0f);
    combBank->setParameter ("dryWet", 100.0f);
    combBank->setParameter ("blend", 100.0f);
    distortion->setParameter ("level", 1.0f);
    distortion->setParameter ("dryWet", 100.0f);  // the engine does the blend
    vocoder->setParameter ("level", 4.0f);
    granulator->setParameter ("window", 2.0f);    // Position scans the last two seconds
    granulator->setParameter ("dryWet", 100.0f);  // the engine blends it in, so the cloud fades cleanly

    freqShifter.setMix (1.0f);
}

void Engine::FxChain::prepare (double sr)
{
    for (auto* m : { distortion.get(), phaser.get(), chorus.get(), reverb.get(), tapeWobble.get(),
                     vowelBank.get(), combBank.get(), vocoder.get(), stereoWidth.get() })
        m->prepare (sr);

    freqShifter.prepare (sr);
    granulator->prepare (sr);
    chorusFx.prepare (sr);
    delayFx.prepare (sr);
    phaserFx.prepare (sr);
    reverbFx.prepare (sr);
    rvLow.setCoefs (Biquad::design (Biquad::Type::HighPass, sr, 150.0f, 0.5f, 0.0f));
    rvMid.setCoefs (Biquad::design (Biquad::Type::Peak, sr, 350.0f, 1.0f, 4.0f));
    rvHigh.setCoefs (Biquad::design (Biquad::Type::HighShelf, sr, 1500.0f, 0.5f, -4.0f));
    // The reference's output has a gentle presence lift: a saw's partials sit ~1.5 dB
    // above the ideal 1/n around 4-6 kHz (measured on plain saws, all patches)
    voicing.setCoefs (Biquad::design (Biquad::Type::Peak, sr, 5000.0f, 0.6f, 1.6f));
    reset();
}

void Engine::FxChain::reset()
{
    for (auto* m : { distortion.get(), phaser.get(), chorus.get(), reverb.get(), tapeWobble.get(),
                     vowelBank.get(), combBank.get(), vocoder.get(), stereoWidth.get(), granulator.get() })
        m->reset();

    freqShifter.reset();
    chorusFx.reset();
    delayFx.reset();
    phaserFx.reset();
    reverbFx.reset();
    rvLow.reset(); rvMid.reset(); rvHigh.reset();

    for (auto* q : { &eqLow, &eqMid, &eqHigh, &charA, &charB, &bankPeak, &distPre, &distPost, &voicing })
        q->reset();
    for (auto& s : bankStages)   s.reset();
    for (auto& s : bankStagesHp) s.reset();
    bankBpLp.reset();
    lastPeakHz = lastPeakReso = -1.0f;
    smoothedBankFreq = -1.0f;
    bankRingPhase = 0.0;
    delayIdle = reverbIdle = granIdle = 1 << 30;

    numVoices = 0;
    lastVoice = -1;
    lastVoiceAge = 0;
    silentSamples = 0;
    dcL = dcR = 0.0f;
    distHold[0] = distHold[1] = 0.0f; distHoldCount = 0;
    distDcX[0] = distDcX[1] = distDcY[0] = distDcY[1] = 0.0f;
    distFilter.reset();
    fadeLeft = 0;
    busL = busR = otherL = otherR = 0.0f;
    orphan = false;
    memo = {};   // a new sample rate means new coefficients
}

//==============================================================================
Engine::Engine()
{
    auto& factory = ModuleFactory::instance();

    osc1 = factory.createInstance ("osc.vibrio");
    osc2 = factory.createInstance ("osc.vibrio");
    osc3 = factory.createInstance ("osc.vibrio");
    subOsc = factory.createInstance ("osc.basic");
    noise = factory.createInstance ("osc.noise");

    filter1 = factory.createInstance ("filter.aquafilter");
    filter2 = factory.createInstance ("filter.aquafilter");

    // The Original engine's filter and amp envelopes run on the reference's measured
    // curves (linear attack, dB-linear decay/release, sloped sustain).
    envFilter = factory.createInstance ("util.measuredenv");
    envAmp = factory.createInstance ("util.measuredenv");
    env3 = factory.createInstance ("util.vibrioenv");
    env4 = factory.createInstance ("util.vibrioenv");
    lfo1 = factory.createInstance ("util.vibriolfo");
    lfo2 = factory.createInstance ("util.vibriolfo");
    lfo3 = factory.createInstance ("util.vibriolfo");
    envFollow = factory.createInstance ("util.envfollow");

    // A module type that failed to register is almost always a .cpp missing
    // from the build. Say so loudly - in every build configuration - instead
    // of crashing on a null pointer in the audio thread later.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(),
                     filter1.get(), filter2.get(), envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                     lfo1.get(), lfo2.get(), lfo3.get(), envFollow.get() })
    {
        if (m == nullptr)
        {
            juce::Logger::writeToLog ("FATAL: Aqua Vibrio could not create one of its modules - "
                                      "a module .cpp is probably missing from the build.");
            jassertfalse;
            std::abort();
        }
    }

    // The base patch's effects rack always exists; the rest come on demand.
    chains[0] = std::make_unique<FxChain>();
    chains[0]->createModules();
    chains[0]->ownerUid = 0;
    chains[0]->state.store (FxChain::InUse);
    numChains.store (1);

    lfoTyped = { static_cast<VibrioLfoModule*> (lfo1.get()),
                 static_cast<VibrioLfoModule*> (lfo2.get()),
                 static_cast<VibrioLfoModule*> (lfo3.get()) };
    ampEnvTyped = static_cast<MeasuredEnvModule*> (envAmp.get());
    for (auto* l : lfoTyped)
    {
        l->setReferenceShapes (true);
        l->setReferenceEnvMode (true);
    }

    const auto oscIdx = [] (SynthModule& m)
    {
        return OscIdx { m.paramIndex ("fmRatio"), m.paramIndex ("wave"), m.paramIndex ("shape"),
                        m.paramIndex ("pulseWidth"), m.paramIndex ("sync"), m.paramIndex ("syncRatio"),
                        m.paramIndex ("detune"), m.paramIndex ("spread"), m.paramIndex ("unison"),
                        m.paramIndex ("mode"), m.paramIndex ("formant"), m.paramIndex ("volume"),
                        m.paramIndex ("initPhase") };
    };
    o1 = oscIdx (*osc1); o2 = oscIdx (*osc2); o3 = oscIdx (*osc3);
    subRatio = subOsc->paramIndex ("fmRatio");
    subWave = subOsc->paramIndex ("waveform");

    f1 = { filter1->paramIndex ("cutoff"), filter1->paramIndex ("resonance"), filter1->paramIndex ("type") };
    f2 = { filter2->paramIndex ("cutoff"), filter2->paramIndex ("resonance"), filter2->paramIndex ("type") };

    eIdx = { envAmp->paramIndex ("attack"), envAmp->paramIndex ("decay"), envAmp->paramIndex ("sustain"),
             envAmp->paramIndex ("release"), envAmp->paramIndex ("sustainTime"), envAmp->paramIndex ("modulator") };
    lIdx = { lfo1->paramIndex ("rate"), lfo1->paramIndex ("symmetry"), lfo1->paramIndex ("shape"),
             lfo1->paramIndex ("mode"), lfo1->paramIndex ("phaseSpread"), lfo1->paramIndex ("envMode"),
             lfo1->paramIndex ("keytrigger") };

    auto& c0 = *chains[0];
    distDrive = c0.distortion->paramIndex ("drive");
    phaserWet = c0.phaser->paramIndex ("dryWet");
    chorusWet = c0.chorus->paramIndex ("dryWet");
    combFreq = c0.combBank->paramIndex ("freq");
    vowelPos = c0.vowelBank->paramIndex ("vowel");

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
        m->setParameter ("drift", 0.0f);          // The reference's oscillators hold their pitch: no drift
        m->setParameter ("panMode", 1.0f);        // unison voices alternate hard left / right, as measured
        m->setParameter ("syncEnvAmount", 0.0f);
        m->setParameter ("syncOut", 0.0f);
    }

    // Free-running oscillators start at unrelated phases - including relative
    // to each other, which is what keeps two same-pitch oscillators from
    // summing coherently (the reference measures +0.6 dB there, not +3).
    {
        juce::uint32 seed = 0x9e3779b9u;
        for (auto* m : { osc1.get(), osc2.get(), osc3.get() })
            static_cast<VibrioOscModule*> (m)->setPhaseSeed (seed += 0x7f4a7c15u);
    }

    // The Classic Shape crossfade at the reference's levels: a linear crossfade
    // in which the wave and the pulse are louder than the saw (measured).
    for (auto* m : { osc1.get(), osc2.get(), osc3.get() })
    {
        static_cast<VibrioOscModule*> (m)->setMorphGains (curves::kMorphWaveGain, curves::kMorphPulseGain);
        static_cast<VibrioOscModule*> (m)->setNarrowestPulse (0.0f);
        static_cast<VibrioOscModule*> (m)->setThroughZeroFm (true);
        static_cast<VibrioOscModule*> (m)->setBandLimitedSync (true);
        static_cast<VibrioOscModule*> (m)->setInstantGate (true);   // notes start at full level (measured)
    }
    static_cast<OscillatorModule*> (subOsc.get())->setInstantGate (true);

    // Oscillator 3 is always a single plain voice; Slave mode copies
    // oscillator 2's wave per voice, the fixed modes set it per layer.
    osc3->setParameterAt (o3.unison, 1.0f);
    osc3->setParameterAt (o3.detune, 0.0f);
    osc3->setParameterAt (o3.mode, 0.0f);
    osc3->setParameterAt (o3.sync, 0.0f);
    osc3->setParameterAt (o3.pw, 0.5f);
    osc3->setParameterAt (o3.formant, 1.0f);
    lfo3->setParameterAt (lIdx.symmetry, 0.0f);

    subOsc->setParameter ("volume", 1.0f);
    noise->setParameter ("type", 0.0f);           // white; Noise Colour is our own filter
    noise->setParameter ("level", 1.0f);

    filter1->setParameter ("drive", 0.0f);
    filter2->setParameter ("drive", 0.0f);
    filter1->setParameter ("resCurve", 0.0f);     // quadratic: resonance spread over the whole knob
    filter2->setParameter ("resCurve", 0.0f);
    filter1->setParameter ("modDepth", 0.0f);
    filter2->setParameter ("modDepth", 0.0f);

    controllers.fill (0.0f);
    controllers[7] = 1.0f;    // channel volume rests at full
    controllers[8] = 0.5f;    // balance rests centred
    controllers[11] = 1.0f;   // expression rests at full

    // Everything a layout can ever need, allocated now so the audio thread
    // never has to.
    const auto n = (size_t) layers::numParams();
    states.resize ((size_t) NoteLayerSnapshot::kMaxSlots);
    slotValues.assign ((size_t) NoteLayerSnapshot::kMaxSlots, std::vector<float> (n, 0.0f));
    live.assign (n, 0.0f);
    paramPtrs.assign (n, nullptr);

    int i = 0;
    for (const auto& p : allParameters())
    {
        bindings[std::string_view (p.id)] = i;
        live[(size_t) i] = (float) (p.scale == ValueScale::Enum ? p.defaultValue - p.minValue : p.defaultValue);
        ++i;
    }

    chains[0]->settings = &states[0];

    // The lookup tables the audio thread uses are built on first use; build
    // them now, so that never happens on the audio thread.
    for (auto scope : { layers::Scope::Voice, layers::Scope::Chain, layers::Scope::Global })
        layers::indicesWithScope (scope);
    layers::scopeOf (0);
    layers::indexOf ("ClockTempo");

    monoStack.reserve (128);
    buildMatrixTables();
    forgetAppliedValues();
}

Engine::~Engine() = default;

int Engine::loadSampledWaves (const juce::File& folder)
{
    if (! folder.isDirectory())
        return 0;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<std::vector<float>> cycles;
    int found = 0;

    // Wave 3 .. Wave 64 (1 and 2, sine and triangle, are generated)
    for (int w = 3; w <= 64; ++w)
    {
        std::vector<float> cycle;
        const auto file = folder.getChildFile ("Wave " + juce::String (w).paddedLeft ('0', 2) + ".wav");
        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
        {
            const int n = (int) std::min<juce::int64> (reader->lengthInSamples, 1 << 16);
            juce::AudioBuffer<float> buf ((int) reader->numChannels, n);
            if (n > 16 && reader->read (&buf, 0, n, 0, true, true))
            {
                // Played inverted: that is how the reference lines the files up
                // against its saw (measured on wave -> saw morphs, where the two
                // fundamentals cancel or add depending on it).
                cycle.resize ((size_t) n);
                for (int i = 0; i < n; ++i)
                    cycle[(size_t) i] = -buf.getSample (0, i);
                ++found;
            }
        }
        cycles.push_back (std::move (cycle));
    }

    if (found == 0)
        return 0;

    waveTables.push_back (std::make_unique<VibrioWaveTable> (cycles));
    classicTable.store (waveTables.back().get());
    numSampledWaves = found;
    markAllDirty();   // wave numbers 3..64 stop falling back to the sine
    return found;
}

int Engine::setTIWavetableFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return 0;

    int n = 0;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        const int number = f.getFileName().getIntValue();   // "14 Upsine Down.wav" -> 14
        if (number >= 1 && number < kNumTI && tiFiles[(size_t) number] == juce::File{})
        {
            tiFiles[(size_t) number] = f;
            ++n;
        }
    }
    numTIFiles = n;
    return n;
}

void Engine::serviceWavetables()
{
    for (auto& want : tiWanted)
    {
        const int id = want.load();
        if (id <= 0 || id >= kNumTI || tiTables[(size_t) id].load() != nullptr || tiFiles[(size_t) id] == juce::File{})
            continue;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (tiFiles[(size_t) id]) };
        if (reader == nullptr)
            continue;

        const int len = (int) std::min<juce::int64> (reader->lengthInSamples, 2048 * 62);
        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
        if (! reader->read (&buf, 0, len, 0, true, true))
            continue;

        std::vector<std::vector<float>> frames;
        for (int start = 0; start + 2048 <= len; start += 2048)
            frames.emplace_back (buf.getReadPointer (0) + start, buf.getReadPointer (0) + start + 2048);
        if (frames.empty())
            continue;

        tiOwned.push_back (std::make_unique<VibrioWaveTable> (frames, false));   // frames keep their relative levels
        tiFrameCount[(size_t) id].store ((int) frames.size());
        tiTables[(size_t) id].store (tiOwned.back().get());
    }
}

// Once per block: which table each oscillator reads. Wavetable models use
// their TI table once it is built (until then, the generated table); the
// classic model reads the sampled hardware waves, if loaded.
void Engine::chooseWaveTables()
{
    const auto& b = base().b;
    const int sel[2] = { b.osc1Mode >= 2 ? b.wtSel1 : 0, b.osc2Mode >= 2 ? b.wtSel2 : 0 };
    SynthModule* oscs[2] = { osc1.get(), osc2.get() };

    for (int n = 0; n < 2; ++n)
    {
        tiWanted[(size_t) n].store (sel[n], std::memory_order_relaxed);
        const auto* t = sel[n] > 0 ? tiTables[(size_t) sel[n]].load (std::memory_order_acquire) : nullptr;
        tiActive[(size_t) n] = t != nullptr ? sel[n] : -1;
        static_cast<VibrioOscModule*> (oscs[n])->setWaveTable (t != nullptr ? t : classicTable.load (std::memory_order_relaxed));
    }
    // oscillator 3 copies oscillator 2 in Slave mode
    static_cast<VibrioOscModule*> (osc3.get())->setWaveTable (b.osc3Mode == 1 && tiActive[1] >= 0
                                                                ? tiTables[(size_t) tiActive[1]].load (std::memory_order_relaxed)
                                                                : classicTable.load (std::memory_order_relaxed));
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
    int i = 0;
    for (const auto& p : allParameters())
        paramPtrs[(size_t) i++] = apvts.getRawParameterValue (p.id);

    // Matrix slots: slots 2 and 3 name their first destination "Destination1"
    // and "Amount1", the others just "Destination" and "Amount".
    const auto idx = [] (const juce::String& id) { return layers::indexOf (id); };

    for (int s = 0; s < 6; ++s)
    {
        const juce::String n ("Assign" + juce::String (s + 1));
        auto& si = slotIndices[(size_t) s];
        si.src = idx (n + "Source");
        si.dest[0] = idx (n + "Destination1") >= 0 ? idx (n + "Destination1") : idx (n + "Destination");
        si.amt[0]  = idx (n + "Amount1") >= 0 ? idx (n + "Amount1") : idx (n + "Amount");
        si.dest[1] = idx (n + "Destination2"); si.amt[1] = idx (n + "Amount2");
        si.dest[2] = idx (n + "Destination3"); si.amt[2] = idx (n + "Amount3");
    }

    for (int s = 0; s < 32; ++s)
    {
        const juce::String n ("Step" + juce::String (s + 1));
        stepIndices[(size_t) s] = { idx (n + "Length"), idx (n + "Velocity"), idx (n + "Bitfield") };
    }

    bound = true;
    markAllDirty();
}

void Engine::prepare (double newSampleRate, int)
{
    const juce::ScopedLock sl (chainLock);

    sampleRate = newSampleRate;

    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(),
                     filter1.get(), filter2.get(), envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                     lfo1.get(), lfo2.get(), lfo3.get(), envFollow.get() })
        m->prepare (newSampleRate);

    for (int c = 0; c < numChains.load(); ++c)
        chains[(size_t) c]->prepare (newSampleRate);

    sampleRateHz = newSampleRate;
    dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 12.57 / newSampleRate);   // measured output highpass
    {
        // the oscillator tone correction (measured; see the oscillator mix)
        const double k = newSampleRate / 44100.0;
        oscTone[0] = Biquad::design (Biquad::Type::Peak, newSampleRate, (float) (9518.0 * k), 0.867f, 1.42f);
        // a shelf this close to Nyquist: designed directly (design() keeps below 0.45 fs)
        const double A = std::pow (10.0, -8.4 / 40.0);
        const double w = juce::MathConstants<double>::twoPi * 20560.0 / 44100.0;
        const double cw = std::cos (w), al = std::sin (w) / (2.0 * 0.368), sq = 2.0 * std::sqrt (A) * al;
        const double B0 = A * ((A + 1) + (A - 1) * cw + sq), B1 = -2 * A * ((A - 1) + (A + 1) * cw),
                     B2 = A * ((A + 1) + (A - 1) * cw - sq), A0 = (A + 1) - (A - 1) * cw + sq,
                     A1 = 2 * ((A - 1) - (A + 1) * cw), A2 = (A + 1) - (A - 1) * cw - sq;
        oscTone[1] = { (float) (B0 / A0), (float) (B1 / A0), (float) (B2 / A0), (float) (A1 / A0), (float) (A2 / A0) };
    }

    // Work out every setting once now, on this thread, so every cached
    // parameter index is resolved before the audio thread runs.
    if (bound)
    {
        markAllDirty();
        forgetAppliedValues();
        beginBlock();
        applyVoiceModules (states[0]);
        appliedSlot = 0;
    }

    reset();
}

void Engine::reset()
{
    for (int c = 0; c < numChains.load(); ++c)
    {
        auto& ch = *chains[(size_t) c];
        ch.reset();
        ch.ownerUid = c == 0 ? 0 : -1;
        ch.settings = c == 0 ? &states[0] : nullptr;
        ch.state.store (c == 0 ? FxChain::InUse : FxChain::Clean);
    }

    envFollow->reset();

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

    arp = Arp {};
    arp.rng.setSeed (0xa4b);
    arpWasRunning = false;
    monoStack.clear();
    keyIsDown.fill (false);
    sustainPedal = false;
    holdLatched = false;
    lastVoice = -1;
    numOrdered = 0;
    orderDirty = true;
    for (int c = 0; c < 2; ++c)
        dcX[c] = dcY[c] = dcX2[c] = dcY2[c] = 0.0f;
}

void Engine::readMatrix (std::array<ModSlot, 6>& matrix) const
{
    for (int s = 0; s < 6; ++s)
    {
        const auto& si = slotIndices[(size_t) s];
        auto& slot = matrix[(size_t) s];

        const int srcByte = juce::roundToInt (valueAt (si.src, 0.0f));
        slot.source = srcByte >= 0 && srcByte < 128 ? srcForByte[(size_t) srcByte] : Src::Off;

        for (int d = 0; d < 3; ++d)
        {
            const int destByte = juce::roundToInt (valueAt (si.dest[d], 0.0f));
            slot.dest[(size_t) d] = destByte >= 0 && destByte < 128 ? destForByte[(size_t) destByte] : Dest::None;
            slot.amount[(size_t) d] = bipolar (valueAt (si.amt[d], 64.0f)) * kMatrixFullScale;
        }
    }
}

//==============================================================================
// Every knob of one layer into musical units. A pure function of the layer's
// raw values (curValues), the tempo and the sample rate: nothing here touches
// a module, so it can run for any number of layers.
//==============================================================================
void Engine::computeSettings (LayerState& L)
{
    auto& b = L.b;
    auto& vm = L.vm;
    auto& fx = L.fx;

    const auto r = [this] (const char* id, float fallback) { return raw (id, fallback); };
    const auto k = [&r] (const char* id, float fallback) { return unit (r (id, fallback)); };
    const auto bi = [&r] (const char* id, float fallback) { return bipolar (r (id, fallback)); };
    const auto i = [&r] (const char* id, int fallback) { return juce::roundToInt (r (id, (float) fallback)); };

    //== oscillators ==========================================================
    b.osc1Mode = i ("Osc1Mode", 0);
    b.osc2Mode = i ("Osc2Mode", 0);
    b.osc3Mode = i ("Osc3Mode", 0);

    b.osc1Semi = r ("Osc1Semitone", 64) - 64.0f;
    b.osc2Semi = r ("Osc2Semitone", 64) - 64.0f;
    b.osc3Semi = r ("Osc3Semitone", 64) - 64.0f;

    // Fine detune is a gentle upward offset: squared so the first half of the
    // knob is the slow-beating range, the end reaches a full semitone.
    // Original: the reference's (much finer) detune curve - 12.5 cents at 64.
    b.osc2DetuneCents = curves::detuneCents (r ("Osc2Detune", 32));
    b.osc3DetuneCents = curves::detuneCents (r ("Osc3Detune", 32));

    // Key follow: 96 is ordinary keyboard tracking.
    b.osc1Track = curves::oscKeyTrack (r ("Osc1Keyfollow", 96));
    b.osc2Track = curves::oscKeyTrack (r ("Osc2Keyfollow", 96));

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
    b.wtSel1 = juce::jlimit (0, kNumTI - 1, i ("Osc1WavetableWavetableselect", 0));
    b.wtSel2 = juce::jlimit (0, kNumTI - 1, i ("Osc2WavetableWavetableselect", 0));
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

    vm.subWave = i ("SuboscillatorShape", 0) == 0 ? 3.0f : 1.0f;

    // Stacks. HyperSaw always stacks (3..9 saws with Density); the Wavetable
    // models stack from Density above zero; Unison adds copies to any model.
    const int unisonCount = juce::jlimit (1, VibrioOscModule::kMaxUnison, i ("UnisonMode", 0) + 1);
    vm.unisonVoices = unisonCount;
    const float unisonCents = curves::unisonCents (r ("UnisonDetune", 48));

    const auto setupStack = [&] (int n, int mode, const char* densityId, const char* spreadId)
    {
        const float density = k (densityId, 0);
        int stack = 1;
        // Only HyperSaw stacks: in the Wavetable models byte 17 is the
        // wavetable index, not a density (the TI plays one wave per voice).
        if (mode == 1) stack = curves::hypersawCount (density * 127.0f);
        stack = juce::jlimit (1, VibrioOscModule::kMaxUnison, juce::jmax (stack, unisonCount));

        const float localCents = mode == 1 ? curves::hypersawSpreadCents (r (spreadId, 64), density * 127.0f) : 0.0f;
        vm.unison[n] = (float) stack;
        vm.detune[n] = juce::jmax (localCents, unisonCount > 1 ? unisonCents * 1.17f : 0.0f);
        // The reference keeps a HyperSaw on its own in mono; only Unison spreads
        // it. Pan Spread (measured on the stereo width of 2 and 4 voice stacks):
        // the reference is already nearly fully spread by the middle of the knob.
        vm.spread[n] = unisonCount > 1 ? [] (float x) { const float t = 1.0f - juce::jmin (1.0f, x / 0.55f); return 1.0f - t * t; } (k ("UnisonPanSpread", 127)) : 0.0f;
        vm.mode[n] = (float) (mode == 0 ? 0 : (mode == 1 ? 1 : 2));
    };

    // The reference keeps a unison stack at about 0.8/sqrt(n) of a single voice per
    // voice (the module already divides by sqrt(n)).
    b.unisonGain = unisonCount > 1 ? 0.8f * std::sqrt ((float) unisonCount) : 1.0f;   // module divides by sqrt(n)

    setupStack (0, b.osc1Mode, "Osc1HypersawDensity", "Osc1HypersawDetunespread");
    setupStack (1, b.osc2Mode, "Osc2HypersawDensity", "Osc2HypersawDetunespread");

    // Formant shift (Wavetable models): below the centre the spectrum slides
    // down, above it slides up; the pitch stays.
    // Original: no formant shift. The reference's wavetable oscillator has none, and
    // the byte reads 0 in most TI ROM patches - which, taken literally, would
    // drag every wavetable 1.5 octaves down in timbre.
    vm.formant[0] = 1.0f;
    vm.formant[1] = 1.0f;

    vm.initPhase = k ("OscInitPhase", 0);

    // Oscillator 3: Off, Slave (a copy of oscillator 2), Saw, Pulse, Sine,
    // Triangle, then Waves 3..64 of the table.
    vm.osc3Fixed = b.osc3Mode >= 2;
    switch (b.osc3Mode)
    {
        case 2:  vm.osc3Wave = 0; vm.osc3Shape = 0.5f; break;  // saw
        case 3:  vm.osc3Wave = 0; vm.osc3Shape = 1.0f; break;  // pulse
        case 4:  vm.osc3Wave = 0; vm.osc3Shape = 0.0f; break;  // sine
        case 5:  vm.osc3Wave = 1; vm.osc3Shape = 0.0f; break;  // triangle
        default:
            vm.osc3Wave = (float) juce::jlimit (0, 63, b.osc3Mode - 4);
            vm.osc3Shape = 0.0f;
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
    {
        const float d = r ("OffsetForFilterlink", 64) - 64.0f;
        b.linkOffsetBytes = d * std::abs (d) / 32.0f;
    }
    b.routing = juce::jlimit (0, 3, i ("FilterRouting", 0));
    b.filterBalance = k ("FilterBalance", 64);
    b.satCurve = juce::jlimit (0, 14, i ("SaturationCurve", 0));

    // Original: Osc Volume above the centre is the saturation drive, and the
    // stage is level matched.
    b.oscVolByte = r ("OscMainvolume", 64);
    b.satDrive = curves::satDrive (b.satCurve, b.oscVolByte);
    b.satMakeup = b.satCurve >= 1 && b.satCurve <= 6 ? curves::satMakeup (b.satCurve, b.satDrive) * curves::satTrim (b.satCurve, b.oscVolByte)
                : (b.satCurve == 7 ? 0.81f : 1.0f);   // Rectifier sits 1.8 dB lower in the reference

    // Filter Select "Both" is what the reference's importer turns into Filter Link:
    // filter 2 then takes filter 1's resonance, env amount and key follow.
    b.filterLink = i ("FilterSelect", 2) == 2;
    b.mode1 = juce::jlimit (0, 7, i ("Filter1Mode", 0));
    b.mode2 = juce::jlimit (0, 3, i ("Filter2Mode", 0));
    if (b.filterLink)
    {
        b.res2 = b.res1; b.env2 = b.env1; b.key2 = b.key1;
        b.env2Vel = b.env1Vel; b.res2Vel = b.res1Vel;
    }

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
    vm.filterType[0] = (float) b.type1;
    vm.filterType[1] = (float) b.type2;

    //== envelopes ============================================================
    b.fA = r ("FilterEnvAttack", 0);   b.fD = r ("FilterEnvDecay", 46);
    b.fS = r ("FilterEnvSustain", 0);  b.fSlope = r ("FilterEnvSustainTime", 64);
    b.fR = r ("FilterEnvRelease", 40);
    b.aA = r ("AmpEnvAttack", 0);      b.aD = r ("AmpEnvDecay", 127);
    b.aS = r ("AmpEnvSustain", 127);   b.aSlope = r ("AmpEnvSustainTime", 64);
    b.aR = r ("AmpEnvRelease", 4);

    b.ampMs[0] = scaling::envMs (b.aA); b.ampMs[1] = scaling::envMs (b.aD); b.ampMs[2] = scaling::envMs (b.aR);
    b.filtMs[0] = scaling::envMs (b.fA); b.filtMs[1] = scaling::envMs (b.fD); b.filtMs[2] = scaling::envMs (b.fR);

    // attack, decay, sustain, slope, release
    const auto setEnv = [&] (float* e, const char* a, const char* d, const char* s, const char* slope, const char* rel)
    {
        e[0] = scaling::envMs (r (a, 20));
        e[1] = scaling::envMs (r (d, 70));
        e[2] = k (s, 64);
        e[3] = bi (slope, 64);
        e[4] = scaling::envMs (r (rel, 70));
    };
    setEnv (vm.env3, "Envelope3Attack", "Envelope3Decay", "Envelope3Sustain", "Envelope3SustainTime", "Envelope3Release");
    setEnv (vm.env4, "Envelope4Attack", "Envelope4Decay", "Envelope4Sustain", "Envelope4SustainTime", "Envelope4Release");

    //== LFOs =================================================================
    const char* rateIds[] = { "Lfo1Rate", "Lfo2Rate", "Lfo3Rate" };
    const char* clockIds[] = { "Lfo1Clock", "Lfo2Clock", "Lfo3Clock" };
    const char* keyIds[] = { "Lfo1Keyfollow", "Lfo2Keyfollow", "Lfo3Keyfollow" };
    const char* shapeIds[] = { "Lfo1Shape", "Lfo2Shape", "Lfo3Shape" };
    const char* modeIds[] = { "Lfo1Mode", "Lfo2Mode", "Lfo3Mode" };

    for (int n = 0; n < 3; ++n)
    {
        b.lfoRateByte[n] = r (rateIds[n], 48);

        // Clocked: the rate is one cycle per note value at the current tempo.
        const float wholeNotes = listValue (kLfoClock, i (clockIds[n], 0));
        b.lfoClockHz[n] = wholeNotes > 0.0f ? (float) (tempoBpm / 60.0 / (wholeNotes * 4.0)) : 0.0f;

        // Key follow: at full, the rate doubles every octave up the keyboard.
        b.lfoKeyFollow[n] = k (keyIds[n], 0);

        vm.lfoShape[n] = (float) juce::jlimit (0, 67, i (shapeIds[n], 1));
        vm.lfoMode[n] = i (modeIds[n], 0) > 0 ? 1.0f : 0.0f;
        b.lfoHz[n] = b.lfoClockHz[n] > 0.0f ? b.lfoClockHz[n] : curves::lfoRateHz (b.lfoRateByte[n]);
    }
    vm.lfoPhaseSpread = k ("UnisonLfoPhase", 64);

    b.lfoContour[0] = bi ("Lfo1Symmetry", 64);
    b.lfoContour[1] = bi ("Lfo2Symmetry", 64);

    vm.lfoEnvMode[0] = i ("Lfo1EnvMode", 0) > 0 ? 1.0f : 0.0f;
    vm.lfoEnvMode[1] = i ("Lfo2EnvMode", 0) > 0 ? 1.0f : 0.0f;
    vm.lfoEnvMode[2] = 0.0f;

    // Keytrigger 0 is free running; above that, every note restarts the LFO
    // at that point of its cycle.
    const int kt1 = i ("Lfo1Keytrigger", 0), kt2 = i ("Lfo2Keytrigger", 0);
    // (measured: phase (Keytrigger - 1) / 127, so 64 lands just short of half way)
    vm.lfoKeytrigger[0] = kt1 == 0 ? -1.0f : (float) (kt1 - 1) / 127.0f;
    vm.lfoKeytrigger[1] = kt2 == 0 ? -1.0f : (float) (kt2 - 1) / 127.0f;
    vm.lfoKeytrigger[2] = 0.0f;   // LFO 3 is a per-note vibrato: always from the top

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
    // Patch Volume on the reference's (steep) curve; kOutputTrim lines the whole
    // engine up with the reference's absolute level.
    b.patchVolume = curves::patchVolumeGain (r ("PatchVolume", 100)) * kOutputTrim;
    b.patchVolumeByte = r ("PatchVolume", 100);
    {
        // (127 is hard right: the upper half spans 63 bytes)
        const float pb = r ("Panorama", 64);
        b.pan = pb < 64.0f ? (pb - 64.0f) / 64.0f : (pb - 64.0f) / 63.0f;
    }
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
    const float glideMs = scaling::portamentoMs (porta) * 2.5f;   // The reference glides about 2.5x slower
    b.glideCoef = (float) std::exp (-4.6 / (glideMs * 0.001 * sampleRate));

    //== arpeggiator ==========================================================
    b.arpMode = juce::jlimit (0, 7, i ("ArpMode", 0));
    b.arpPattern = juce::jlimit (0, 63, i ("ArpPatternSelct", 1));
    b.arpOctaves = juce::jlimit (1, 4, i ("ArpOctaveRange", 0) + 1);
    b.arpStepBeats = listValue (kArpClock, i ("ArpClock", 4)) * 4.0f;
    b.arpGate = scaling::arpGate (r ("ArpNoteLength", 64));
    b.arpSwing = 0.5f * r ("ArpSwing", 0) / 127.0f;   // measured: the off-beat moves up to half a step
    b.arpHold = i ("ArpHoldEnable", 0) > 0;

    //== effects ==============================================================
    b.inputMode = i ("InputMode", 0);
    b.inputSelect = i ("InputSelect", 1);
    b.inputRing = k ("InputRingmodulator", 0);
    b.followerMode = i ("InputFollowerMode", 0);

    // Distortion
    b.distCurve = juce::jlimit (0, 25, i ("DistortionCurve", 0));
    b.distDrive = k ("DistortionIntensity", 0);
    b.distMix = k ("PatchDistortionMix", 127);
    fx.distTone = k ("PatchDistortionTone127", 64);   // the filter-type curves use it directly
    {
        // Treble Booster: a high shelf INTO the distortion, so the top end
        // gets driven harder (the classic pedal trick). Tone: a tilt AFTER
        // it - below the centre darker, above brighter.
        using T = Biquad::Type;
        fx.distPre = Biquad::design (T::HighShelf, sampleRate, 2500.0f, 0.7f, 15.0f * k ("PatchDistortionTrebleBooster", 0));
        const float tone = bi ("PatchDistortionTone127", 64);
        fx.distPost = Biquad::design (T::HighShelf, sampleRate, 1200.0f, 0.6f, 12.0f * tone);
        fx.distToneOn = std::abs (tone) > 0.01f;
    }
    fx.distHighCut = k ("PatchDistortionHighCut", 127);

    // Filter bank: one Frequency knob serves every type.
    b.bankType = juce::jlimit (0, 11, i ("FilterBankType", 0));
    b.bankFreq = k ("FilterBankFrequency", 64);
    b.bankReso = k ("FilterBankResonance", 64);
    b.bankMix = k ("FilterBankMix", 127);
    b.bankPoles = k ("FilterBankPoles", 0);
    b.bankSlope = k ("FilterBankSlope", 0);
    b.bankStereo = bi ("FilterBankStereoPhase", 64);
    fx.vowelReso = 40.0f + 58.0f * b.bankReso;
    fx.vowelSize = b.bankStereo * 6.0f;
    fx.combFeedback = b.bankReso * 97.0f;

    // Vocoder
    b.vocoderMode = i ("VocoderMode", 0);
    b.vocoderBalance = k ("VocoderBalance", 64);
    const int bands = i ("VocoderBands", 31);
    fx.vocBands = bands < 11 ? 0.0f : (bands < 22 ? 1.0f : 2.0f);
    fx.vocAttack = 0.1f + 99.0f * k ("VocoderAttack", 0) * k ("VocoderAttack", 0);
    fx.vocRelease = 1.0f + 499.0f * k ("VocoderRelease", 46) * k ("VocoderRelease", 46);
    fx.vocBright = k ("VocoderSpectralBalance", 64) * 100.0f;
    const float carrierHz = scaling::vocoderCentreHz (r ("VocoderCarrierCenterFrequency", 64));
    const float modHz = i ("VocoderLink", 1) > 0
                          ? carrierHz * std::pow (2.0f, bi ("VocoderModulatorFrequencyOffset", 64) * 2.0f)
                          : scaling::vocoderCentreHz (r ("VocoderModulatorCenterFrequency", 64));
    fx.vocCarrierHz = carrierHz;
    fx.vocModHz = juce::jlimit (100.0f, 5000.0f, modHz);
    fx.vocCarrierSpread = 55.0f + 45.0f * k ("CarrierFrequencySpread", 0);   // even the minimum spans a voice
    fx.vocModSpread = 55.0f + 45.0f * k ("VocoderModulatorFrequencySpread", 127);
    fx.vocCarrierQ = 0.2f + 5.8f * k ("VocoderCarrierQFactor", 64);
    fx.vocModQ = 0.2f + 5.8f * k ("VocoderModulatorQFactor", 64);

    // Character: type, intensity and tune (the old Analog Boost pair).
    b.charType = juce::jlimit (0, 8, i ("CharacterType", 0));
    b.charAmount = k ("BassIntensity", 0);
    {
        const float tune = k ("BassTune", 32);
        const float a = b.charAmount;
        using T = Biquad::Type;
        const auto d = [this] (T t, float f, float q, float g) { return Biquad::design (t, sampleRate, f, q, g); };
        switch (b.charType)
        {
            case 0:  // Analog Boost, as measured: low shelf + (at high Tune) a high-shelf cut
            {
                const auto ab = curves::analogBoost (r ("BassIntensity", 0), r ("BassTune", 32));
                fx.charA = d (T::LowShelf, ab.lowHz, 0.5f, ab.lowDb);
                fx.charB = d (T::HighShelf, 5000.0f, 0.5f, ab.highDb); break;
            }
            case 1: case 2: case 3:  // Vintage: saturation, darkened progressively
                fx.charA = d (T::LowPass, (18000.0f / (float) b.charType) * (0.5f + tune), 0.6f, 0.0f);
                fx.charB = d (T::LowShelf, 120.0f, 0.7f, a * 3.0f); break;
            case 4:  // Pad Opener: air on top
                fx.charA = d (T::HighShelf, 2000.0f * std::pow (5.0f, tune), 0.7f, a * 9.0f);
                fx.charB = d (T::Peak, 300.0f, 0.8f, -a * 3.0f); break;
            case 5:  // Lead Enhancer: presence
                fx.charA = d (T::Peak, 600.0f * std::pow (6.0f, tune), 1.1f, a * 8.0f);
                fx.charB = d (T::LowShelf, 150.0f, 0.7f, -a * 3.0f); break;
            case 6:  // Bass Enhancer: weight plus a little harmonic drive
                fx.charA = d (T::LowShelf, 50.0f * std::pow (4.0f, tune), 0.8f, a * 9.0f);
                fx.charB = d (T::Peak, 800.0f, 1.0f, a * 2.0f); break;
            case 7:  // Stereo Widener: flat EQ, the width does the work
                fx.charA = d (T::Peak, 1000.0f, 0.7f, 0.0f);
                fx.charB = d (T::Peak, 1000.0f, 0.7f, 0.0f); break;
            default: // Speaker Cabinet: band-limited, boxy
                fx.charA = d (T::LowPass, 2000.0f + 5000.0f * tune * (1.0f - 0.5f * a), 0.9f, 0.0f);
                fx.charB = d (T::HighPass, 70.0f + 150.0f * a, 0.8f, 0.0f); break;
        }
        fx.widthWidth = b.charType == 7 ? 100.0f + 100.0f * a : 100.0f;
        fx.widthBassMono = b.charType == 7 ? 60.0f + 200.0f * tune : 0.0f;
    }

    // EQ: low shelf, mid bell, high shelf.
    {
        const float lg = scaling::eqGainDb (r ("LoweqGain", 64)), mg = scaling::eqGainDb (r ("MideqGain", 64)),
                    hg = scaling::eqGainDb (r ("HigheqGain", 64));
        b.eqOn = std::abs (lg) > 0.01f || std::abs (mg) > 0.01f || std::abs (hg) > 0.01f;
        using T = Biquad::Type;
        const float midHz = curves::eqMidHz (r ("MideqFrequency", 84));
        const float highHz = curves::eqHighHz (r ("HigheqFrequency", 0));
        fx.eqLow = Biquad::design (T::LowShelf, sampleRate, curves::eqLowHz (r ("LoweqFrequency", 5)), curves::eqShelfQ(), lg);
        fx.eqMid = Biquad::design (T::Peak, sampleRate, midHz, curves::eqQ (r ("MideqQFactor", 32)),
                                   midHz < sampleRate * 0.45 ? mg : 0.0f);
        fx.eqHigh = Biquad::design (T::HighShelf, sampleRate, highHz, curves::eqShelfQ(),
                                    highHz < sampleRate * 0.45 ? hg : 0.0f);
    }

    // Phaser (measured; see MeasuredPhaser): Mode 0..5 = 1..6 sections.
    b.phaserMix = k ("PhaserMix", 0);
    fx.ph.sections = juce::jlimit (1, 6, i ("PhaserMode", 3) + 1);
    fx.ph.centreHz = scaling::phaserCentreHz (r ("PhaserFrequency", 64));
    fx.ph.spreadOct = 1.68f * k ("PhaserSpread", 127);
    fx.ph.q = 2.9f * std::pow (0.78f / 2.9f, k ("PhaserSpread", 127));
    {
        const float fb = r ("PhaserFeedback", 64);
        fx.ph.feedback = juce::jlimit (-0.995f, 0.995f, fb < 64.0f ? 0.88f * (fb - 64.0f) / 64.0f : 0.93f * (fb - 64.0f) / 63.0f);
    }
    fx.ph.rateHz = 0.0204f * std::exp (0.0533f * r ("PhaserRate", 36));
    fx.ph.depthOct = 3.76f * k ("PhaserDepth", 112);   // (traced: the resonance moves ~0.6 of the sections' sweep)
    fx.phStages = (float) juce::jlimit (0, 5, i ("PhaserMode", 3));
    fx.phRate = scaling::phaserRateHz (r ("PhaserRate", 36));
    fx.phDepth = k ("PhaserDepth", 112) * 100.0f;
    fx.phCentre = scaling::phaserCentreHz (r ("PhaserFrequency", 64));
    fx.phFeedback = k ("PhaserFeedback", 64) * 95.0f;
    fx.phSpread = k ("PhaserSpread", 127) * 100.0f;

    // Chorus: Type picks the flavour; Mix, Rate, Depth, Delay and LFO Shape
    // mean the same thing for all of them.
    b.chorusType = juce::jlimit (0, 6, i ("ChorusType", 1));
    // A hardware A/B/C patch has no Chorus Type byte (it reads 0): its chorus is
    // on whenever Chorus Mix is up, the classic chorus - which is how the reference
    // imports them.
    if (b.chorusType == 0)
        b.chorusType = 1;
    b.chorusMix = k ("ChorusMix", 0);
    {
        const int t = b.chorusType;
        const float rate = scaling::chorusRateHz (r ("ChorusRate", 64));
        fx.chRate = t == 6 ? juce::jmax (0.7f, rate * 2.0f) : rate;
        fx.chDepth = k ("ChorusDepth", 16) * 100.0f;
        fx.chDelay = scaling::chorusDelayMs (r ("ChorusDelay", 64));
        fx.chSpread = t == 5 ? 0.0f : 100.0f;
        fx.chVoices = t == 3 ? 4.0f : (t == 4 ? 3.0f : (t == 5 ? 1.0f : 2.0f));
        fx.chLfoShape = (float) juce::jlimit (0, 5, i ("ChorusLfoShape", 1));
        fx.chFeedback = bi ("ChorusFeedback", 64) * 0.97f;
        fx.chDelay = 0.1f + 19.9f * k ("ChorusDelay", 64);   // Original: the measured chorus reaches down to no delay
    }

    // Delay: a send. Clocked, the time is a note value at the current tempo.
    // The mode decides each side's first echo and the loop period (see
    // PatternDelay, all measured).
    b.delayMode = i ("DelayMode", 1);
    b.delaySend = k ("DelaySend", 0);
    {
        float ms = curves::delayTimeMs (r ("DelayTime", 64));
        const float clock = listValue (kDelayClock, i ("DelayClock", 0));
        if (clock > 0.0f)
            ms = (float) (60000.0 / tempoBpm * clock * 4.0);
        ms = juce::jlimit (1.0f, 2400.0f, ms);

        auto& d = fx.dl;
        d.firstMs[0] = d.firstMs[1] = d.periodMs = ms;
        switch (b.delayMode)
        {
            case 2: d.firstMs[1] = ms * 0.5f;   break;   // Ping Pong 2:1
            case 3: d.firstMs[1] = ms * 0.75f;  break;   // 4:3
            case 4: d.firstMs[1] = ms * 0.25f;  break;   // 4:1
            case 5: d.firstMs[1] = ms * 0.875f; break;   // 8:7
            default:
                if (b.delayMode >= 6)
                {
                    static const int patterns[][2] = { {1,1},{2,1},{3,1},{4,1},{5,1},{2,3},{2,5},{3,2},{3,3},
                                                       {3,4},{3,5},{4,3},{4,5},{5,2},{5,3},{5,4},{5,5} };
                    const auto& pt = patterns[juce::jlimit (0, 16, b.delayMode - 6)];
                    const float unit = ms * 0.5f;
                    d.firstMs[0] = unit * (float) pt[0];
                    d.firstMs[1] = unit * (float) pt[1];
                    d.periodMs = unit * (float) pt[0];
                }
                break;
        }
        b.delayMs = juce::jmax (d.firstMs[0], d.firstMs[1]);
        d.feedback = 0.88f * k ("DelayFeedback", 0);
        // Modulation (measured through a held sine's phase): the rate doubles
        // every 11.65 bytes, 0.38 Hz at 64; +-17 ms at depth 64; the two
        // sides move in opposite directions.
        d.modHz = 0.38f * std::exp (0.0595f * (r ("DlyRateRevDecay", 16) - 64.0f));
        d.modMs = 34.0f * k ("DlyDepth", 12);
        const float hz = curves::delayColourHz (r ("DelayColor", 64), d.colourMode);
        d.colourCoef = d.colourMode == 0 ? 0.0f : PatternDelay::colourCoefFor (d.colourMode, hz, sampleRate);

        const int type = i ("DelayType", 0);    // Classic, Tape Clocked, Tape Free, Tape Doppler
        const float wow = k ("DelayTapeDelayModulation", 12);
        fx.tapeWow = type == 0 ? 0.0f : (type == 3 ? 40.0f : 10.0f) + 60.0f * wow;
        fx.tapeFlutter = type == 0 ? 0.0f : 10.0f + 30.0f * wow;
        fx.tapeDrive = type == 0 ? 0.0f : 25.0f;
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
        fx.rvRoom = rooms[type] * (0.4f + 0.6f * t);
        fx.rvDecay = 0.3f + 19.7f * t * std::sqrt (t);
        fx.rvDamping = k ("ReverbDamping", 10) * 100.0f;
        fx.rvPre = scaling::reverbPredelayMs (r ("ReverbPredelay", 20));
        fx.rvColour = bi ("ReverbColor", 64);
        fx.rvFeedback = mode >= 2 ? k ("ReverbFeedback", 64) * 90.0f : 0.0f;
        // Original: the measured reverb (type, damping and the feedback modes make no difference there)
        fx.rvRt60 = curves::reverbRt60 (r ("ReverbTime", 64));
        fx.rvLevel = juce::Decibels::decibelsToGain (curves::reverbLevelDb (r ("ReverbTime", 64)));
        fx.rvPreMs = curves::reverbPredelayMs (r ("ReverbPredelay", 20));
        {
            const float col = r ("ReverbColor", 64);
            fx.rvColourMode = std::abs (col - 64.0f) < 0.5f ? 0 : (col < 64.0f ? -1 : 1);
            fx.rvColourG = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * curves::reverbColourHz (col) / (float) sampleRate);
        }
    }

    b.secondOut = juce::jmax (0.0f, bi ("SecondOutputBalance", 0));

    // Granulator. Its own dry/wet stays at 100 %: the engine blends it in, so
    // the cloud can fade away cleanly when the mix is closed.
    b.granMix = k ("Atomizer", 0);
    fx.grGrains = 1.0f + 23.0f * k ("GranulatorGrains", 40);
    fx.grSize = 5.0f * std::pow (100.0f, k ("GranulatorSize", 48));      // 5 .. 500 ms
    fx.grPos = k ("GranulatorPosition", 0) * 100.0f;
    fx.grSpray = k ("GranulatorSpray", 12) * 100.0f;
    fx.grFreeze = i ("GranulatorFreeze", 0) > 0 ? 1.0f : 0.0f;
    fx.grPitch = bi ("GranulatorPitch", 64) * 24.0f;
    fx.grPitchDisp = k ("GranulatorPitchDisp", 0) * 100.0f;
    fx.grStereo = k ("GranulatorStereo", 38) * 100.0f;

    readMatrix (L.matrix);
}

//==============================================================================
// Once per block, from the base patch only: the things that decide which
// notes play, or exist once for the whole instrument.
//==============================================================================
void Engine::updateGlobals()
{
    curValues = slotValues[0].data();
    const Block& b = base().b;

    const auto r = [this] (const char* id, float fallback) { return raw (id, fallback); };
    const auto k = [&r] (const char* id, float fallback) { return unit (r (id, fallback)); };
    const auto i = [&r] (const char* id, int fallback) { return juce::roundToInt (r (id, (float) fallback)); };

    SETP (*envFollow, "attack", 0.1f + 150.0f * k ("InputFollowerAttack", 64) * k ("InputFollowerAttack", 64));
    SETP (*envFollow, "release", 1.0f + 1500.0f * k ("InputFollowerRelease", 64) * k ("InputFollowerRelease", 64));
    SETP (*envFollow, "gain", 1.0f + 7.0f * k ("InputFollowerLevel", 0));

    userPatternLength = juce::jlimit (1, 32, i ("ArpeggiatorUserpatternlength", 15) + 1);
    for (int s = 0; s < 32; ++s)
    {
        const auto& si = stepIndices[(size_t) s];
        stepOn[(size_t) s] = valueAt (si.on, 1.0f) > 0.5f;
        stepVelocity[(size_t) s] = unit (valueAt (si.velocity, 100.0f));
        stepLength[(size_t) s] = unit (valueAt (si.length, 64.0f));
    }

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
// Note layers: which values each slot plays, computed only when needed.
//==============================================================================
void Engine::markAllDirty()
{
    for (auto& s : states)
        s.computed = false;
    appliedSlot = -1;
}

void Engine::forgetAppliedValues()
{
    appliedValues.fill (std::numeric_limits<float>::quiet_NaN());
    appliedSlot = -1;
}

void Engine::ensureComputed (int slot)
{
    const int numSlots = (int) layout->slots.size();
    slot = juce::jlimit (0, numSlots - 1, slot);

    auto& L = states[(size_t) slot];
    if (L.computed)
        return;

    if (slot != 0)
        ensureComputed (0);

    const int n = (int) live.size();
    auto& vals = slotValues[(size_t) slot];
    const float* bv = baseValues();
    const auto& desc = layout->slots[(size_t) slot];

    if (slot == 0)
    {
        std::copy (bv, bv + n, vals.begin());
    }
    else if (desc.live)
    {
        // The selection being edited plays the knobs as they are now - apart
        // from the global ones, which always belong to the base patch.
        std::copy (live.begin(), live.end(), vals.begin());
        for (int idx : layers::indicesWithScope (layers::Scope::Global))
            vals[(size_t) idx] = bv[idx];
    }
    else
    {
        std::copy (bv, bv + n, vals.begin());
        for (const auto& [idx, value] : desc.overrides)
            if (idx >= 0 && idx < n && layers::scopeOf (idx) != layers::Scope::Global)
                vals[(size_t) idx] = value;
    }

    L.uid = desc.uid;
    curValues = vals.data();
    computeSettings (L);

    if (slot == 0)
    {
        L.fxSameAsBase = true;
    }
    else
    {
        // A layer that left the effects alone shares the base patch's rack:
        // it then sounds exactly as it did before it had a layer at all.
        const auto& baseVals = slotValues[0];
        bool same = true;
        for (int idx : layers::indicesWithScope (layers::Scope::Chain))
            if (vals[(size_t) idx] != baseVals[(size_t) idx]) { same = false; break; }
        L.fxSameAsBase = same;

        // A Mono LFO has one phase for the whole instrument, so it runs at
        // the base patch's rate whatever the layer says.
        const auto& B = states[0].b;
        for (int lfo = 0; lfo < 3; ++lfo)
            if (L.vm.lfoMode[lfo] > 0.5f)
            {
                L.b.lfoRateByte[lfo] = B.lfoRateByte[lfo];
                L.b.lfoClockHz[lfo] = B.lfoClockHz[lfo];
                L.b.lfoHz[lfo] = B.lfoHz[lfo];
                L.b.lfoKeyFollow[lfo] = B.lfoKeyFollow[lfo];
            }
    }

    L.computed = true;
    if (slot == appliedSlot)
        appliedSlot = -1;
}

void Engine::beginBlock()
{
    // 1. the newest note layout
    bool layoutChanged = false;
    if (const auto* latest = pendingLayers.load (std::memory_order_acquire);
        latest != nullptr && latest != layout && ! latest->slots.empty()
        && (int) latest->slots.size() <= NoteLayerSnapshot::kMaxSlots)
    {
        layout = latest;
        layersInUse.store (latest->serial, std::memory_order_release);
        layoutChanged = true;
    }

    // 2. the knobs
    bool liveChanged = false;
    for (size_t i = 0; i < live.size(); ++i)
    {
        if (paramPtrs[i] == nullptr)
            continue;
        const float v = paramPtrs[i]->load (std::memory_order_relaxed);
        if (v != live[i])
        {
            live[i] = v;
            liveChanged = true;
        }
    }

    // 3. tempo: the host's when it has one, otherwise the patch's own clock
    static const int clockIdx = layers::indexOf ("ClockTempo");
    const double newTempo = hostTempoValid ? hostTempo
                                           : (double) scaling::tempoBpm (clockIdx >= 0 ? baseValues()[clockIdx] : 57.0f);
    const bool tempoChanged = newTempo != tempoBpm;
    tempoBpm = newTempo;

    if (layoutChanged || tempoChanged)
    {
        markAllDirty();
    }
    else if (liveChanged)
    {
        if (layout->sessionOpen)
        {
            // Only the selection being edited plays the live knobs.
            for (size_t s = 1; s < layout->slots.size(); ++s)
                if (layout->slots[s].live)
                    states[s].computed = false;
            if (appliedSlot > 0 && ! states[(size_t) appliedSlot].computed)
                appliedSlot = -1;
        }
        else
        {
            markAllDirty();
        }
    }

    ensureComputed (0);
    updateGlobals();

    // The shared (Mono) LFO phase advances at the base patch's rate until a
    // voice says otherwise.
    for (int n = 0; n < 3; ++n)
        lfoTyped[(size_t) n]->setParameterAt (lIdx.rate, states[0].b.lfoHz[n]);

    // 4. every sounding voice: its layer, and the effects it feeds
    for (int v = 0; v < kPolyVoices; ++v)
    {
        auto& voice = voices[(size_t) v];
        if (! voice.active)
            continue;

        const int slot = slotOf (voice.note);
        ensureComputed (slot);
        voice.slot = juce::jlimit (0, (int) layout->slots.size() - 1, slot);

        const int c = chainForSlot (voice.slot);
        if (c != voice.chain)
        {
            voice.chain = c;
            auto& ch = *chains[(size_t) c];
            if (voice.age > ch.lastVoiceAge)
            {
                ch.lastVoice = v;
                ch.lastVoiceAge = voice.age;
            }
        }
    }
    orderDirty = true;

    // 5. the effect chains
    bindChains();
}

//==============================================================================
void Engine::applyVoiceModules (const LayerState& L)
{
    const auto& vm = L.vm;

    // Only what differs from the layer applied last is written: two layers
    // that share most of their settings switch with a handful of stores.
    int slot = 0;
    const auto put = [this, &slot] (SynthModule& m, int index, float value)
    {
        auto& last = appliedValues[(size_t) slot++];
        if (! (last == value))
        {
            m.setParameterAt (index, value);
            last = value;
        }
    };

    SynthModule* oscs[] = { osc1.get(), osc2.get() };
    const OscIdx* ix[] = { &o1, &o2 };
    for (int n = 0; n < 2; ++n)
    {
        put (*oscs[n], ix[n]->unison, vm.unison[n]);
        put (*oscs[n], ix[n]->detune, vm.detune[n]);
        put (*oscs[n], ix[n]->spread, vm.spread[n]);
        put (*oscs[n], ix[n]->mode, vm.mode[n]);
        put (*oscs[n], ix[n]->formant, vm.formant[n]);
        // Init Phase (measured): any setting restarts oscillator 1 at the
        // start of its cycle; only oscillator 2 takes the phase it names.
        put (*oscs[n], ix[n]->initPhase, n == 0 && vm.initPhase > 0.0f ? 1.0e-6f : vm.initPhase);
    }
    put (*osc3, o3.initPhase, vm.initPhase);

    // Oscillator 3's wave is also written per voice in Slave mode, so the
    // fixed modes always write it rather than trusting the cache.
    if (vm.osc3Fixed)
    {
        osc3->setParameterAt (o3.wave, vm.osc3Wave);
        osc3->setParameterAt (o3.shape, vm.osc3Shape);
        osc3->setParameterAt (o3.pw, 0.5f);
    }

    put (*subOsc, subWave, vm.subWave);
    put (*filter1, f1.type, vm.filterType[0]);
    put (*filter2, f2.type, vm.filterType[1]);

    for (auto* env : { env3.get(), env4.get() })
    {
        const float* e = env == env3.get() ? vm.env3 : vm.env4;
        put (*env, eIdx.attack, e[0]);
        put (*env, eIdx.decay, e[1]);
        put (*env, eIdx.sustain, e[2]);
        put (*env, eIdx.slope, e[3]);
        put (*env, eIdx.release, e[4]);
    }

    for (int n = 0; n < 3; ++n)
    {
        auto& m = *lfoTyped[(size_t) n];
        put (m, lIdx.shape, vm.lfoShape[n]);
        put (m, lIdx.mode, vm.lfoMode[n]);
        put (m, lIdx.phaseSpread, vm.lfoPhaseSpread);
        put (m, lIdx.envMode, vm.lfoEnvMode[n]);
        put (m, lIdx.keytrigger, vm.lfoKeytrigger[n]);
    }

    jassert (slot <= (int) appliedValues.size());
}

void Engine::applyFxModules (FxChain& c)
{
    if (c.settings == nullptr)
        return;

    const auto& b = c.settings->b;
    const auto& fx = c.settings->fx;

    SETP (*c.distortion, "curve", (float) b.distCurve);
    SETP (*c.distortion, "tone", fx.distTone);
    SETP (*c.distortion, "highCut", fx.distHighCut);
    c.distPre.setCoefs (fx.distPre);
    c.distPost.setCoefs (fx.distPost);

    SETP (*c.vowelBank, "resonance", fx.vowelReso);
    SETP (*c.vowelBank, "size", fx.vowelSize);
    SETP (*c.combBank, "feedback", fx.combFeedback);

    SETP (*c.vocoder, "bands", fx.vocBands);
    SETP (*c.vocoder, "attack", fx.vocAttack);
    SETP (*c.vocoder, "release", fx.vocRelease);
    SETP (*c.vocoder, "bright", fx.vocBright);
    SETP (*c.vocoder, "carrierCentre", fx.vocCarrierHz);
    SETP (*c.vocoder, "modCentre", fx.vocModHz);
    SETP (*c.vocoder, "carrierSpread", fx.vocCarrierSpread);
    SETP (*c.vocoder, "modSpread", fx.vocModSpread);
    SETP (*c.vocoder, "carrierQ", fx.vocCarrierQ);
    SETP (*c.vocoder, "modQ", fx.vocModQ);

    c.charA.setCoefs (fx.charA);
    c.charB.setCoefs (fx.charB);
    SETP (*c.stereoWidth, "width", fx.widthWidth);
    SETP (*c.stereoWidth, "bassMono", fx.widthBassMono);

    c.eqLow.setCoefs (fx.eqLow);
    c.eqMid.setCoefs (fx.eqMid);
    c.eqHigh.setCoefs (fx.eqHigh);

    SETP (*c.phaser, "stages", fx.phStages);
    SETP (*c.phaser, "rate", fx.phRate);
    SETP (*c.phaser, "depth", fx.phDepth);
    SETP (*c.phaser, "centre", fx.phCentre);
    SETP (*c.phaser, "feedback", fx.phFeedback);
    SETP (*c.phaser, "spread", fx.phSpread);

    SETP (*c.chorus, "rate", fx.chRate);
    SETP (*c.chorus, "depth", fx.chDepth);
    SETP (*c.chorus, "baseDelay", fx.chDelay);
    SETP (*c.chorus, "spread", fx.chSpread);
    SETP (*c.chorus, "voices", fx.chVoices);
    SETP (*c.chorus, "lfoShape", fx.chLfoShape);

    SETP (*c.tapeWobble, "wow", fx.tapeWow);
    SETP (*c.tapeWobble, "flutter", fx.tapeFlutter);
    SETP (*c.tapeWobble, "drive", fx.tapeDrive);

    SETP (*c.reverb, "roomSize", fx.rvRoom);
    SETP (*c.reverb, "decay", fx.rvDecay);
    SETP (*c.reverb, "damping", fx.rvDamping);
    SETP (*c.reverb, "preDelay", fx.rvPre);
    SETP (*c.reverb, "colour", fx.rvColour);
    SETP (*c.reverb, "feedback", fx.rvFeedback);

    SETP (*c.granulator, "grains", fx.grGrains);
    SETP (*c.granulator, "size", fx.grSize);
    SETP (*c.granulator, "position", fx.grPos);
    SETP (*c.granulator, "spray", fx.grSpray);
    SETP (*c.granulator, "freeze", fx.grFreeze);
    SETP (*c.granulator, "pitch", fx.grPitch);
    SETP (*c.granulator, "pitchDisp", fx.grPitchDisp);
    SETP (*c.granulator, "stereo", fx.grStereo);
}

//==============================================================================
// Effect chains: lending, binding, retiring
//==============================================================================
int Engine::chainForSlot (int slot)
{
    const auto& L = states[(size_t) slot];
    if (slot == 0 || L.fxSameAsBase)
        return 0;

    const int nc = numChains.load (std::memory_order_acquire);

    // Already has one?
    for (int c = 1; c < nc; ++c)
    {
        auto& ch = *chains[(size_t) c];
        if (ch.state.load (std::memory_order_relaxed) == FxChain::InUse && ch.ownerUid == L.uid && ch.fadeLeft == 0)
        {
            ch.settings = &L;
            ch.orphan = false;
            return c;
        }
    }

    // Take a clean one.
    for (int c = 1; c < nc; ++c)
    {
        auto& ch = *chains[(size_t) c];
        if (ch.state.load (std::memory_order_acquire) == FxChain::Clean)
        {
            ch.ownerUid = L.uid;
            ch.settings = &L;
            ch.orphan = false;
            ch.numVoices = 0;
            ch.lastVoice = -1;
            ch.lastVoiceAge = 0;
            ch.silentSamples = 0;
            ch.voicelessSince = sampleClock;
            ch.fadeLeft = 0;
            applyFxModules (ch);
            ch.state.store (FxChain::InUse, std::memory_order_release);
            return c;
        }
    }

    // None free right now: ask for more (made off the audio thread), and let
    // the note use the base patch's effects until one is ready.
    if (nc < kMaxChains)
        wantedChains.store (juce::jmin (kMaxChains, nc + 2), std::memory_order_relaxed);
    return 0;
}

void Engine::bindChains()
{
    const int nc = numChains.load (std::memory_order_acquire);

    // (Only running racks: a retired one may be being cleared on the message thread.)
    for (int c = 0; c < nc; ++c)
        if (chains[(size_t) c]->state.load (std::memory_order_acquire) == FxChain::InUse)
            chains[(size_t) c]->numVoices = 0;

    for (const auto& voice : voices)
        if (voice.active)
            ++chains[(size_t) voice.chain]->numVoices;

    int clean = 0;

    for (int c = 0; c < nc; ++c)
    {
        auto& ch = *chains[(size_t) c];
        const int state = ch.state.load (std::memory_order_acquire);

        if (state == FxChain::Clean)
            ++clean;
        if (state != FxChain::InUse)
            continue;

        if (c == 0)
        {
            ch.settings = &states[0];
        }
        else
        {
            // Still owned by a layer with effects of its own? Otherwise it is
            // a tail ringing out on its last settings.
            int owner = -1;
            for (size_t s = 1; s < layout->slots.size(); ++s)
                if (layout->slots[s].uid == ch.ownerUid)
                {
                    ensureComputed ((int) s);
                    if (! states[s].fxSameAsBase)
                        owner = (int) s;
                    break;
                }

            if (owner >= 0)
            {
                ch.settings = &states[(size_t) owner];
                ch.orphan = false;
            }
            else if (! ch.orphan && ch.settings != nullptr)
            {
                ch.frozen = *ch.settings;
                ch.settings = &ch.frozen;
                ch.orphan = true;
            }

            if (ch.numVoices > 0)
                ch.voicelessSince = sampleClock;

            // Free it once its tail has really gone: long enough to cover a
            // delay repeat or a grain cloud reaching back into its buffer.
            if (ch.numVoices == 0 && ch.fadeLeft == 0 && ch.settings != nullptr)
            {
                const auto& b = ch.settings->b;
                double window = 0.3;
                if (b.delayMode > 0)  window += b.delayMs * 0.001 + 0.2;
                if (b.granMix > 0.0f) window += 2.0;

                if ((double) ch.silentSamples > window * sampleRate)
                {
                    ch.ownerUid = -1;
                    ch.state.store (FxChain::Dirty, std::memory_order_release);
                    continue;
                }
            }
        }

        applyFxModules (ch);
    }

    // Keep one chain in reserve: grow the pool while it may grow, and once it
    // is full, retire the tail that has been ringing the longest.
    if (clean == 0)
    {
        if (nc < kMaxChains)
        {
            wantedChains.store (juce::jmin (kMaxChains, nc + 2), std::memory_order_relaxed);
        }
        else
        {
            FxChain* victim = nullptr;
            for (int c = 1; c < nc; ++c)
            {
                auto& ch = *chains[(size_t) c];
                if (ch.state.load (std::memory_order_relaxed) == FxChain::InUse && ch.numVoices == 0
                    && ch.fadeLeft == 0 && (victim == nullptr || ch.voicelessSince < victim->voicelessSince))
                    victim = &ch;
            }
            if (victim != nullptr)
                retireChain (*victim);
        }
    }
}

void Engine::retireChain (FxChain& c)
{
    c.fadeLeft = juce::jmax (1, (int) (sampleRate * 0.01));   // 10 ms
}

void Engine::requestChains (int count)
{
    const int want = juce::jlimit (1, kMaxChains, count);
    int current = wantedChains.load();
    while (want > current && ! wantedChains.compare_exchange_weak (current, want)) {}
}

void Engine::serviceChains()
{
    const juce::ScopedLock sl (chainLock);

    int nc = numChains.load (std::memory_order_acquire);
    const int want = juce::jlimit (1, kMaxChains, wantedChains.load());

    while (nc < want)
    {
        auto c = std::make_unique<FxChain>();
        c->createModules();
        c->prepare (sampleRate);
        c->ownerUid = -1;
        c->state.store (FxChain::Clean, std::memory_order_release);
        chains[(size_t) nc] = std::move (c);
        ++nc;
        numChains.store (nc, std::memory_order_release);
    }

    // A retired chain still holds its old tail in its buffers. Clearing five
    // seconds of delay line is far too slow for the audio thread, so it is
    // done here and the chain handed back clean.
    for (int c = 1; c < nc; ++c)
    {
        auto& ch = *chains[(size_t) c];
        if (ch.state.load (std::memory_order_acquire) == FxChain::Dirty)
        {
            ch.reset();
            ch.settings = nullptr;
            ch.ownerUid = -1;
            ch.state.store (FxChain::Clean, std::memory_order_release);
        }
    }
}

int Engine::getChainsInUse() const
{
    int n = 0;
    for (int c = 0; c < numChains.load (std::memory_order_acquire); ++c)
        if (chains[(size_t) c]->state.load (std::memory_order_relaxed) == FxChain::InUse)
            ++n;
    return n;
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


void Engine::rebuildOrder()
{
    numOrdered = 0;
    for (int v = 0; v < kPolyVoices; ++v)
        if (voices[(size_t) v].active)
            order[(size_t) numOrdered++] = v;

    // grouped by layer, in voice order within a layer (insertion sort: 16 at most)
    for (int a = 1; a < numOrdered; ++a)
    {
        const int v = order[(size_t) a];
        int b = a - 1;
        while (b >= 0 && voices[(size_t) order[(size_t) b]].slot > voices[(size_t) v].slot)
        {
            order[(size_t) b + 1] = order[(size_t) b];
            --b;
        }
        order[(size_t) b + 1] = v;
    }

    orderDirty = false;
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

    const bool freshDetune = ! wasActive || retriggerEnvelopes;

    voice.note = note;
    voice.velocity = velocity;
    voice.held = true;
    voice.sustained = false;
    voice.active = true;
    voice.age = ++ageCounter;

    // The note's layer, and the effects it feeds. The modules read a few of
    // their settings (start phase, LFO keytrigger) at note-on, so the layer's
    // settings go in before they hear about the note.
    const int slot = slotOf (note);
    ensureComputed (slot);
    voice.slot = juce::jlimit (0, (int) layout->slots.size() - 1, slot);
    voice.chain = chainForSlot (voice.slot);
    const auto& L = states[(size_t) voice.slot];

    if (appliedSlot != voice.slot)
    {
        applyVoiceModules (L);
        appliedSlot = voice.slot;
    }

    auto& ch = *chains[(size_t) voice.chain];
    ch.lastVoice = v;
    ch.lastVoiceAge = voice.age;
    ++ch.numVoices;
    ch.voicelessSince = sampleClock;
    orderDirty = true;

    const bool envelopes = retriggerEnvelopes || ! wasActive;

    if (envelopes)
    {
        voice.random = random.nextFloat() * 2.0f - 1.0f;
        // Punch (measured): a gain pulse at the start of the note whose time
        // constant follows the key - 0.68 ms at C1, halving every octave - and
        // whose height falls away towards the top of the keyboard.
        voice.punch = 1.0f;
        voice.punchCoef = (float) std::exp (-1.0 / (curves::punchSeconds ((float) note) * sampleRateHz));
        voice.punchDepth = curves::punchKeyWeight ((float) note);
        voice.lfo3Fade = L.b.lfo3FadeSeconds > 0.0f ? 0.0f : 1.0f;
    }

    // Sound sources: 'retrigger' when the voice is still sounding keeps the
    // oscillator phases running, so a stolen or legato voice does not click.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get() })
    {
        m->voiceNoteOn (v, note, wasActive);
        m->voiceVelocity (v, 1.0f);   // velocity is applied at the amplifier, once
    }

    // A fresh note takes the next per-note detune (a legato note keeps its
    // voice's). A unison stack is that many voices on the reference: each copy
    // takes its own step of the cycle.
    if (freshDetune)
    {
        const int copies = juce::jmax (1, L.vm.unisonVoices);
        if (copies == 1)
        {
            voice.voiceCents = curves::voiceDetuneCents (detuneCounter++, note);
            for (int u = 0; u < VibrioOscModule::kMaxUnison; ++u)
                for (auto* m : { osc1.get(), osc2.get() })
                    static_cast<VibrioOscModule*> (m)->setPartialCents (v, u, 0.0f);
        }
        else
        {
            voice.voiceCents = 0.0f;
            for (int u = 0; u < VibrioOscModule::kMaxUnison; ++u)
            {
                const float c = curves::voiceDetuneCents (detuneCounter + (juce::uint32) (u % copies), note);
                for (auto* m : { osc1.get(), osc2.get() })
                    static_cast<VibrioOscModule*> (m)->setPartialCents (v, u, c);
            }
            detuneCounter += (juce::uint32) copies;
        }
    }

    // Oscillator 1 is the sync master and the FM triangle's clock: both start
    // on its phase, so FM with sync gives the same cycle on every note.
    if (! wasActive)
    {
        const double p1 = static_cast<VibrioOscModule*> (osc1.get())->partialPhase (v);
        static_cast<VibrioOscModule*> (osc2.get())->setSyncPhase (v, p1);
        voice.fmTriPhase = p1;
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
//
// Key mode belongs to the base patch; portamento to the layer of the note.
void Engine::keyDown (int note, float velocity)
{
    const int mode = base().b.keyMode;
    const int noteSlot = slotOf (note);
    ensureComputed (noteSlot);
    const bool glideOn = states[(size_t) juce::jlimit (0, (int) layout->slots.size() - 1, noteSlot)].b.glideOn;

    if (mode >= 1 && mode <= 4)
    {
        const bool legato = ! monoStack.empty();
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        if (monoStack.size() < monoStack.capacity())
            monoStack.push_back (note);

        const bool singleTrigger = mode == 2 || mode == 4;
        const bool glide = glideOn && (mode <= 2 || legato);
        startVoice (0, note, velocity, ! (legato && singleTrigger), glide);
        return;
    }

    // Poly: a note that is still ringing (released or on the pedal) is
    // reused rather than doubled.
    for (int v = 0; v < kPolyVoices; ++v)
        if (voices[(size_t) v].active && voices[(size_t) v].note == note)
        {
            startVoice (v, note, velocity, true, glideOn);
            return;
        }

    startVoice (allocateVoice(), note, velocity, true, glideOn);
}

void Engine::keyUp (int note, float releaseVelocity)
{
    const int mode = base().b.keyMode;

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
            const int back = monoStack.back();
            ensureComputed (slotOf (back));
            const bool glideOn = states[(size_t) juce::jlimit (0, (int) layout->slots.size() - 1, slotOf (back))].b.glideOn;
            startVoice (0, back, voice.velocity, ! singleTrigger, glideOn);
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
    const Block& b = base().b;   // the arp and key mode belong to the base patch
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
    const Block& b = base().b;   // the arp and key mode belong to the base patch
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
    const Block& b = base().b;   // the arp and key mode belong to the base patch
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
    const Block& b = base().b;   // the arp and key mode belong to the base patch
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

// Patterns (measured on the reference, 32 sixteenths of one held note):
// '#' starts a note on that step. 0 is the user grid - a patch without one
// (all steps on, the default) plays eighths, as the reference does.
namespace
{
    const std::array<juce::uint32, 64>& arpPatternMasks()
    {
        static const std::array<juce::uint32, 64> masks = []
        {
            static const char* const steps[64] = {
          "#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.", "################################",
          "###.###.###.###.###.###.###.###.", "################################",
          "#.#.##.##.#.##.##.#.##.##.#.##.#", "#..##.#####.#.###..##.#####.#.##",
          "#..#.#.##..#.#.##..#.#.##..#.#.#", "#.#######.#######.#######.######",
          "#..##.#.#..##.#.#..##.#.#..##.#.", "#.##.##.##.##.###.##.##.##.##.##",
          "#.#.##.#.#..#...#.#.##.#.#..#...", "####.#.#####.#.#####.#.#####.#.#",
          "#####.###.##.#.######.###.##.#.#", "##.#.##.##.##.####.#.##.##.##.##",
          "#####.##......#######.##.#..####", "#####.##...##########.##...#####",
          "##.#..#.#..#..#.#..#..#.#..#..##", "#..#.#.##..#.#.##..#.#.##..#.#.#",
          "#..#..#.....#.#.#..#..#.....#.#.", "#.##..##..##..#.#.##..##..##..#.",
          "#.#.#.#.#.#.#..##.#.#.#.#.#.####", "#.#.#.##.#.##.###.#.#.##.#..#.#.",
          "#.##.##.#.#####.#.##.##.#.######", "#.#..#..#.#..#..#.#..#..#.#..#..",
          "#.....#...#.......#...#...#.....", "#......#.##....##......#.##....#",
          "###..#.#.##....####..#.#.##...##", "###.#.##.##.##.####.##.#.##.####",
          "#...#.....##.#..#...#....###.#..", "#..####.....###.#..####....####.",
          "#..##......##..##..##......##.##", "#.#....##.#....##.#....##.#..#..",
          "##.#...###.#...###.#...###.#.#.#", "##.###.###.###.###.###.###.###.#",
          "#######.###############.########", "#..##.####.##.###..##.####.##.##",
          "#......#..#.....#......#..#...##", "##.#.#.#.#.#.#####.#.#.#.#.#.#.#",
          "#####.#####.###.#####.#####.###.", "#.#####..####...#.#####..####...",
          "#...#..#....#..##...#..#....#..#", "#...#..#....#.......#..#....#...",
          "################################", "################################",
          "################################", "################################",
          "####.#######.#######.#######.###", "#......#..#...#.#......#..#...#.",
          "#.##.#.#.#.##.###.##.#.#.#.#####", "#.#.#...#..#..#...#.#...#..#..#.",
          "#..##..##..##..##..##..##..##..#", "################################",
          "#.....#...#.....#...#.#...#.#.##", "#.#..#.##.#.##.##.#.##.##.#.##.#",
          "#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.", "#.##.##.##.##.###.##.##.##.##.##",
          "#.....#........##.....#...#...#.", "#.#.##.##.#.##.##.#.##.##.#.##.#",
          "##.#.##.##.#.##.##.#.##.##.#.##.", "#.#.#.........#.#.#.#.....#.#.#.",
          "################################", "#####.#######.#.#####.#######.#.",
          "#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.##", "#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.##" };
            std::array<juce::uint32, 64> m {};
            for (int p = 0; p < 64; ++p)
                for (int s = 0; s < 32; ++s)
                    if (steps[p][s] == '#')
                        m[(size_t) p] |= 1u << s;
            return m;
        }();
        return masks;
    }
}

bool Engine::arpStepIsOn (int patternStep, float& velocityScale, float& lengthScale) const
{
    const Block& b = base().b;   // the arp and key mode belong to the base patch
    velocityScale = 1.0f;
    lengthScale = 1.0f;

    if (b.arpPattern == 0)
    {
        bool allDefault = userPatternLength == 32 || userPatternLength == 16;
        for (int s = 0; s < 32 && allDefault; ++s)
            allDefault = stepOn[(size_t) s];
        if (! allDefault)
        {
            const int s = patternStep % userPatternLength;
            velocityScale = juce::jmax (0.05f, stepVelocity[(size_t) s]);
            lengthScale = 0.1f + 1.8f * stepLength[(size_t) s];
            return stepOn[(size_t) s];
        }
    }

    const auto mask = arpPatternMasks()[(size_t) juce::jlimit (0, 63, b.arpPattern)];
    return ((mask >> (patternStep % 32)) & 1u) != 0;
}

void Engine::arpAdvance()
{
    const Block& b = base().b;   // the arp and key mode belong to the base patch
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
                       float* hold, int& holdCount, double& holdAcc, OnePole& filt)
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
        case 8:                                                                      // Bit Reducer
        {
            // Original: amount01 carries the Osc Volume byte / 127. Measured on a
            // saw (the staircase's steps, and the level at full): a step of 0.2
            // of the saw's peak at 96, doubling every 16 bytes, 0.92 at 127 -
            // there only -peak, 0 and +peak are left. Fine (0.05) at 64 and below.
            static const float xs[] { 64, 80, 96, 112, 127 };
            static const float ys[] { 0.05f, 0.1f, 0.2f, 0.4f, 0.92f };
            const float step = curves::lookup (xs, ys, juce::jlimit (64.0f, 127.0f, amount01 * 127.0f)) * 0.7071f;
            return std::round (x / step) * step;
        }
        case 9:                                                                      // Rate Reducer
        case 10:                                                                     // Rate + Follow
        {
            // Original: amount01 carries the Osc Volume byte / 127. Measured on
            // white noise (the hold's nulls sit at multiples of its rate): 6.5 kHz
            // at 80, falling 1.61x every 8 bytes (390 Hz at 127, ~17 kHz at 64).
            // Rate + Follow is the same law at C4 and tracks the note fully, but
            // never holds for longer than 1/366 s.
            const float ov = amount01 * 127.0f;
            float hz = 6500.0f * std::exp2 (-(ov - 80.0f) * 0.0858f);
            if (curveId == 10)
                hz = juce::jmax (366.0f, hz * noteHz / 261.63f);
            if (hz >= (float) sr * 0.5f)
                return x;
            if (ch == 0)
            {
                holdAcc += hz / sr;
                holdCount = holdAcc >= 1.0 ? 0 : 1;   // 0: take a new sample on both channels
                holdAcc -= std::floor (holdAcc);
            }
            if (holdCount == 0) hold[ch] = x;
            return hold[ch];
        }
        case 11: case 12:                                                            // Low Pass (+ Follow)
        case 13: case 14:                                                            // High Pass (+ Follow)
        {
            // Original: amount01 carries the Osc Volume byte / 127 here. Measured in
            // measured: the Low Pass closes from fully open at 64 (5.6 kHz at 80, 26 Hz
            // at 127); the High Pass opens from ~24 Hz at 64 (4.8 kHz at 112).
            const float ov = amount01 * 127.0f;
            float hz = curveId <= 12 ? 5594.0f * std::exp2 (-(ov - 80.0f) * 0.162f)
                                     : 137.0f * std::exp2 ((ov - 80.0f) * 0.159f);
            if (curveId == 12 || curveId == 14)   // + Follow: about half-way key tracking from C2
                hz *= 0.46f * std::pow (juce::jmax (8.0f, noteHz) / 65.41f, 0.55f);
            hz = juce::jlimit (curveId >= 13 ? 24.0f : 5.0f, (float) (sr * 0.45), hz);
            if (curveId <= 12 && ov <= 64.0f) return x;   // fully open
            const float lp = filt.lp (x, OnePole::coef (hz, sr), ch);
            return curveId <= 12 ? lp : x - lp;
        }
        default: return x;
    }
}


void Engine::renderVoice (int v, const LayerState& L, float& outL, float& outR, ModArray& mod)
{
    const Block& b = L.b;
    const auto& matrix = L.matrix;
    const Block& gb = base().b;   // the global parts: input mode and follower
    auto& voice = voices[(size_t) v];
    const ModArray& pm = voice.prevMod;
    const float vel = voice.velocity;
    const float note = (float) voice.note;

    // Portamento
    voice.glideNote = b.glideOn ? note + (voice.glideNote - note) * b.glideCoef : note;
    const float glideOffset = voice.glideNote - note;

    //-- envelopes (matrix offsets are one sample late, which nobody can hear)
    // Original: every stage on the reference's measured curves. The matrix offsets
    // the bytes, as it does on the hardware.
    const auto envParams = [&] (SynthModule& m, const float*, float A, float D, float S, float Sl, float R,
                                Dest dA, Dest dD, Dest dS, Dest dSl, Dest dR)
    {
        const auto byteOf = [&] (float byte, Dest d) { return byte + pm[(size_t) d] * 127.0f; };
        m.setParameterAt (eIdx.attack, curves::attackMs (byteOf (A, dA)));
        m.setParameterAt (eIdx.decay, curves::decayDbPerSecond (byteOf (D, dD)));
        m.setParameterAt (eIdx.sustain, curves::sustainLevel (byteOf (S, dS)));
        m.setParameterAt (eIdx.slope, curves::slopeDbPerSecond (byteOf (Sl, dSl)));
        m.setParameterAt (eIdx.release, curves::decayDbPerSecond (byteOf (R, dR)));
    };

    envParams (*envAmp, b.ampMs, b.aA, b.aD, b.aS, b.aSlope, b.aR,
               Dest::AmpAttack, Dest::AmpDecay, Dest::AmpSustain, Dest::AmpSlope, Dest::AmpRelease);
    {
        // The filter envelope as a modulator (measured through Osc 2's pitch):
        // its own, slower attack; decay and release on the same dB/s table;
        // sustain a plain level; Sustain Time a straight-line slope.
        const auto byteOf = [&] (float byte, Dest d) { return byte + pm[(size_t) d] * 127.0f; };
        auto& m = *envFilter;
        m.setParameterAt (eIdx.attack, curves::modAttackMs (byteOf (b.fA, Dest::FiltAttack)));
        m.setParameterAt (eIdx.decay, curves::decayDbPerSecond (byteOf (b.fD, Dest::FiltDecay)));
        m.setParameterAt (eIdx.sustain, juce::jlimit (0.0f, 1.0f, byteOf (b.fS, Dest::FiltSustain) / 127.0f));
        m.setParameterAt (eIdx.slope, curves::slopeDbPerSecond (byteOf (b.fSlope, Dest::FiltSlope)) * 0.054f);
        m.setParameterAt (eIdx.release, curves::decayDbPerSecond (byteOf (b.fR, Dest::FiltRelease)));
        m.setParameterAt (eIdx.modulator, 1.0f);
    }

    envAmp->processVoiceSample (v, in.data(), out.data());     const float ampEnv = out[0][0];
    envFilter->processVoiceSample (v, in.data(), out.data());
    const float filterEnvRaw = out[0][0];
    env3->processVoiceSample (v, in.data(), out.data());       const float env3Value = out[0][0];
    env4->processVoiceSample (v, in.data(), out.data());       const float env4Value = out[0][0];

    // With the Input Follower on, the filter envelope follows the audio input.
    const float filtEnv = gb.followerMode > 0 ? juce::jlimit (0.0f, 1.0f, followerValue) : filterEnvRaw;

    //-- LFOs
    const Dest rateDest[] = { Dest::Lfo1Rate, Dest::Lfo2Rate, Dest::Lfo3Rate };
    float lfo[3];
    const float octavesFromC = (note - 60.0f) / 12.0f;

    for (int n = 0; n < 3; ++n)
    {
        float hz = b.lfoHz[n];
        const float octaves = pm[(size_t) rateDest[n]] * 9.97f + b.lfoKeyFollow[n] * octavesFromC;
        if (octaves != 0.0f)
            hz *= voice.memo.lfoOctaves[n].get (octaves, [] (float o) { return std::exp2 (o); });

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
    const float lfo3Pitch = lfo[2] * curves::lfo3PitchSemis (lfo3Amt);   // measured: 2.8 st at 100, 8 at 127
    const float lfo3Pw = lfo[2] * lfo3Amt * 0.5f;
    const int l3 = b.lfo3Dest;

    //-- pitch
    const float pb = b.bendExp ? pitchBend * std::abs (pitchBend) : pitchBend;
    const float bend = pb >= 0.0f ? pb * b.bendUp : pb * b.bendDown;
    const float common = b.transpose + m (Dest::Transpose) * 96.0f + bend + glideOffset + voice.voiceCents * 0.01f;
    const float fromMiddle = note - 60.0f;

    const float semis1 = b.osc1Semi + fromMiddle * (b.osc1Track - 1.0f) + common
                       + lfo[0] * curves::lfoPitchCents (amt (b.lfo1Osc1, Dest::Lfo1Osc1)) * 0.01f
                       + (l3 == 0 || l3 == 1 ? lfo3Pitch : 0.0f)
                       + m (Dest::Osc1Pitch) * kPitchModSemis;

    const float detune2 = clamp01 (voice.memo.detune.get (b.osc2DetuneCents, [] (float c) { return std::sqrt (c / 100.0f); })
                                   + m (Dest::Osc2Detune));
    // Osc 3 Slave doubles oscillator 2: the pair share osc 2's detune, split
    // either side of the note (measured), never quite in unison.
    const bool slave3 = b.osc3Mode == 1;
    const float slaveSpread = slave3 ? juce::jmax (detune2 * detune2, kSlaveMinSpread) : 0.0f;
    const float semis2 = b.osc2Semi + (slave3 ? 0.5f * slaveSpread : detune2 * detune2) + fromMiddle * (b.osc2Track - 1.0f) + common
                       + lfo[0] * curves::lfoPitchCents (amt (b.lfo1Osc2, Dest::Lfo1Osc2)) * 0.01f
                       + (l3 == 1 || l3 == 2 ? lfo3Pitch : 0.0f)
                       + m (Dest::Osc2Pitch) * kPitchModSemis
                       + filtEnv * curves::envPitchSemis (amt (b.osc2EnvPitch, Dest::FiltEnvOsc2Pitch));

    const float r1 = voice.memo.ratio1.get (semis1, [] (float s) { return std::exp2 (s / 12.0f); });
    const float r2 = voice.memo.ratio2.get (semis2, [] (float s) { return std::exp2 (s / 12.0f); });

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

    // In the Wavetable model byte 17 is the wavetable index, not Shape: the
    // wave must not also be morphed towards saw and pulse by it.
    const float shape1 = wt1 ? 0.0f : clamp01 (b.osc1Shape + b.osc1ShapeVel * vel * 0.5f + lfo2Shape + m (Dest::Osc1Shape));
    const float shape2 = wt2 ? 0.0f : clamp01 (b.osc2Shape + b.osc2ShapeVel * vel * 0.5f + lfo2Shape + m (Dest::Osc2Shape));
    const float pw1 = curves::pulseWidth (b.osc1Pw + m (Dest::Osc1Pw) + b.pwVel * vel * 0.5f + lfo1Pw
                                          + (l3 == 3 || l3 == 4 ? lfo3Pw : 0.0f));
    const float pw2 = curves::pulseWidth (b.osc2Pw + m (Dest::Osc2Pw) + b.pwVel * vel * 0.5f + lfo1Pw
                                          + (l3 == 4 || l3 == 5 ? lfo3Pw : 0.0f));
    // A sampled TI wavetable: the index sweeps its frames (wave slots 2..).
    const auto tiPos = [] (int frames, float index, float interp)
    {
        float pos = clamp01 (index) * (float) (frames - 1);
        if (interp > 0.0f)
        {
            const float whole = std::floor (pos), frac = pos - whole;
            pos = whole + clamp01 ((frac - 0.5f) / juce::jmax (0.02f, 1.0f - interp) + 0.5f);
        }
        return 2.0f + pos;
    };
    const int ti1 = wt1 ? tiActive[0] : -1, ti2 = wt2 ? tiActive[1] : -1;
    // LFO 2 -> Shape sweeps the wavetable index in the Wavetable model.
    const float idx1 = b.osc1WtIdx + m (Dest::Osc1WtIndex) + (wt1 ? m (Dest::Osc1Shape) + lfo2Shape : 0.0f);
    const float idx2 = b.osc2WtIdx + m (Dest::Osc2WtIndex) + (wt2 ? m (Dest::Osc2Shape) + lfo2Shape : 0.0f);
    const float wave1 = ti1 >= 0 ? tiPos (tiFrameCount[(size_t) ti1].load (std::memory_order_relaxed), idx1, b.osc1Interp)
                                 : wavePos (wt1, b.osc1Wave, b.osc1WtBase, idx1, b.osc1Interp);
    const float wave2 = ti2 >= 0 ? tiPos (tiFrameCount[(size_t) ti2].load (std::memory_order_relaxed), idx2, b.osc2Interp)
                                 : wavePos (wt2, b.osc2Wave, b.osc2WtBase, idx2, b.osc2Interp);

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

    //-- oscillator 2: FM from oscillator 1, hard sync to oscillator 1, and in
    //   the HyperSaw model Sync Frequency (plus the filter envelope) as a
    //   pitch-preserving sync. Bytes 27 and 30 are FM Amount and FiltEnv > FM
    //   in Classic and Wavetable, Sync Frequency and FiltEnv > Sync in HyperSaw.
    const bool hyper2 = b.osc2Mode == 1;
    // (measured: FiltEnv > FM adds up to 1.45 x amount of knob travel on a
    // gentler envelope curve than the filter's, env^0.6; LFO 2 > FM swings
    // the knob by 0.67 x amount)
    const float fmKnob = hyper2 ? 0.0f : clamp01 (b.fmAmount + m (Dest::Osc2Fm) + b.fmVel * vel * 0.5f
                                  + lfo[1] * 0.67f * amt (b.lfo2Fm, Dest::Lfo2Fm)
                                  + voice.memo.fmEnvCurve.get (filtEnv, [] (float e) { return std::pow (juce::jmax (0.0f, e), 0.6f); })
                                      * 1.45f * amt (b.fmEnv, Dest::FiltEnvFmSync));
    // FM (measured): oscillator 2's frequency runs at f2 * (1 + index * m),
    // through zero. The modulator m is a triangle at oscillator 1's pitch
    // (Pos Triangle: the same lifted to 0..1, which also raises the pitch),
    // or oscillator 1's own wave at its own level (a sine reads as 1).
    const float fmIndex = voice.memo.fmIndex.get (fmKnob, [] (float k) { return curves::fmIndex (k * 127.0f); });

    float fmSource = 0.0f;
    voice.fmTriPhase += (double) (r1 * voice.memo.noteHz.get (voice.glideNote,
                                     [] (float n) { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); })) / sampleRate;
    voice.fmTriPhase -= std::floor (voice.fmTriPhase);
    if (fmIndex > 0.0f)
    {
        const float p = (float) voice.fmTriPhase;
        const float tri = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        switch (b.fmMode)
        {
            case 0:  fmSource = tri * 0.5f + 0.5f; break;                                     // Pos Triangle
            case 1:  fmSource = tri; break;                                                   // Triangle
            case 3:  fmSource = random.nextFloat() * 2.0f - 1.0f; break;                      // Noise
            case 4:  fmSource = inputL; break;
            case 5:  fmSource = (inputL + inputR) * 0.5f; break;
            case 6:  fmSource = inputR; break;
            default: fmSource = (o1L + o1R) * (0.7071f / curves::kMorphWaveGain); break;   // Wave (each side carries 0.707)
        }
    }

    const float syncOctaves = ! hyper2 ? 0.0f
                            : juce::jlimit (0.0f, 4.5f, b.syncFreq * 4.5f
                                                        + filtEnv * curve (amt (b.syncEnv, Dest::FiltEnvFmSync)) * 4.5f);
                            // (LFO 3 > Sync Phase: no audible effect in the reference)
    const float spread2 = wt2 ? 1.0f + 3.0f * b.osc2FormantSpread : 1.0f;
    const float slave2 = r2 * voice.memo.syncRatio.get (syncOctaves, [] (float o) { return std::exp2 (o); }) * spread2;
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
    // (Slave ignores Osc 3 Volume and Detune - measured - and sits level with osc 2)
    const float osc3Level = slave3 ? kSlaveLevel / kOsc3Gain : clamp01 (b.osc3Volume + m (Dest::Osc3Volume));
    if (b.osc3Mode > 0 && osc3Level > 0.0f)
    {
        const float detune3 = -b.osc3DetuneCents / 100.0f;   // osc 3 detunes downwards (measured)
        if (slave3)   // Slave: oscillator 2's sound, the other side of its detune
        {
            osc3->setParameterAt (o3.ratio, r2 * voice.memo.osc3Ratio.get ((m (Dest::Osc3Pitch) * kPitchModSemis - slaveSpread) / 12.0f,
                                                                          [] (float x) { return std::exp2 (x); }));
            osc3->setParameterAt (o3.wave, wt2 ? wave2 : b.osc2Wave);
            osc3->setParameterAt (o3.shape, shape2);
            osc3->setParameterAt (o3.pw, pw2);
        }
        else
        {
            osc3->setParameterAt (o3.ratio, voice.memo.osc3Ratio.get ((b.osc3Semi + detune3 + common + m (Dest::Osc3Pitch) * kPitchModSemis) / 12.0f,
                                                                      [] (float x) { return std::exp2 (x); }));
        }
        osc3->processVoiceSample (v, in.data(), out.data());
        o3L = out[0][0] * osc3Level * kOsc3Gain;
        o3R = out[0][1] * osc3Level * kOsc3Gain;
    }

    float subL = 0.0f, subR = 0.0f;
    const float subLevel = curves::subVolumeGain (clamp01 (b.subVolume + m (Dest::SubVolume)));
    if (subLevel > 0.0f)
    {
        subOsc->setParameterAt (subRatio, 0.5f * r1);   // an octave under oscillator 1, wherever it goes
        subOsc->processVoiceSample (v, in.data(), out.data());
        // The reference's triangle sub is far hotter than its square (about 4x the
        // amplitude, measured on clean triangle harmonics) - kept as heard.
        const float subShapeGain = L.vm.subWave == 1.0f ? 3.94f : 1.0f;
        subL = out[0][0] * subLevel * subShapeGain;
        subR = out[0][1] * subLevel * subShapeGain;
    }

    float nL = 0.0f, nR = 0.0f;
    const float noiseLevel = curves::noiseVolumeGain (127.0f * clamp01 (b.noiseVolume + m (Dest::NoiseVolume)));
    if (noiseLevel > 0.0f)
    {
        noise->processVoiceSample (v, in.data(), out.data());
        const float colourByte = juce::jlimit (0.0f, 127.0f, 64.0f + 64.0f * (b.noiseColour + 2.0f * m (Dest::NoiseColor)));

        // one-pole low pass below the centre, one-pole high pass above it,
        // each with its measured gain (see curves::noiseLowPassHz)
        const auto& nf = voice.memo.noiseFilter.get (colourByte, [sr = (float) sampleRate] (float c)
        {
            // the low pass is the plain one-pole (its top end levels off near
            // Nyquist, as the reference's does); the high pass the matched
            // (bilinear) one-pole, flat all the way up
            const bool low = c < 64.0f;
            const float k = low ? 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (curves::noiseLowPassHz (c), sr) / sr)
                                : [&] { const float g = std::tan (juce::MathConstants<float>::pi * curves::noiseHighPassHz (c) / sr); return g / (1.0f + g); }();
            const float gain = juce::Decibels::decibelsToGain (low ? curves::noiseLowPassDb (c) : curves::noiseHighPassDb (c));
            return std::array<float, 3> { k, gain, low ? 1.0f : 0.0f };
        });

        for (int ch = 0; ch < 2; ++ch)
        {
            float x = out[0][(size_t) ch];
            float& s = voice.noiseLp[ch];
            if (nf[2] > 0.5f)
            {
                s += (x - s) * nf[0];
                x = s;
            }
            else
            {
                const float vv = (x - s) * nf[0], lp = vv + s;
                s = lp + vv;
                x -= lp;
            }
            (ch == 0 ? nL : nR) = x * nf[1] * noiseLevel * kNoiseGain;
        }
    }

    const float ringLevel = curves::ringVolumeGain (127.0f * clamp01 (b.ringVolume + m (Dest::RingMod)));

    //-- mixer (Original: measured against the reference)
    // Osc Balance: measured gains for each side (curves::balanceGain1/2).
    const float balByte = clamp01 (b.balance + m (Dest::OscBalance)) * 127.0f;
    const float g1 = curves::balanceGain1 (balByte) * kOscTrim;
    const float g2 = curves::balanceGain2 (balByte) * kOscTrim;

    // Split routing sends oscillator 1 (and the sub) to filter 1, the rest to
    // filter 2; every other routing sums the two halves.
    // (the reference's sub square sits at about the saw's own amplitude.)
    float aL = o1L * g1 + subL * 0.68f, aR = o1R * g1 + subR * 0.68f;
    // Osc 3 in Slave mode is a copy of oscillator 2 and sits on its side of
    // the balance (measured: silent at balance 0); the other modes do not.
    // The doubled pair each come out a little under a lone osc 2.
    const float g3 = slave3 ? g2 : 1.0f;
    const float g2s = slave3 ? g2 * kSlaveLevel : g2;
    float bL = o2L * g2s + o3L * g3;
    float bR = o2R * g2s + o3R * g3;

    // Polarity (measured, sample by sample): the reference's saw rises and its
    // pulse and sine start downwards - every oscillator the other way up from
    // ours. Turned over here, so whatever follows that is not symmetric (the
    // shaping curves, the ring modulator against the dry oscillators) meets
    // the waveform the right way round. (The ring takes the product, which
    // the flip leaves alone.)
    aL = -aL; aR = -aR; bL = -bL; bR = -bR;

    // Oscillator tone (measured on saws, pulses and the sub at C2..C4, the
    // same at every pitch): the reference's oscillators are about 1.2 dB
    // brighter around 9 kHz than ours and fall away above 16 kHz (-3 dB at
    // 20 kHz). A peak and a high shelf, set relative to the sample rate as
    // the difference is in how the steps are band-limited. Noise is not in it.
    {
        const auto tone = [this, &voice] (float x, int i)
        {
            for (int st = 0; st < 2; ++st)
            {
                const auto& c = oscTone[st];
                float* z = voice.toneZ[i][st];
                const float y = c.b0 * x + z[0];
                z[0] = c.b1 * x - c.a1 * y + z[1];
                z[1] = c.b2 * x - c.a2 * y;
                x = y;
            }
            return x;
        };
        aL = tone (aL, 0); aR = tone (aR, 1);
        bL = tone (bL, 2); bR = tone (bR, 3);
    }

    // Ring modulator: oscillator 1 (and the sub) times oscillator 2 as the
    // balance leaves it. It joins after Osc Volume, with the noise - at Osc
    // Volume 0 only ring and noise remain (several ROM patches rely on it).
    const float ringL = (o1L + subL * 0.68f) * o2L * ringLevel * kRingGain;
    const float ringR = (o1R + subR * 0.68f) * o2R * ringLevel * kRingGain;

    if (gb.inputMode == 1)   // Dynamic: the audio input played through each voice
    {
        aL += inputL; aR += inputR;
    }

    // Osc Volume: below the centre a fade of oscillators, sub and ring;
    // above it only the saturation drive moves.
    const float ovByte = juce::jlimit (0.0f, 127.0f, b.oscVolByte + m (Dest::OscVolume) * 127.0f);
    const float pre = curves::oscVolumeGain (ovByte);
    aL *= pre; aR *= pre; bL *= pre; bR *= pre;

    // Noise joins after Osc Volume (measured: at full it sits ~1.6 dB above a saw).
    bL += nL + ringL; bR += nR + ringR;

    // LFO 1 > Filter Gain (measured with the LFO frozen): the filter's input
    // gain, so the saturation sees it too. Any amount pulls the level down
    // (to 0.16 at full) and the LFO swings it around that; deep settings
    // close it completely on the far half of the cycle.
    {
        const float x = amt (b.lfo1Gain, Dest::Lfo1FiltGain);
        if (x != 0.0f)
        {
            const float g = curves::lfoFilterGain (x, lfo[0]);
            aL *= g; aR *= g; bL *= g; bR *= g;
        }
    }

    // Punch: a short gain pulse ahead of the filters (measured: a closed filter
    // smears it, so it is not in the amplifier).
    {
        const float byte = clamp01 (b.punch + m (Dest::Punch)) * 127.0f;
        if (byte > 0.0f && voice.punch > 1.0e-4f)
        {
            const float g = 1.0f + curves::punchHeight (byte) * voice.punchDepth * voice.punch;
            aL *= g; aR *= g; bL *= g; bR *= g;
        }
        voice.punch *= voice.punchCoef;
    }

    //-- filters
    // Key follow tracks the pitch that sounds, transpose included (a patch
    // transposed down two octaves keeps its filters where the notes are).
    const float keySemis = note + b.transpose - b.keyBase;
    constexpr float kBytesPerSemitone = 12.86f / 12.0f;   // The reference's cutoff: an octave every 12.86 bytes
    const float lfo2Cut1 = lfo[1] * curve (amt (b.lfo2Cut1, Dest::Lfo2Cutoff1)) * 0.5f;
    const float lfo2Cut2 = lfo[1] * curve (amt (b.lfo2Cut2, Dest::Lfo2Cutoff2)) * 0.5f;

    // Velocity scales the envelope amount (measured: full sensitivity gives
    // amount x velocity; nothing is added when the amount is zero).
    const auto velScale = [vel] (float sens)
    {
        // (negative: at -100 % a velocity-100 note keeps 0.39 of the amount - measured)
        return sens >= 0.0f ? 1.0f - std::pow (sens, 1.5f) * (1.0f - vel) : 1.0f - std::pow (-sens, 1.5f) * vel * 0.775f;
    };
    const float envAmt1 = (b.env1 + m (Dest::Filter1EnvAmt)) * velScale (b.env1Vel);
    const float envAmt2 = (b.env2 + m (Dest::Filter2EnvAmt)) * velScale (b.env2Vel);

    const float cutByte1 = (b.cut1 + m (Dest::Cutoff1) + lfo2Cut1) * 127.0f
                         + filtEnv * b.pol1 * curves::envAmountBytes (envAmt1)
                         + keySemis * b.key1 * kBytesPerSemitone;

    // Cutoff Link (measured): filter 2 follows filter 1's knob (not its key
    // follow or envelope) shifted by d|d|/32 bytes, d = Cutoff 2 - 64; key
    // follow is filter 2's own either way.
    const float cutByte2 = (b.link ? (b.cut1 + m (Dest::Cutoff1)) * 127.0f + b.linkOffsetBytes + lfo2Cut2 * 127.0f
                                   : (b.cut2 + lfo2Cut2) * 127.0f)
                         + keySemis * b.key2 * kBytesPerSemitone
                         + m (Dest::Cutoff2) * 127.0f
                         + filtEnv * b.pol2 * curves::envAmountBytes (envAmt2);

    const float lfo1Reso = lfo[0] * curve (amt (b.lfo1Reso, Dest::Lfo1Reso)) * 0.5f;
    const float resByte1 = 127.0f * clamp01 (b.res1 + m (Dest::Reso1) + b.res1Vel * vel * 0.5f + lfo1Reso);
    const float resByte2 = 127.0f * clamp01 (b.res2 + m (Dest::Reso2) + b.res2Vel * vel * 0.5f + lfo1Reso);

    const bool serial6 = b.routing == 1;
    const bool analog1 = b.mode1 >= 4;
    const int poles1 = b.mode1 - 3;   // analog: 1..4

    // Coefficients only when cutoff or resonance moved (by a hair or more).
    if (std::abs (cutByte1 - voice.lastCut1) > 0.01f || resByte1 != voice.lastRes1)
    {
        voice.lastCut1 = cutByte1; voice.lastRes1 = resByte1;
        const float hz = curves::cutoffHz (cutByte1);
        const float q = curves::resoQ (resByte1) * curves::baseQ (cutByte1) / 0.629f;
        voice.c1a = Svf::make (hz, q, sampleRate);
        voice.c1b = Svf::make (hz, 0.70f, sampleRate);
        // the Analog modes: measured pole frequency, feedback and level
        const float hzA = curves::cutoffHz (cutByte1) * curves::analogPoleRatio (cutByte1, resByte1);
        const float gA = std::tan (juce::MathConstants<float>::pi * juce::jlimit (5.0f, (float) (sampleRate * 0.49), hzA) / (float) sampleRate);
        voice.g1Ladder = gA / (1.0f + gA);
        voice.k1Ladder = curves::analogK (cutByte1, resByte1);
        voice.duck1Ladder = juce::Decibels::decibelsToGain (-curves::analogDuckDb (cutByte1, resByte1));
        {
            // the first stage's soft square while the ladder sings (measured on
            // the 1 and 2 pole outputs' odd harmonics; not at the top of the range)
            static const float cx[] { 70.0f, 85.0f, 100.0f };
            static const float cy[] { 1.0f, 0.5f, 0.0f };
            voice.ladder.sat1 = juce::jmin (1.0f, curves::analogSingWeight (cutByte1, resByte1)) * curves::lookup (cx, cy, cutByte1);
        }
        voice.loss1Ladder = (1.0f + voice.k1Ladder) * juce::Decibels::decibelsToGain (curves::analogLevelDb (cutByte1, resByte1)
                                                                                    + (analog1 ? curves::analogTapBoostDb (poles1, cutByte1, resByte1) : 0.0f));
        voice.res1Gain = curves::modeGain (b.mode1, resByte1);
    }
    if (std::abs (cutByte2 - voice.lastCut2) > 0.01f || resByte2 != voice.lastRes2)
    {
        voice.lastCut2 = cutByte2; voice.lastRes2 = resByte2;
        // Filter 2 is the same filter as filter 1 (measured: identical cutoff, Q and level).
        const float q = curves::resoQ (resByte2) * curves::baseQ (cutByte2) / 0.629f;
        voice.c2 = Svf::make (curves::cutoffHz (cutByte2), q, sampleRate);
        voice.res2Gain = curves::modeGain (b.mode2, resByte2);
    }

    // The reference takes an opened-up filter 2 out of the circuit; filter 1 keeps
    // its resonance (and the level that costs) even at the top.
    const float open1 = b.mode1 == 0 && resByte1 < 0.5f ? clamp01 ((cutByte1 - 120.0f) / 7.0f) : 0.0f;
    const float open2 = b.mode2 == 0 ? clamp01 ((cutByte2 - 120.0f) / 7.0f) : 0.0f;

    const auto filter1Run = [&] (float x, int ch)
    {
        if (analog1)
        {
            // Analog modes: resonance from feedback round the ladder, and a
            // stronger pass-band loss than the digital filter (about 13 dB at
            // 64 against 8.3 - measured), the ladder's own loss undone first.
            return voice.ladder.process (x * voice.duck1Ladder, voice.g1Ladder, voice.k1Ladder, poles1, ch)
                   * (poles1 >= 2 ? 1.68f : 1.16f) * voice.loss1Ladder;
        }
        float y = voice.f1a.process (x, voice.c1a, b.mode1, ch);
        if (serial6)
            y = voice.f1b.process (y, voice.c1b, b.mode1, ch);
        y *= voice.res1Gain;
        // A low pass opened all the way is out of the circuit (the reference switches
        // unused filters off) - faded in over the last few bytes.
        return open1 > 0.0f ? y + (x - y) * open1 : y;
    };
    const auto filter2Run = [&] (float x, int ch)
    {
        const float y = voice.f2.process (x, voice.c2, b.mode2, ch) * voice.res2Gain;
        return open2 > 0.0f ? y + (x - y) * open2 : y;
    };

    // Saturation after filter 1: level matched for curves 1..6 (the drive
    // changes tone, not loudness), the TI curves as the native engine has them.
    const float noteHz = voice.memo.noteHz.get (voice.glideNote, [] (float n) { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); });
    // An Analog filter mode replaces the saturation stage (the reference sets its
    // saturation to the ladder's own whenever filter 1 is analog).
    const bool satOn = b.satCurve > 0 && ! analog1;
    const float drive = b.satCurve >= 1 && b.satCurve <= 6 && m (Dest::OscVolume) != 0.0f
                          ? curves::satDrive (b.satCurve, ovByte) : b.satDrive;
    const float makeup = drive != b.satDrive ? curves::satMakeup (b.satCurve, drive) * curves::satTrim (b.satCurve, ovByte) : b.satMakeup;
    constexpr float kRef = 0.7071f;   // a full-scale saw's level per channel at this point
    const auto sat = [&] (float& l, float& r)
    {
        if (! satOn) return;
        if (b.satCurve == 7)
        {
            // Rectifier (measured on a saw, waveforms and harmonics): Osc Volume
            // blends the signal into its full-wave rectified self - nothing at
            // 64 and below, a half-wave rectifier at 96, full-wave (a saw turns
            // into a triangle) at 127 - and lifts it by (1 + blend), which keeps
            // the level within ~1.5 dB. The DC it makes is blocked. It keeps the
            // lower half: at 127 the output is -|x| (sample-matched, with the
            // oscillators the reference's way up).
            const float mix = clamp01 ((ovByte - 64.0f) / 63.0f);
            float* sides[2] = { &l, &r };
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = *sides[ch];
                const float y = ((1.0f - mix) * x - mix * std::abs (x)) * (1.0f + mix);
                const float hp = y - voice.satDcX[ch] + 0.9993f * voice.satDcY[ch];
                voice.satDcX[ch] = y; voice.satDcY[ch] = hp;
                *sides[ch] = hp;
            }
            return;
        }
        if (b.satCurve <= 6)
        {
            l = curves::satShape (b.satCurve, l / kRef * drive) * makeup * kRef;
            r = curves::satShape (b.satCurve, r / kRef * drive) * makeup * kRef;
        }
        else
        {
            const float amount = b.satCurve >= 8 ? ovByte / 127.0f : clamp01 ((ovByte - 64.0f) / 63.0f) * 0.5f + 0.5f;
            l = saturate (b.satCurve, l, 0, amount, noteHz, sampleRate, voice.satHold, voice.satCount, voice.satAcc, voice.satFilter) * makeup;
            r = saturate (b.satCurve, r, 1, amount, noteHz, sampleRate, voice.satHold, voice.satCount, voice.satAcc, voice.satFilter) * makeup;
        }
    };

    const float fbal = clamp01 (b.filterBalance + m (Dest::FilterBalance));
    float yL, yR;

    if (b.routing <= 1)   // Serial 4 / Serial 6
    {
        float s1L = filter1Run (aL + bL, 0), s1R = filter1Run (aR + bR, 1);
        sat (s1L, s1R);

        // Balance: -64 filter 1 (+ saturation) alone, 0 both in series, +63
        // filter 2 alone - each end blending in as the knob comes back.
        // (byte 64 is exactly the centre: nothing leaks past the filters)
        const float fb = fbal * 127.0f;
        const float t = fb >= 64.0f ? juce::jmin (1.0f, (fb - 64.0f) / 63.0f) : juce::jmax (-1.0f, (fb - 64.0f) / 64.0f);
        if (t <= 0.0f)
        {
            const float w = std::pow (-t, 1.4f);
            yL = filter2Run (s1L, 0) * (1.0f - w) + s1L * w;
            yR = filter2Run (s1R, 1) * (1.0f - w) + s1R * w;
        }
        else
        {
            const float w = std::pow (t, 1.4f);
            yL = filter2Run (s1L * (1.0f - w) + (aL + bL) * w, 0);
            yR = filter2Run (s1R * (1.0f - w) + (aR + bR) * w, 1);
        }
    }
    else
    {
        const bool split = b.routing == 3;
        float p1L = filter1Run (split ? aL : aL + bL, 0), p1R = filter1Run (split ? aR : aR + bR, 1);
        sat (p1L, p1R);
        const float p2L = filter2Run (split ? bL : aL + bL, 0), p2R = filter2Run (split ? bR : aR + bR, 1);

        const float fb = fbal * 127.0f;
        if (split)
        {
            // Split (measured): filter 1 plays on the left only, filter 2 on
            // the right only, each well above Parallel's level; Filter Balance
            // trades them with the oscillator balance law.
            yL = p1L * curves::balanceGain1 (fb) * kSplitGain;
            yR = p2R * curves::balanceGain2 (fb) * kSplitGain;
        }
        else
        {
            // Parallel: an equal-power balance of the two outputs.
            const float pb = fb >= 64.0f ? 0.5f + 0.5f * juce::jmin (1.0f, (fb - 64.0f) / 63.0f) : 0.5f * juce::jmax (0.0f, fb / 64.0f);
            const float w1 = std::cos (pb * juce::MathConstants<float>::halfPi) * 0.7071f;
            const float w2 = std::sin (pb * juce::MathConstants<float>::halfPi) * 0.7071f;
            yL = p1L * w1 + p2L * w2;
            yR = p1R * w1 + p2R * w2;
        }
    }

    //-- amplifier
    const float velGain = curves::velocityGain (b.ampVel, vel);


    // Patch Volume as a matrix destination moves the volume knob itself (a full
    // amount swings it +-63 bytes), through the knob's own steep curve - so a
    // tremolo on it is deep and lopsided, and clips at the top (measured).
    // (memo key: the knob position the modulation lands on)
    const float volMod = m (Dest::PatchVolume) == 0.0f ? 1.0f
        : voice.memo.volMod.get (b.patchVolumeByte + 127.0f * m (Dest::PatchVolume), [pv = b.patchVolumeByte] (float at)
          {
              const float base = curves::patchVolumeGain (pv);
              return base > 0.0f ? curves::patchVolumeGain (at) / base : 0.0f;
          });
    const float gain = ampEnv * velGain * volMod * b.unisonGain;

    const float pan = juce::jlimit (-1.0f, 1.0f, b.pan + 2.0f * m (Dest::Panorama) + b.panVel * vel
                                                 + lfo[1] * curve (amt (b.lfo2Pan, Dest::Lfo2Pan)));
    // Panorama (measured): a linear law that clips - each side's gain is
    // (1 -+ pan) against the centre, at most +3.6 dB, so the near side is
    // already full a quarter of the way out and the far side falls straight
    // to nothing. A Split voice is two sources, filter 1 hard left and filter 2
    // hard right: Panorama moves both (at -50 % filter 2 sits in the middle,
    // at -100 % both are left), each a little lower in the middle (-6.6 dB).
    if (b.routing == 3)
    {
        const auto place = [] (float pos) { return std::make_pair (juce::jmin (0.94f, 0.47f * (1.0f - pos)),
                                                                    juce::jmin (0.94f, 0.47f * (1.0f + pos))); };
        const auto g1 = voice.memo.panA.get (pan, [&place] (float pp) { return place (juce::jlimit (-1.0f, 1.0f, -1.0f + 2.0f * pp)); });
        const auto g2 = voice.memo.panB.get (pan, [&place] (float pp) { return place (juce::jlimit (-1.0f, 1.0f, 1.0f + 2.0f * pp)); });
        constexpr float k = 1.0f / 0.94f;   // a source hard to its own side keeps its level
        outL = (yL * g1.first + yR * g2.first) * gain * k;
        outR = (yL * g1.second + yR * g2.second) * gain * k;
    }
    else
    {
        const auto panGains = voice.memo.pan.get (pan, [] (float pp) { return std::make_pair (juce::jmin (1.513f, 1.0f - pp),
                                                                                                juce::jmin (1.513f, 1.0f + pp)); });
        outL = yL * gain * panGains.first;
        outR = yR * gain * panGains.second;
    }

    voice.prevMod = mod;
}


//==============================================================================
// The Filter Bank: one Frequency, one Resonance and one Mix knob, and a Type
// that decides what they drive.
//==============================================================================
void Engine::processFilterBank (FxChain& c, const Block& b, float& l, float& r, float freqMod, float resoMod)
{
    const float target = clamp01 (b.bankFreq + freqMod);
    c.smoothedBankFreq = c.smoothedBankFreq < 0.0f ? target : c.smoothedBankFreq + (target - c.smoothedBankFreq) * 0.002f;
    const float f = c.smoothedBankFreq;
    const float reso = clamp01 (b.bankReso + resoMod);
    const float dryL = l, dryR = r;
    float wetL = l, wetR = r;

    switch (b.bankType)
    {
        case 1:   // Ring Modulator: a sine carrier from 20 Hz to 20 kHz
        {
            const double hz = 20.0 * std::exp2 ((double) f * 9.97);
            c.bankRingPhase += hz / sampleRate;
            c.bankRingPhase -= std::floor (c.bankRingPhase);
            const double ph = c.bankRingPhase * juce::MathConstants<double>::twoPi;
            wetL = l * (float) std::sin (ph);
            wetR = r * (float) std::sin (ph + (double) b.bankStereo * juce::MathConstants<double>::pi);
            break;
        }

        case 2:   // Frequency Shifter: centre is no shift, either side up to 2.5 kHz
        {
            const float x = (f - 0.5f) * 2.0f;
            c.freqShifter.setShift (x * std::abs (x) * 2500.0f);
            c.freqShifter.process (wetL, wetR);
            break;
        }

        case 3:   // Vowel Filter
            c.vowelBank->setParameterAt (vowelPos, f * 100.0f);
            in[0] = { l, r };
            c.vowelBank->processSample (in.data(), out.data());
            wetL = out[0][0]; wetR = out[0][1];
            break;

        case 4:   // Comb Filter: 20 Hz .. 8 kHz
            c.combBank->setParameterAt (combFreq, 20.0f * std::exp2 (f * 8.64f));
            in[0] = { l, r };
            c.combBank->processSample (in.data(), out.data());
            wetL = out[0][0]; wetR = out[0][1];
            break;

        default:  // 5..8 XFade (1, 2, 4, 6 poles), 9..11 VariSlope LP / HP / BP
        {
            const double sr = sampleRate;
            const float hzL = c.memo.bankHz.get (f, scaling::cutoffHz);
            const float hzR = hzL * c.memo.bankStereo.get (b.bankStereo, [] (float x) { return std::exp2 (x); });
            const float GL = c.memo.bankGL.get (hzL, [sr] (float hz) { return OnePole::coef (hz, sr); });
            const float GR = c.memo.bankGR.get (hzR, [sr] (float hz) { return OnePole::coef (hz, sr); });

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
                    lp = c.bankStages[(size_t) s].lp (lp, G, ch);
                    hp = hp - c.bankStagesHp[(size_t) s].lp (hp, G, ch);
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
                else                       y = c.bankBpLp.lp (hp, G, ch);         // BP: high pass into low pass

                (ch == 0 ? wetL : wetR) = y;
            }

            // Resonance: a band pass peak at the cutoff on top of the slope.
            if (reso > 0.01f)
            {
                if (std::abs (hzL - c.lastPeakHz) > hzL * 0.01f || std::abs (reso - c.lastPeakReso) > 0.01f)
                {
                    c.bankPeak.set (Biquad::Type::BandPass, sampleRate, hzL, 0.7f + reso * 12.0f, 0.0f);
                    c.lastPeakHz = hzL; c.lastPeakReso = reso;
                }
                wetL += c.bankPeak.process (l, 0) * reso * 2.0f;
                wetR += c.bankPeak.process (r, 1) * reso * 2.0f;
            }
            break;
        }
    }

    l = dryL + (wetL - dryL) * b.bankMix;
    r = dryR + (wetR - dryR) * b.bankMix;
}


//==============================================================================
// One effects rack, one sample. The base chain also takes the audio input's
// straight-through path, which must only ever be heard once.
//==============================================================================
void Engine::processChain (FxChain& c, bool isBase, float& l, float& r)
{
    const auto& b = c.settings->b;
    const auto& fx = c.settings->fx;

    // Global destinations follow the newest voice's matrix in this chain.
    const ModArray* gm = c.lastVoice >= 0 && voices[(size_t) c.lastVoice].active
                           ? &voices[(size_t) c.lastVoice].prevMod : nullptr;
    const auto g = [gm] (Dest d) { return gm != nullptr ? (*gm)[(size_t) d] : 0.0f; };

    //-- Input section
    if (isBase && base().b.inputMode == 2) { l += inputL; r += inputR; }   // Static: straight in
    if (b.inputRing > 0.0f)
    {
        const float mono = (inputL + inputR) * 0.5f;
        l += (l * mono * 2.0f - l) * b.inputRing;
        r += (r * mono * 2.0f - r) * b.inputRing;
    }

    const auto run = [this] (SynthModule& mdl, float& a, float& z)
    {
        in[0] = { a, z };
        mdl.processSample (in.data(), out.data());
        a = out[0][0]; z = out[0][1];
    };

    //-- Distortion
    const float distDriveNow = clamp01 (b.distDrive + g (Dest::DistIntensity));
    if (b.distCurve >= 1 && b.distCurve <= 11)
    {
        // The original curves, measured: before Patch Volume (so the input is
        // brought back to voice level), each a fitted shaper or filter.
        const float intensity = distDriveNow * 127.0f;
        const float toVoice = 1.0f / (juce::jmax (1.0e-6f, c.inputGain) * curves::kDistRef);
        const float fromVoice = 1.0f / toVoice;
        float dl = l, dr = r;
        switch (b.distCurve)
        {
            case 1: case 2: case 3: case 4: case 5: case 6:
            {
                float drive, gain; curves::distShaper (b.distCurve, intensity, drive, gain);
                dl = curves::distShape (b.distCurve, l * toVoice * drive) * gain * fromVoice;
                dr = curves::distShape (b.distCurve, r * toVoice * drive) * gain * fromVoice;
                break;
            }
            case 7:   // Rectifier (DC removed after the fold)
            {
                const float cpt = curves::distFoldPoint (intensity), mk = curves::distFoldMakeup (cpt);
                float* sides[2] = { &dl, &dr };
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float y = (std::abs ((ch == 0 ? l : r) * toVoice - cpt) - cpt) * mk;
                    const float hp = y - c.distDcX[ch] + 0.9993f * c.distDcY[ch];
                    c.distDcX[ch] = y; c.distDcY[ch] = hp;
                    *sides[ch] = -hp * fromVoice;
                }
                break;
            }
            case 8:   // Bit Reducer
            {
                const float steps = curves::distSteps (intensity);
                const float mk = juce::Decibels::decibelsToGain (intensity > 100.0f ? 1.4f * (intensity - 100.0f) / 27.0f : 0.2f);
                dl = std::round (l * toVoice * steps) / steps * mk * fromVoice;
                dr = std::round (r * toVoice * steps) / steps * mk * fromVoice;
                break;
            }
            case 9:   // Rate Reducer
            {
                const int hold = curves::distHold (intensity);
                if (++c.distHoldCount >= hold) { c.distHoldCount = 0; c.distHold[0] = l; c.distHold[1] = r; }
                dl = hold > 1 ? c.distHold[0] : l;
                dr = hold > 1 ? c.distHold[1] : r;
                break;
            }
            case 10: case 11:   // Low Pass / High Pass
            {
                if (intensity > 0.0f)
                {
                    const bool hp = b.distCurve == 11;
                    const float hz = hp ? curves::distHighPassHz (intensity) : curves::distLowPassHz (intensity);
                    const float G = OnePole::coef (hz, sampleRate);
                    const float lpL = c.distFilter.lp (l, G, 0), lpR = c.distFilter.lp (r, G, 1);
                    const float loss = hp ? juce::Decibels::decibelsToGain (curves::distHighPassLossDb (intensity)) : 1.0f;
                    dl = hp ? (l - lpL) * loss : lpL;
                    dr = hp ? (r - lpR) * loss : lpR;
                }
                break;
            }
            default: break;
        }
        l += (dl - l) * b.distMix;   // Mix blends clean and distorted
        r += (dr - r) * b.distMix;
    }
    else if (b.distCurve > 11 && distDriveNow > 0.0f)
    {
        // The later curves keep the module, level matched with a slow follower.
        const float drive = distDriveNow;
        c.distortion->setParameterAt (distDrive, 1.0f + 30.0f * drive * drive);
        float dl = c.distPre.process (l, 0), dr = c.distPre.process (r, 1);
        run (*c.distortion, dl, dr);
        if (fx.distToneOn)
        {
            dl = c.distPost.process (dl, 0);
            dr = c.distPost.process (dr, 1);
        }
        const float k = (float) (1.0 - std::exp (-1.0 / (0.05 * sampleRate)));
        c.distInPow += (0.5f * (l * l + r * r) - c.distInPow) * k;
        c.distOutPow += (0.5f * (dl * dl + dr * dr) - c.distOutPow) * k;
        const float trim = c.distOutPow > 1.0e-12f ? juce::jlimit (0.05f, 4.0f, std::sqrt (c.distInPow / c.distOutPow)) : 1.0f;
        l += (dl * trim - l) * b.distMix;
        r += (dr * trim - r) * b.distMix;
    }

    //-- Filter Bank
    if (b.bankType > 0 && b.bankMix > 0.0f)
        processFilterBank (c, b, l, r, g (Dest::BankFreq), g (Dest::BankReso));

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
        c.vocoder->processSample (in.data(), out.data());
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

        float cl = c.charB.process (c.charA.process (l, 0), 0);
        float cr = c.charB.process (c.charA.process (r, 1), 1);

        if (b.charType == 1 || b.charType == 2 || b.charType == 3 || b.charType == 8)
        {
            // the filtering types blend in with intensity
            cl = l + (cl - l) * a;
            cr = r + (cr - r) * a;
        }
        l = cl; r = cr;

        if (b.charType == 7)
            run (*c.stereoWidth, l, r);
    }

    l = c.voicing.process (l, 0);
    r = c.voicing.process (r, 1);

    //-- EQ
    if (b.eqOn)
    {
        l = c.eqHigh.process (c.eqMid.process (c.eqLow.process (l, 0), 0), 0);
        r = c.eqHigh.process (c.eqMid.process (c.eqLow.process (r, 1), 1), 1);
    }

    //-- Phaser
    const float phaserMix = clamp01 (b.phaserMix + g (Dest::PhaserMix));
    if (phaserMix > 0.0f)
    {
        c.phaserFx.process (l, r, phaserMix, fx.ph);
    }

    //-- Chorus
    const float chorusMix = clamp01 (b.chorusMix + g (Dest::ChorusMix));
    if (b.chorusType > 0 && chorusMix > 0.0f)
        c.chorusFx.process (l, r, sampleRate, fx.chRate, fx.chDelay, fx.chDepth * 0.01f, fx.chFeedback, chorusMix,
                            (int) fx.chLfoShape);

    //-- Delay (send). Keeps running for a while after the send closes so
    //   the echoes die away instead of stopping.
    const float delaySend = clamp01 (b.delaySend + g (Dest::DelaySend));
    if (b.delayMode > 0)
    {
        c.delayIdle = delaySend > 0.0f ? 0 : c.delayIdle + 1;
        if (c.delayIdle < (int) (sampleRate * 12.0))
        {
            // Original: the reference keeps the dry signal at full level and brings
            // the echoes up on their own curve (-16 dB at 32, -8 at 64, -1 at
            // 96, measured against the dry note); only at the very top (127)
            // does the dry signal go, leaving the echoes alone.
            const float sb = delaySend * 127.0f;
            static const float sendDb[5] = { -40.0f, -16.0f, -8.0f, -1.0f, 0.0f };
            const float pos = juce::jlimit (0.0f, 3.999f, sb / 32.0f);
            const int si = (int) pos;
            const float sendGain = sb < 0.5f ? 0.0f
                                 : juce::Decibels::decibelsToGain (sendDb[si] + (sendDb[si + 1] - sendDb[si]) * (pos - (float) si))
                                     * (sb < 32.0f ? sb / 32.0f : 1.0f);
            float dl, dr;
            c.delayFx.process (l * sendGain, r * sendGain, dl, dr, fx.dl);
            if (sb >= 126.5f) { l = 0.0f; r = 0.0f; }
            if (b.delayTape)
                run (*c.tapeWobble, dl, dr);
            l += dl; r += dr;
        }
    }

    //-- Reverb (send). Measured: the send rises on a (send)^1.33 law, and at the
    //   top of the send the dry signal goes, as with the delay.
    const float reverbSend = clamp01 (b.reverbSend + g (Dest::ReverbSend));
    if (b.reverbOn)
    {
        c.reverbIdle = reverbSend > 0.0f ? 0 : c.reverbIdle + 1;
        if (c.reverbIdle < (int) (sampleRate * 30.0))
        {
            const float sg = curves::reverbSendGain (reverbSend) * fx.rvLevel * kReverbReturn;
            float wl, wr;
            c.reverbFx.process (l * sg, r * sg, wl, wr, fx.rvRt60, fx.rvPreMs, fx.rvColourG, fx.rvColourMode);
            wl = c.rvHigh.process (c.rvMid.process (c.rvLow.process (wl, 0), 0), 0);
            wr = c.rvHigh.process (c.rvMid.process (c.rvLow.process (wr, 1), 1), 1);
            if (reverbSend * 127.0f >= 126.5f) { l = 0.0f; r = 0.0f; }
            l += wl; r += wr;
        }
    }

    // Granulator. Like the sends, it keeps running for a while after the
    // mix is closed so its last grains ring out instead of stopping dead.
    if (b.granMix > 0.0f || c.granIdle < (int) (sampleRate * 4.0))
    {
        c.granIdle = b.granMix > 0.0f ? 0 : c.granIdle + 1;
        float gl = l, gr = r;
        run (*c.granulator, gl, gr);
        l += (gl - l) * b.granMix;
        r += (gr - r) * b.granMix;
    }
}

//==============================================================================
void Engine::renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (! bound)
    {
        buffer.clear();
        return;
    }

    beginBlock();
    chooseWaveTables();

    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get(), noise.get(), filter1.get(), filter2.get(),
                     envFollow.get() })
        m->blockStart();

    const int nc = numChains.load (std::memory_order_acquire);
    for (int c = 0; c < nc; ++c)
    {
        auto& ch = *chains[(size_t) c];
        if (ch.state.load (std::memory_order_relaxed) != FxChain::InUse)
            continue;
        for (auto* m : { ch.distortion.get(), ch.phaser.get(), ch.chorus.get(), ch.reverb.get(),
                         ch.tapeWobble.get(), ch.vowelBank.get(), ch.combBank.get(), ch.vocoder.get(),
                         ch.stereoWidth.get(), ch.granulator.get() })
            m->blockStart();
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    const int numSamples = buffer.getNumSamples();

    const Block& gb = base().b;
    const float chainGain = controllers[7] * 0.35f;
    const float balanceTilt = (controllers[8] - 0.5f) * 2.0f;
    const bool arpRunning = gb.arpMode > 0;
    const float fadeTotal = (float) juce::jmax (1, (int) (sampleRate * 0.01));
    ModArray scratch {};

    const auto renderOne = [&] (int index)
    {
        // Audio input, for the Input section, vocoder, follower and FM.
        const float rawL = left[index], rawR = right[index];
        switch (gb.inputSelect)
        {
            case 0:  inputL = inputR = rawL; break;
            case 2:  inputL = inputR = rawR; break;
            default: inputL = rawL; inputR = rawR; break;
        }

        if (gb.followerMode > 0)
        {
            const float x = gb.followerMode == 1 ? rawL : (gb.followerMode == 3 ? rawR : (rawL + rawR) * 0.5f);
            in[0] = { x, x };
            envFollow->setInputConnected (0, true);
            envFollow->processSample (in.data(), out.data());
            followerValue = out[0][0];
        }

        for (auto* l : lfoTyped)
            l->advanceShared();

        if (arpRunning)
            arpAdvance();

        const int numC = numChains.load (std::memory_order_acquire);   // a rack made this block is visible complete
        for (int c = 0; c < numC; ++c)
        {
            auto& ch = *chains[(size_t) c];
            if (ch.state.load (std::memory_order_relaxed) == FxChain::InUse)
                ch.busL = ch.busR = ch.otherL = ch.otherR = 0.0f;
        }

        if (orderDirty)
            rebuildOrder();

        // Voices, grouped by layer: the shared modules are re-pointed only
        // when the layer changes.
        for (int k = 0; k < numOrdered; ++k)
        {
            const int v = order[(size_t) k];
            auto& voice = voices[(size_t) v];
            if (! voice.active)
                continue;

            const auto& L = states[(size_t) voice.slot];
            if (appliedSlot != voice.slot)
            {
                applyVoiceModules (L);
                appliedSlot = voice.slot;
            }

            float vl, vr;
            renderVoice (v, L, vl, vr, scratch);

            // Patch Volume: voices at the chain's own volume are scaled with
            // the rack's input gain exactly as before; a layer with a volume
            // of its own is scaled on the way in.
            auto& ch = *chains[(size_t) voice.chain];
            if (ch.settings == nullptr || L.b.patchVolume == ch.settings->b.patchVolume)
            {
                ch.busL += vl;
                ch.busR += vr;
            }
            else
            {
                ch.otherL += vl * L.b.patchVolume;
                ch.otherR += vr * L.b.patchVolume;
            }

            if (! voice.held && ! voice.sustained && ampEnvTyped->isIdle (v))
                voice.active = false;
        }

        // Every running effects rack, then the sum.
        float mainL = 0.0f, mainR = 0.0f, secL = 0.0f, secR = 0.0f;

        for (int c = 0; c < numC; ++c)
        {
            auto& ch = *chains[(size_t) c];
            if (ch.state.load (std::memory_order_relaxed) != FxChain::InUse || ch.settings == nullptr)
                continue;

            const float patchGain = ch.settings->b.patchVolume * controllers[7] * 0.35f;   // 0.35: headroom for chords
            ch.inputGain = patchGain;
            float l = ch.busL * patchGain + ch.otherL * chainGain;
            float r = ch.busR * patchGain + ch.otherR * chainGain;
            processChain (ch, c == 0, l, r);

            if (ch.fadeLeft > 0)
            {
                const float fade = (float) ch.fadeLeft / fadeTotal;
                l *= fade;
                r *= fade;
                if (--ch.fadeLeft == 0)
                {
                    ch.ownerUid = -1;
                    ch.state.store (FxChain::Dirty, std::memory_order_release);
                }
            }

            // Silence, measured around any DC the rack leaves behind.
            ch.dcL += (l - ch.dcL) * 0.0005f;
            ch.dcR += (r - ch.dcR) * 0.0005f;
            if (std::abs (l - ch.dcL) < 1.0e-5f && std::abs (r - ch.dcR) < 1.0e-5f)
                ++ch.silentSamples;
            else
                ch.silentSamples = 0;

            if (secondOut != nullptr)
            {
                const float so = ch.settings->b.secondOut;
                mainL += l * (1.0f - so); mainR += r * (1.0f - so);
                secL += l * so;           secR += r * so;
            }
            else
            {
                mainL += l; mainR += r;
            }
        }

        // AC couple: a one-pole highpass at 12.6 Hz (measured on a slow
        // square - the reference's flat tops droop at exactly that rate).
        const auto dcBlock = [dcR = dcCoef] (float x, float& xs, float& ys)
        {
            const float y = x - xs + dcR * ys;
            xs = x;
            ys = y;
            return y;
        };

        // Output stage: linear to -2 dB, then a gentle knee that never
        // passes full scale.
        const auto softLimit = [] (float x)
        {
            constexpr float t = 0.8f;
            if (x > t)  return t + (1.0f - t) * std::tanh ((x - t) / (1.0f - t));
            if (x < -t) return -t + (1.0f - t) * std::tanh ((x + t) / (1.0f - t));
            return x;
        };

        float l = softLimit (dcBlock (mainL, dcX[0], dcY[0]));   // last, so nothing after it can push past full scale
        float r = softLimit (dcBlock (mainR, dcX[1], dcY[1]));

        if (secondOut != nullptr)
        {
            secondOut[0][index] = softLimit (dcBlock (secL, dcX2[0], dcY2[0]));
            secondOut[1][index] = softLimit (dcBlock (secR, dcX2[1], dcY2[1]));
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

    sampleClock += numSamples;
}

} // namespace original
} // namespace aquavibrio
