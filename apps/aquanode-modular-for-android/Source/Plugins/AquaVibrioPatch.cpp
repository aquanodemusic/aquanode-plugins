//==============================================================================
// Aqua Vibrio - Patch version
//
// Aqua Vibrio's Original engine is one signal path: every voice (three
// oscillators, sub, noise, two filters, four envelopes, three LFOs, the
// modulation matrix, unison, the arpeggiator, the note layers) is summed at
// the patch volume into the effects rack (distortion, character, filter
// bank, phaser, chorus, delay, reverb, EQ, vocoder, granulator), and the
// output stage (DC block, soft limit) follows. The matrix also reaches into
// the rack - seven of its destinations are effect amounts. The patch version
// splits it exactly there:
//
//   Aqua Vibrio - Voice  --audio--------------------> Aqua Vibrio - Effects
//                        --7 effect modulations-----> (rack + output stage)
//
// Both modules run the plugin's own Original engine (one in voices-only
// mode, one in rack-only mode), so the sound is the original's. Their knobs
// are the plugin's own parameters and take cables; everything else of the
// patch (the matrix slots, arpeggiator steps, note layers, soft knobs)
// travels in each module's state.
//
// Not split further: the voice engine shares its LFOs, matrix and voice
// allocation across every stage, and one instance of it is 12.8 MB.
// Note layers with an effects rack of their own play through the base rack.
//==============================================================================
#include "PluginAquaVibrioConfig.h"

namespace aqp_aquavibrio
{
    #include AQP_AQUAVIBRIO_SRC(PluginProcessor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "aquavibrio" };
    using AV = aqp_aquavibrio::aquavibrio::AquaVibrioProcessor;
    using AVEngine = aqp_aquavibrio::aquavibrio::original::Engine;
    namespace avp = aqp_aquavibrio::aquavibrio;
    constexpr int kMods = AVEngine::kNumFxMods;
    constexpr int kBlock = 32;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.aquavibrio." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    struct KnobDef { const char* id; const char* label; int row; };

    // The knobs, by the plugin's own parameter ids (each with that parameter's
    // own range and choices).
    const std::vector<KnobDef>& voiceKnobs()
    {
        static const std::vector<KnobDef> k {
            { "Osc1Mode", "Osc 1", 0 }, { "Osc1WaveSelect", "Wave", 0 }, { "Osc1Shape", "Shape", 0 },
            { "Osc1Pulsewidth", "PW", 0 }, { "Osc1Semitone", "Semi", 0 }, { "Osc1Keyfollow", "Key", 0 },
            { "Osc1HypersawDensity", "Density", 0 }, { "Osc1HypersawDetunespread", "Spread", 0 },
            { "Osc1WavetableWavetableselect", "Table", 0 }, { "Osc1WavetableWavetableindex", "Index", 0 },

            { "Osc2Mode", "Osc 2", 1 }, { "Osc2WaveSelect", "Wave", 1 }, { "Osc2Shape", "Shape", 1 },
            { "Osc2Pulsewidth", "PW", 1 }, { "Osc2Semitone", "Semi", 1 }, { "Osc2Detune", "Detune", 1 },
            { "Osc2FmAmount", "FM", 1 }, { "OscFmMode", "FM Mode", 1 }, { "Osc2Sync", "Sync", 1 },
            { "Osc2FiltEnvAmt", "FE>Pitch", 1 }, { "FmFiltEnvAmt", "FE>FM", 1 }, { "Osc2Keyfollow", "Key", 1 },

            { "Osc3Mode", "Osc 3", 2 }, { "Osc3Volume", "Level", 2 }, { "Osc3Semitone", "Semi", 2 },
            { "Osc3Detune", "Detune", 2 }, { "OscBalance", "Balance", 2 }, { "SuboscillatorVolume", "Sub", 2 },
            { "SuboscillatorShape", "Sub Wave", 2 }, { "OscMainvolume", "Osc Vol", 2 },
            { "RingmodulatorVolume", "Ring", 2 }, { "NoiseVolume", "Noise", 2 }, { "NoiseColor", "Color", 2 },

            { "Cutoff", "Cutoff 1", 3 }, { "Filter1Resonance", "Reso", 3 }, { "Filter1EnvAmt", "Env", 3 },
            { "Filter1Keyfollow", "Key", 3 }, { "Filter1Mode", "Mode", 3 }, { "Filter1EnvPolarity", "Pol", 3 },
            { "FilterBalance", "F.Bal", 3 }, { "SaturationCurve", "Saturation", 3 }, { "FilterRouting", "Routing", 3 },

            { "Cutoff2", "Cutoff 2", 4 }, { "Filter2Resonance", "Reso", 4 }, { "Filter2EnvAmt", "Env", 4 },
            { "Filter2Keyfollow", "Key", 4 }, { "Filter2Mode", "Mode", 4 }, { "Filter2EnvPolarity", "Pol", 4 },
            { "Filter2CutoffLink", "Link", 4 }, { "OffsetForFilterlink", "Offset", 4 },

            { "FilterEnvAttack", "F.Att", 5 }, { "FilterEnvDecay", "F.Dec", 5 }, { "FilterEnvSustain", "F.Sus", 5 },
            { "FilterEnvSustainTime", "F.Time", 5 }, { "FilterEnvRelease", "F.Rel", 5 },
            { "AmpEnvAttack", "A.Att", 5 }, { "AmpEnvDecay", "A.Dec", 5 }, { "AmpEnvSustain", "A.Sus", 5 },
            { "AmpEnvSustainTime", "A.Time", 5 }, { "AmpEnvRelease", "A.Rel", 5 },

            { "Lfo1Rate", "LFO 1", 6 }, { "Lfo1Shape", "Shape", 6 }, { "Lfo1Mode", "Mode", 6 },
            { "Lfo1Symmetry", "Sym", 6 }, { "Lfo1Clock", "Clock", 6 }, { "Osc1Lfo1Amount", ">Osc1", 6 },
            { "Osc2Lfo1Amount", ">Osc2", 6 }, { "PwLfo1Amount", ">PW", 6 }, { "ResoLfo1Amount", ">Reso", 6 },
            { "FiltgainLfo1Amount", ">F.Gain", 6 }, { "Lfo1AssignDest", "Assign", 6 }, { "Lfo1AssignAmount", "Amt", 6 },

            { "Lfo2Rate", "LFO 2", 7 }, { "Lfo2Shape", "Shape", 7 }, { "Lfo2Mode", "Mode", 7 },
            { "Lfo2Symmetry", "Sym", 7 }, { "Lfo2Clock", "Clock", 7 }, { "ShapeLfo2Amount", ">Shape", 7 },
            { "FmLfo2Amount", ">FM", 7 }, { "Cutoff1Lfo2Amount", ">Cut1", 7 }, { "Cutoff2Lfo2Amount", ">Cut2", 7 },
            { "PanLfo2Amount", ">Pan", 7 }, { "Lfo2AssignDest", "Assign", 7 }, { "Lfo2AssignAmount", "Amt", 7 },

            { "Lfo3Rate", "LFO 3", 8 }, { "Lfo3Shape", "Shape", 8 }, { "Lfo3Destination", "Dest", 8 },
            { "OscLfo3Amount", "Amount", 8 }, { "Lfo3FadeInTime", "Fade In", 8 },
            { "Transpose", "Transpose", 8 }, { "KeyMode", "Key Mode", 8 }, { "PortamentoTime", "Porta", 8 },
            { "BenderRangeUp", "Bend Up", 8 }, { "BenderRangeDown", "Bend Dn", 8 },

            { "PatchVolume", "Volume", 9 }, { "Panorama", "Pan", 9 }, { "AmpVelocity", "Vel>Amp", 9 },
            { "PunchIntensity", "Punch", 9 }, { "UnisonMode", "Unison", 9 }, { "UnisonDetune", "U.Det", 9 },
            { "UnisonPanSpread", "U.Pan", 9 },

            { "ArpMode", "Arp", 10 }, { "ArpPatternSelct", "Pattern", 10 }, { "ArpOctaveRange", "Octaves", 10 },
            { "ArpClock", "Clock", 10 }, { "ArpNoteLength", "Length", 10 }, { "ArpSwing", "Swing", 10 },
            { "ArpHoldEnable", "Hold", 10 }
        };
        return k;
    }

    const std::vector<KnobDef>& effectKnobs()
    {
        static const std::vector<KnobDef> k {
            { "DistortionCurve", "Distortion", 0 }, { "DistortionIntensity", "Drive", 0 },
            { "PatchDistortionMix", "Mix", 0 }, { "PatchDistortionTone64", "Tone", 0 },
            { "PatchDistortionHighCut", "High Cut", 0 }, { "PatchDistortionTrebleBooster", "Treble", 0 },
            { "CharacterType", "Character", 0 }, { "BassIntensity", "Bass", 0 }, { "BassTune", "B.Tune", 0 },

            { "FilterBankType", "Bank", 1 }, { "FilterBankMix", "Mix", 1 }, { "FilterBankFrequency", "Freq", 1 },
            { "FilterBankResonance", "Reso", 1 }, { "FilterBankFilterType", "Type", 1 }, { "FilterBankPoles", "Poles", 1 },
            { "FilterBankSlope", "Slope", 1 }, { "FilterBankStereoPhase", "Phase", 1 },

            { "PhaserMode", "Phaser", 2 }, { "PhaserMix", "Mix", 2 }, { "PhaserRate", "Rate", 2 },
            { "PhaserDepth", "Depth", 2 }, { "PhaserFrequency", "Freq", 2 }, { "PhaserFeedback", "Fdbk", 2 },
            { "PhaserSpread", "Spread", 2 },

            { "ChorusType", "Chorus", 3 }, { "ChorusMix", "Mix", 3 }, { "ChorusRate", "Rate", 3 },
            { "ChorusDepth", "Depth", 3 }, { "ChorusDelay", "Delay", 3 }, { "ChorusFeedback", "Fdbk", 3 },
            { "ChorusLfoShape", "LFO", 3 }, { "ChorusXOver", "X-Over", 3 },

            { "DelayMode", "Delay", 4 }, { "DelaySend", "Send", 4 }, { "DelayTime", "Time", 4 },
            { "DelayFeedback", "Fdbk", 4 }, { "DlyRateRevDecay", "Rate", 4 }, { "DlyDepth", "Depth", 4 },
            { "DelayColor", "Color", 4 }, { "DelayClock", "Clock", 4 }, { "DelayType", "Type", 4 },

            { "ReverbMode", "Reverb", 5 }, { "ReverbSend", "Send", 5 }, { "ReverbType", "Type", 5 },
            { "ReverbTime", "Time", 5 }, { "ReverbDamping", "Damp", 5 }, { "ReverbColor", "Color", 5 },
            { "ReverbPredelay", "Predelay", 5 }, { "ReverbFeedback", "Fdbk", 5 },

            { "LoweqFrequency", "Low F", 6 }, { "LoweqGain", "Low", 6 }, { "MideqFrequency", "Mid F", 6 },
            { "MideqGain", "Mid", 6 }, { "MideqQFactor", "Mid Q", 6 }, { "HigheqFrequency", "High F", 6 },
            { "HigheqGain", "High", 6 },

            { "VocoderMode", "Vocoder", 7 }, { "VocoderBalance", "Balance", 7 }, { "VocoderBands", "Bands", 7 },
            { "VocoderAttack", "Att", 7 }, { "VocoderRelease", "Rel", 7 },
            { "InputMode", "Input", 7 }, { "InputSelect", "In Sel", 7 }, { "InputRingmodulator", "In Ring", 7 },

            { "Atomizer", "Granular", 8 }, { "GranulatorGrains", "Grains", 8 }, { "GranulatorSize", "Size", 8 },
            { "GranulatorPosition", "Position", 8 }, { "GranulatorPitch", "Pitch", 8 }
        };
        return k;
    }

    const avp::ParamInfo* infoFor (const juce::String& id)
    {
        for (const auto& p : avp::allParameters())
            if (id == p.id)
                return &p;
        return nullptr;
    }

    // a knob with the parameter's own range, or a list with its own choices
    ParamSpec specFor (const KnobDef& k)
    {
        const auto* p = infoFor (k.id);
        if (p == nullptr)
            return makeRotary (k.id, k.label, 0, 127, 0, k.row, "", false, 1.0f);
        if (p->scale == avp::ValueScale::Enum)
        {
            // as the plugin's parameter layout builds it
            auto ch = avp::choicesFor (p->choiceList);
            const int wanted = p->maxValue - p->minValue + 1;
            while (ch.size() > wanted) ch.remove (ch.size() - 1);
            while (ch.size() < wanted) ch.add (juce::String (ch.size() + p->minValue));
            return makeCombo (k.id, k.label, ch, juce::jlimit (0, ch.size() - 1, p->defaultValue - p->minValue),
                              k.row, ch.size() > 2 ? 2 : 1);
        }
        return makeRotary (k.id, k.label, (float) p->minValue, (float) p->maxValue, (float) p->defaultValue,
                           k.row, "", false, 1.0f);
    }

    const char* modName (int m)
    {
        static const char* names[kMods] = { "Dist", "Bank Freq", "Bank Reso", "Phaser", "Chorus", "Delay", "Reverb" };
        return names[m];
    }
    juce::String modId (int m)
    {
        static const char* ids[kMods] = { "modDist", "modBankFreq", "modBankReso", "modPhaser", "modChorus",
                                          "modDelay", "modReverb" };
        return ids[m];
    }

    //==========================================================================
    // What both modules share: an Aqua Vibrio instance on its Original engine,
    // knobs linked to its parameters, its whole state saved with the module.
    class VibrioHostModule : public SynthModule
    {
    public:
        VibrioHostModule()
        {
            auto p = createOriginal (kPlugin, 48000.0);
            av.reset (dynamic_cast<AV*> (p.get()));
            if (av != nullptr)
            {
                p.release();
                forceOriginal();
            }
        }

        VoiceMode voiceMode() const override { return VoiceMode::Global; }

        juce::String saveCustomState() const override
        {
            if (av == nullptr) return {};
            juce::MemoryBlock mb;
            av->getStateInformation (mb);
            return mb.toBase64Encoding();
        }
        void loadCustomState (const juce::String& s) override
        {
            juce::MemoryBlock mb;
            if (av != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
                setPluginState (mb);
        }

        void setPluginState (const juce::MemoryBlock& mb)
        {
            if (av == nullptr) return;
            av->setStateInformation (mb.getData(), (int) mb.getSize());
            forceOriginal();
            for (auto& l : links) l.last = -1.0f;
        }

        AV* plugin() const { return av.get(); }

    protected:
        void prepareHost (double sr)
        {
            if (av == nullptr) return;
            const juce::ScopedLock sl (av->getCallbackLock());
            av->releaseResources();
            av->setPlayConfigDetails (2, 2, sr, kBlock);
            av->prepareToPlay (sr, kBlock);
        }

        // the module's knobs -> the plugin's parameters (only on change)
        void relink()
        {
            links.clear();
            if (av == nullptr) return;
            const auto& d = getDescriptor();
            for (int i = 0; i < (int) d.params.size(); ++i)
                if (auto* prm = kit::findParam (*av, d.params[(size_t) i].id))
                    links.push_back ({ prm, i, -1.0f });
        }
        void pushKnobs()
        {
            for (auto& l : links)
            {
                const float v = l.param->convertTo0to1 (param (l.knob));
                if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
            }
        }

        std::unique_ptr<AV> av;

    private:
        // the patch version always plays the Original engine (the builder
        // set the level for it); the state's own Engine switch is overridden
        void forceOriginal()
        {
            if (auto* e = kit::findParam (*av, "EngineMode"))
                if (e->getValue() < 0.5f)
                    av->useOriginalEngineKeepingLevel();
        }

        struct Link { juce::RangedAudioParameter* param; int knob; float last; };
        std::vector<Link> links;
    };
}

//==============================================================================
// Aqua Vibrio - Voice
//==============================================================================
class AquaVibrioVoiceModule final : public VibrioHostModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("voice", "Voice", ModuleSection::Oscillator, 0);
        d.description = "Aqua Vibrio's voices, played by its Original engine: three oscillators (classic, "
                        "HyperSaw, wavetable, formant...), sub, ring and noise, two filters with saturation, "
                        "filter and amp envelopes, three LFOs, unison, the arpeggiator, the modulation matrix "
                        "and the note layers - summed at the patch volume, as the plugin hands them to its "
                        "effects rack. The seven Mod outputs are the matrix's effect destinations, for the "
                        "Aqua Vibrio Effects. Audio In is its Input oscillator and envelope follower. The "
                        "matrix slots, arpeggiator steps and note layers come with the module (saved in the patch).";
        d.sockets = { midiIn ("midiIn", "Midi In"), audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        for (int m = 0; m < kMods; ++m)
            d.sockets.push_back (modOut (modId (m), juce::String ("Mod ") + modName (m)));
        for (const auto& k : voiceKnobs())
            d.params.push_back (specFor (k));
        return d;
    }

    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override { midiQueue.addEvent (m, pos); }
    void midiAllNotesOff() override { midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        prepareHost (sr);
        if (av != nullptr)
        {
            auto& e = av->getOriginalEngine();
            e.setSplit (AVEngine::Split::VoicesOnly);
            e.setSecondOutput (nullptr);
        }
        relink();
        midiQueue.ensureSize (1024);
        io.setSize (2, kBlock);
        io.clear();
        for (auto& o : out) o.fill (0.0f);
        for (auto& m : mods) m.fill (0.0f);
        for (auto& m : modOutBuf) m.fill (0.0f);
        pos = 0;
    }

    void reset() override { midiQueue.clear(); midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        inBuf[0][(size_t) pos] = in[0][0];
        inBuf[1][(size_t) pos] = in[0][1];
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        for (int m = 0; m < kMods; ++m)
        {
            const float v = mods[(size_t) m][(size_t) pos];
            outputs[1 + m] = { v, v };
        }
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

private:
    void runBlock()
    {
        if (av == nullptr)
            return;
        pushKnobs();

        for (int c = 0; c < 2; ++c)
            io.copyFrom (c, 0, inBuf[(size_t) c].data(), kBlock);

        bool rendered = false;
        {
            const juce::ScopedTryLock sl (av->getCallbackLock());
            if (sl.isLocked())
            {
                auto& e = av->getOriginalEngine();
                float* outs[kMods];
                for (int m = 0; m < kMods; ++m)
                    outs[m] = modOutBuf[(size_t) m].data();
                e.setSplit (AVEngine::Split::VoicesOnly);
                e.setTempo (tempoBpm);
                e.setFxModOutputs (outs);
                e.renderBlock (io, midiQueue);
                e.setFxModOutputs (nullptr);
                rendered = true;
            }
        }
        midiQueue.clear();

        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
        }
        for (int m = 0; m < kMods; ++m)
            for (int i = 0; i < kBlock; ++i)
            {
                const float v = modOutBuf[(size_t) m][(size_t) i];
                mods[(size_t) m][(size_t) i] = rendered && std::isfinite (v) ? v : 0.0f;
            }
    }

    juce::MidiBuffer midiQueue;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> inBuf {}, out {};
    std::array<std::array<float, kBlock>, kMods> mods {}, modOutBuf {};
    int pos { 0 };
};

//==============================================================================
// Aqua Vibrio - Effects
//==============================================================================
class AquaVibrioEffectsModule final : public VibrioHostModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("effects", "Effects", ModuleSection::Effect, 1);
        d.description = "Aqua Vibrio's effects rack, run by its Original engine: distortion and character, "
                        "the filter bank (comb, vowel, filters), phaser, chorus, delay, reverb, EQ, vocoder "
                        "and granulator, then the output stage (DC block, soft limit) and the master Volume. "
                        "Voice In takes the Aqua Vibrio Voice; the Mod inputs are the matrix's effect amounts "
                        "(from the Voice, or any modulation). Audio In is the vocoder's modulator and the "
                        "Input section.";
        d.sockets = { audioIn ("busIn", "Voice In"), audioIn ("audioIn", "Audio In") };
        for (int m = 0; m < kMods; ++m)
            d.sockets.push_back (modIn (modId (m), juce::String ("Mod ") + modName (m)));
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        for (const auto& k : effectKnobs())
            d.params.push_back (specFor (k));
        d.params.push_back (makeRotary ("MasterVolume", "Volume", -24.0f, 24.0f, 12.0f, 9, "dB"));
        return d;
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        prepareHost (sr);
        if (av != nullptr)
            av->getOriginalEngine().setSplit (AVEngine::Split::EffectsOnly);
        relink();
        io.setSize (2, kBlock);
        io.clear();
        for (auto& o : out) o.fill (0.0f);
        for (auto& b : busBuf) b.fill (0.0f);
        for (auto& m : modBuf) m.fill (0.0f);
        pos = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        busBuf[0][(size_t) pos] = in[0][0];
        busBuf[1][(size_t) pos] = in[0][1];
        inBuf[0][(size_t) pos] = in[1][0];
        inBuf[1][(size_t) pos] = in[1][1];
        for (int m = 0; m < kMods; ++m)
            modBuf[(size_t) m][(size_t) pos] = in[2 + m][0];
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

private:
    void runBlock()
    {
        if (av == nullptr)
            return;
        pushKnobs();

        for (int c = 0; c < 2; ++c)
            io.copyFrom (c, 0, inBuf[(size_t) c].data(), kBlock);

        bool rendered = false;
        {
            const juce::ScopedTryLock sl (av->getCallbackLock());
            if (sl.isLocked())
            {
                auto& e = av->getOriginalEngine();
                const float* bus[2] = { busBuf[0].data(), busBuf[1].data() };
                const float* m[kMods];
                for (int k = 0; k < kMods; ++k)
                    m[k] = modBuf[(size_t) k].data();
                e.setSplit (AVEngine::Split::EffectsOnly);
                e.setTempo (tempoBpm);
                e.setEffectsInput (bus, m);
                av->processBlock (io, noMidi);   // the engine, then the master volume
                e.setEffectsInput (nullptr, nullptr);
                rendered = true;
            }
        }

        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
        }
    }

    juce::MidiBuffer noMidi;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> busBuf {}, inBuf {}, out {};
    std::array<std::array<float, kBlock>, kMods> modBuf {};
    int pos { 0 };
};

AQUANODE_REGISTER_MODULE (AquaVibrioVoiceModule,   AquaVibrioVoiceModule::descriptor)
AQUANODE_REGISTER_MODULE (AquaVibrioEffectsModule, AquaVibrioEffectsModule::descriptor)

//==============================================================================
namespace aquanode::plugins::aquavibrio
{
    // The plugin's state, set to the Original engine. A patch on the Native
    // engine gets the level that engine switch gives it (+12 dB).
    static juce::MemoryBlock originalEngineState (juce::AudioProcessor& original)
    {
        juce::MemoryBlock mb;
        original.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (xml == nullptr)
            return mb;
        auto tree = juce::ValueTree::fromXml (*xml);
        auto engine = tree.getChildWithProperty ("id", "EngineMode");
        const bool wasNative = ! engine.isValid() || (float) engine.getProperty ("value", 0.0f) < 0.5f;
        if (wasNative)
        {
            if (engine.isValid())
                engine.setProperty ("value", 1.0f, nullptr);
            auto master = tree.getChildWithProperty ("id", "MasterVolume");
            if (master.isValid())
                master.setProperty ("value", juce::jlimit (-24.0f, 24.0f,
                                    (float) master.getProperty ("value", 0.0f) + AV::kOriginalMasterDb), nullptr);
        }
        if (auto out = tree.createXml())
        {
            mb.reset();
            juce::AudioProcessor::copyXmlToBinary (*out, mb);
        }
        return mb;
    }

    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const auto state = originalEngineState (original);

        const int voice = b.add ("plugin.aquavibrio.voice", 0, 0);
        const int fx = b.add ("plugin.aquavibrio.effects", 1400, 0);

        for (const int m : { voice, fx })
            if (auto* h = dynamic_cast<VibrioHostModule*> (b.dsp (m)))
                if (auto* p = h->plugin())
                {
                    h->setPluginState (state);
                    b.copy (m, *p);   // the knobs: the converted values (Original engine, its level)
                }

        b.cable (voice, "audioOut", fx, "busIn");
        for (int m = 0; m < kMods; ++m)
            b.cable (voice, modId (m), fx, modId (m));

        PatchPorts ports;
        ports.name = "Aqua Vibrio";
        ports.midiIn = { { voice, "midiIn" } };
        ports.audioIn = { { voice, "audioIn" }, { fx, "audioIn" } };
        ports.audioOut = { { fx, "audioOut" } };
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* av = dynamic_cast<AV*> (&original);
        return av != nullptr && av->loadBank (file) > 0;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
