#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================
//  Parameter Layout
// ==============================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AlphaBetaAudioProcessor::createLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    const juce::StringArray waves = InternalWaves::allWaveNames();
    juce::StringArray octs{ "-2","-1","0","+1","+2" };
    juce::StringArray ftype{ "LP12","LP24","LP24+","BP","HP" };

    // --- OSC 1 ---
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1WA, "OSC1 Wave A", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1OCA, "OSC1 Oct A", octs, 1));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1WB, "OSC1 Wave B", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1OCB, "OSC1 Oct B", octs, 1));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1MRP, "OSC1 Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1DET, "OSC1 Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 20.0f));

    // --- OSC 2 ---
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2WA, "OSC2 Wave A", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2OCA, "OSC2 Oct A", octs, 2));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2WB, "OSC2 Wave B", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2OCB, "OSC2 Oct B", octs, 2));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2MRP, "OSC2 Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2DET, "OSC2 Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    // --- Mix ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::MIX, "OSC Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.375f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::DRV, "Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FM, "FM",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f, 0.4f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::SPR, "Width",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));

    // --- Filter ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FCUT, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.28f), 250.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FRES, "Filter Res",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::FTYP, "Filter Type", ftype, 2));

    auto envR = juce::NormalisableRange<float>(0.001f, 10.0f, 0.0f, 0.4f);
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FATT, "F Attack", envR, 0.01f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FDEC, "F Decay", envR, 0.4f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FSUS, "F Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FREL, "F Release", envR, 0.6f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FFAD, "F Fade",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.65f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FDEP, "F Depth",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.55f));

    // --- Amp ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AVOL, "Amp Vol",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AVEL, "Amp Vel",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AATT, "A Attack", envR, 0.005f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::ADEC, "A Decay", envR, 0.25f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::ASUS, "A Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AREL, "A Release", envR, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AFAD, "A Fade",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // --- Chorus ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::HWET, "Chorus Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::HTIM, "Chorus Time",
        juce::NormalisableRange<float>(1.0f, 50.0f), 35.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::HRAT, "Chorus Rate",
        juce::NormalisableRange<float>(0.1f, 10.0f), 0.2f));

    // --- Glide ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::GLID, "Glide",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.0f, 0.5f), 0.0f));

    // --- Resonance curve ---
    juce::StringArray rcurve{ "Quadratic", "Cubic" };
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::RTYP, "Res Curve", rcurve, 1));

    // --- OSC pitch (semitones, -24..+24, 0.01 step) ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1PIT, "OSC1 Pitch",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2PIT, "OSC2 Pitch",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));

    // --- Alpha 3 extras ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::NOISE, "Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::RING, "Ringmod",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FFM, "Filter FM",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.5f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::FFMS, "Filter FM Source",
        juce::StringArray{ "Osc 1", "Osc 2", "Noise" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::BEND, "Bend Range",
        juce::NormalisableRange<float>(0.0f, 24.0f, 1.0f), 2.0f));

    // --- 1.3: unison + wavetable position ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::UNI, "Unison",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1BWTP, "OSC1 B WT Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2BWTP, "OSC2 B WT Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::TUNE, "Master Tune",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1WTP, "OSC1 A WT Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2WTP, "OSC2 A WT Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // --- 1.4: voice mode + analog ---
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::VMODE, "Voice Mode",
        juce::StringArray{ "Poly", "Mono", "Legato" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::ANALOG, "Analog",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // --- Modulation matrix ---
    for (int i = 0; i < Mod::NUM_SLOTS; ++i) {
        auto n = juce::String(i + 1);
        p.push_back(std::make_unique<juce::AudioParameterChoice>(Mod::slotSrcID(i),
            "Matrix " + n + " Source", Mod::sourceNames(), 0));
        p.push_back(std::make_unique<juce::AudioParameterFloat>(Mod::slotAmtID(i),
            "Matrix " + n + " Amount", juce::NormalisableRange<float>(-100.0f, 100.0f, 0.01f), 0.0f));
        p.push_back(std::make_unique<juce::AudioParameterChoice>(Mod::slotDstID(i),
            "Matrix " + n + " Dest", Mod::destNames(), 0));
    }

    // --- LFOs ---
    // Rate: 0.01 .. 32 Hz on a log dial, like the original "freq" knob
    juce::NormalisableRange<float> rateR(0.01f, 32.0f,
        [](float s, float e, float v) { return s * std::pow(e / s, v); },
        [](float s, float e, float v) { return std::log(v / s) / std::log(e / s); });
    for (int k = 0; k < Mod::NUM_LFOS; ++k) {
        auto n = juce::String(k + 1);
        p.push_back(std::make_unique<juce::AudioParameterChoice>(Mod::lfoWaveID(k),
            "LFO " + n + " Wave", Mod::lfoWaveNames(), Mod::LFO_TRI));
        p.push_back(std::make_unique<juce::AudioParameterFloat>(Mod::lfoRateID(k),
            "LFO " + n + " Rate", rateR, 2.0f));
        p.push_back(std::make_unique<juce::AudioParameterChoice>(Mod::lfoSyncID(k),
            "LFO " + n + " Sync", Mod::syncNames(), 0));
        p.push_back(std::make_unique<juce::AudioParameterFloat>(Mod::lfoAttID(k),
            "LFO " + n + " Attack", juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f, 0.4f), 0.0f));
        p.push_back(std::make_unique<juce::AudioParameterChoice>(Mod::lfoModeID(k),
            "LFO " + n + " Mode", juce::StringArray{ "Mono", "Poly" }, 1));
    }

    return { p.begin(), p.end() };
}

// ==============================================================
//  Constructor
// ==============================================================
AlphaBetaAudioProcessor::AlphaBetaAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output",
        juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "AlphaBeta", createLayout())
{
    // Add 32 voices
    InternalWaves::tables();          // build the internal waves before audio starts
    for (int i = 0; i < 32; ++i) {
        auto* v = new AlphaVoice();
        v->voiceIdx = i;
        synth.addVoice(v);
    }
    synth.addSound(new AlphaSound());
    synth.midi = &modCtx.midi;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<AlphaVoice*>(synth.getVoice(i)))
            v->ctx = &modCtx;
    apvts.state.setProperty("patchName", "init", nullptr);

    for (int i = 0; i < Mod::NUM_SLOTS; ++i)
        slotP[(size_t)i] = { apvts.getRawParameterValue(Mod::slotSrcID(i)),
                             apvts.getRawParameterValue(Mod::slotAmtID(i)),
                             apvts.getRawParameterValue(Mod::slotDstID(i)) };
    for (int k = 0; k < Mod::NUM_LFOS; ++k)
        lfoP[(size_t)k] = { apvts.getRawParameterValue(Mod::lfoWaveID(k)),
                            apvts.getRawParameterValue(Mod::lfoRateID(k)),
                            apvts.getRawParameterValue(Mod::lfoSyncID(k)),
                            apvts.getRawParameterValue(Mod::lfoAttID(k)),
                            apvts.getRawParameterValue(Mod::lfoModeID(k)) };
    bendP = apvts.getRawParameterValue(PID::BEND);
    for (int i = 0; i < (int)syncBeatsTable.size(); ++i)
        syncBeatsTable[(size_t)i] = Mod::syncBeats(i);
}

// ==============================================================
//  Prepare
// ==============================================================
void AlphaBetaAudioProcessor::prepareToPlay(double sr, int blockSize) {
    sampleRateHz = sr;
    synth.setCurrentPlaybackSampleRate(sr);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<AlphaVoice*>(synth.getVoice(i)))
            v->prepareVoice(sr);

    chorus.prepare(sr, blockSize);
}

// ==============================================================
//  Process
// ==============================================================
static int waveFromInt(int v) {   // choice index == oscillator wave index
    return juce::jlimit(0, InternalWaves::FIRST_INDEX + InternalWaves::names().size() - 1, v);
}

void AlphaBetaAudioProcessor::updateVoiceParams() {
    int   o1wa = (int)*apvts.getRawParameterValue(PID::O1WA);
    int   o1oa = (int)*apvts.getRawParameterValue(PID::O1OCA) - 2;
    int   o1wb = (int)*apvts.getRawParameterValue(PID::O1WB);
    int   o1ob = (int)*apvts.getRawParameterValue(PID::O1OCB) - 2;
    float o1m = *apvts.getRawParameterValue(PID::O1MRP);
    float o1d = *apvts.getRawParameterValue(PID::O1DET);
    float o1p = *apvts.getRawParameterValue(PID::O1PIT);

    int   o2wa = (int)*apvts.getRawParameterValue(PID::O2WA);
    int   o2oa = (int)*apvts.getRawParameterValue(PID::O2OCA) - 2;
    int   o2wb = (int)*apvts.getRawParameterValue(PID::O2WB);
    int   o2ob = (int)*apvts.getRawParameterValue(PID::O2OCB) - 2;
    float o2m = *apvts.getRawParameterValue(PID::O2MRP);
    float o2d = *apvts.getRawParameterValue(PID::O2DET);
    float o2p = *apvts.getRawParameterValue(PID::O2PIT);

    float mix = *apvts.getRawParameterValue(PID::MIX);
    float drv = *apvts.getRawParameterValue(PID::DRV);
    float fm = *apvts.getRawParameterValue(PID::FM);
    float spr = *apvts.getRawParameterValue(PID::SPR);
    float glide = *apvts.getRawParameterValue(PID::GLID);

    float fcut = *apvts.getRawParameterValue(PID::FCUT);
    float fres = *apvts.getRawParameterValue(PID::FRES);
    int   ftyp = (int)*apvts.getRawParameterValue(PID::FTYP);
    int   rtyp = (int)*apvts.getRawParameterValue(PID::RTYP);
    float fdep = *apvts.getRawParameterValue(PID::FDEP);

    ADSRFade::Params fep, aep;
    fep.attack = *apvts.getRawParameterValue(PID::FATT);
    fep.decay = *apvts.getRawParameterValue(PID::FDEC);
    fep.sustain = *apvts.getRawParameterValue(PID::FSUS);
    fep.release = *apvts.getRawParameterValue(PID::FREL);
    fep.fade = *apvts.getRawParameterValue(PID::FFAD);

    aep.attack = *apvts.getRawParameterValue(PID::AATT);
    aep.decay = *apvts.getRawParameterValue(PID::ADEC);
    aep.sustain = *apvts.getRawParameterValue(PID::ASUS);
    aep.release = *apvts.getRawParameterValue(PID::AREL);
    aep.fade = *apvts.getRawParameterValue(PID::AFAD);

    float avol = *apvts.getRawParameterValue(PID::AVOL);
    float avel = *apvts.getRawParameterValue(PID::AVEL);

    float nz = *apvts.getRawParameterValue(PID::NOISE);
    float rm = *apvts.getRawParameterValue(PID::RING);
    float ffm = *apvts.getRawParameterValue(PID::FFM);
    int   ffs = (int)*apvts.getRawParameterValue(PID::FFMS);
    float uni = *apvts.getRawParameterValue(PID::UNI);
    float ana = *apvts.getRawParameterValue(PID::ANALOG);
    const int vmode = (int)*apvts.getRawParameterValue(PID::VMODE);
    synth.setMode(vmode);
    const bool monoMode = vmode != 0;
    const std::array<float, 4> wpos{ apvts.getRawParameterValue(PID::O1WTP)->load(),
                                     apvts.getRawParameterValue(PID::O1BWTP)->load(),
                                     apvts.getRawParameterValue(PID::O2WTP)->load(),
                                     apvts.getRawParameterValue(PID::O2BWTP)->load() };
    const float tune = apvts.getRawParameterValue(PID::TUNE)->load();

    // Pick up newly loaded wavetables without ever blocking the audio thread
    {
        const juce::ScopedTryLock tl(wtLock);
        if (tl.isLocked()) wtAudio = wtCurrent;
    }

    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (auto* v = dynamic_cast<AlphaVoice*>(synth.getVoice(i))) {
            v->o1wA = waveFromInt(o1wa); v->o1octA = o1oa;
            v->o1wB = waveFromInt(o1wb); v->o1octB = o1ob;
            v->o1morph = o1m;  v->o1detune = o1d;  v->o1pitch = o1p;

            v->o2wA = waveFromInt(o2wa); v->o2octA = o2oa;
            v->o2wB = waveFromInt(o2wb); v->o2octB = o2ob;
            v->o2morph = o2m;  v->o2detune = o2d;  v->o2pitch = o2p;

            v->oscMix = mix; v->drive = drv; v->fm = fm;
            v->stereoSpread = spr; v->glideTime = glide;

            v->filterCutoff = fcut; v->filterRes = fres;
            v->filterType = ftyp; v->resCurve = rtyp; v->filterDepth = fdep;
            v->fEnvP = fep;

            v->aEnvP = aep; v->ampVol = avol; v->ampVelSens = avel;
            v->noiseMix = nz; v->ringMix = rm;
            v->filterFM = ffm; v->filterFMSrc = ffs;
            v->unison = uni; v->wtPos = wpos; v->analog = ana; v->tuneCents = tune;
            if (!monoMode) v->glideOnStart = true;
            for (int k = 0; k < NUM_WT_SLOTS; ++k) v->wtSlot[(size_t)k] = wtAudio[(size_t)k].get();
        }
    }
}

// Reads matrix + LFO parameters and advances the shared (mono) LFOs.
void AlphaBetaAudioProcessor::updateModContext(int numSamples) {
    for (int i = 0; i < Mod::NUM_SLOTS; ++i) {
        auto& sl = modCtx.slots[(size_t)i];
        const auto& sp = slotP[(size_t)i];
        sl.src = (int)sp.src->load();
        sl.amt = sp.amt->load() * 0.01f;
        sl.dst = (int)sp.dst->load();
    }
    modCtx.bendRange = bendP->load();

    // Host tempo / position
    double ppq = 0.0; bool playing = false;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto bpm = pos->getBpm()) modCtx.bpm = *bpm;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    }

    for (int k = 0; k < Mod::NUM_LFOS; ++k) {
        auto& lp = modCtx.lfo[(size_t)k];
        const auto& pp = lfoP[(size_t)k];
        lp.wave = (int)pp.wave->load();
        lp.rateHz = pp.rate->load();
        lp.syncBeats = syncBeatsTable[(size_t)juce::jlimit(0, (int)syncBeatsTable.size() - 1, (int)pp.sync->load())];
        lp.attack = pp.att->load();
        lp.poly = (int)pp.mode->load() == 1;

        double hz = Mod::lfoRateHz(lp, modCtx.bpm);
        double inc = hz / sampleRateHz;
        // A synced mono LFO locks to the song position while the host plays
        if (lp.syncBeats > 0.0 && playing)
            monoLfoPhase[(size_t)k] = ppq / lp.syncBeats;
        modCtx.monoPhase0[(size_t)k] = monoLfoPhase[(size_t)k];
        modCtx.monoInc[(size_t)k] = inc;
        monoLfoPhase[(size_t)k] += inc * numSamples;
    }
}

void AlphaBetaAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    updateVoiceParams();
    updateModContext(buffer.getNumSamples());
    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());

    // Chorus
    float wet = *apvts.getRawParameterValue(PID::HWET);
    float time = *apvts.getRawParameterValue(PID::HTIM);
    float rate = *apvts.getRawParameterValue(PID::HRAT);
    chorus.setWet(wet);
    chorus.setTime(time);
    chorus.setRate(rate);
    chorus.process(buffer);

    // Stereo spread: M/S widening driven by spread param
    float spread = *apvts.getRawParameterValue(PID::SPR);
    if (spread > 0.001f && buffer.getNumChannels() >= 2) {
        float width = 1.0f + spread * 1.5f;
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            float m = (L[i] + R[i]) * 0.5f;
            float s = (L[i] - R[i]) * 0.5f * width;
            L[i] = m + s;
            R[i] = m - s;
        }
    }
}

// ==============================================================
//  State save/restore
// ==============================================================
void AlphaBetaAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void AlphaBetaAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml && xml->hasTagName(apvts.state.getType())) {
        // Presets saved before the matrix existed lack the new parameters.
        // Give them their defaults instead of keeping whatever was loaded last.
        for (auto* param : getParameters()) {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param);
            if (rp == nullptr) continue;
            if (xml->getChildByAttribute("id", rp->getParameterID()) == nullptr) {
                auto* e = xml->createNewChildElement("PARAM");
                e->setAttribute("id", rp->getParameterID());
                e->setAttribute("value", rp->convertFrom0to1(rp->getDefaultValue()));
            }
        }
        if (!xml->hasAttribute("patchName")) xml->setAttribute("patchName", "init");
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        rebuildWavetablesFromState();
    }
}

// ==============================================================
//  Plugin entry point
// ==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AlphaBetaAudioProcessor();
}

// ==============================================================
//  Wavetables
// ==============================================================
void AlphaBetaAudioProcessor::setWavetable(int slot, Wavetable::Ptr wt) {
    if (wt != nullptr) wtKeepAlive.add(wt);
    {
        const juce::ScopedLock sl(wtLock);
        wtCurrent[(size_t)slot] = wt;
    }
    // Drop tables that only the keep-alive list still references. A table the
    // audio thread holds has a count > 1 and stays; one that is no longer
    // current can't be picked up again.
    const juce::ScopedLock sl(wtLock);
    for (int i = wtKeepAlive.size(); --i >= 0;)
        if (wtKeepAlive.getObjectPointerUnchecked(i)->getReferenceCount() == 1)
            wtKeepAlive.remove(i);
}

// 1.5 stores "slot" (0..3). 1.3/1.4 stored "osc" (0/1) with one table per
// oscillator, which now becomes that oscillator's wave A slot.
int AlphaBetaAudioProcessor::slotOf(const juce::ValueTree& c) {
    if (c.hasProperty("slot")) return juce::jlimit(0, NUM_WT_SLOTS - 1, (int)c.getProperty("slot"));
    return juce::jlimit(0, 1, (int)c.getProperty("osc")) * 2;
}

juce::String AlphaBetaAudioProcessor::loadWavetable(int slot, const juce::File& file, int frameSize) {
    juce::String error;
    auto wt = Wavetable::fromFile(file, error, frameSize);
    if (wt == nullptr) return error;

    // Store the frames in the state so the table travels with the patch
    for (int i = apvts.state.getNumChildren(); --i >= 0;) {
        auto c = apvts.state.getChild(i);
        if (c.hasType("WAVETABLE") && slotOf(c) == slot) apvts.state.removeChild(i, nullptr);
    }
    apvts.state.appendChild(wt->toValueTree(slot), nullptr);
    setWavetable(slot, wt);
    return {};
}

void AlphaBetaAudioProcessor::clearWavetable(int slot) {
    for (int i = apvts.state.getNumChildren(); --i >= 0;) {
        auto c = apvts.state.getChild(i);
        if (c.hasType("WAVETABLE") && slotOf(c) == slot) apvts.state.removeChild(i, nullptr);
    }
    setWavetable(slot, nullptr);
}

juce::String AlphaBetaAudioProcessor::getWavetableName(int slot) const {
    for (const auto& c : apvts.state)
        if (c.hasType("WAVETABLE") && slotOf(c) == slot)
            return c.getProperty("name").toString();
    return {};
}

void AlphaBetaAudioProcessor::rebuildWavetablesFromState() {
    for (int slot = 0; slot < NUM_WT_SLOTS; ++slot) {
        Wavetable::Ptr wt;
        for (const auto& c : apvts.state)
            if (c.hasType("WAVETABLE") && slotOf(c) == slot) { wt = Wavetable::fromValueTree(c); break; }
        setWavetable(slot, wt);
    }
}
