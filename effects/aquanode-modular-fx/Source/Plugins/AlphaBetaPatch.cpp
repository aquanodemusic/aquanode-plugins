//==============================================================================
// AlphaBeta - Patch version (the Original engine: the FreeAlpha 3.3 model)
//
// AlphaBeta's original engine voice, taken apart along its own signal flow:
//
//   Osc 1 --+                                                    +- Chorus - Output
//           +- Balance & Drive - Filter - Amp --(voices summed)--+
//   Osc 2 --+                      |       |
//          Filter Env -------------+       +- Amp Env
//   LFO, and the Mod Matrix: 11 rows of source -> destination -> amount, out
//   to every destination the original has (pitch, symmetry and level of each
//   oscillator, cutoff, resonance, main level and pitch, LFO speed).
//
// The modules run the engine's own measured DSP (AlphaOrigEngine.h): the
// mip-mapped tables of the 30 waves with the symmetry warp, the Chamberlin SVF
// with its LP24+ variant, the drive curves, the 576-sample envelope grid, the
// LFO, the 4-tap chorus and the limiter - so a patch built from a preset
// sounds like the plugin's Original engine.
//
// Where the patch differs: the plugin evaluates its matrix once every 16
// samples and ramps between; the modules do it every sample. Mono legato
// (Voices = mono) is emulated - each new note's voice takes over the sounding
// voice's envelopes, filter, LFO and phases when "Legato" is on.
//==============================================================================
#include "PluginAlphaBetaConfig.h"

namespace aqp_alphabeta
{
    #include AQP_ALPHABETA_SRC(PluginProcessor.h)
    #include AQP_ALPHABETA_SRC(AlphaOrigPreset.h)
    #include AQP_ALPHABETA_SRC(EngineTransfer.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;
namespace AO = aqp_alphabeta::AlphaOrig;

namespace
{
    const juce::String kPlugin { "alphabeta" };

    juce::StringArray list (const char* const* names, int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i)
            a.add (names[i]);
        return a;
    }

    juce::StringArray voiceChoices()
    {
        juce::StringArray a { "mono" };
        for (int i = 2; i <= kMaxVoices; ++i)
            a.add (juce::String (i));
        return a;
    }

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.alphabeta." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    using LegatoTracker = kit::LegatoTracker;   // AlphaBeta's mono legato
}

//==============================================================================
// AlphaBeta - Envelope (used twice: Amp Env and Filter Env)
//==============================================================================
// AlphaBeta's control rate: the original evaluates its matrix, filter
// coefficients, pitch and levels once per 16-sample block (counted from the
// note-on, where the plugin splits its rendering) from the envelope and LFO
// values at the block's start, and ramps to them across the block. The
// modules below keep that clock per voice, so sharp attacks into a closed
// filter behave exactly as in the plugin.
constexpr int kCtlBlock = 16;

class AlphaBetaEnvModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("env", "Envelope", ModuleSection::Utility, 2);
        d.description = "AlphaBeta's envelope: attack, decay, sustain and release with the original's curves, "
                        "plus Fade - while a key is held the sustain drifts up (+) or down (-), faster the "
                        "further from 0. Env Out is 0..1; Ctrl Out is the level the matrix and filter read "
                        "(before the sample's step, as in the plugin). Legato continues the sounding note's envelope.";
        d.sockets = { modOut ("envOut", "Env Out"), modOut ("ctrlOut", "Ctrl Out") };
        d.params = {
            makeRotary ("attack",  "Attack",  0.001f, 10.0f, 0.001f, 0, "s", true),
            makeRotary ("decay",   "Decay",   0.001f, 10.0f, 0.001f, 0, "s", true),
            makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 1.0f, 0),
            makeRotary ("fade",    "Fade",    -10.0f, 10.0f, 0.0f, 0),
            makeRotary ("release", "Release", 0.001f, 10.0f, 0.001f, 0, "s", true),
            makeCombo ("legato", "Legato", { "Off", "On" }, 0, 1, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (auto& e : env) { e.setSampleRate (sr); e.kill(); }
        tracker.reset();
    }

    void reset() override { for (auto& e : env) e.kill(); tracker.reset(); }
    void voiceReset (int v) override { env[(size_t) v].kill(); }

    void voiceNoteOn (int v, int, bool) override
    {
        const auto p = paramsNow();
        const int from = tracker.noteOn (v, param (5) >= 0.5f);
        if (from >= 0)
        {
            env[(size_t) v] = env[(size_t) from];   // legato: the line goes on where it was
            return;
        }
        env[(size_t) v].noteOn (p, ! env[(size_t) v].active());
    }

    void voiceNoteOff (int v) override
    {
        tracker.noteOff();
        env[(size_t) v].noteOff (paramsNow());
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        const float before = (float) env[(size_t) v].value();
        const float e = (float) env[(size_t) v].next (paramsNow());
        out[0] = { e, 0.0f };
        out[1] = { before, 0.0f };
    }

    double voiceTailSeconds() const override { return getParameterBase (4) * 1.01 + 0.05; }

private:
    AO::EnvParams paramsNow() const
    {
        AO::EnvParams p;
        p.attack = param (0); p.decay = param (1); p.sustain = param (2);
        p.fade = param (3); p.release = param (4);
        return p;
    }

    std::array<AO::Envelope, kMaxVoices> env;
    LegatoTracker tracker;
};

//==============================================================================
// AlphaBeta - LFO
//==============================================================================
class AlphaBetaLfoModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("lfo", "LFO", ModuleSection::Utility, 3);
        d.description = "AlphaBeta's LFO, restarting with every note: six waves (Noise and S&H at 8 values "
                        "per cycle), a rate or a tempo sync, and an attack fading it in. Speed Mod changes "
                        "the rate by up to 4 octaves each way (the matrix's LFO 1 Speed).";
        d.sockets = { modIn ("speedIn", "Speed Mod"), modOut ("lfoOut", "LFO Out"), modOut ("ctrlOut", "Ctrl Out") };
        d.params = {
            makeCombo ("wave", "Wave", list (AO::kLfoWaveNames, 6), 1, 0, 2),
            makeRotary ("rate", "Rate", 0.01f, 32.0f, 1.0f, 0, "Hz", true),
            makeRotary ("attack", "Attack", 0.0f, 10.0f, 0.0f, 0, "s"),
            makeSteppedList ("sync", "Sync", list (AO::kSyncNames, AO::kNumSync), 0, 0),
            makeCombo ("legato", "Legato", { "Off", "On" }, 0, 1, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); tracker.reset(); }
    void reset() override { tracker.reset(); }

    void voiceNoteOn (int v, int, bool) override
    {
        const int from = tracker.noteOn (v, param (4) >= 0.5f);
        if (from >= 0) { lfo[(size_t) v] = lfo[(size_t) from]; ctl[(size_t) v] = ctl[(size_t) from]; return; }
        lfo[(size_t) v].reset (++seed);
        ctl[(size_t) v] = {};
    }
    void voiceNoteOff (int) override { tracker.noteOff(); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& c = ctl[(size_t) v];
        if (c.k == 0)
        {
            // the rate (and the matrix's speed row) once per control block
            const int sync = (int) param (3);
            c.hz = sync > 0 ? (tempoBpm / 60.0) / AO::syncBeats (sync) : (double) param (1);
            c.hz *= std::exp2 (4.0 * in[0][0]);
        }
        c.k = (c.k + 1) % kCtlBlock;
        const float value = lfo[(size_t) v].tick ((int) param (0), c.hz, param (2), 1.0 / sampleRate);
        out[0] = { value, 0.0f };
        out[1] = { c.last, 0.0f };   // what the matrix sees: the previous sample's value
        c.last = value;
    }

private:
    struct Clock { int k { 0 }; double hz { 1.0 }; float last { 0.0f }; };
    std::array<Clock, kMaxVoices> ctl {};
    std::array<AO::LFO, kMaxVoices> lfo;
    LegatoTracker tracker;
    uint32_t seed { 1 };
};

//==============================================================================
// AlphaBeta - Mod Matrix
//==============================================================================
class AlphaBetaMatrixModule final : public SynthModule
{
public:
    enum Out { oOsc1Amp, oOsc1Pitch, oOsc1Sym, oOsc2Amp, oOsc2Pitch, oOsc2Sym,
               oCutoff, oReso, oMainAmp, oMainPitch, oLfoSpeed, kNumOuts };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("matrix", "Mod Matrix", ModuleSection::Utility, 4);
        d.description = "AlphaBeta's modulation matrix: 11 rows of source, destination and amount, the "
                        "CRT screen of the plugin. Note, velocity, wheels and pressure are built in; patch "
                        "the two envelopes and the LFO into its inputs. Rows 1-8 can be modulated by rows "
                        "aimed at Matrix Depth 1-8. Amp outputs are gain factors (1 = no change).";
        d.sockets = {
            modIn ("ampEnvIn", "Amp Env"), modIn ("filterEnvIn", "Filter Env"), modIn ("lfoIn", "LFO"),
            modOut ("osc1Amp", "Osc 1 Amp"), modOut ("osc1Pitch", "Osc 1 Pitch"), modOut ("osc1Sym", "Osc 1 Sym"),
            modOut ("osc2Amp", "Osc 2 Amp"), modOut ("osc2Pitch", "Osc 2 Pitch"), modOut ("osc2Sym", "Osc 2 Sym"),
            modOut ("cutoff", "Cutoff"), modOut ("resonance", "Resonance"), modOut ("mainAmp", "Main Amp"),
            modOut ("mainPitch", "Main Pitch"), modOut ("lfoSpeed", "LFO Speed")
        };
        const auto srcs = list (AO::kSrcNames, AO::NUM_SRC), dsts = list (AO::kDstNames, AO::NUM_DST);
        for (int i = 0; i < AO::kNumRows; ++i)
        {
            const auto n = juce::String (i + 1);
            d.params.push_back (makeCombo ("src" + n, "Source " + n, srcs, 0, i, 2));
            d.params.push_back (makeHBar ("amt" + n, "Amount", -100.0f, 100.0f, 0.0f, i, "%", 1));
            d.params.push_back (makeCombo ("dst" + n, "Dest " + n, dsts, 0, i, 2));
        }
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    int preferredModuleWidth() const override { return 460; }

    void voiceNoteOn (int v, int note, bool) override { key[(size_t) v] = note; }
    void voiceVelocity (int v, float vel) override { velocity[(size_t) v] = juce::roundToInt (vel * 127.0f); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        double src[AO::NUM_SRC] = {};
        src[AO::SRC_NOTE_LIN] = src[AO::SRC_NOTE_LOG] = (key[(size_t) v] - 63) / 48.0;
        src[AO::SRC_VELOCITY] = 2.0 * velocity[(size_t) v] / 127.0 - 1.0;
        if (midiControls != nullptr)
        {
            src[AO::SRC_AFTERTOUCH] = midiControls->aftertouch;
            src[AO::SRC_PITCHWHEEL] = midiControls->bend;
            src[AO::SRC_MODWHEEL] = midiControls->modWheel;
        }
        src[AO::SRC_AMPENV] = in[0][0];
        src[AO::SRC_FILTENV] = in[1][0];
        src[AO::SRC_LFO1] = in[2][0];
        src[AO::SRC_CONSTANT] = 1.0;

        int rs[AO::kNumRows], rd[AO::kNumRows];
        double ra[AO::kNumRows];
        for (int i = 0; i < AO::kNumRows; ++i)
        {
            rs[i] = (int) param (i * 3);
            ra[i] = param (i * 3 + 1) / 100.0;
            rd[i] = (int) param (i * 3 + 2);
        }

        // the original's two passes: depth rows first, then everything else
        double depth[8] = {};
        for (int i = 0; i < AO::kNumRows; ++i)
            if (rd[i] >= AO::DST_DEPTH1 && rd[i] <= AO::DST_DEPTH8 && rs[i] != AO::SRC_OFF)
                depth[rd[i] - AO::DST_DEPTH1] += src[rs[i]] * ra[i];

        double pitch1 = 0, pitch2 = 0, pitchM = 0, sym1 = 0, sym2 = 0, cut = 0, res = 0, speed = 0;
        double amp1 = 1, amp2 = 1, ampM = 1;
        for (int i = 0; i < AO::kNumRows; ++i)
        {
            if (rs[i] == AO::SRC_OFF || rd[i] == AO::DST_OFF || (rd[i] >= AO::DST_DEPTH1 && rd[i] <= AO::DST_DEPTH8))
                continue;
            double a = ra[i];
            if (i < 8) a += depth[i];
            const double m = a * src[rs[i]];
            switch (rd[i])
            {
                case AO::DST_OSC1_AMP:   amp1 *= 1.0 + m; break;
                case AO::DST_OSC2_AMP:   amp2 *= 1.0 + m; break;
                case AO::DST_MAIN_AMP:   ampM *= 1.0 + m; break;
                case AO::DST_OSC1_PITCH: pitch1 += m; break;
                case AO::DST_OSC2_PITCH: pitch2 += m; break;
                case AO::DST_MAIN_PITCH: pitchM += m; break;
                case AO::DST_OSC1_SYM:   sym1 += m; break;
                case AO::DST_OSC2_SYM:   sym2 += m; break;
                case AO::DST_CUTOFF:     cut += m; break;
                case AO::DST_RESONANCE:  res += m; break;
                case AO::DST_LFO_SPEED:  speed += m; break;
                default: break;
            }
        }

        const double values[kNumOuts] = { amp1, pitch1, sym1, amp2, pitch2, sym2, cut, res, ampM, pitchM, speed };
        for (int o = 0; o < kNumOuts; ++o)
            out[o] = { (float) values[o], 0.0f };
    }

private:
    std::array<int, kMaxVoices> key {}, velocity {};
};

//==============================================================================
// AlphaBeta - Oscillator
//==============================================================================
class AlphaBetaOscModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("osc", "Oscillator", ModuleSection::Oscillator, 0);
        d.description = "One AlphaBeta oscillator: two of the 30 waves (A and B) at their own ranges, "
                        "crossfaded by Mix, with the symmetry warp, Detune and Spread (three copies). "
                        "Pitch Mod takes the matrix's pitch rows, Amp Mod its level factor. Voices and "
                        "Glide are the plugin's.";
        d.sockets = {
            modIn ("pitchIn", "Pitch Mod"), modIn ("symIn", "Sym Mod"), modIn ("ampIn", "Amp Mod"),
            audioOut ("audioOut", "Audio Out")
        };
        const auto waves = list (aqp_alphabeta::AlphaOrigData::kWaveNames, aqp_alphabeta::AlphaOrigData::kNumWaves);
        const auto ranges = list (AO::kRangeNames, 5);
        d.params = {
            makeCombo ("waveA", "Wave A", waves, 2, 0, 2),
            makeSteppedList ("rangeA", "Range A", ranges, 2, 0),
            makeCombo ("which", "Osc", { "Osc 1", "Osc 2" }, 0, 0, 2),
            makeCombo ("waveB", "Wave B", waves, 3, 1, 2),
            makeSteppedList ("rangeB", "Range B", ranges, 2, 1),
            makeRotary ("mix", "Mix", 0.0f, 1.0f, 0.5f, 1),
            makeRotary ("detune", "Detune", -100.0f, 100.0f, 0.0f, 1, "ct"),
            makeRotary ("spread", "Spread", 0.0f, 50.0f, 0.0f, 2, "ct"),
            makeCombo ("glideMode", "Glide", list (AO::kGlideModeNames, 4), 0, 2, 1),
            makeRotary ("glideTime", "Glide Time", 0.001f, 4.0f, 0.03f, 2, "s", true),
            makeRotary ("glideBend", "Bend", -48.0f, 48.0f, 0.0f, 2, "st", false, 1.0f),
            makeSteppedList ("voices", "Voices", voiceChoices(), kMaxVoices - 1, 3).noMod(),
            makeCombo ("legato", "Legato", { "Off", "On" }, 0, 3, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        tables = &AO::Tables::get (sr);
        for (auto& vs : voices) vs = {};
        pool.resetAll();
        tracker.reset();
        lastSemis = -1.0;
    }

    void reset() override
    {
        for (auto& vs : voices) vs = {};
        pool.resetAll();
        tracker.reset();
    }

    void voiceReset (int v) override { voices[(size_t) v] = {}; pool.resetVoice (v); }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& vs = voices[(size_t) v];
        const int mode = (int) param (8);
        const bool anyHeld = tracker.held > 0;
        const bool glideAllowed = (mode == 2 || (mode == 1 && anyHeld)) && lastSemis > 0.0;
        const int from = tracker.noteOn (v, param (12) >= 0.5f);

        if (from >= 0)
        {
            vs = voices[(size_t) from];   // phases and the sounding pitch go on
            vs.target = (double) note;
            vs.glideRate = glideAllowed ? std::abs (vs.target - vs.cur) / juce::jmax (1.0e-4, (double) param (9)) : 0.0;
            if (vs.glideRate == 0.0) vs.cur = vs.target;
        }
        else
        {
            vs = {};
            vs.target = (double) note;
            if (mode == 3)
            {
                vs.cur = vs.target + param (10);
                vs.glideRate = std::abs (param (10)) / juce::jmax (1.0e-4, (double) param (9));
            }
            else if (glideAllowed)
            {
                vs.cur = lastSemis;
                vs.glideRate = std::abs (vs.target - vs.cur) / juce::jmax (1.0e-4, (double) param (9));
            }
            else
            {
                vs.cur = vs.target;
            }
        }
        lastSemis = note;
        pool.noteOn (v, voiceLimit());
    }

    void voiceNoteOff (int v) override
    {
        tracker.noteOff();
        pool.noteOff (v, voiceLimit());
    }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& vs = voices[(size_t) v];
        const int which = param (2) >= 0.5f ? 1 : 0;

        if (vs.k == 0)
        {
            // one control block: glide, pitch, wave zones, symmetry and level
            // targets, ramped to across the block (AlphaOrig::Voice::render)
            const double dt = 1.0 / sampleRate;
            if (vs.glideRate > 0.0)
            {
                const double step = vs.glideRate * dt * kCtlBlock;
                if (std::abs (vs.target - vs.cur) <= step) { vs.cur = vs.target; vs.glideRate = 0.0; }
                else vs.cur += vs.target > vs.cur ? step : -step;
            }

            const int rA = (int) param (1) - 2, rB = (int) param (4) - 2;
            const int rmin = juce::jmin (rA, rB);
            vs.multA = 1 << (rA - rmin); vs.multB = 1 << (rB - rmin);

            const double detuneDisplay = param (6);
            const double det = (detuneDisplay >= 0 ? 1.0 : -1.0)
                             * 4.015 * std::floor (std::abs (detuneDisplay) * 10.0 + 1e-6) / 10.0 / 100.0;
            const double semis = vs.cur + pitchBendSemitones() + AO::pitchModSemis (in[0][0]) + det;
            const double hz = 440.0 * std::exp2 ((semis - 69.0) / 12.0 + rmin);
            vs.tabA = tables->table ((int) param (0), AO::Tables::zoneFor (hz * vs.multA));
            vs.tabB = tables->table ((int) param (3), AO::Tables::zoneFor (hz * vs.multB));

            const double incT = hz / sampleRate, sT = AO::symFromMod (in[1][0]);
            const double ampT = isInputConnected (2) ? (double) in[2][0] : 1.0;
            if (vs.first) { vs.inc = incT; vs.sym = sT; vs.amp = ampT; vs.first = false; }
            vs.dInc = (incT - vs.inc) / kCtlBlock;
            vs.dSym = (sT - vs.sym) / kCtlBlock;
            vs.dAmp = (ampT - vs.amp) / kCtlBlock;
        }
        vs.k = (vs.k + 1) % kCtlBlock;

        const float* tabA = vs.tabA;
        const float* tabB = vs.tabB;
        const int multA = vs.multA, multB = vs.multB;
        const double s = vs.sym;
        const double mix = param (5);
        const double inc = vs.inc;

        auto oscSample = [&] (double& ph, double incr)
        {
            const double w = AO::symWarp (ph, s);
            double a = 0.0, b = 0.0;
            if (mix < 1.0) { double q = w * multA; q -= std::floor (q); a = AO::tableRead (tabA, q); }
            if (mix > 0.0) { double q = w * multB; q -= std::floor (q); b = AO::tableRead (tabB, q); }
            ph += incr;
            if (ph >= 1.0) ph -= std::floor (ph);
            return a + (b - a) * mix;
        };

        double o = 0.0;
        const double spread = param (7);
        if (spread <= 1.0e-6)
        {
            o = oscSample (vs.phase, inc);
        }
        else
        {
            // three copies, starting in phase, each at half level (measured)
            static const double d1[3] = { 0.0, -0.001008, 0.000948 }, d2[3] = { 0.0, -0.0011704, 0.0010982 };
            const double* d = which == 0 ? d1 : d2;
            o += oscSample (vs.phase, inc * (1.0 + d[0] * spread));
            o += oscSample (vs.spread[0], inc * (1.0 + d[1] * spread));
            o += oscSample (vs.spread[1], inc * (1.0 + d[2] * spread));
            o *= 0.5;
        }

        // the plugin steps its ramps after the oscillators, before the level
        vs.amp += vs.dAmp; vs.inc += vs.dInc; vs.sym += vs.dSym;
        const float y = (float) (o * vs.amp) * pool.nextGain (v, sampleRate);
        out[0] = { y, y };
    }

private:
    struct VoiceState
    {
        double phase { 0.0 }, spread[2] {};
        double cur { 60.0 }, target { 60.0 }, glideRate { 0.0 };
        // the control block
        int k { 0 }; bool first { true };
        double inc { 0.0 }, sym { 0.49 }, amp { 1.0 }, dInc { 0.0 }, dSym { 0.0 }, dAmp { 0.0 };
        const float* tabA { nullptr }; const float* tabB { nullptr };
        int multA { 1 }, multB { 1 };
    };

    std::array<VoiceState, kMaxVoices> voices {};
    const AO::Tables* tables { nullptr };
    ModuleVoicePool pool;
    LegatoTracker tracker;
    double lastSemis { -1.0 };
};

//==============================================================================
// AlphaBeta - Balance & Drive
//==============================================================================
class AlphaBetaDriveModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("drive", "Balance & Drive", ModuleSection::Effect, 5);
        d.description = "Where AlphaBeta's two oscillators meet: Balance crossfades Osc 1 and Osc 2, then the "
                        "measured 'Hard' drive saturates them on their way into the filter.";
        d.sockets = { audioIn ("osc1In", "Osc 1 In"), audioIn ("osc2In", "Osc 2 In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("balance", "Balance", 0.0f, 1.0f, 0.5f, 0),
            makeRotary ("drive",   "Drive",   0.0f, 1.0f, 0.0f, 0)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override { process (in, out); }
    void processSample (const StereoFrame* in, StereoFrame* out) override { process (in, out); }

private:
    void process (const StereoFrame* in, StereoFrame* out)
    {
        const float d = param (1);
        if (d != lastDrive) { lastDrive = d; drive.set (d); }
        const double bal = param (0);
        const double x = 0.35 * (in[0][0] * (1.0 - bal) + in[1][0] * bal);
        const float y = (float) drive.process (x);
        out[0] = { y, y };
    }

    AO::Drive drive;
    float lastDrive { -1.0f };
};

//==============================================================================
// AlphaBeta - Filter
//==============================================================================
class AlphaBetaFilterModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("filter", "Filter", ModuleSection::Filter, 1);
        d.description = "AlphaBeta's smooth filter: the original's Chamberlin state-variable filter with its "
                        "cutoff and resonance laws, LP24+ being the two-stage 'alternative design'. Env Depth "
                        "moves the cutoff toward 20 kHz (+) or toward the bottom (-) with the Env In.";
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("envIn", "Env In"), modIn ("cutIn", "Cutoff Mod"),
                      modIn ("resIn", "Reso Mod"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("type", "Type", list (AO::kFilterNames, 5), 0, 0, 2),
            makeRotary ("cutoff", "Cutoff", 40.0f, 20000.0f, 20000.0f, 0, "Hz", true),
            makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.0f, 0),
            makeRotary ("envDepth", "Env Depth", -1.0f, 1.0f, 0.0f, 0),
            makeCombo ("legato", "Legato", { "Off", "On" }, 0, 1, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& f : filt) f = {}; tracker.reset(); }
    void voiceReset (int v) override { filt[(size_t) v] = {}; }

    void voiceNoteOn (int v, int, bool) override
    {
        const int from = tracker.noteOn (v, param (4) >= 0.5f);
        if (from >= 0)
            filt[(size_t) v] = filt[(size_t) from];
        else
        {
            if (! filt[(size_t) v].sounding)
                filt[(size_t) v] = {};
            // a new note starts a new control clock (the plugin's first block)
            filt[(size_t) v].k = 0;
            filt[(size_t) v].first = true;
        }
        filt[(size_t) v].sounding = true;
    }
    void voiceNoteOff (int) override { tracker.noteOff(); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& f = filt[(size_t) v];
        if (f.k == 0)
        {
            // coefficient targets once per control block, from the filter
            // envelope and the matrix at the block's start
            // (the cutoff knob in Hz is the original's knob law already
            // applied: u = log2 (Hz / 78.125) / 8, AlphaOrig::knobToU)
            const double u0 = std::log2 (juce::jmax (1.0f, param (1)) / 78.125) / 8.0 + in[2][0];
            const double fe = in[1][0], depth = param (3);
            const double u = depth >= 0 ? u0 + depth * fe * (1.0 - u0) : u0 * (1.0 + depth * fe);
            const double FT = AO::cutoffCoefAt (u, sampleRate);
            const double qT = AO::resoDamp (param (2) * (1.0 + in[3][0]));
            const double rT = juce::jlimit (0.0, 1.0, (double) param (2) * (1.0 + in[3][0]));
            if (f.first) { f.F = FT; f.q = qT; f.r = rT; f.first = false; }
            f.dF = (FT - f.F) / kCtlBlock; f.dq = (qT - f.q) / kCtlBlock; f.dr = (rT - f.r) / kCtlBlock;
        }
        f.k = (f.k + 1) % kCtlBlock;
        f.F += f.dF; f.q += f.dq; f.r += f.dr;
        const double F = f.F, q = f.q, rr = f.r;
        const double x = in[0][0];

        double y;
        switch ((int) param (0))
        {
            case 1:  y = f.f2.processLPfree (f.f1.processLP (x, F, q), F, 1.0); break;
            case 2:  y = f.lad.process (x, F, rr); break;
            case 3:  y = f.f1.processBP (x, F, q); break;
            case 4:  y = f.f1.processHP (x, F, q); break;
            default: y = f.f1.processLP (x, F, q); break;
        }
        out[0] = { (float) y, (float) y };
    }

private:
    struct State
    {
        AO::SVF f1, f2; AO::LP24Plus lad; bool sounding { false };
        int k { 0 }; bool first { true };
        double F { 1.0 }, q { 1.0 }, r { 0.0 }, dF { 0.0 }, dq { 0.0 }, dr { 0.0 };
    };
    std::array<State, kMaxVoices> filt {};
    LegatoTracker tracker;
};

//==============================================================================
// AlphaBeta - Amp
//==============================================================================
class AlphaBetaAmpModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("amp", "Amp", ModuleSection::Utility, 6);
        d.description = "AlphaBeta's voice amplifier: the amp envelope, the velocity (Velocity sets how much "
                        "it matters, with the original's curve) and the matrix's Main Amplitude factor.";
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("envIn", "Env In"), modIn ("ampIn", "Amp Mod"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = { makeRotary ("velocity", "Velocity", 0.0f, 1.0f, 0.73f, 0) };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void voiceVelocity (int v, float vel) override { velocity[(size_t) v] = (float) juce::roundToInt (vel * 127.0f); }
    void voiceNoteOn (int v, int, bool) override { ctl[(size_t) v] = {}; }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& c = ctl[(size_t) v];
        if (c.k == 0)
        {
            // the matrix's Main Amplitude factor: once per control block, ramped
            const double t = isInputConnected (2) ? (double) in[2][0] : 1.0;
            if (c.first) { c.amp = t; c.first = false; }
            c.dAmp = (t - c.amp) / kCtlBlock;
        }
        c.k = (c.k + 1) % kCtlBlock;
        c.amp += c.dAmp;

        const double velGain = 1.0 - param (0) * (1.0 - velocity[(size_t) v] / 127.0);
        const float g = (float) (in[1][0] * velGain * c.amp);
        out[0] = { in[0][0] * g, in[0][1] * g };
    }

private:
    struct Clock { int k { 0 }; bool first { true }; double amp { 1.0 }, dAmp { 0.0 }; };
    std::array<Clock, kMaxVoices> ctl {};
    std::array<float, kMaxVoices> velocity {};
};

//==============================================================================
// AlphaBeta - Chorus
//==============================================================================
class AlphaBetaChorusModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("chorus", "Chorus", ModuleSection::Effect, 7);
        d.description = "AlphaBeta's chorus as measured: one delay line, two triangle-swept taps per side at "
                        "1.0 / 0.7 / 1.3 / 1.0 times Rate. It is what washes the filter sweeps out.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("wet", "Wet", 0.0f, 1.0f, 0.6f, 0),
            makeRotary ("time", "Time", 0.01f, 6.0f, 2.0f, 0, "ms", true),
            makeRotary ("rate", "Rate", 0.02f, 4.0f, 0.5f, 0, "Hz", true)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); chorus.prepare (sr); chorus.reset (param (2)); }
    void reset() override { chorus.reset (param (2)); }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = 0.5f * (in[0][0] + in[0][1]), r = l;   // the voices are mono
        chorus.process (&l, &r, 1, param (0), param (1), param (2));
        out[0] = { l, r };
    }

private:
    AO::Chorus chorus;
};

//==============================================================================
// AlphaBeta - Output
//==============================================================================
class AlphaBetaOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Effect, 8);
        d.description = "The end of AlphaBeta's chain: Volume in the original's 0.2 dB steps, the 9.6 Hz DC "
                        "blocker and the output limiter.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = { makeRotary ("volume", "Volume", -18.2f, 18.0f, 0.0f, 0, "dB") };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        limiter.prepare (sr);
        hpR = 1.0 - 2.0 * AO::kPi * 9.57 / sr;
        reset();
    }
    void reset() override { hpX[0] = hpX[1] = hpY[0] = hpY[1] = 0.0; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const double vg = param (0) <= -18.19f ? 0.0 : std::pow (10.0, param (0) / 20.0);
        float lr[2];
        for (int c = 0; c < 2; ++c)
        {
            const double x = in[0][(size_t) c] * vg;
            const double y = x - hpX[c] + hpR * hpY[c];
            hpX[c] = x; hpY[c] = y;
            lr[c] = (float) y;
        }
        limiter.process (&lr[0], &lr[1], 1);
        out[0] = { lr[0], lr[1] };
    }

private:
    AO::Limiter limiter;
    double hpR { 0.9986 }, hpX[2] {}, hpY[2] {};
};

AQUANODE_REGISTER_MODULE (AlphaBetaOscModule,    AlphaBetaOscModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaDriveModule,  AlphaBetaDriveModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaFilterModule, AlphaBetaFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaEnvModule,    AlphaBetaEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaLfoModule,    AlphaBetaLfoModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaMatrixModule, AlphaBetaMatrixModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaAmpModule,    AlphaBetaAmpModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaChorusModule, AlphaBetaChorusModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaOutputModule, AlphaBetaOutputModule::descriptor)

//==============================================================================
// The patch builder
//==============================================================================
namespace aquanode::plugins::alphabeta
{
    namespace OP = aqp_alphabeta::OrigP;

    // The plugin's Original-engine settings in the engine's own units, read
    // the way the plugin reads them on its audio thread. A sound made on the
    // Native engine is first translated by the plugin's own engine transfer -
    // the same as pressing TRANSFER in AlphaBeta.
    static AO::Patch readPatch (juce::AudioProcessor& original)
    {
        auto* ab = dynamic_cast<aqp_alphabeta::AlphaBetaAudioProcessor*> (&original);
        if (ab == nullptr)
            return {};
        if (ab->apvts.getRawParameterValue (OP::ENGINE)->load() < 0.5f)
            aqp_alphabeta::EngineTransfer::nativeToOriginal (ab->apvts);
        OP::Cache cache;
        cache.init (ab->apvts);
        return cache.build (120.0);
    }

    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        const auto P = readPatch (original);
        kit::Builder b { patch, origin };

        const bool mono = P.voices == 1;
        const float voicesIndex = P.voices == 0 ? (float) (kMaxVoices - 1) : (float) juce::jlimit (0, kMaxVoices - 1, P.voices - 1);
        auto knobToHz = [] (double knob) { return (float) AO::Law::cutoffHz (knob); };

        // --- the voice -------------------------------------------------------
        const int osc1 = b.add ("plugin.alphabeta.osc", 0, 0);
        const int osc2 = b.add ("plugin.alphabeta.osc", 0, 420);
        for (int o = 0; o < 2; ++o)
        {
            const int m = o == 0 ? osc1 : osc2;
            b.set (m, "which", (float) o);
            b.set (m, "waveA", (float) P.wave[o][0]);
            b.set (m, "rangeA", (float) (P.range[o][0] + 2));
            b.set (m, "waveB", (float) P.wave[o][1]);
            b.set (m, "rangeB", (float) (P.range[o][1] + 2));
            b.set (m, "mix", (float) P.mix[o]);
            b.set (m, "detune", (float) (o == 0 ? P.detune : -P.detune));
            b.set (m, "spread", (float) P.spread);
            b.set (m, "glideMode", (float) P.glideMode);
            b.set (m, "glideTime", (float) P.glideTime);
            b.set (m, "glideBend", (float) P.glideBend);
            b.set (m, "voices", voicesIndex);
            b.set (m, "legato", mono ? 1.0f : 0.0f);
        }

        const int drive = b.add ("plugin.alphabeta.drive", 300, 200);
        b.set (drive, "balance", (float) P.balance);
        b.set (drive, "drive", (float) P.drive);
        b.cable (osc1, "audioOut", drive, "osc1In");
        b.cable (osc2, "audioOut", drive, "osc2In");

        const int filter = b.add ("plugin.alphabeta.filter", 580, 200);
        b.set (filter, "type", (float) P.filterType);
        b.set (filter, "cutoff", knobToHz (P.cutoff));
        b.set (filter, "resonance", (float) P.resonance);
        b.set (filter, "envDepth", (float) P.envDepth);
        b.set (filter, "legato", mono ? 1.0f : 0.0f);
        b.cable (drive, "audioOut", filter, "audioIn");

        const int amp = b.add ("plugin.alphabeta.amp", 860, 200);
        b.set (amp, "velocity", (float) P.velSens);
        b.cable (filter, "audioOut", amp, "audioIn");

        auto makeEnv = [&] (const AO::EnvParams& e, int x, int y)
        {
            const int m = b.add ("plugin.alphabeta.env", x, y);
            b.set (m, "attack", (float) e.attack);
            b.set (m, "decay", (float) e.decay);
            b.set (m, "sustain", (float) e.sustain);
            b.set (m, "fade", (float) e.fade);
            b.set (m, "release", (float) e.release);
            b.set (m, "legato", mono ? 1.0f : 0.0f);
            return m;
        };
        const int ampEnv = makeEnv (P.ampEnv, 860, 500);
        const int filEnv = makeEnv (P.filEnv, 580, 500);
        b.cable (ampEnv, "envOut", amp, "envIn");
        b.cable (filEnv, "ctrlOut", filter, "envIn");   // the filter reads the block-start level

        const int lfo = b.add ("plugin.alphabeta.lfo", 300, 800);
        b.set (lfo, "wave", (float) P.lfoWave);
        b.set (lfo, "rate", (float) P.lfoHz);
        b.set (lfo, "sync", (float) P.lfoSync);
        b.set (lfo, "attack", (float) P.lfoAttack);
        b.set (lfo, "legato", mono ? 1.0f : 0.0f);

        // --- the matrix: wired only where a row actually goes ----------------
        bool usesDst[AO::NUM_DST] = {};
        bool usesSrc[AO::NUM_SRC] = {};
        bool anyRow = false;
        for (const auto& r : P.rows)
            if (r.src != AO::SRC_OFF && r.dst != AO::DST_OFF)
            {
                usesDst[r.dst] = usesSrc[r.src] = anyRow = true;
            }

        if (anyRow)
        {
            const int mx = b.add ("plugin.alphabeta.matrix", 580, 800);
            for (int i = 0; i < AO::kNumRows; ++i)
            {
                const auto n = juce::String (i + 1);
                b.set (mx, "src" + n, (float) P.rows[(size_t) i].src);
                b.set (mx, "amt" + n, (float) (P.rows[(size_t) i].amount * 100.0));
                b.set (mx, "dst" + n, (float) P.rows[(size_t) i].dst);
            }
            if (usesSrc[AO::SRC_AMPENV])  b.cable (ampEnv, "ctrlOut", mx, "ampEnvIn");
            if (usesSrc[AO::SRC_FILTENV]) b.cable (filEnv, "ctrlOut", mx, "filterEnvIn");
            if (usesSrc[AO::SRC_LFO1])    b.cable (lfo, "ctrlOut", mx, "lfoIn");

            const bool mainPitch = usesDst[AO::DST_MAIN_PITCH];
            if (usesDst[AO::DST_OSC1_PITCH]) b.cable (mx, "osc1Pitch", osc1, "pitchIn");
            if (usesDst[AO::DST_OSC2_PITCH]) b.cable (mx, "osc2Pitch", osc2, "pitchIn");
            if (mainPitch) { b.cable (mx, "mainPitch", osc1, "pitchIn"); b.cable (mx, "mainPitch", osc2, "pitchIn"); }
            if (usesDst[AO::DST_OSC1_SYM])  b.cable (mx, "osc1Sym", osc1, "symIn");
            if (usesDst[AO::DST_OSC2_SYM])  b.cable (mx, "osc2Sym", osc2, "symIn");
            if (usesDst[AO::DST_OSC1_AMP])  b.cable (mx, "osc1Amp", osc1, "ampIn");
            if (usesDst[AO::DST_OSC2_AMP])  b.cable (mx, "osc2Amp", osc2, "ampIn");
            if (usesDst[AO::DST_CUTOFF])    b.cable (mx, "cutoff", filter, "cutIn");
            if (usesDst[AO::DST_RESONANCE]) b.cable (mx, "resonance", filter, "resIn");
            if (usesDst[AO::DST_MAIN_AMP])  b.cable (mx, "mainAmp", amp, "ampIn");
            if (usesDst[AO::DST_LFO_SPEED]) b.cable (mx, "lfoSpeed", lfo, "speedIn");
        }

        // --- the global end -------------------------------------------------
        const int chorus = b.add ("plugin.alphabeta.chorus", 1140, 200);
        b.set (chorus, "wet", (float) P.chorusWet);
        b.set (chorus, "time", (float) P.chorusMs);
        b.set (chorus, "rate", (float) P.chorusHz);
        b.cable (amp, "audioOut", chorus, "audioIn");

        const int output = b.add ("plugin.alphabeta.output", 1420, 200);
        b.set (output, "volume", P.volume <= 0.0 ? -18.2f : (float) AO::Law::volumeDb (P.volume));
        b.cable (chorus, "audioOut", output, "audioIn");

        PatchPorts ports;
        ports.name = "AlphaBeta";
        ports.audioOut = { { output, "audioOut" } };
        return ports;
    }

    // AlphaBeta's own loaders: .abpreset is its state, .fxp / .fxb the
    // original FreeAlpha's (read exactly by the plugin's Original engine code)
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* ab = dynamic_cast<aqp_alphabeta::AlphaBetaAudioProcessor*> (&original);
        juce::MemoryBlock data;
        if (ab == nullptr || ! file.loadFileAsData (data) || data.getSize() == 0)
            return false;

        if (file.hasFileExtension ("abpreset"))
        {
            ab->setStateInformation (data.getData(), (int) data.getSize());
            return true;
        }

        auto orig = aqp_alphabeta::AlphaOrig::readFxp (static_cast<const uint8_t*> (data.getData()), data.getSize());
        if (! orig.ok)
            return false;
        OP::applyPatch (ab->apvts, orig.patch);
        if (auto* e = ab->apvts.getParameter (OP::ENGINE))
            e->setValueNotifyingHost (1.0f);
        return true;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
