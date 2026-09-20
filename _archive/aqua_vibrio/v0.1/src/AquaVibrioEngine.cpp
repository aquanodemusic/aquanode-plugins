#include "AquaVibrioEngine.h"

namespace aquavibrio
{

using namespace aquanode;

//==============================================================================
// Every module the engine owns, in no particular order - used wherever the
// same call has to reach all of them.
std::array<SynthModule*, 31> Engine::allModules()
{
    return { osc1.get(), osc2.get(), osc3.get(),
             subOsc.get(), noise.get(), ringMod.get(),
             filter1.get(), filter2.get(),
             envFilter.get(), envAmp.get(), env3.get(), env4.get(),
             lfo1.get(), lfo2.get(), lfo3.get(), arp.get(),
             distortion.get(), eq.get(), phaser.get(), chorus.get(),
             delay.get(), reverb.get(),
             filterBank.get(), combBank.get(), vocoder.get(),
             tapeWobble.get(), bankRingMod.get(), bankFilter.get(),
             envFollow.get(), stereoWidth.get(), characterShaper.get() };
}

//==============================================================================
// Matrix tables, built by name so a patch byte always means what the panel
// says it means.
void Engine::buildMatrixTables()
{
    destForByte.assign (128, Dest::None);
    srcForByte.assign (128, Src::Off);

    const auto& dests = choicesFor ("modmatrixDest");
    const auto& sources = choicesFor ("modmatrixSource");

    for (int i = 0; i < juce::jmin (128, dests.size()); ++i)
    {
        const auto& n = dests[i];

        const auto set = [&] (Dest d) { destForByte[(size_t) i] = d; };

        if      (n == "Patch Volume")        set (Dest::PatchVolume);
        else if (n == "Panorama")            set (Dest::Panorama);
        else if (n == "Osc Balance")         set (Dest::OscBalance);
        else if (n == "Osc Volume")          set (Dest::OscVolume);
        else if (n == "Sub Osc Volume")      set (Dest::SubVolume);
        else if (n == "Noise Volume")        set (Dest::NoiseVolume);
        else if (n.containsIgnoreCase ("Ringmod") || n.containsIgnoreCase ("Ring Mod"))
                                             set (Dest::RingModVolume);
        else if (n.startsWith ("Osc 3 Volume")) set (Dest::Osc3Volume);
        else if (n == "Osc 1 Pitch")         set (Dest::Osc1Pitch);
        else if (n == "Osc 2 Pitch")         set (Dest::Osc2Pitch);
        else if (n == "Osc 2 Detune")        set (Dest::Osc2Detune);
        else if (n.containsIgnoreCase ("Osc 1+2 Pitch") || n == "Osc Pitch")
                                             set (Dest::OscPitch);
        else if (n.startsWith ("Osc 1 Shape") || n == "Osc 1 Pulse Width")
                                             set (Dest::Osc1Shape);
        else if (n.startsWith ("Osc 2 Shape") || n == "Osc 2 Pulse Width")
                                             set (Dest::Osc2Shape);
        else if (n == "Filter 1 Cutoff")     set (Dest::Cutoff1);
        else if (n == "Filter 2 Cutoff")     set (Dest::Cutoff2);
        else if (n == "Filter 1 Resonance")  set (Dest::Reso1);
        else if (n == "Filter 2 Resonance")  set (Dest::Reso2);
        else if (n.containsIgnoreCase ("Filter Balance")) set (Dest::FilterBalance);
        else if (n.containsIgnoreCase ("Filter1 Env") || n == "Filter 1 Env Amount")
                                             set (Dest::Filter1EnvAmt);
        else if (n.containsIgnoreCase ("Filter2 Env") || n == "Filter 2 Env Amount")
                                             set (Dest::Filter2EnvAmt);
        else if (n == "LFO 1 Rate")          set (Dest::Lfo1Rate);
        else if (n == "LFO 2 Rate")          set (Dest::Lfo2Rate);
        else if (n == "LFO 3 Rate")          set (Dest::Lfo3Rate);
        else if (n.startsWith ("LFO 1") && n.containsIgnoreCase ("Amt")) set (Dest::Lfo1Amount);
        else if (n.startsWith ("LFO 2") && n.containsIgnoreCase ("Amt")) set (Dest::Lfo2Amount);
        else if (n.startsWith ("LFO 3") && n.containsIgnoreCase ("Amt")) set (Dest::Lfo3Amount);
        else if (n == "Amp Env Attack")      set (Dest::AmpAttack);
        else if (n == "Amp Env Decay")       set (Dest::AmpDecay);
        else if (n == "Amp Env Sustain")     set (Dest::AmpSustain);
        else if (n == "Amp Env Release")     set (Dest::AmpRelease);
        else if (n == "Filter Env Attack")   set (Dest::FilterAttack);
        else if (n == "Filter Env Decay")    set (Dest::FilterDecay);
        else if (n == "Filter Env Sustain")  set (Dest::FilterSustain);
        else if (n == "Filter Env Release")  set (Dest::FilterRelease);
    }

    for (int i = 0; i < juce::jmin (128, sources.size()); ++i)
    {
        const auto& n = sources[i];

        const auto set = [&] (Src v) { srcForByte[(size_t) i] = v; };

        if      (n == "Pitch Bend")      set (Src::PitchBend);
        else if (n == "Chan Pressure")   set (Src::ChanPressure);
        else if (n == "Mod Wheel")       set (Src::ModWheel);
        else if (n == "Breath")          set (Src::Breath);
        else if (n == "Controller 3")    set (Src::Controller3);
        else if (n == "Foot Pedal")      set (Src::FootPedal);
        else if (n == "Data Entry")      set (Src::DataEntry);
        else if (n == "Balance")         set (Src::Balance);
        else if (n == "Controller 9")    set (Src::Controller9);
        else if (n == "Expression")      set (Src::Expression);
        else if (n == "Controller 12")   set (Src::Controller12);
        else if (n == "Controller 13")   set (Src::Controller13);
        else if (n == "Controller 14")   set (Src::Controller14);
        else if (n == "Controller 15")   set (Src::Controller15);
        else if (n == "Controller 16")   set (Src::Controller16);
        else if (n == "Hold Pedal")      set (Src::HoldPedal);
        else if (n == "Portamento Sw")   set (Src::PortamentoSwitch);
        else if (n == "Sost Pedal")      set (Src::SostenutoPedal);
        else if (n == "Amp Envelope")    set (Src::AmpEnv);
        else if (n == "Filter Envelope") set (Src::FilterEnv);
        else if (n == "Envelope 3")      set (Src::Env3);
        else if (n == "Envelope 4")      set (Src::Env4);
        else if (n == "LFO 1 bipolar")   set (Src::Lfo1Bi);
        else if (n == "LFO 2 bipolar")   set (Src::Lfo2Bi);
        else if (n == "LFO 3 bipolar")   set (Src::Lfo3Bi);
        else if (n == "LFO 1 unipolar")  set (Src::Lfo1Uni);
        else if (n == "LFO 2 unipolar")  set (Src::Lfo2Uni);
        else if (n == "LFO 3 unipolar")  set (Src::Lfo3Uni);
        else if (n == "Velocity On")     set (Src::VelocityOn);
        else if (n == "Velocity Off")    set (Src::VelocityOff);
        else if (n == "Key Follow")      set (Src::KeyFollow);
        else if (n == "Random")          set (Src::RandomPerNote);
        else if (n == "1% constant")     set (Src::Const1);
        else if (n == "10% constant")    set (Src::Const10);
        // Arp Input and the four AnaKey sources are TI hardware-only and have
        // nothing to read from here; they stay Off.
    }
}

// The LFOs have their own destination list, shorter than the matrix's and
// numbered differently. Same approach: match by name, so the bytes in a patch
// mean what the panel says.
void Engine::buildLfoDestTable()
{
    lfoDestForByte.assign (128, Dest::None);

    const auto& names = choicesFor ("lfoDest");

    for (int i = 0; i < juce::jmin (128, names.size()); ++i)
    {
        const auto& n = names[i];
        const auto set = [&] (Dest d) { lfoDestForByte[(size_t) i] = d; };

        if      (n.containsIgnoreCase ("Osc1+2 Pitch") || n == "Osc Pitch") set (Dest::OscPitch);
        else if (n.containsIgnoreCase ("Osc 1 Pitch") || n == "Osc1 Pitch") set (Dest::Osc1Pitch);
        else if (n.containsIgnoreCase ("Osc 2 Pitch") || n == "Osc2 Pitch") set (Dest::Osc2Pitch);
        else if (n.containsIgnoreCase ("Osc Balance"))   set (Dest::OscBalance);
        else if (n.containsIgnoreCase ("Sub"))           set (Dest::SubVolume);
        else if (n.containsIgnoreCase ("Filter1 Cutoff") || n == "Filter 1 Cutoff") set (Dest::Cutoff1);
        else if (n.containsIgnoreCase ("Filter2 Cutoff") || n == "Filter 2 Cutoff") set (Dest::Cutoff2);
        else if (n.containsIgnoreCase ("Filter1+2") || n.containsIgnoreCase ("Cutoff 1+2")) set (Dest::Cutoff1);
        else if (n.containsIgnoreCase ("Resonance"))     set (Dest::Reso1);
        else if (n.containsIgnoreCase ("Panorama") || n.containsIgnoreCase ("Pan")) set (Dest::Panorama);
        else if (n.containsIgnoreCase ("Volume"))        set (Dest::PatchVolume);
        else if (n.containsIgnoreCase ("Shape"))         set (Dest::Osc1Shape);
        else if (n.containsIgnoreCase ("FM"))            set (Dest::Osc2Shape);
    }
}

void Engine::readMatrix()
{
    const auto lookupDest = [this] (int byte)
    {
        return byte >= 0 && byte < (int) destForByte.size() ? destForByte[(size_t) byte] : Dest::None;
    };

    for (int slot = 0; slot < 6; ++slot)
    {
        const juce::String n (slot + 1);

        // The hardware's own naming is inconsistent here: slots 2 and 3 call
        // their first destination "Destination1", the others just
        // "Destination". Rather than encode that quirk, ask for both and take
        // whichever exists.
        const auto firstDest = hasBinding ("Assign" + n + "Destination1")
                                 ? "Assign" + n + "Destination1"
                                 : "Assign" + n + "Destination";
        const auto firstAmt  = hasBinding ("Assign" + n + "Amount1")
                                 ? "Assign" + n + "Amount1"
                                 : "Assign" + n + "Amount";

        const int srcByte = raw (("Assign" + n + "Source").toRawUTF8());

        matrix[(size_t) slot].source = srcByte >= 0 && srcByte < (int) srcForByte.size()
                                         ? srcForByte[(size_t) srcByte] : Src::Off;

        matrix[(size_t) slot].dest[0] = lookupDest (raw (juce::String (firstDest).toRawUTF8()));
        matrix[(size_t) slot].dest[1] = lookupDest (raw (("Assign" + n + "Destination2").toRawUTF8()));
        matrix[(size_t) slot].dest[2] = lookupDest (raw (("Assign" + n + "Destination3").toRawUTF8()));

        matrix[(size_t) slot].amount[0] = scaling::bipolar (raw (juce::String (firstAmt).toRawUTF8(), 64));
        matrix[(size_t) slot].amount[1] = scaling::bipolar (raw (("Assign" + n + "Amount2").toRawUTF8(), 64));
        matrix[(size_t) slot].amount[2] = scaling::bipolar (raw (("Assign" + n + "Amount3").toRawUTF8(), 64));
    }
}

// How deep is "full amount"? The hardware does not treat the matrix as a
// separate depth control: an assignment ADDS to the destination parameter's
// own byte, in that parameter's own units. An amount of +63 on Filter 1
// Cutoff shifts the cutoff byte by about +63, i.e. half the knob's travel;
// the same +63 on Osc 1 Pitch shifts the semitone byte by 63, which is why
// pitch assignments on a Virus need such small values to be musical. That is
// the model here: one unit of amount times one unit of source equals 64/127
// of the destination parameter's full range.
static constexpr float kMatrixFullScale = 64.0f / 127.0f;

// Soft Knobs are deliberately NOT applied here.
//
// A soft knob is a remote control for a parameter, not a modulator of it:
// turning it sets that parameter's value, the way moving the real knob would.
// Its stored value is the knob's resting position, which for most patches is
// 0 - so treating it as a bipolar offset subtracted full scale from whatever
// it pointed at. With Patch Volume as the destination, that silenced the
// patch outright, which is exactly what was happening to most of the bank.
//
// The three destinations and values still load and save; they just have no
// business in the audio path until the UI exposes them as knobs.

void Engine::applyMatrix (const std::array<float, (size_t) Src::Count>& sources, ModTargets& t) const
{
    for (const auto& slot : matrix)
    {
        if (slot.source == Src::Off)
            continue;

        const float value = sources[(size_t) slot.source];

        for (int d = 0; d < 3; ++d)
        {
            const float amount = value * slot.amount[(size_t) d] * kMatrixFullScale;

            if (amount == 0.0f)
                continue;

            switch (slot.dest[(size_t) d])
            {
                case Dest::PatchVolume:   t.patchVolume   += amount; break;
                case Dest::Panorama:      t.panorama      += amount; break;
                case Dest::OscBalance:    t.oscBalance    += amount; break;
                case Dest::OscVolume:     t.oscVolume     += amount; break;
                case Dest::SubVolume:     t.subVolume     += amount; break;
                case Dest::NoiseVolume:   t.noiseVolume   += amount; break;
                case Dest::RingModVolume: t.ringModVolume += amount; break;
                case Dest::Osc3Volume:    t.osc3Volume    += amount; break;
                case Dest::Osc1Pitch:     t.osc1Pitch     += amount; break;
                case Dest::Osc2Pitch:     t.osc2Pitch     += amount; break;
                case Dest::OscPitch:      t.osc1Pitch     += amount;
                                          t.osc2Pitch     += amount; break;
                case Dest::Osc2Detune:    t.osc2Detune    += amount; break;
                case Dest::Osc1Shape:     t.osc1Shape     += amount; break;
                case Dest::Osc2Shape:     t.osc2Shape     += amount; break;
                case Dest::Cutoff1:       t.cutoff1       += amount; break;
                case Dest::Cutoff2:       t.cutoff2       += amount; break;
                case Dest::Reso1:         t.reso1         += amount; break;
                case Dest::Reso2:         t.reso2         += amount; break;
                case Dest::FilterBalance: t.filterBalance += amount; break;
                case Dest::Filter1EnvAmt: t.filter1EnvAmt += amount; break;
                case Dest::Filter2EnvAmt: t.filter2EnvAmt += amount; break;
                case Dest::Lfo1Rate:      t.lfo1Rate      += amount; break;
                case Dest::Lfo2Rate:      t.lfo2Rate      += amount; break;
                case Dest::Lfo3Rate:      t.lfo3Rate      += amount; break;
                case Dest::Lfo1Amount:    t.lfo1Amount    += amount; break;
                case Dest::Lfo2Amount:    t.lfo2Amount    += amount; break;
                case Dest::Lfo3Amount:    t.lfo3Amount    += amount; break;
                case Dest::AmpAttack:     t.ampAttack     += amount; break;
                case Dest::AmpDecay:      t.ampDecay      += amount; break;
                case Dest::AmpSustain:    t.ampSustain    += amount; break;
                case Dest::AmpRelease:    t.ampRelease    += amount; break;
                case Dest::FilterAttack:  t.filterAttack  += amount; break;
                case Dest::FilterDecay:   t.filterDecay   += amount; break;
                case Dest::FilterSustain: t.filterSustain += amount; break;
                case Dest::FilterRelease: t.filterRelease += amount; break;
                case Dest::None:
                default: break;
            }
        }
    }
}

Engine::Engine()
{
    auto& factory = ModuleFactory::instance();

    // Sound sources
    // All three oscillator models now live in one module, so there is no
    // second instance to switch between - Mode does it.
    osc1 = factory.createInstance ("osc.virus");
    osc2 = factory.createInstance ("osc.virus");
    osc3 = factory.createInstance ("osc.virus");
    subOsc = factory.createInstance ("osc.basic");
    noise = factory.createInstance ("osc.noise");
    ringMod = factory.createInstance ("util.ringmod");

    // Shaping
    filter1 = factory.createInstance ("filter.aquafilter");
    filter2 = factory.createInstance ("filter.aquafilter");
    // Sustain Time needs an envelope with a sloped sustain.
    envFilter = factory.createInstance ("util.virusenv");
    envAmp = factory.createInstance ("util.virusenv");
    env3 = factory.createInstance ("util.virusenv");
    env4 = factory.createInstance ("util.virusenv");
    lfo1 = factory.createInstance ("util.viruslfo");
    lfo2 = factory.createInstance ("util.viruslfo");
    lfo3 = factory.createInstance ("util.viruslfo");
    arp = factory.createInstance ("util.arp");

    // Effects
    distortion = factory.createInstance ("fx.virusdistortion");
    eq = factory.createInstance ("filter.belleq");
    phaser = factory.createInstance ("fx.phaser");
    chorus = factory.createInstance ("fx.chorus");
    delay = factory.createInstance ("fx.adelaysr");
    reverb = factory.createInstance ("fx.reverb");
    tapeWobble = factory.createInstance ("fx.tapewobble");
    vocoder = factory.createInstance ("fx.vocode");

    // The Filter Bank is several effects behind one Type control
    filterBank = factory.createInstance ("filter.formant");
    combBank = factory.createInstance ("filter.combfilter");
    bankRingMod = factory.createInstance ("util.ringmod");
    bankFilter = factory.createInstance ("filter.svf");

    // Input follower and the Character section
    envFollow = factory.createInstance ("util.envfollow");
    stereoWidth = factory.createInstance ("fx.stereowidth");
    characterShaper = factory.createInstance ("fx.waveshaper");

    // If a module type failed to register - almost always a .cpp missing from
    // the build - every later call on it would be a null dereference in the
    // audio thread. Catch it here, right after construction and BEFORE any
    // of the modules below are dereferenced, instead of crashing deep inside
    // whichever paramIndex() call happens to run first.
    //
    // NOTE: this must NOT be a jassert - jassert/jassertfalse compile away
    // entirely in Release builds, which is exactly the kind of build that
    // was crashing with a bare access violation instead of ever hitting the
    // old check. juce::Logger and std::abort are active in every build
    // configuration, so this always fires and always names the culprit.
    struct ModuleCheck { SynthModule* module; const char* typeId; };
    const ModuleCheck moduleChecks[] = {
        { osc1.get(), "osc.virus" }, { osc2.get(), "osc.virus" }, { osc3.get(), "osc.virus" },
        { subOsc.get(), "osc.basic" }, { noise.get(), "osc.noise" }, { ringMod.get(), "util.ringmod" },
        { filter1.get(), "filter.aquafilter" }, { filter2.get(), "filter.aquafilter" },
        { envFilter.get(), "util.virusenv" }, { envAmp.get(), "util.virusenv" },
        { env3.get(), "util.virusenv" }, { env4.get(), "util.virusenv" },
        { lfo1.get(), "util.viruslfo" }, { lfo2.get(), "util.viruslfo" },
        { lfo3.get(), "util.viruslfo" }, { arp.get(), "util.arp" },
        { distortion.get(), "fx.virusdistortion" }, { eq.get(), "filter.belleq" },
        { phaser.get(), "fx.phaser" }, { chorus.get(), "fx.chorus" },
        { delay.get(), "fx.adelaysr" }, { reverb.get(), "fx.reverb" },
        { filterBank.get(), "filter.formant" }, { combBank.get(), "filter.combfilter" },
        { vocoder.get(), "fx.vocode" }, { tapeWobble.get(), "fx.tapewobble" },
        { bankRingMod.get(), "util.ringmod" }, { bankFilter.get(), "filter.svf" },
        { envFollow.get(), "util.envfollow" }, { stereoWidth.get(), "fx.stereowidth" },
        { characterShaper.get(), "fx.waveshaper" }
    };

    for (auto& c : moduleChecks)
    {
        if (c.module == nullptr)
        {
            juce::Logger::writeToLog (juce::String ("FATAL: ModuleFactory failed to create \"")
                                       + c.typeId + "\" - its .cpp is likely missing from the build "
                                       + "(stale/un-regenerated project, incremental build miss, or the "
                                       + "linker discarding its self-registration symbol). Aborting "
                                       + "instead of crashing with a null-pointer access violation.");
            jassertfalse;
            std::abort();
        }
    }

    // Resolve every parameter index once. After this the audio thread only
    // ever deals in integers.
    const auto oscIndices = [] (SynthModule& m)
    {
        return OscIndices { m.paramIndex ("volume"), m.paramIndex ("fmRatio"),
                            m.paramIndex ("waveform"), m.paramIndex ("unison"),
                            m.paramIndex ("detune"), m.paramIndex ("spread"),
                            m.paramIndex ("drift") };
    };

    const auto fltIndices = [] (SynthModule& m)
    {
        return FltIndices { m.paramIndex ("cutoff"), m.paramIndex ("resonance"),
                            m.paramIndex ("drive"), m.paramIndex ("modDepth"),
                            m.paramIndex ("type") };
    };

    osc1Idx = oscIndices (*osc1);
    osc2Idx = oscIndices (*osc2);
    flt1Idx = fltIndices (*filter1);
    flt2Idx = fltIndices (*filter2);

    envIdx = { envAmp->paramIndex ("attack"), envAmp->paramIndex ("decay"),
               envAmp->paramIndex ("sustain"), envAmp->paramIndex ("release") };

    lfoIdx = { lfo1->paramIndex ("rate"), lfo1->paramIndex ("shape"),
               lfo1->paramIndex ("symmetry"), lfo1->paramIndex ("keytrigger") };

    userActiveNotes.fill (-1);
}

void Engine::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    for (auto* m : allModules())
        if (m != nullptr)
            m->prepare (newSampleRate);

    freqShifter.prepare (newSampleRate);
    atomizer.prepare (newSampleRate);

    reset();
}

void Engine::reset()
{
    freqShifter.reset();
    atomizer.reset();

    userStep = 0;
    userStepCounter = 0.0;
    userActiveCount = 0;
    heldKeys.fill (false);

    for (int v = 0; v < kMaxVoices; ++v)
    {
        voices[(size_t) v] = {};

        for (auto* m : allModules())
            if (m != nullptr)
                m->voiceReset (v);
    }
}

void Engine::bindTo (juce::AudioProcessorValueTreeState& apvts)
{
    bindings.clear();
    for (const auto& p : allParameters())
        bindings[p.id] = { apvts.getRawParameterValue (p.id) };

    buildMatrixTables();
    buildLfoDestTable();
}

//==============================================================================
void Engine::updateParameters()
{
    c.osc1Wave    = raw ("Osc1WaveSelect", 2);
    c.osc1Shape   = raw ("Osc1Shape", 64);
    c.osc1Semi    = raw ("Osc1Semitone", 64);
    c.osc1Density = raw ("Osc1HypersawDensity");
    c.osc1Spread  = raw ("Osc1HypersawDetunespread");
    c.osc1Mode    = raw ("Osc1Mode");

    c.osc2Wave    = raw ("Osc2WaveSelect", 2);
    c.osc2Shape   = raw ("Osc2Shape", 64);
    c.osc2Semi    = raw ("Osc2Semitone", 64);
    c.osc2Detune  = raw ("Osc2Detune", 64);
    c.osc2Density = raw ("Osc2HypersawDensity");
    c.osc2Mode    = raw ("Osc2Mode");

    c.oscBalance  = raw ("OscBalance", 64);
    c.oscVolume   = raw ("OscMainvolume", 100);
    c.subVolume   = raw ("SuboscillatorVolume");
    c.noiseVolume = raw ("NoiseVolume");

    c.flt1Cut     = raw ("Cutoff", 127);
    c.flt1Res     = raw ("Filter1Resonance");
    c.flt1EnvAmt  = raw ("Filter1EnvAmt");
    c.flt1KeyFol  = raw ("Filter1Keyfollow", 64);
    c.flt1Mode    = raw ("Filter1Mode");

    c.flt2Cut     = raw ("Cutoff2", 64);
    c.flt2Res     = raw ("Filter2Resonance");
    c.flt2EnvAmt  = raw ("Filter2EnvAmt");
    c.flt2KeyFol  = raw ("Filter2Keyfollow", 64);
    c.flt2Mode    = raw ("Filter2Mode");

    c.filterRouting = raw ("FilterRouting");
    c.saturation    = raw ("SaturationCurve");

    c.envFA = raw ("FilterEnvAttack");
    c.envFD = raw ("FilterEnvDecay", 46);
    c.envFS = raw ("FilterEnvSustain", 64);
    c.envFR = raw ("FilterEnvRelease", 40);

    c.envAA = raw ("AmpEnvAttack");
    c.envAD = raw ("AmpEnvDecay", 127);
    c.envAS = raw ("AmpEnvSustain", 127);
    c.envAR = raw ("AmpEnvRelease", 40);

    c.lfo1Rate  = raw ("Lfo1Rate", 48);
    c.lfo1Shape = raw ("Lfo1Shape", 1);
    c.lfo1Amt   = raw ("Lfo1AssignAmount");
    c.lfo1Dest  = raw ("Lfo1AssignDest");
    c.lfo2Rate  = raw ("Lfo2Rate", 48);
    c.lfo2Shape = raw ("Lfo2Shape", 1);
    c.lfo2Amt   = raw ("Lfo2AssignAmount");
    c.lfo3Rate  = raw ("Lfo3Rate", 48);
    c.lfo3Shape = raw ("Lfo3Shape", 1);
    c.lfo3Amt   = raw ("OscLfo3Amount");

    c.unisonMode   = raw ("UnisonMode");
    c.unisonDetune = raw ("UnisonDetune");
    c.unisonSpread = raw ("UnisonPanSpread");

    c.patchVolume = raw ("PatchVolume", 100);
    c.panorama    = raw ("Panorama", 64);
    c.keyMode     = raw ("KeyMode");

    c.osc3Mode   = raw ("Osc3Mode");
    c.osc3Volume = raw ("Osc3Volume");
    c.osc3Semi   = raw ("Osc3Semitone", 64);
    c.osc3Detune = raw ("Osc3Detune", 64);
    c.noiseColor = raw ("NoiseColor", 64);
    c.ringModVolume = raw ("RingmodulatorVolume");

    c.distCurve     = raw ("DistortionCurve");
    c.distIntensity = raw ("DistortionIntensity");
    c.distMix       = raw ("PatchDistortionMix", 127);

    c.eqLowGain  = raw ("LoweqGain", 64);
    c.eqLowFreq  = raw ("LoweqFrequency", 20);
    c.eqMidGain  = raw ("MideqGain", 64);
    c.eqMidFreq  = raw ("MideqFrequency", 64);
    c.eqMidQ     = raw ("MideqQFactor", 64);
    c.eqHighGain = raw ("HigheqGain", 64);
    c.eqHighFreq = raw ("HigheqFrequency", 100);

    c.chorusMix      = raw ("ChorusMix");
    c.chorusRate     = raw ("ChorusRate", 40);
    c.chorusDepth    = raw ("ChorusDepth", 64);
    c.chorusDelay    = raw ("ChorusDelay", 40);
    c.chorusFeedback = raw ("ChorusFeedback", 64);

    c.phaserMix      = raw ("PhaserMix");
    c.phaserMode     = raw ("PhaserMode");
    c.phaserRate     = raw ("PhaserRate", 30);
    c.phaserDepth    = raw ("PhaserDepth", 64);
    c.phaserFreq     = raw ("PhaserFrequency", 64);
    c.phaserFeedback = raw ("PhaserFeedback", 64);

    c.delaySend     = raw ("DelaySend");
    c.delayTime     = raw ("DelayTime", 64);
    c.delayFeedback = raw ("DelayFeedback", 64);
    c.delayColor    = raw ("DelayColor", 64);
    c.delayMode     = raw ("DelayMode");
    c.delayClock    = raw ("DelayClock");

    c.reverbSend     = raw ("ReverbSend");
    c.reverbTime     = raw ("ReverbTime", 64);
    c.reverbDamping  = raw ("ReverbDamping", 64);
    c.reverbType     = raw ("ReverbType", 1);
    c.reverbPredelay = raw ("ReverbPredelay");

    c.arpMode       = raw ("ArpMode");
    c.arpPattern    = raw ("ArpPatternSelct");
    c.arpOctaves    = raw ("ArpOctaveRange");
    c.arpNoteLength = raw ("ArpNoteLength", 64);
    c.arpSwing      = raw ("ArpSwing", 64);
    c.arpClock      = raw ("ArpClock", 8);
    c.arpHold       = raw ("ArpHoldEnable");

    c.filterBankType = raw ("FilterBankType");
    c.filterBankFreq = raw ("FilterBankCombFrequency", 64);
    c.filterBankReso = raw ("FilterBankResonance", 64);
    c.filterBankMix  = raw ("FilterBankMix");

    c.vocoderMode    = raw ("VocoderMode");
    c.vocoderBands   = raw ("VocoderBands", 64);
    c.vocoderAttack  = raw ("VocoderAttack", 20);
    c.vocoderRelease = raw ("VocoderRelease", 40);
    c.vocoderBalance = raw ("VocoderBalance", 64);

    c.osc1Interp  = raw ("Osc1WavetableInterpolation", 64);
    c.osc2Interp  = raw ("Osc2WavetableInterpolation", 64);
    c.osc1WtIndex = raw ("Osc1WavetableWavetableindex");
    c.osc2WtIndex = raw ("Osc2WavetableWavetableindex");

    c.lfo1Osc1Amt      = raw ("Osc1Lfo1Amount", 64);
    c.lfo1Osc2Amt      = raw ("Osc2Lfo1Amount", 64);
    c.lfo1PwAmt        = raw ("PwLfo1Amount", 64);
    c.lfo1ResoAmt      = raw ("ResoLfo1Amount", 64);
    c.lfo1FiltGainAmt  = raw ("FiltgainLfo1Amount", 64);

    c.lfo2FmAmt        = raw ("FmLfo2Amount", 64);
    c.lfo2Cutoff1Amt   = raw ("Cutoff1Lfo2Amount", 64);
    c.lfo2Cutoff2Amt   = raw ("Cutoff2Lfo2Amount", 64);
    c.lfo2PanAmt       = raw ("PanLfo2Amount", 64);
    c.lfo2ShapeAmt     = raw ("ShapeLfo2Amount", 64);

    c.lfo1Mode = raw ("Lfo1Mode");
    c.lfo2Mode = raw ("Lfo2Mode");
    c.lfo3Mode = raw ("Lfo3Mode");
    c.lfo1EnvMode = raw ("Lfo1EnvMode");
    c.lfo2EnvMode = raw ("Lfo2EnvMode");
    c.lfo1Keytrigger = raw ("Lfo1Keytrigger");
    c.lfo2Keytrigger = raw ("Lfo2Keytrigger");
    c.unisonLfoPhase = raw ("UnisonLfoPhase");
    c.oscInitPhase   = raw ("OscInitPhase");

    c.osc2FiltEnvAmt   = raw ("Osc2FiltEnvAmt", 64);
    c.fmFiltEnvAmt     = raw ("FmFiltEnvAmt", 64);
    c.osc2FiltEnvPitch = raw ("Osc2HypersawFilterenvPitch", 64);
    c.osc2HsawSpread   = raw ("Osc2HypersawDetunespread");
    c.osc2HsawSyncFreq = raw ("Osc2HsawFiltEnvSyncFreq", 64);
    c.osc1WtSync   = raw ("Osc1WavetableSync");
    c.osc2WtSync   = raw ("Osc2WavetableSync");
    c.osc1WtDetune = raw ("Osc1WavetableInternalDetune");
    c.osc2WtDetune = raw ("Osc2WavetableInternalDetune");

    c.chorusType       = raw ("ChorusType");
    c.chorusLfoShape   = raw ("ChorusLfoShape");
    c.chorusXOver      = raw ("ChorusXOver");
    c.chorusAmount     = raw ("ChorusAmount", 64);
    c.chorusDistance   = raw ("ChorusDistance", 64);
    c.chorusMicAngle   = raw ("ChorusMicAngle", 64);
    c.chorusLowHighBal = raw ("ChorusLowhighBal", 64);
    c.chorusMix2       = raw ("ChorusMix2");
    c.chorusSpeed      = raw ("ChorusSpeed", 64);

    c.delayType     = raw ("DelayType");
    c.delayLfoShape = raw ("DelayLfoShape");
    c.delayRate     = raw ("DlyRateRevDecay", 64);
    c.delayDepth    = raw ("DlyDepth");
    c.tapeTime      = raw ("DelayTapeDelayTime", 64);
    c.tapeFeedback  = raw ("DelayTapeDelayFeedback", 64);
    c.tapeCentre    = raw ("DelayTapeDelayCenterFrequency", 64);
    c.tapeBandwidth = raw ("DelayTapeDelayBandwidth", 64);
    c.tapeRatio     = raw ("DelayTapeDelayRatio", 64);

    c.reverbMode     = raw ("ReverbMode");
    c.reverbColor    = raw ("ReverbColor", 64);
    c.reverbClock    = raw ("ReverbClock");
    c.reverbFeedback = raw ("ReverbFeedback", 64);

    c.phaserSpread = raw ("PhaserSpread", 64);
    c.distTreble   = raw ("PatchDistortionTrebleBooster", 64);
    c.distHighCut  = raw ("PatchDistortionHighCut", 127);
    c.distTone     = raw ("PatchDistortionTone127", 64);

    c.vocCarrierFreq     = raw ("VocoderCarrierCenterFrequency", 64);
    c.vocModFreq         = raw ("VocoderModulatorCenterFrequency", 64);
    c.vocModOffset       = raw ("VocoderModulatorFrequencyOffset", 64);
    c.vocCarrierQ        = raw ("VocoderCarrierQFactor", 64);
    c.vocModQ            = raw ("VocoderModulatorQFactor", 64);
    c.vocCarrierSpread   = raw ("CarrierFrequencySpread", 64);
    c.vocModSpread       = raw ("VocoderModulatorFrequencySpread", 64);
    c.vocSpectralBalance = raw ("VocoderSpectralBalance", 64);
    c.vocLink            = raw ("VocoderLink");

    c.bankFreq2        = raw ("FilterBankFrequency", 64);
    c.bankVowelFreq    = raw ("FilterBankVowelFrequency", 64);
    c.bankFilterFreq   = raw ("FilterBankFilterFrequency", 64);
    c.bankStereoPhase  = raw ("FilterBankStereoPhase", 64);
    c.bankFilterType   = raw ("FilterBankFilterType");
    c.bankPoles        = raw ("FilterBankPoles");
    c.bankShapeL       = raw ("FilterBankShapeL", 64);
    c.bankShapeR       = raw ("FilterBankShapeR", 64);
    c.bankSlope        = raw ("FilterBankSlope", 64);

    c.inputMode     = raw ("InputMode");
    c.inputSelect   = raw ("InputSelect");
    c.inputRingMod  = raw ("InputRingmodulator");

    // The patch's own tempo, used when the host is not running - the Virus
    // keeps arpeggios and clocked delays going on its internal clock.
    c.clockTempo = raw ("ClockTempo", 55);

    if (! hostTempoValid)
        tempoBpm = 63.0 + (double) c.clockTempo * 1.4;   // roughly 63..240 BPM

    c.channelVolume = raw ("ChannelVolume", 127);
    c.balance       = raw ("Balance", 64);
    c.partVolume    = raw ("PartVolume", 127);
    c.partDetune    = raw ("PartDetune", 64);
    c.cutoff2Offset = raw ("Cutoff2Offset", 64);
    c.benderScale   = raw ("BenderScale");
    c.controlSmooth = raw ("ControlSmoothMode", 2);

    c.softKnob1Dest  = raw ("SoftKnob1Single");
    c.softKnob2Dest  = raw ("SoftKnob2Single");
    c.softKnob3Dest  = raw ("SoftKnobsDestination3");
    c.softKnob1Value = raw ("SoftKnob0ConfigValue", 64);
    c.softKnob2Value = raw ("SoftKnob1ConfigValue", 64);
    c.softKnob3Value = raw ("SoftKnob2ConfigValue", 64);

    c.lfo1Symmetry = raw ("Lfo1Symmetry", 64);
    c.lfo2Symmetry = raw ("Lfo2Symmetry", 64);
    c.lfo3FadeIn   = raw ("Lfo3FadeInTime");
    c.lfo2Dest     = raw ("Lfo2AssignDest");
    c.lfo2DestAmt  = raw ("Lfo2AssignAmount", 64);
    c.lfo3Dest     = raw ("Lfo3Destination");

    c.lfo1Clock  = raw ("Lfo1Clock");
    c.lfo2Clock  = raw ("Lfo2Clock");
    c.lfo3Clock  = raw ("Lfo3Clock");
    c.lfo1KeyFol = raw ("Lfo1Keyfollow", 64);
    c.lfo2KeyFol = raw ("Lfo2Keyfollow", 64);
    c.lfo3KeyFol = raw ("Lfo3Keyfollow", 64);

    c.flt1EnvAmtVel = raw ("Flt1EnvamtVelocity", 64);
    c.flt2EnvAmtVel = raw ("Flt2EnvamtVelocity", 64);
    c.reso1Vel      = raw ("Resonance1Velocity", 64);
    c.reso2Vel      = raw ("Resonance2Velocity", 64);

    c.filterLink        = raw ("Filter2CutoffLink");
    c.filterLinkOffset  = raw ("OffsetForFilterlink", 64);
    c.filterKeytrackBase= raw ("FilterKeytrackBase", 36);

    c.osc1Pw      = raw ("Osc1Pulsewidth", 64);
    c.osc2Pw      = raw ("Osc2Pulsewidth", 64);
    c.pwVelocity  = raw ("PulsewidthVelocity", 64);
    c.osc2Sync    = raw ("Osc2Sync");
    c.syncFreq    = raw ("Osc2HypersawCrossoscsyncfreq", 64);
    c.osc1Formant = raw ("Osc1WavetableFormantshift", 64);
    c.osc2Formant = raw ("Osc2WavetableFormantshift", 64);
    c.osc1WtSelect= raw ("Osc1WavetableWavetableselect");
    c.osc2WtSelect= raw ("Osc2WavetableWavetableselect");

    c.ampSustainTime    = raw ("AmpEnvSustainTime", 64);
    c.filterSustainTime = raw ("FilterEnvSustainTime", 64);

    c.characterType      = raw ("CharacterType");
    c.characterIntensity = raw ("PatchDistortionQuality", 64);
    c.characterTune      = raw ("BassTune", 64);

    c.followerMode    = raw ("InputFollowerMode");
    c.followerLevel   = raw ("InputFollowerLevel", 64);
    c.followerAttack  = raw ("InputFollowerAttack", 20);
    c.followerRelease = raw ("InputFollowerRelease", 40);

    c.secondOutputBalance = raw ("SecondOutputBalance", 64);

    c.transpose   = raw ("Transpose", 64);
    c.portamento  = raw ("PortamentoTime");
    c.benderUp    = raw ("BenderRangeUp", 66);
    c.benderDown  = raw ("BenderRangeDown", 62);

    c.osc1KeyFol      = raw ("Osc1Keyfollow", 96);
    c.osc2KeyFol      = raw ("Osc2Keyfollow", 96);
    c.osc2FmAmount    = raw ("Osc2FmAmount");
    c.oscFmMode       = raw ("OscFmMode");
    c.fmAmountVelocity= raw ("FmAmountVelocity", 64);
    c.osc1ShapeVel    = raw ("Osc1ShapeVelocity", 64);
    c.osc2ShapeVel    = raw ("Osc2ShapeVelocity", 64);

    c.filterBalance      = raw ("FilterBalance", 64);
    c.filter1EnvPolarity = raw ("Filter1EnvPolarity", 1);
    c.filter2EnvPolarity = raw ("Filter2EnvPolarity", 1);

    c.ampVelocity      = raw ("AmpVelocity", 64);
    c.panoramaVelocity = raw ("PanoramaVelocity", 64);
    c.punchIntensity   = raw ("PunchIntensity");
    c.bassIntensity    = raw ("BassIntensity");
    c.bassTune         = raw ("BassTune", 64);

    c.atomizerAmount   = raw ("Atomizer");
    c.arpPatternLength = raw ("ArpeggiatorUserpatternlength", 15);

    // Pattern 0 is the user pattern; everything above it is a factory one
    // that the arp module can play itself.
    userPattern = c.arpMode > 0 && c.arpPattern == 0;

    readMatrix();

    activeVoiceLimit = c.keyMode == 1 || c.keyMode == 2 ? 1 : 16;   // mono modes

    //== push into the modules ================================================
    // HyperSaw: density drives how many detuned copies stack up, and the
    // hardware's own spread control becomes their detune. Not Access's exact
    // distribution - a stack of saws walking slightly apart, which is what the
    // parameter is for.
    const auto unisonFor = [] (int mode, int density, int unisonMode)
    {
        const int hyper = mode == 1 ? 2 + juce::roundToInt (scaling::unit (density) * 7.0f) : 1;
        const int uni = unisonMode > 0 ? juce::jlimit (1, 4, unisonMode + 1) : 1;
        return juce::jlimit (1, VirusOscModule::kMaxUnison, juce::jmax (hyper, uni));
    };

    osc1->setParameter ("unison", (float) unisonFor (c.osc1Mode, c.osc1Density, c.unisonMode));
    osc2->setParameter ("unison", (float) unisonFor (c.osc2Mode, c.osc2Density, c.unisonMode));

    const float hyperDetune1 = c.osc1Mode == 1 ? scaling::unit (c.osc1Spread) * 60.0f : 0.0f;
    const float unisonDetune = scaling::unit (c.unisonDetune) * 40.0f;

    osc1->setParameter ("detune", juce::jmax (hyperDetune1, unisonDetune));
    osc2->setParameter ("detune", juce::jmax (hyperDetune1, unisonDetune));

    const float spread = scaling::unit (c.unisonSpread);
    osc1->setParameter ("spread", spread);
    osc2->setParameter ("spread", spread);

    // Portamento is a mono behaviour in the module, which matches the Virus:
    // with more than one voice there is no single previous note to slide
    // from. Key Mode decides whether it applies at all.
    const bool monoMode = c.keyMode == 1 || c.keyMode == 2;
    const float glideMs = scaling::unit (c.portamento) * 1000.0f;

    for (auto* m : { osc1.get(), osc2.get(), osc3.get(), subOsc.get() })
    {
        m->setParameter ("voices", monoMode ? 1.0f : (float) kMaxVoices);
        m->setParameter ("glide", monoMode ? glideMs : 0.0f);
    }

    // A little analogue drift on everything: this is the module's reason for
    // existing, and the Virus was never clinically stable either.
    // The Virus is digital and stays put; a little movement keeps unison
    // alive but three cents of random walk was audibly chorusing every note
    // and smearing its harmonics.
    osc1->setParameter ("drift", 0.8f);
    osc2->setParameter ("drift", 0.8f);

    // Oscillator balance is a crossfade, so the two volumes are complementary.
    const float balance = scaling::unit (c.oscBalance);
    // Oscillator Section Volume is bipolar as well - centre is unity gain.
    const float master = std::pow (2.0f, scaling::bipolar (c.oscVolume));
    osc1->setParameter ("volume", (1.0f - balance) * master);
    osc2->setParameter ("volume", balance * master);

    // Semitone and detune ride on the frequency ratio rather than the note
    // number, so they stay continuous.
    osc1->setParameter ("fmRatio", std::pow (2.0f, scaling::semitones (c.osc1Semi) / 12.0f));
    osc2->setParameter ("fmRatio", std::pow (2.0f, scaling::semitones (c.osc2Semi) / 12.0f)
                                     * scaling::ratioFromCents (scaling::cents (c.osc2Detune)));

    //== filters ===============================================================
    // Virus filter modes: Low Pass, High Pass, Band Pass, Band Stop.
    // AquaFilter offers LP12, LP24, LP24+, BP, HP - so low pass gets the 24 dB
    // version, which is closer to the Virus's character than a 12 dB slope.
    const auto filterType = [] (int mode)
    {
        switch (mode)
        {
            case 0:  return 1;   // Low Pass  -> LP24
            case 1:  return 4;   // High Pass -> HP
            case 2:  return 3;   // Band Pass -> BP
            default: return 1;
        }
    };

    filter1->setParameter ("type", (float) filterType (c.flt1Mode));
    filter2->setParameter ("type", (float) filterType (c.flt2Mode));
    filter1->setParameter ("resonance", scaling::unit (c.flt1Res) * 0.98f);
    filter2->setParameter ("resonance", scaling::unit (c.flt2Res) * 0.98f);

    const float drive = scaling::unit (c.saturation > 0 ? 64 : 0);
    filter1->setParameter ("drive", drive);
    filter2->setParameter ("drive", drive);

    //== envelopes =============================================================
    envAmp->setParameter ("attack",  scaling::envMs (c.envAA));
    envAmp->setParameter ("decay",   scaling::envMs (c.envAD));
    envAmp->setParameter ("sustain", scaling::unit (c.envAS));
    envAmp->setParameter ("release", scaling::envMs (c.envAR));
    envAmp->setParameter ("sustainTime", scaling::bipolar (c.ampSustainTime));

    envFilter->setParameter ("attack",  scaling::envMs (c.envFA));
    envFilter->setParameter ("decay",   scaling::envMs (c.envFD));
    envFilter->setParameter ("sustain", scaling::unit (c.envFS));
    envFilter->setParameter ("release", scaling::envMs (c.envFR));
    envFilter->setParameter ("sustainTime", scaling::bipolar (c.filterSustainTime));

    //== LFOs ==================================================================
    // With an LFO clock set, the rate is a division of the host tempo rather
    // than a free frequency - the same table the delay uses.
    const auto lfoRate = [this] (int rateByte, int clock)
    {
        if (clock <= 0 || tempoBpm <= 1.0)
            return scaling::lfoRateHz (rateByte);

        static const float beatFractions[] = { 4.0f, 3.0f, 2.0f, 1.5f, 1.0f,
                                               0.75f, 0.5f, 0.375f, 0.25f, 0.1875f, 0.125f };
        const int step = juce::jlimit (0, 10, (clock - 1) % 11);
        return (float) (tempoBpm / 60.0) / beatFractions[step];
    };

    lfo1->setParameter ("rate", lfoRate (c.lfo1Rate, c.lfo1Clock));
    lfo2->setParameter ("rate", lfoRate (c.lfo2Rate, c.lfo2Clock));
    lfo3->setParameter ("rate", lfoRate (c.lfo3Rate, c.lfo3Clock));
    // The new LFO reads the whole 68-entry shape list, so nothing falls back
    // to a triangle any more.
    lfo1->setParameter ("shape", (float) juce::jlimit (0, 67, c.lfo1Shape));
    lfo2->setParameter ("shape", (float) juce::jlimit (0, 67, c.lfo2Shape));
    lfo3->setParameter ("shape", (float) juce::jlimit (0, 67, c.lfo3Shape));

    lfo1->setParameter ("symmetry", scaling::bipolar (c.lfo1Symmetry) * 0.95f);
    lfo2->setParameter ("symmetry", scaling::bipolar (c.lfo2Symmetry) * 0.95f);

    // Virus LFO Mode: Poly, Mono, and the clocked variants which are mono too.
    lfo1->setParameter ("mode", c.lfo1Mode == 0 ? 0.0f : 1.0f);
    lfo2->setParameter ("mode", c.lfo2Mode == 0 ? 0.0f : 1.0f);
    lfo3->setParameter ("mode", c.lfo3Mode == 0 ? 0.0f : 1.0f);

    lfo1->setParameter ("envMode", c.lfo1EnvMode > 0 ? 1.0f : 0.0f);
    lfo2->setParameter ("envMode", c.lfo2EnvMode > 0 ? 1.0f : 0.0f);

    // Keytrigger off is -1, which the module reads as free-running; anything
    // else is the phase a new note restarts at.
    lfo1->setParameter ("keytrigger", c.lfo1Keytrigger == 0 ? -1.0f : scaling::unit (c.lfo1Keytrigger));
    lfo2->setParameter ("keytrigger", c.lfo2Keytrigger == 0 ? -1.0f : scaling::unit (c.lfo2Keytrigger));

    // Unison LFO Phase fans the voices across the cycle.
    const float lfoFan = scaling::unit (c.unisonLfoPhase);
    lfo1->setParameter ("phaseSpread", lfoFan);
    lfo2->setParameter ("phaseSpread", lfoFan);
    lfo3->setParameter ("phaseSpread", lfoFan);

    //== sub, noise, ring mod ==================================================
    // The Virus sub is always an octave below oscillator 1, square or
    // triangle depending on Sub Osc Shape.
    subOsc->setParameter ("waveform", raw ("SuboscillatorShape") == 0 ? 3.0f : 1.0f);
    subOsc->setParameter ("fmRatio", 0.5f);
    subOsc->setParameter ("volume", scaling::unit (c.subVolume));

    noise->setParameter ("type", c.noiseColor < 64 ? 1.0f : 0.0f);   // dark end goes pink
    noise->setParameter ("level", scaling::unit (c.noiseVolume));

    ringMod->setParameter ("level", scaling::unit (c.ringModVolume));

    //== effects ===============================================================
    // All 26 curves, in the hardware's own order, so the byte picks the
    // curve it names instead of the nearest family.
    distortion->setParameter ("curve", (float) juce::jlimit (0, 25, c.distCurve));
    distortion->setParameter ("drive", 1.0f + scaling::unit (c.distIntensity) * 30.0f);
    distortion->setParameter ("tone", scaling::unit (c.distTone));
    distortion->setParameter ("highCut", scaling::unit (c.distHighCut));
    distortion->setParameter ("level", 0.9f + scaling::unit (c.distTreble) * 0.4f);
    distortion->setParameter ("dryWet", c.distCurve == 0 ? 0.0f : scaling::unit (c.distMix) * 100.0f);

    // EQ: the hardware's three bands map one to one onto the three bells.
    const auto eqGainDb = [] (int v) { return scaling::bipolar (v) * 16.0f; };

    eq->setParameter ("freq0", 40.0f + scaling::unit (c.bassIntensity > 0 ? c.bassTune : c.eqLowFreq) * 460.0f);
    // Bass Boost is a low shelf on the hardware. With only bells available it
    // rides along with the EQ's own low band, which is close enough that a
    // patch using either one still sounds like itself.
    eq->setParameter ("gain0", eqGainDb (c.eqLowGain) + scaling::unit (c.bassIntensity) * 12.0f);
    eq->setParameter ("q0", 0.7f);
    eq->setParameter ("freq1", 200.0f + scaling::unit (c.eqMidFreq) * 4800.0f);
    eq->setParameter ("gain1", eqGainDb (c.eqMidGain));
    eq->setParameter ("q1", 0.3f + scaling::unit (c.eqMidQ) * 6.0f);
    eq->setParameter ("freq2", 2000.0f + scaling::unit (c.eqHighFreq) * 14000.0f);
    eq->setParameter ("gain2", eqGainDb (c.eqHighGain));
    eq->setParameter ("q2", 0.7f);

    chorus->setParameter ("rate", 0.1f + scaling::unit (c.chorusRate) * 8.0f);
    chorus->setParameter ("depth", scaling::unit (c.chorusDepth) * 100.0f);
    chorus->setParameter ("baseDelay", 1.0f + scaling::unit (c.chorusDelay) * 18.0f);
    chorus->setParameter ("spread", scaling::unit (c.chorusFeedback) * 100.0f);
    // Chorus Type picks how wide and how many voices; Distance and Mic Angle
    // are the hardware's names for pre-delay and stereo spread.
    chorus->setParameter ("baseDelay", 1.0f + scaling::unit (c.chorusDistance) * 24.0f);
    chorus->setParameter ("spread", scaling::unit (c.chorusMicAngle) * 100.0f);
    // Chorus Type is really a voice count on the hardware: the later types
    // are the same chorus with more of it.
    chorus->setParameter ("voices", (float) juce::jlimit (1, 4, 1 + c.chorusType));
    chorus->setParameter ("lfoShape", (float) juce::jlimit (0, 3, c.chorusLfoShape));
    chorus->setParameter ("dryWet",
        scaling::unit (juce::jmax (c.chorusMix, c.chorusMix2)) * 100.0f);

    phaser->setParameter ("rate", 0.05f + scaling::unit (c.phaserRate) * 6.0f);
    phaser->setParameter ("depth", scaling::unit (c.phaserDepth) * 100.0f);
    phaser->setParameter ("centre", 100.0f + scaling::unit (c.phaserFreq) * 4000.0f);
    phaser->setParameter ("feedback", scaling::unit (c.phaserFeedback) * 100.0f);
    phaser->setParameter ("spread", scaling::unit (c.phaserSpread) * 100.0f);
    // Phaser Mode is the stage count on the hardware - 1, 2, 4 or 6 stages -
    // so it drives the module's stage list rather than a separate control.
    phaser->setParameter ("stages", (float) juce::jlimit (0, 5, c.phaserMode - 1));
    phaser->setParameter ("dryWet", c.phaserMode == 0 ? 0.0f : scaling::unit (c.phaserMix) * 100.0f);

    // Delay time. With the Virus clock off this is free-running, roughly 2 ms
    // to 700 ms across the byte. With the clock on the hardware divides the
    // host tempo, so the milliseconds are worked out from the BPM rather than
    // quantised onto somebody else's list of note lengths.
    float delayMs = 2.0f + scaling::unit (c.delayTime) * 698.0f;

    if (c.delayClock > 0 && tempoBpm > 1.0)
    {
        static const float beatFractions[] = { 0.25f, 0.375f, 0.5f, 0.75f, 1.0f,
                                               1.5f, 2.0f, 3.0f, 4.0f };
        const int step = juce::jlimit (0, 8, (c.delayClock - 1) % 9);
        delayMs = (float) (60000.0 / tempoBpm) * beatFractions[step];
    }

    delay->setParameter ("time", juce::jlimit (0.02f, 5000.0f, delayMs));

    // Tap Vol is this module's regeneration path, so it is the Virus's
    // feedback control. It reaches 120%, which is past self-oscillation; the
    // hardware stops short of that, so the byte maps to 0..110%.
    // In Tape mode the hardware uses its own time and feedback controls,
    // and Ratio offsets the right channel against the left.
    const auto& delayModes = choicesFor ("delayMode");
    const bool tapeMode = c.delayMode < delayModes.size()
                            && delayModes[c.delayMode].containsIgnoreCase ("Tape");

    if (tapeMode)
        delay->setParameter ("time", juce::jlimit (0.02f, 5000.0f,
            (2.0f + scaling::unit (c.tapeTime) * 698.0f)
              * (1.0f + scaling::bipolar (c.tapeRatio) * 0.5f)));

    delay->setParameter ("tapVolume",
        scaling::unit (tapeMode ? c.tapeFeedback : c.delayFeedback) * 110.0f);

    // The delay now has its own time modulation and a left/right ratio.
    delay->setParameter ("modRate", scaling::unit (c.delayRate) * 8.0f);
    delay->setParameter ("modDepth", scaling::unit (c.delayDepth) * 100.0f);
    delay->setParameter ("ratio", scaling::bipolar (tapeMode ? c.tapeRatio : c.delayType) * 0.5f);

    // Any Virus delay mode with Ping Pong in its name gets the module's
    // cross-feedback mode; the rest are plain stereo.
    const auto& delayModeNames = choicesFor ("delayMode");
    const bool pingPong = c.delayMode < delayModeNames.size()
                            && delayModeNames[c.delayMode].containsIgnoreCase ("Pong");

    delay->setParameter ("mode", pingPong ? 2.0f : 1.0f);
    delay->setParameter ("dryWet", c.delayMode == 0 ? 0.0f : scaling::unit (c.delaySend) * 100.0f);

    reverb->setParameter ("roomSize", 20.0f + scaling::unit (c.reverbType) * 80.0f);
    reverb->setParameter ("decay", 0.2f + scaling::unit (c.reverbTime) * 12.0f);
    reverb->setParameter ("damping", scaling::unit (c.reverbDamping) * 100.0f);
    reverb->setParameter ("preDelay", scaling::unit (c.reverbPredelay) * 120.0f);
    reverb->setParameter ("feedback", scaling::unit (c.reverbFeedback) * 100.0f);
    reverb->setParameter ("colour", scaling::bipolar (c.reverbColor));

    reverb->setParameter ("dryWet", scaling::unit (c.reverbSend) * 100.0f);

    //== oscillator 3 ==========================================================
    // Osc 3 Mode is Off, Slave (locked to Osc 2's pitch) or one of the
    // waveforms. Slave is the interesting one: same pitch, own detune, which
    // is how the Virus thickens a patch without a third pitch to tune.
    const bool osc3Slave = c.osc3Mode == 1;
    const int osc3Wave = c.osc3Mode <= 1 ? 2 : juce::jlimit (0, 3, c.osc3Mode - 2);

    osc3->setParameter ("waveform", (float) osc3Wave);
    osc3->setParameter ("volume", scaling::unit (c.osc3Volume));
    osc3->setParameter ("drift", 0.8f);
    osc3->setParameter ("fmRatio",
        std::pow (2.0f, scaling::semitones (osc3Slave ? c.osc2Semi : c.osc3Semi) / 12.0f)
          * scaling::ratioFromCents (scaling::cents (c.osc3Detune)));

    //== oscillator models =====================================================
    // One module per oscillator now, with Mode choosing Classic, HyperSaw or
    // Wavetable - so wave select, pulse width, sync and formant shift are all
    // just parameters rather than a second instance to switch between.
    const auto setupOsc = [this] (SynthModule& m, int mode, int wave, int shape, int shapeVel,
                                  int pulseWidth, int formant, int wtSelect, float velocity)
    {
        m.setParameter ("mode", (float) juce::jlimit (0, 2, mode));

        // Classic reads the wave table directly; Wavetable mode uses the
        // table select as a coarse offset into the same 64 waves, which is
        // how one table of waveforms stands in for the hardware's banks.
        const int waveIndex = mode == 2
                                ? juce::jlimit (0, 63, (wtSelect * 63) / 127)
                                : juce::jlimit (0, 63, (wave * 63) / 127);

        m.setParameter ("wave", (float) waveIndex);

        // Shape Velocity tilts Shape with how hard the note was struck.
        const float shapeValue = juce::jlimit (0.0f, 1.0f,
            scaling::unit (shape) + scaling::bipolar (shapeVel) * velocity);

        m.setParameter ("shape", shapeValue);
        // Pulse Width byte 0 is a SQUARE and rising bytes narrow the pulse -
        // it is not a bipolar control. Reading it as one put every patch at a
        // 2% needle pulse, since most patches leave the byte at 0.
        m.setParameter ("pulseWidth", juce::jlimit (0.02f, 0.5f,
            0.5f - scaling::unit (pulseWidth) * 0.48f
                 - scaling::bipolar (c.pwVelocity) * velocity * 0.2f));

        // Formant Shift is bipolar on the hardware: below centre the spectrum
        // slides down, above it slides up, and the pitch stays where it is.
        m.setParameter ("formant", std::pow (2.0f, scaling::bipolar (formant) * 1.5f));
    };

    // Velocity here is the last note's, which is all a block-rate parameter
    // update can know; per-voice velocity still rides the amplifier.
    setupOsc (*osc1, c.osc1Mode, c.osc1Wave, c.osc1Shape, c.osc1ShapeVel,
              c.osc1Pw, c.osc1Formant, c.osc1WtSelect, lastVelocity);

    setupOsc (*osc2, c.osc2Mode, c.osc2Wave, c.osc2Shape, c.osc2ShapeVel,
              c.osc2Pw, c.osc2Formant, c.osc2WtSelect, lastVelocity);

    // Osc 3's Mode is its waveform: Off, Slave, then Sine, Triangle and the
    // rest of the list. Slave keeps Osc 2's pitch but plays a saw of its own,
    // which is what the hardware does with it.
    {
        const int osc3Wave = c.osc3Mode <= 1
                               ? 40                                    // Slave: a saw-ish wave
                               : juce::jlimit (0, 127, (c.osc3Mode - 2) * 2);

        setupOsc (*osc3, 0, osc3Wave, 64, 64, 64, 64, 0, lastVelocity);
    }

    // Hard sync: oscillator 2 syncs to oscillator 1. Cross Osc Sync Freq is
    // the Virus's own control for what pitch the master runs at, which is
    // what makes a sync sweep sound like a sweep rather than a detune.
    osc2->setParameter ("sync", c.osc2Sync > 0 ? 1.0f : 0.0f);
    osc2->setParameter ("syncRatio", 0.25f + scaling::unit (c.syncFreq) * 7.75f);
    osc2->setParameter ("syncEnvAmount", scaling::bipolar (c.osc2HsawSyncFreq));

    // The wavetable model has its own sync switch and an internal detune that
    // spreads its unison stack independently of the global one.
    if (c.osc1Mode == 2)
    {
        osc1->setParameter ("sync", c.osc1WtSync > 0 ? 1.0f : 0.0f);
        osc1->setParameter ("detune", juce::jmax (osc1->getParameter ("detune"),
                                                  scaling::unit (c.osc1WtDetune) * 40.0f));
    }

    if (c.osc2Mode == 2)
    {
        osc2->setParameter ("sync", c.osc2WtSync > 0 ? 1.0f : 0.0f);
        osc2->setParameter ("detune", juce::jmax (osc2->getParameter ("detune"),
                                                  scaling::unit (c.osc2WtDetune) * 40.0f));
    }

    // Oscillator 2 has its own HyperSaw spread, separate from oscillator 1's.
    if (c.osc2Mode == 1)
        osc2->setParameter ("detune", scaling::unit (c.osc2HsawSpread) * 60.0f);

    // Init Phase: when it is off the oscillators free-run, which is the
    // analogue behaviour; set, every note starts at the same point.
    osc1->setParameter ("initPhase", scaling::unit (c.oscInitPhase));
    osc2->setParameter ("initPhase", scaling::unit (c.oscInitPhase));
    osc3->setParameter ("initPhase", scaling::unit (c.oscInitPhase));

    //== arpeggiator ===========================================================
    // Virus arp modes: Off, Up, Down, Up&Down, As Played, Random, Chord.
    // Chord has no equivalent in the module, so it plays As Played.
    const auto arpMode = [] (int mode)
    {
        switch (mode)
        {
            case 2:  return 1;   // Down
            case 3:  return 2;   // Up & Down
            case 4:  return 4;   // As Played
            case 5:  return 3;   // Random
            default: return 0;   // Up, and Chord falls here too
        }
    };

    arp->setParameter ("mode", (float) arpMode (c.arpMode));
    arp->setParameter ("octaves", (float) juce::jlimit (1, 4, c.arpOctaves + 1));
    arp->setParameter ("gate", 5.0f + scaling::unit (c.arpNoteLength) * 95.0f);
    arp->setParameter ("swing", scaling::unit (c.arpSwing) * 75.0f);
    arp->setParameter ("division", (float) juce::jlimit (0, 11, c.arpClock));
    arp->setTempo (tempoBpm);

    arpRunning = c.arpMode > 0;

    //== filter bank ===========================================================
    // The Virus filter bank is a vowel filter or a comb, depending on Type.
    // Two modules cover that: the formant filter for the vowel families, the
    // comb filter for the comb ones.
    const auto& bankNames = choicesFor ("filterBankType");
    const bool bankIsComb = c.filterBankType < bankNames.size()
                              && bankNames[c.filterBankType].containsIgnoreCase ("Comb");

    filterBank->setParameter ("vowel", scaling::unit (c.filterBankFreq) * 100.0f);
    filterBank->setParameter ("resonance", 40.0f + scaling::unit (c.filterBankReso) * 60.0f);
    filterBank->setParameter ("vowel", scaling::unit (c.bankVowelFreq) * 100.0f);
    // Per-side shape, and the poles that give a psy lead its scream.
    filterBank->setParameter ("shapeL", scaling::bipolar (c.bankShapeL));
    filterBank->setParameter ("shapeR", scaling::bipolar (c.bankShapeR));
    filterBank->setParameter ("poles", (float) juce::jlimit (1, 4, c.bankPoles + 1));
    filterBank->setParameter ("slope", scaling::unit (c.bankSlope));
    filterBank->setParameter ("size", scaling::bipolar (c.bankStereoPhase) * 12.0f);
    filterBank->setParameter ("dryWet", scaling::unit (c.filterBankMix) * 100.0f);

    combBank->setParameter ("freq", 40.0f + scaling::unit (c.filterBankFreq) * 3000.0f);
    combBank->setParameter ("feedback", scaling::unit (c.filterBankReso) * 99.0f);
    combBank->setParameter ("dryWet", scaling::unit (c.filterBankMix) * 100.0f);

    juce::ignoreUnused (bankIsComb);

    //== vocoder ===============================================================
    // Carrier is the synth, modulator is the plugin's audio input - which is
    // how the hardware does it too when the vocoder is fed from the analogue
    // inputs.
    vocoder->setParameter ("bands", c.vocoderBands < 43 ? 0.0f : (c.vocoderBands < 85 ? 1.0f : 2.0f));
    vocoder->setParameter ("attack", 0.1f + scaling::unit (c.vocoderAttack) * 40.0f);
    vocoder->setParameter ("release", 1.0f + scaling::unit (c.vocoderRelease) * 200.0f);
    vocoder->setParameter ("bright", scaling::unit (c.vocSpectralBalance) * 100.0f);
    // Separate carrier and modulator banks, as on the hardware: sliding one
    // against the other is what turns speech into a robot.
    vocoder->setParameter ("carrierCentre", 100.0f + scaling::unit (c.vocCarrierFreq) * 4900.0f);
    vocoder->setParameter ("modCentre", 100.0f + scaling::unit (
        c.vocLink > 0 ? c.vocCarrierFreq : c.vocModFreq) * 4900.0f);
    vocoder->setParameter ("carrierSpread", scaling::unit (c.vocCarrierSpread) * 100.0f);
    vocoder->setParameter ("modSpread", scaling::unit (c.vocModSpread) * 100.0f);
    vocoder->setParameter ("carrierQ", 0.2f + scaling::unit (c.vocCarrierQ) * 5.8f);
    vocoder->setParameter ("modQ", 0.2f + scaling::unit (c.vocModQ) * 5.8f);
    vocoder->setParameter ("bright", scaling::unit (c.vocSpectralBalance) * 100.0f);
    vocoder->setParameter ("level", 0.5f + scaling::unit (c.vocoderBalance) * 1.5f);

    //== character =============================================================
    // The Character types are each a small combination of things the rack can
    // already do: a drive stage, a tilt in the EQ, and a stereo width. Rather
    // than nine separate algorithms, each type sets those three.
    const auto& characterNames = choicesFor ("characters");
    const juce::String character = c.characterType < characterNames.size()
                                     ? characterNames[c.characterType] : juce::String();
    const float charAmount = scaling::unit (c.characterIntensity);

    float widthPercent = 100.0f;
    float charDrive = 1.0f;

    if (character.containsIgnoreCase ("Widener"))
        widthPercent = 100.0f + charAmount * 100.0f;
    else if (character.containsIgnoreCase ("Vintage") || character.containsIgnoreCase ("Boost"))
        charDrive = 1.0f + charAmount * 6.0f;
    else if (character.containsIgnoreCase ("Enhancer") || character.containsIgnoreCase ("Opener"))
    {
        charDrive = 1.0f + charAmount * 2.0f;
        widthPercent = 100.0f + charAmount * 40.0f;
    }
    else if (character.containsIgnoreCase ("Cabinet"))
        charDrive = 1.0f + charAmount * 3.0f;

    characterShaper->setParameter ("shape", 0.0f);   // Tanh: saturation, not clipping
    characterShaper->setParameter ("drive", charDrive);
    characterShaper->setParameter ("out", 0.9f);
    characterShaper->setParameter ("dryWet", charAmount * 100.0f);

    stereoWidth->setParameter ("width", widthPercent);
    stereoWidth->setParameter ("bassMono", character.containsIgnoreCase ("Bass")
                                             ? 60.0f + scaling::unit (c.characterTune) * 200.0f : 0.0f);

    //== input follower ========================================================
    envFollow->setParameter ("attack", 0.1f + scaling::unit (c.followerAttack) * 40.0f);
    envFollow->setParameter ("release", 1.0f + scaling::unit (c.followerRelease) * 500.0f);
    envFollow->setParameter ("gain", 0.1f + scaling::unit (c.followerLevel) * 4.0f);

    //== the two hand-written pieces ===========================================
    atomizer.setTempo (tempoBpm);
    atomizer.setAmount (scaling::unit (c.atomizerAmount));

    // Frequency Shifter is a Filter Bank type, not a separate effect. Its
    // frequency control is the bank's own frequency knob, read as a bipolar
    // shift so the lower half of the knob shifts partials down.
    freqShifter.setShift (scaling::bipolar (c.filterBankFreq) * 1000.0f);
    freqShifter.setMix (scaling::unit (c.filterBankMix));

    // Ring Modulator, also a bank type: a sine at the bank frequency.
    bankRingMod->setParameter ("depth", scaling::unit (c.filterBankMix) * 100.0f);

    // The XFade and VariSlope bank types are ordinary filters with the bank's
    // frequency and resonance.
    bankFilter->setParameter ("cutoff", scaling::cutoffHz (scaling::unit (
        c.bankFilterFreq != 64 ? c.bankFilterFreq : c.bankFreq2)));
    bankFilter->setParameter ("mode", (float) juce::jlimit (0, 3, c.bankFilterType));
    bankFilter->setParameter ("modDepth", scaling::unit (c.bankSlope));
    bankFilter->setParameter ("resonance", scaling::unit (c.filterBankReso) * 0.95f);

    // Tape Delay mode: the delay with wow, flutter and a little saturation
    // over it, which is what the hardware's tape mode is doing.
    tapeWobble->setParameter ("wow", 15.0f + scaling::unit (raw ("DelayTapeDelayModulation")) * 60.0f);
    tapeWobble->setParameter ("flutter", 10.0f + scaling::unit (c.tapeBandwidth) * 40.0f);
    tapeWobble->setParameter ("drive", 5.0f + scaling::unit (c.tapeCentre) * 40.0f);
    tapeWobble->setParameter ("dryWet", 100.0f);
}

//==============================================================================
// The rack, in the order the hardware runs it: distortion into EQ, then the
// modulation effects, then the two sends.
// One sample of the arpeggiator's own clock. It reports at most one note off
// and one note on, which the engine turns into voices exactly as it does for
// the player's keyboard.
void Engine::advanceArpeggiator()
{
    if (! arpRunning)
        return;

    // Pattern 0 is the user grid, which the module knows nothing about.
    if (userPattern)
    {
        advanceUserPattern();
        return;
    }

    int onNote = -1, offNote = -1;
    arp->midiDriverAdvance (sampleRate, onNote, offNote);

    if (offNote >= 0)
        noteOff (offNote, 0.0f);

    if (onNote >= 0)
        noteOn (onNote, 0.8f);
}

// The 32-step user pattern. Pattern 0 on the hardware is not a factory
// arpeggio at all - it is this grid, one step at a time: Bitfield says
// whether the step sounds, Velocity how hard, Length how long the note is
// held as a fraction of the step.
//
// The arp module plays the factory patterns; this plays the user one, walking
// the held keys itself the way the module would.
void Engine::advanceUserPattern()
{
    const double beatSamples = 60.0 / tempoBpm * sampleRate;
    const double stepSamples = juce::jmax (32.0, beatSamples * 0.25);   // 1/16 steps

    // release any note whose length has run out
    for (int i = 0; i < userActiveCount; ++i)
        if (userActiveNotes[(size_t) i] >= 0 && userStepCounter >= userNoteOffAt[(size_t) i])
        {
            noteOff (userActiveNotes[(size_t) i], 0.0f);
            userActiveNotes[(size_t) i] = -1;
        }

    if (++userStepCounter < stepSamples)
        return;

    userStepCounter = 0.0;
    userStep = (userStep + 1) % juce::jlimit (1, 32, c.arpPatternLength + 1);

    const juce::String n (userStep + 1);

    if (raw (("Step" + n + "Bitfield").toRawUTF8()) == 0)
        return;

    // which held key this step plays - the pattern walks upwards through
    // whatever is under the fingers, wrapping at the top
    int keys[16];
    int keyCount = 0;

    for (int note = 0; note < 128 && keyCount < 16; ++note)
        if (heldKeys[(size_t) note])
            keys[keyCount++] = note;

    if (keyCount == 0)
        return;

    const int note = keys[userHeldIndex % keyCount];
    userHeldIndex = (userHeldIndex + 1) % keyCount;

    const float velocity = juce::jmax (0.05f, scaling::unit (raw (("Step" + n + "Velocity").toRawUTF8(), 100)));
    const float length = scaling::unit (raw (("Step" + n + "Length").toRawUTF8(), 64));

    noteOn (note, velocity);

    // remember when to let it go
    for (int i = 0; i < 8; ++i)
        if (userActiveNotes[(size_t) i] < 0 || i >= userActiveCount)
        {
            userActiveNotes[(size_t) i] = note;
            userNoteOffAt[(size_t) i] = stepSamples * (0.1 + (double) length * 0.9);
            userActiveCount = juce::jmax (userActiveCount, i + 1);
            break;
        }
}

void Engine::renderEffects (float& l, float& r)
{
    const auto run = [this] (SynthModule& m, float& left, float& right)
    {
        in[0] = { left, right };
        m.processSample (in.data(), out.data());
        left = out[0][0];
        right = out[0][1];
    };

    // The Input section: the plugin's audio input can be mixed in, and ring
    // modulated against the synth, which is what the hardware's analogue
    // inputs do when Input Mode is on.
    if (c.inputMode > 0 && (inputL != 0.0f || inputR != 0.0f))
    {
        const float inGain = c.inputSelect == 1 ? 0.0f : 1.0f;   // L only, or L+R

        l += inputL;
        r += inputR * inGain;

        if (c.inputRingMod > 0)
        {
            const float amount = scaling::unit (c.inputRingMod);
            l = l * (1.0f - amount) + l * inputL * amount * 2.0f;
            r = r * (1.0f - amount) + r * inputR * amount * 2.0f;
        }
    }

    if (c.distCurve > 0)  run (*distortion, l, r);

    if (c.eqLowGain != 64 || c.eqMidGain != 64 || c.eqHighGain != 64)
        run (*eq, l, r);

    // The Filter Bank is really six different effects sharing three knobs.
    // Its Type picks which: a vowel filter, a comb, a ring modulator, a
    // frequency shifter, or one of the pole/slope filters.
    if (c.filterBankType > 0 && c.filterBankMix > 0)
    {
        const auto& bankNames = choicesFor ("filterBankType");
        const juce::String type = c.filterBankType < bankNames.size()
                                    ? bankNames[c.filterBankType] : juce::String();

        if (type.containsIgnoreCase ("Frequency Shifter"))
            freqShifter.process (l, r);
        else if (type.containsIgnoreCase ("Ring Mod"))
        {
            in[0] = { l, r };
            in[1] = { l, r };
            bankRingMod->processSample (in.data(), out.data());
            l = out[0][0];
            r = out[0][1];
            in[1] = { 0.0f, 0.0f };
        }
        else if (type.containsIgnoreCase ("Vowel"))
            run (*filterBank, l, r);
        else if (type.containsIgnoreCase ("Comb"))
            run (*combBank, l, r);
        else
            run (*bankFilter, l, r);   // the XFade and VariSlope families
    }

    // Vocoder: the synth is the carrier, the plugin's audio input the
    // modulator. With nothing plugged in there is nothing to vocode, so it
    // stays out of the path.
    if (c.vocoderMode > 0 && (inputL != 0.0f || inputR != 0.0f))
    {
        in[0] = { l, r };
        in[1] = { inputL, inputR };
        vocoder->processSample (in.data(), out.data());
        l = out[0][0];
        r = out[0][1];
        in[1] = { 0.0f, 0.0f };
    }

    if (c.phaserMode > 0) run (*phaser, l, r);
    if (c.chorusMix > 0)  run (*chorus, l, r);
    if (c.delayMode > 0 && c.delaySend > 0)
    {
        run (*delay, l, r);

        // Tape Delay mode is the same delay with tape behaviour over it.
        const auto& delayModeNames = choicesFor ("delayMode");
        if (c.delayMode < delayModeNames.size()
              && delayModeNames[c.delayMode].containsIgnoreCase ("Tape"))
            run (*tapeWobble, l, r);
    }
    if (c.reverbSend > 0) run (*reverb, l, r);

    // Character, then width, then the Atomizer. Character is a colouring
    // stage rather than an effect proper, which is why it sits after the
    // rack and before the stereo stage.
    if (c.characterType > 0 && c.characterIntensity > 0)
    {
        run (*characterShaper, l, r);
        run (*stereoWidth, l, r);
    }

    // Atomizer sits at the very end, chopping whatever came before it.
    atomizer.process (l, r);
}

//==============================================================================
int Engine::allocateVoice()
{
    for (int v = 0; v < activeVoiceLimit; ++v)
        if (! voices[(size_t) v].active)
            return v;

    // steal the oldest
    int oldest = 0;
    juce::uint64 bestAge = std::numeric_limits<juce::uint64>::max();

    for (int v = 0; v < activeVoiceLimit; ++v)
        if (voices[(size_t) v].age < bestAge)
        {
            bestAge = voices[(size_t) v].age;
            oldest = v;
        }

    return oldest;
}

void Engine::noteOn (int midiNote, float velocity)
{
    const int v = allocateVoice();
    auto& voice = voices[(size_t) v];

    const bool retrigger = voice.active;

    voice.note = midiNote;
    voice.velocity = velocity;
    voice.held = true;
    voice.active = true;
    voice.releaseCountdown = 0.0f;
    voice.age = ++ageCounter;

    voiceRandom[(size_t) v] = random.nextFloat() * 2.0f - 1.0f;
    lastVelocity = velocity;
    lfo3Fade[(size_t) v] = 0.0f;

    for (auto* m : { osc1.get(), osc2.get(), osc3.get(),
                     subOsc.get(), noise.get(), ringMod.get(),
                     envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                     lfo1.get(), lfo2.get(), lfo3.get() })
        m->voiceNoteOn (v, midiNote, retrigger);

    // The oscillator module scales its own output by velocity. The Virus
    // applies velocity at the amplifier, and we do that below - so the
    // oscillators are told the note was struck at full force, or velocity
    // would end up applied twice.
    osc1->voiceVelocity (v, 1.0f);
    osc2->voiceVelocity (v, 1.0f);
    osc3->voiceVelocity (v, 1.0f);
    subOsc->voiceVelocity (v, 1.0f);
    noise->voiceVelocity (v, 1.0f);

    for (auto* m : { envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                     lfo1.get(), lfo2.get(), lfo3.get() })
        m->voiceVelocity (v, velocity);
}

void Engine::noteOff (int midiNote, float releaseVelocity)
{
    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto& voice = voices[(size_t) v];

        if (voice.active && voice.held && voice.note == midiNote)
        {
            voice.held = false;
            voiceReleaseVelocity[(size_t) v] = releaseVelocity;

            // Keep the voice alive for its own release, plus a little margin.
            voice.releaseCountdown = (float) (sampleRate
                * (double) (scaling::envMs (c.envAR) * 0.001f + 0.05));

            for (auto* m : { osc1.get(), osc2.get(), osc3.get(),
                             subOsc.get(), noise.get(),
                             envFilter.get(), envAmp.get(), env3.get(), env4.get(),
                             lfo1.get(), lfo2.get(), lfo3.get() })
                m->voiceNoteOff (v);
        }
    }
}

void Engine::allNotesOff()
{
    for (int v = 0; v < kMaxVoices; ++v)
        if (voices[(size_t) v].active)
            noteOff (voices[(size_t) v].note, 0.0f);
}

//==============================================================================
void Engine::renderVoice (int v, float& outL, float& outR)
{
    auto& voice = voices[(size_t) v];

    //-- modulators ------------------------------------------------------------
    envAmp->processVoiceSample (v, in.data(), out.data());
    const float ampEnv = out[0][0];

    envFilter->processVoiceSample (v, in.data(), out.data());
    const float filterEnv = out[0][0];

    env3->processVoiceSample (v, in.data(), out.data());
    const float env3Value = out[0][0];

    env4->processVoiceSample (v, in.data(), out.data());
    const float env4Value = out[0][0];

    // LFO Key Follow: the higher the note, the faster the LFO runs. Applied
    // before the LFO is asked for its value, and per voice, which is the only
    // way it means anything.
    const float lfoKeyTrack = ((float) voices[(size_t) v].note - 60.0f) / 12.0f;

    if (c.lfo1KeyFol != 0) lfo1->setParamMod (lfoIdx.rate, lfoKeyTrack * scaling::unit (c.lfo1KeyFol) * 4.0f);
    if (c.lfo2KeyFol != 0) lfo2->setParamMod (lfoIdx.rate, lfoKeyTrack * scaling::unit (c.lfo2KeyFol) * 4.0f);
    if (c.lfo3KeyFol != 0) lfo3->setParamMod (lfoIdx.rate, lfoKeyTrack * scaling::unit (c.lfo3KeyFol) * 4.0f);

    lfo1->processVoiceSample (v, in.data(), out.data());
    const float lfo1Raw = out[0][0];

    lfo2->processVoiceSample (v, in.data(), out.data());
    const float lfo2Raw = out[0][0];

    lfo3->processVoiceSample (v, in.data(), out.data());
    float lfo3Raw = out[0][0];

    // Symmetry skews the waveform: at the centre it is untouched, away from
    // it one half of the cycle stretches at the other's expense. Done on the
    // output rather than the phase, which is an approximation, but it moves
    // in the right direction and keeps the module untouched.
    const auto skew = [] (float value, int symmetry)
    {
        if (symmetry == 64)
            return value;

        const float k = std::pow (2.0f, -scaling::bipolar (symmetry) * 1.5f);
        return value >= 0.0f ? std::pow (value, k) : -std::pow (-value, k);
    };

    // LFO 3 Fade In ramps the LFO up over the first seconds of a note, which
    // is how a patch gets vibrato that arrives rather than starts.
    if (c.lfo3FadeIn > 0)
    {
        const float fadeRate = (float) (1.0 / (sampleRate * (0.05 + scaling::unit (c.lfo3FadeIn) * 8.0)));
        lfo3Fade[(size_t) v] = juce::jmin (1.0f, lfo3Fade[(size_t) v] + fadeRate);
        lfo3Raw *= lfo3Fade[(size_t) v];
    }

    // Filter key follow pivots around Keytrack Base rather than middle C -
    // that parameter is what decides where the filter tracks from.
    const float keyTrackBaseNote = 16.0f + (float) c.filterKeytrackBase;
    const float keyTrack = ((float) voice.note - keyTrackBaseNote) / 60.0f;

    //-- modulation matrix -----------------------------------------------------
    std::array<float, (size_t) Src::Count> sources {};
    sources[(size_t) Src::PitchBend]     = pitchBend;

    // Pitch bend is a fixed path as well as a matrix source: the wheel bends
    // the oscillators by Bender Range Up or Down, which are separate on the
    // Virus so a patch can bend two semitones up and an octave down.
    const float bendSemis = pitchBend >= 0.0f
                              ? pitchBend * scaling::semitones (c.benderUp)
                              : pitchBend * -scaling::semitones (c.benderDown);
    sources[(size_t) Src::ChanPressure]  = channelPressure;

    // Every controller-derived source is the same lookup into the live CC
    // table, so they cost nothing to support in full.
    sources[(size_t) Src::ModWheel]          = controllers[1];
    sources[(size_t) Src::Breath]            = controllers[2];
    sources[(size_t) Src::Controller3]       = controllers[3];
    sources[(size_t) Src::FootPedal]         = controllers[4];
    sources[(size_t) Src::DataEntry]         = controllers[6];
    sources[(size_t) Src::Balance]           = controllers[8];
    sources[(size_t) Src::Controller9]       = controllers[9];
    sources[(size_t) Src::Expression]        = controllers[11];
    sources[(size_t) Src::Controller12]      = controllers[12];
    sources[(size_t) Src::Controller13]      = controllers[13];
    sources[(size_t) Src::Controller14]      = controllers[14];
    sources[(size_t) Src::Controller15]      = controllers[15];
    sources[(size_t) Src::Controller16]      = controllers[16];
    sources[(size_t) Src::HoldPedal]         = controllers[64];
    sources[(size_t) Src::PortamentoSwitch]  = controllers[65];
    sources[(size_t) Src::SostenutoPedal]    = controllers[66];

    sources[(size_t) Src::VelocityOff]   = voiceReleaseVelocity[(size_t) v];
    sources[(size_t) Src::AmpEnv]        = ampEnv;
    sources[(size_t) Src::FilterEnv]     = filterEnv;
    sources[(size_t) Src::Env3]          = env3Value;
    sources[(size_t) Src::Env4]          = env4Value;
    sources[(size_t) Src::Lfo1Bi]        = lfo1Raw;
    sources[(size_t) Src::Lfo2Bi]        = lfo2Raw;
    sources[(size_t) Src::Lfo3Bi]        = lfo3Raw;
    sources[(size_t) Src::Lfo1Uni]       = lfo1Raw * 0.5f + 0.5f;
    sources[(size_t) Src::Lfo2Uni]       = lfo2Raw * 0.5f + 0.5f;
    sources[(size_t) Src::Lfo3Uni]       = lfo3Raw * 0.5f + 0.5f;
    sources[(size_t) Src::VelocityOn]    = voice.velocity;
    sources[(size_t) Src::KeyFollow]     = keyTrack;
    sources[(size_t) Src::RandomPerNote] = voiceRandom[(size_t) v];
    sources[(size_t) Src::Const1]        = 0.01f;
    sources[(size_t) Src::Const10]       = 0.10f;

    ModTargets mod;
    applyMatrix (sources, mod);

    // Destinations that belong to a module parameter go in as parameter
    // modulation, which the module applies on its next call. Because every
    // one of these modules is evaluated per voice, this stays per voice even
    // though the parameter itself is shared.
    if (mod.lfo1Rate != 0.0f) lfo1->setParamMod (lfoIdx.rate, mod.lfo1Rate * 100.0f);
    if (mod.lfo2Rate != 0.0f) lfo2->setParamMod (lfoIdx.rate, mod.lfo2Rate * 100.0f);
    if (mod.lfo3Rate != 0.0f) lfo3->setParamMod (lfoIdx.rate, mod.lfo3Rate * 100.0f);

    if (mod.ampAttack  != 0.0f) envAmp->setParamMod (envIdx.attack,  mod.ampAttack * 5000.0f);
    if (mod.ampDecay   != 0.0f) envAmp->setParamMod (envIdx.decay,   mod.ampDecay * 5000.0f);
    if (mod.ampSustain != 0.0f) envAmp->setParamMod (envIdx.sustain, mod.ampSustain);
    if (mod.ampRelease != 0.0f) envAmp->setParamMod (envIdx.release, mod.ampRelease * 5000.0f);

    if (mod.filterAttack  != 0.0f) envFilter->setParamMod (envIdx.attack,  mod.filterAttack * 5000.0f);
    if (mod.filterDecay   != 0.0f) envFilter->setParamMod (envIdx.decay,   mod.filterDecay * 5000.0f);
    if (mod.filterSustain != 0.0f) envFilter->setParamMod (envIdx.sustain, mod.filterSustain);
    if (mod.filterRelease != 0.0f) envFilter->setParamMod (envIdx.release, mod.filterRelease * 5000.0f);

    // LFO amount destinations scale the LFO's contribution for this voice.
    const float lfo1Value = skew (lfo1Raw, c.lfo1Symmetry) * juce::jlimit (0.0f, 2.0f, 1.0f + mod.lfo1Amount);
    const float lfo2Value = skew (lfo2Raw, c.lfo2Symmetry) * juce::jlimit (0.0f, 2.0f, 1.0f + mod.lfo2Amount);
    const float lfo3Value = lfo3Raw * juce::jlimit (0.0f, 2.0f, 1.0f + mod.lfo3Amount);

    // Each LFO's own Assign Dest, which is a route of its own quite separate
    // from the matrix. Feeding it into the same ModTargets means every
    // destination behaves identically however it was reached.
    const auto assignLfo = [this, &mod] (float value, int destByte, float amount)
    {
        if (destByte <= 0 || amount == 0.0f)
            return;

        const Dest d = destByte < (int) lfoDestForByte.size()
                         ? lfoDestForByte[(size_t) destByte] : Dest::None;

        const float a = value * amount;

        switch (d)
        {
            case Dest::PatchVolume: mod.patchVolume += a; break;
            case Dest::Panorama:    mod.panorama    += a; break;
            case Dest::OscBalance:  mod.oscBalance  += a; break;
            case Dest::SubVolume:   mod.subVolume   += a; break;
            case Dest::OscPitch:    mod.osc1Pitch   += a; mod.osc2Pitch += a; break;
            case Dest::Osc1Pitch:   mod.osc1Pitch   += a; break;
            case Dest::Osc2Pitch:   mod.osc2Pitch   += a; break;
            case Dest::Osc1Shape:   mod.osc1Shape   += a; break;
            case Dest::Osc2Shape:   mod.osc2Shape   += a; break;
            case Dest::Cutoff1:     mod.cutoff1     += a; break;
            case Dest::Cutoff2:     mod.cutoff2     += a; break;
            case Dest::Reso1:       mod.reso1       += a; break;
            default: break;
        }
    };

    assignLfo (lfo1Value, c.lfo1Dest, scaling::bipolar (c.lfo1Amt));
    assignLfo (lfo2Value, c.lfo2Dest, scaling::bipolar (c.lfo2DestAmt));
    assignLfo (lfo3Value, c.lfo3Dest, scaling::unit (c.lfo3Amt));

    //-- oscillators -----------------------------------------------------------
    // Pitch destinations are the Semitone parameter, whose full range is
    // -64..+63 semitones - so a normalised unit of modulation is 128
    // semitones, and pitch is a multiply rather than an add.
    if (osc1Idx.fmRatio >= 0)
    {
        // LFO 1's own assignment to oscillator pitch is a fixed path on the
        // hardware, separate from the matrix, so it is added here rather than
        // routed through it.
        // LFO 1 has its own amount per destination on the hardware, separate
        // from both its Assign amount and the matrix - so oscillator 1 and 2
        // can be vibratoed by different depths from the same LFO.
        const float lfoPitch = lfo1Value * scaling::bipolar (c.lfo1Amt) * 12.0f + bendSemis;
        const float lfoPitch1 = lfoPitch + lfo1Value * scaling::bipolar (c.lfo1Osc1Amt) * 12.0f;
        const float lfoPitch2 = lfoPitch + lfo1Value * scaling::bipolar (c.lfo1Osc2Amt) * 12.0f;

        // Oscillator key follow: at 96 (the default) the oscillator tracks
        // the keyboard exactly. Below that it tracks less, so the pitch
        // flattens out across the keyboard; above it, it exaggerates.
        const float track1 = (float) c.osc1KeyFol / 96.0f;
        const float track2 = (float) c.osc2KeyFol / 96.0f;
        const float fromMiddle = (float) voice.note - 60.0f;

        const float semis1 = mod.osc1Pitch * 128.0f + scaling::semitones (c.partDetune) + lfoPitch1
                               + fromMiddle * (track1 - 1.0f) + scaling::semitones (c.transpose);
        // The filter envelope has its own fixed path to oscillator 2's pitch,
        // which is where sync sweeps and FM growls come from on this synth.
        const float envToPitch2 = filterEnv * scaling::bipolar (c.osc2FiltEnvPitch) * 24.0f
                                    + filterEnv * scaling::bipolar (c.osc2FiltEnvAmt) * 12.0f;

        const float semis2 = mod.osc2Pitch * 128.0f + mod.osc2Detune * 128.0f / 12.0f
                               + envToPitch2 + scaling::semitones (c.partDetune)
                               + lfoPitch2
                               + fromMiddle * (track2 - 1.0f) + scaling::semitones (c.transpose);

        osc1->setParamMod (osc1Idx.fmRatio,
            osc1->getParameterBase (osc1Idx.fmRatio) * (std::pow (2.0f, semis1 / 12.0f) - 1.0f));

        osc2->setParamMod (osc2Idx.fmRatio,
            osc2->getParameterBase (osc2Idx.fmRatio) * (std::pow (2.0f, semis2 / 12.0f) - 1.0f));
    }

    in[0] = { 0.0f, 0.0f };
    in[1] = { 0.0f, 0.0f };

    // LFO 1 to pulse width and LFO 2 to shape, both fixed paths on the
    // hardware. Pulse width modulation is most of what makes a Virus pad
    // move, so it is worth having even though it costs a parameter write.
    if (c.lfo1PwAmt != 64)
    {
        const float pw = juce::jlimit (0.02f, 0.5f,
            0.5f - scaling::unit (c.osc1Pw) * 0.48f
                 - std::abs (lfo1Value * scaling::bipolar (c.lfo1PwAmt)) * 0.45f);

        osc1->setParameter ("pulseWidth", pw);
        osc2->setParameter ("pulseWidth", pw);
    }

    if (c.lfo2ShapeAmt != 64)
    {
        osc1->setParamMod (osc1->paramIndex ("shape"),
                           lfo2Value * scaling::bipolar (c.lfo2ShapeAmt));
        osc2->setParamMod (osc2->paramIndex ("shape"),
                           lfo2Value * scaling::bipolar (c.lfo2ShapeAmt));
    }

    osc1->processVoiceSample (v, in.data(), out.data());
    const float osc1L = out[0][0], osc1R = out[0][1];

    // Oscillator 2's FM input is fed from oscillator 1, which is exactly how
    // the hardware's FM Amount works - Osc 1 is always the modulator. The
    // module takes it on its fmIn socket, so this needs the socket marked
    // connected or it falls back to no modulation at all.
    const float fmAmount = juce::jmax (0.0f,
        scaling::unit (c.osc2FmAmount)
          * (1.0f + scaling::bipolar (c.fmAmountVelocity) * voice.velocity)
          + lfo2Value * scaling::bipolar (c.lfo2FmAmt)
          + filterEnv * scaling::bipolar (c.fmFiltEnvAmt));

    if (fmAmount > 0.0f)
    {
        in[0] = { (osc1L + osc1R) * 0.5f * fmAmount * 4.0f, 0.0f };
        osc2->setInputConnected (0, true);
    }
    else
    {
        in[0] = { 0.0f, 0.0f };
        osc2->setInputConnected (0, false);
    }

    osc2->processVoiceSample (v, in.data(), out.data());
    const float osc2L = out[0][0], osc2R = out[0][1];

    in[0] = { 0.0f, 0.0f };

    // Osc Balance as a destination tilts the two oscillators against each
    // other on top of whatever the knob says.
    const float balanceMod = juce::jlimit (-1.0f, 1.0f, mod.oscBalance);
    const float g1 = juce::jlimit (0.0f, 2.0f, 1.0f - balanceMod);
    const float g2 = juce::jlimit (0.0f, 2.0f, 1.0f + balanceMod);

    float mixL = osc1L * g1 + osc2L * g2;
    float mixR = osc1R * g1 + osc2R * g2;

    // Ring modulator: the two oscillators multiplied, which is exactly what
    // the hardware's ring mod is and exactly what the module wants.
    if (c.ringModVolume > 0)
    {
        in[0] = { osc1L, osc1R };
        in[1] = { osc2L, osc2R };
        ringMod->processVoiceSample (v, in.data(), out.data());
        const float ringLevel = juce::jlimit (0.0f, 2.0f, 1.0f + mod.ringModVolume);
        mixL += out[0][0] * ringLevel;
        mixR += out[0][1] * ringLevel;
        in[1] = { 0.0f, 0.0f };
    }

    // Oscillator 3, when it is switched on at all.
    if (c.osc3Mode > 0 && c.osc3Volume > 0)
    {
        in[0] = { 0.0f, 0.0f };
        osc3->processVoiceSample (v, in.data(), out.data());
        const float level = juce::jlimit (0.0f, 2.0f, 1.0f + mod.osc3Volume);
        mixL += out[0][0] * level;
        mixR += out[0][1] * level;
    }

    if (c.subVolume > 0)
    {
        in[0] = { 0.0f, 0.0f };
        subOsc->processVoiceSample (v, in.data(), out.data());
        const float level = 1.0f + mod.subVolume;
        mixL += out[0][0] * level;
        mixR += out[0][1] * level;
    }

    if (c.noiseVolume > 0)
    {
        noise->processVoiceSample (v, in.data(), out.data());
        const float level = 1.0f + mod.noiseVolume;
        mixL += out[0][0] * level;
        mixR += out[0][1] * level;
    }

    const float oscLevel = juce::jlimit (0.0f, 2.0f, 1.0f + mod.oscVolume);
    mixL *= oscLevel;
    mixR *= oscLevel;

    //-- filters ---------------------------------------------------------------
    // Cutoff is summed in normalised knob space - base, filter envelope, key
    // follow, matrix - and converted to Hz once.
    // Env Polarity flips the filter envelope for that filter, which is how a
    // patch gets a filter that closes on the attack instead of opening.
    const float polarity1 = c.filter1EnvPolarity == 0 ? -1.0f : 1.0f;
    const float polarity2 = c.filter2EnvPolarity == 0 ? -1.0f : 1.0f;

    const float cut1 = scaling::unit (c.flt1Cut)
                     + filterEnv * polarity1 * (scaling::unit (c.flt1EnvAmt) + mod.filter1EnvAmt
                                                  + scaling::bipolar (c.flt1EnvAmtVel) * voice.velocity)
                     + lfo2Value * scaling::bipolar (c.lfo2Cutoff1Amt)
                     + keyTrack * scaling::bipolar (c.flt1KeyFol)
                     + lfo2Value * scaling::bipolar (c.lfo2Amt) * 0.2f   // LFO 2's own cutoff path
                     + mod.cutoff1;

    const float cut2 = scaling::unit (c.flt2Cut)
                     + filterEnv * polarity2 * (scaling::unit (c.flt2EnvAmt) + mod.filter2EnvAmt
                                                  + scaling::bipolar (c.flt2EnvAmtVel) * voice.velocity)
                     + lfo2Value * scaling::bipolar (c.lfo2Cutoff2Amt)
                     + keyTrack * scaling::bipolar (c.flt2KeyFol)
                     + mod.cutoff2;

    filter1->setParameter ("cutoff", scaling::cutoffHz (cut1));

    // Cutoff Link makes Filter 2 follow Filter 1 at a fixed offset, so one
    // knob sweeps both and keeps the gap between them.
    filter2->setParameter ("cutoff", scaling::cutoffHz (
        (c.filterLink > 0 ? cut1 + scaling::bipolar (c.filterLinkOffset) : cut2)
          + scaling::bipolar (c.cutoff2Offset)));

    // Resonance takes velocity and LFO 1 on the hardware as well as the
    // matrix, which is where a lot of the Virus's bite under the fingers
    // comes from.
    if (c.reso1Vel != 64 || c.lfo1ResoAmt != 64)
        filter1->setParamMod (flt1Idx.resonance,
            scaling::bipolar (c.reso1Vel) * voice.velocity * 0.5f
              + lfo1Value * scaling::bipolar (c.lfo1ResoAmt) * 0.4f
              + mod.reso1 * 0.5f);

    if (c.reso2Vel != 64)
        filter2->setParamMod (flt2Idx.resonance,
            scaling::bipolar (c.reso2Vel) * voice.velocity * 0.5f + mod.reso2 * 0.5f);

    // Routing by NAME, not by index: the list is Serial 4, Serial 6,
    // Parallel 4, Split Mode, so index 1 was being treated as parallel when
    // it is actually the six-pole serial mode.
    const auto& routingNames = choicesFor ("filterRouting");
    const juce::String routing = c.filterRouting < routingNames.size()
                                   ? routingNames[c.filterRouting] : juce::String ("Serial 4");

    const bool parallel = routing.containsIgnoreCase ("Parallel");
    const bool split = routing.containsIgnoreCase ("Split");

    in[0] = { mixL, mixR };
    in[1] = { 0.0f, 0.0f };

    filter1->processVoiceSample (v, in.data(), out.data());
    const float f1L = out[0][0], f1R = out[0][1];

    float voiceL, voiceR;

    if (parallel || split)
    {
        // Filter Balance crossfades the two filters in parallel routing.
        // Split Mode feeds each filter its own oscillator, but from here the
        // summing is the same - the difference is made upstream by the
        // balance, so the two share this path.
        const float bal = juce::jlimit (0.0f, 1.0f,
                            scaling::unit (c.filterBalance) + mod.filterBalance);

        in[0] = { mixL, mixR };
        filter2->processVoiceSample (v, in.data(), out.data());
        voiceL = f1L * (1.0f - bal) + out[0][0] * bal;
        voiceR = f1R * (1.0f - bal) + out[0][1] * bal;
    }
    else
    {
        in[0] = { f1L, f1R };
        filter2->processVoiceSample (v, in.data(), out.data());
        voiceL = out[0][0];
        voiceR = out[0][1];
    }

    //-- amp -------------------------------------------------------------------
    // Amp Velocity sets how much velocity affects loudness at all: at the
    // centre it is the plain velocity scaling, at zero the patch plays at
    // full level however softly it is struck.
    const float velDepth = scaling::bipolar (c.ampVelocity);
    const float velGain = juce::jlimit (0.0f, 2.0f, 1.0f + velDepth * (voice.velocity - 1.0f));

    // Punch adds a short burst of extra level at the start of a note. The
    // hardware does it inside the amplifier, and so do we: it rides on the
    // attack portion of the envelope rather than on a timer of its own.
    const float punch = c.punchIntensity > 0 && ampEnv > 0.0f
                          ? 1.0f + scaling::unit (c.punchIntensity)
                                     * juce::jmax (0.0f, ampEnv - lastAmpEnv[(size_t) v]) * 40.0f
                          : 1.0f;

    lastAmpEnv[(size_t) v] = ampEnv;

    // LFO 1 to filter gain is a level path on the hardware, so it belongs at
    // the amplifier rather than in the filter.
    const float lfoGain = c.lfo1FiltGainAmt != 64
                            ? juce::jlimit (0.0f, 2.0f,
                                1.0f + lfo1Value * scaling::bipolar (c.lfo1FiltGainAmt))
                            : 1.0f;

    const float gain = ampEnv * velGain * punch * lfoGain
                         * juce::jlimit (0.0f, 2.0f, 1.0f + mod.patchVolume);

    // Per-voice pan, which is what makes the matrix's Panorama destination and
    // unison spread audible as movement rather than a static balance.
    const float pan = juce::jlimit (-1.0f, 1.0f,
                        scaling::bipolar (c.panorama) + mod.panorama
                          + scaling::bipolar (c.panoramaVelocity) * voice.velocity
                          + lfo2Value * scaling::bipolar (c.lfo2PanAmt));
    const float panAngle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    outL = voiceL * gain * std::cos (panAngle) * 1.414f;
    outR = voiceR * gain * std::sin (panAngle) * 1.414f;

    // Parameter modulation is per voice, so it has to be wiped before the
    // next voice is evaluated or the last voice's matrix would leak into it.
    for (auto* m : { osc1.get(), osc2.get(), osc3.get(),
                     filter1.get(), filter2.get(), envAmp.get(), envFilter.get(),
                     lfo1.get(), lfo2.get(), lfo3.get() })
        m->clearParamMods();
}

//==============================================================================
void Engine::renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    updateParameters();

    for (auto* m : allModules())
        if (m != nullptr)
            m->blockStart();

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    const int numSamples = buffer.getNumSamples();
    // Patch Volume is the patch's own level and is a plain 0..127.
    //
    // Channel Volume and Balance are NOT: they are MIDI channel controllers
    // (CC 7 and CC 8), and a patch stores whatever they happened to be. Their
    // defaults in the parameter table are 0, so reading them as gains muted
    // every patch and panned what was left hard left. They come from the live
    // controller table instead, which starts at unity.
    //
    // Part Volume is bipolar - 64 is unity, not half - so it is a trim in
    // octaves rather than a fraction.
    const float partTrim = std::pow (2.0f, scaling::bipolar (c.partVolume));

    const float patchGain = scaling::unit (c.patchVolume)
                              * partTrim
                              * controllers[7];

    const float balanceTilt = (controllers[8] - 0.5f) * 2.0f;

    int sample = 0;

    // One sample of the whole instrument: every live voice, then the rack.
    const auto renderOneSample = [&] (int index)
    {
        // The plugin's own input, read before it is overwritten - the vocoder
        // and the input follower both want it.
        inputL = left[index];
        inputR = right[index];

        // Input follower: tracks the level of whatever is plugged in, which
        // the hardware uses as a modulation source and for the vocoder.
        if (c.followerMode > 0)
        {
            in[0] = { inputL, inputR };
            envFollow->setInputConnected (0, true);
            envFollow->processSample (in.data(), out.data());
            followerValue = out[0][0];
        }

        advanceArpeggiator();

        float l = 0.0f, r = 0.0f;

        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& voice = voices[(size_t) v];

            if (! voice.active)
                continue;

            float vl, vr;
            renderVoice (v, vl, vr);
            l += vl;
            r += vr;

            if (! voice.held && (voice.releaseCountdown -= 1.0f) <= 0.0f)
                voice.active = false;
        }

        l *= patchGain;
        r *= patchGain;

        renderEffects (l, r);

        // Soft limit before the DC blocker. Resonant filters, distortion and
        // stacked unison can all push a voice past full scale; the hardware
        // has an output stage that gives way gracefully rather than clipping
        // square, and this is the same idea: linear up to about -6 dB, then
        // increasingly gentle, never past 1.
        const auto softLimit = [] (float x)
        {
            constexpr float threshold = 0.5f;

            if (x > threshold)       return threshold + (1.0f - threshold) * std::tanh ((x - threshold) / (1.0f - threshold));
            if (x < -threshold)      return -threshold + (1.0f - threshold) * std::tanh ((x + threshold) / (1.0f - threshold));
            return x;
        };

        l = softLimit (l);
        r = softLimit (r);

        // AC couple the output at about 5 Hz.
        constexpr float dcR = 0.9993f;
        const float outL = l - dcX1L + dcR * dcY1L;
        const float outR = r - dcX1R + dcR * dcY1R;
        dcX1L = l; dcY1L = outL;
        dcX1R = r; dcY1R = outR;
        l = outL; r = outR;

        // Second Output Balance sends part of the patch to the second output
        // pair instead of the main one. At the centre everything stays here.
        if (secondOut != nullptr)
        {
            const float toSecond = juce::jmax (0.0f, scaling::bipolar (c.secondOutputBalance));
            const float toMain = 1.0f - toSecond;

            secondOut[0][index] = l * toSecond;
            secondOut[1][index] = r * toSecond;

            l *= toMain;
            r *= toMain;
        }

        if (balanceTilt != 0.0f)
        {
            l *= juce::jlimit (0.0f, 1.0f, 1.0f - balanceTilt);
            r *= juce::jlimit (0.0f, 1.0f, 1.0f + balanceTilt);
        }

        left[index] = l;
        right[index] = r;
    };

    for (const auto meta : midi)
    {
        const auto message = meta.getMessage();
        const int eventSample = juce::jlimit (0, numSamples, meta.samplePosition);

        for (; sample < eventSample; ++sample)
            renderOneSample (sample);

        if (message.isNoteOn())
        {
            // With the arp running, a key press feeds the arpeggiator rather
            // than sounding a voice directly; the arp decides what plays.
            heldKeys[(size_t) message.getNoteNumber()] = true;

            if (arpRunning)
                arp->midiDriverHeldNoteOn (message.getNoteNumber());
            else
                noteOn (message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff())
        {
            heldKeys[(size_t) message.getNoteNumber()] = false;

            if (arpRunning)
            {
                // Arp Hold keeps the pattern running after the keys are let
                // go, which is the whole point of the button.
                if (c.arpHold == 0)
                    arp->midiDriverHeldNoteOff (message.getNoteNumber());
            }
            else
            {
                noteOff (message.getNoteNumber(), message.getFloatVelocity());
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            arp->midiDriverAllHeldOff();
            heldKeys.fill (false);
            allNotesOff();
        }
        else if (message.isPitchWheel())
            pitchBend = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f;
        else if (message.isChannelPressure())
            channelPressure = (float) message.getChannelPressureValue() / 127.0f;
        else if (message.isAftertouch())
            channelPressure = (float) message.getAfterTouchValue() / 127.0f;
        else if (message.isController())
            controllers[(size_t) juce::jlimit (0, 127, message.getControllerNumber())]
                = (float) message.getControllerValue() / 127.0f;
    }

    for (; sample < numSamples; ++sample)
        renderOneSample (sample);
}

} // namespace aquavibrio
