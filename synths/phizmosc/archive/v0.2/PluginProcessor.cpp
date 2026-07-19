#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
TranswaveAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- Evolution curve ---
    // Time knob: 0.1s .. 100s (log-skewed, skew ~0.25)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoTime", "Evo Cycle Time (s)",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.001f, 0.25f), 4.0f));
    // Stepped toggle (0=smooth, 1=stepped)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoStepped", "Evo Stepped",
        juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f), 0.0f));
    // LFOs on top of curve
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoLFORate", "Evo LFO Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "evoLFODepth", "Evo LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "posLFORate", "Pos LFO Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "posLFODepth", "Pos LFO Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // --- Curve points (32) - stored as APVTS params so they are preset-saved ---
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String id = "evoPoint_" + juce::String::formatted("%02d", i);
        float def = (float)i / (float)(EVO_POINTS - 1);   // default: linear ramp
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            id, id, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    }

    // --- Pitch ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "detune", "Detune (cents)",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchLFO", "Pitch LFO Depth (st)",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchLFORate", "Pitch LFO Rate",
        juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 1.0f));

    // --- Envelope ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack",  "Attack",  juce::NormalisableRange<float>(0.001f,8.f,0.001f,0.35f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay",   "Decay",   juce::NormalisableRange<float>(0.001f,8.f,0.001f,0.35f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sustain", "Sustain", juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release", juce::NormalisableRange<float>(0.001f,8.f,0.001f,0.35f), 0.5f));

    // --- Character ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bitCrush", "Bit Depth",
        juce::NormalisableRange<float>(4.0f,16.0f,0.01f), 16.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "grit", "Grit", juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.0f));

    // --- Scan (5 modes now) ---
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scanStyle", "Scan Style",
        juce::StringArray{ "Forward","Fwd Stay","Back & Forth","Bwd Stay","Backward" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "jumpProb", "Jump Probability",
        juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.0f));

    // --- Filter ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterFreq", "Filter Frequency",
        juce::NormalisableRange<float>(20.0f,20000.0f,0.1f,0.3f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterQ", "Filter Resonance",
        juce::NormalisableRange<float>(0.1f,12.0f,0.01f,0.5f), 0.707f));

    // --- Stereo ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "spread",      "Osc Spread (cents)", juce::NormalisableRange<float>(0.f,50.f,0.1f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "stereoWidth", "Stereo Width",       juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "uniDetune",   "Unison Detune",      juce::NormalisableRange<float>(0.f,50.f,0.1f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "stereoPhase", "Osc B Phase Offset", juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.f));

    // --- FX ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusRate",  "Chorus Rate",  juce::NormalisableRange<float>(0.01f,8.f,0.01f,0.5f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusDepth", "Chorus Depth", juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbSize",  "Reverb Size",  juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbDamp",  "Reverb Damp",  juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbWet",   "Reverb Wet",   juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.f));

    // --- Output ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Output Gain", juce::NormalisableRange<float>(0.f,1.f,0.001f), 0.7f));

    return { params.begin(), params.end() };
}

//==============================================================================
TranswaveAudioProcessor::TranswaveAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Init curve atomics from APVTS defaults
    for (int i = 0; i < EVO_POINTS; ++i)
    {
        juce::String id = "evoPoint_" + juce::String::formatted("%02d", i);
        auto* p = apvts.getRawParameterValue(id);
        evoCurve[i].store(p ? p->load() : (float)i/(float)(EVO_POINTS-1));
    }

    pEvoTime      = apvts.getRawParameterValue("evoTime");
    pEvoStepped   = apvts.getRawParameterValue("evoStepped");
    pEvoLFORate   = apvts.getRawParameterValue("evoLFORate");
    pEvoLFODepth  = apvts.getRawParameterValue("evoLFODepth");
    pPosLFORate   = apvts.getRawParameterValue("posLFORate");
    pPosLFODepth  = apvts.getRawParameterValue("posLFODepth");
    pAttack       = apvts.getRawParameterValue("attack");
    pDecay        = apvts.getRawParameterValue("decay");
    pSustain      = apvts.getRawParameterValue("sustain");
    pRelease      = apvts.getRawParameterValue("release");
    pGain         = apvts.getRawParameterValue("gain");
    pBitCrush     = apvts.getRawParameterValue("bitCrush");
    pGrit         = apvts.getRawParameterValue("grit");
    pDetune       = apvts.getRawParameterValue("detune");
    pPitchLFO     = apvts.getRawParameterValue("pitchLFO");
    pPitchLFORate = apvts.getRawParameterValue("pitchLFORate");
    pScanStyle    = apvts.getRawParameterValue("scanStyle");
    pJumpProb     = apvts.getRawParameterValue("jumpProb");
    pFilterFreq   = apvts.getRawParameterValue("filterFreq");
    pFilterQ      = apvts.getRawParameterValue("filterQ");
    pSpread       = apvts.getRawParameterValue("spread");
    pStereoWidth  = apvts.getRawParameterValue("stereoWidth");
    pUniDetune    = apvts.getRawParameterValue("uniDetune");
    pStereoPhase  = apvts.getRawParameterValue("stereoPhase");
    pChorusRate   = apvts.getRawParameterValue("chorusRate");
    pChorusDepth  = apvts.getRawParameterValue("chorusDepth");
    pReverbSize   = apvts.getRawParameterValue("reverbSize");
    pReverbDamp   = apvts.getRawParameterValue("reverbDamp");
    pReverbWet    = apvts.getRawParameterValue("reverbWet");
}

TranswaveAudioProcessor::~TranswaveAudioProcessor() {}

//==============================================================================
void TranswaveAudioProcessor::setCurvePoint (int idx, float val)
{
    idx = juce::jlimit(0, EVO_POINTS-1, idx);
    val = juce::jlimit(0.0f, 1.0f, val);
    evoCurve[idx].store(val);
    // Also update APVTS so it gets saved in presets
    juce::String id = "evoPoint_" + juce::String::formatted("%02d", idx);
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(val);
}

float TranswaveAudioProcessor::evalCurve (float t) const
{
    t = juce::jlimit(0.0f, 1.0f, t);
    float fi = t * (float)(EVO_POINTS - 1);
    int   i0 = juce::jlimit(0, EVO_POINTS-1, (int)fi);
    int   i1 = juce::jlimit(0, EVO_POINTS-1, i0+1);
    float frac = fi - (float)i0;
    float v0 = evoCurve[i0].load();
    float v1 = evoCurve[i1].load();

    if (pEvoStepped && pEvoStepped->load() > 0.5f)
        return v0;                      // stepped: no interpolation
    return v0 + frac*(v1-v0);         // smooth: linear interpolation
}

float TranswaveAudioProcessor::getCurrentEvoFramePos() const
{
    return evalCurve(evoPlayhead.load());
}

//==============================================================================
void TranswaveAudioProcessor::prepareToPlay (double sr, int)
{
    currentSampleRate = sr;
    gainSmooth.reset(sr, 0.05);
    gainSmooth.setCurrentAndTargetValue(pGain->load());

    evoLFOPhase = posLFOPhase = pitchLFOPhase = 0.0;
    curvePhase  = 0.0;
    scanDir     = 1;
    curveFinished = false;

    filter.reset(); chorus.reset(); reverb.reset();

    juce::Reverb::Parameters rp;
    rp.roomSize=pReverbSize->load(); rp.damping=pReverbDamp->load();
    rp.wetLevel=pReverbWet->load(); rp.dryLevel=1.f-pReverbWet->load();
    rp.width=1.f; rp.freezeMode=0.f;
    reverb.setParameters(rp);

    for (auto& v : voices)
    { v.active=false; v.envStage=TranswaveVoice::Env::Idle; v.envLevel=0.f; v.phaseA=v.phaseB=0.0; v.frameOffset=0.f; }
}

void TranswaveAudioProcessor::releaseResources() {}

//==============================================================================
void TranswaveAudioProcessor::loadWavetable (const juce::File& file, int cs, int slot)
{
    jassert(slot==0||slot==1);
    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
    if (!reader) return;

    juce::AudioBuffer<float> buf(1,(int)reader->lengthInSamples);
    reader->read(&buf,0,(int)reader->lengthInSamples,0,true,true);
    int total=buf.getNumSamples();
    cs = juce::jlimit(16,total,cs);
    int nf=total/cs; if(nf<1) return;

    auto q16=[](float s){ return juce::jlimit(-1.f,1.f,std::round(s*32767.f)/32767.f); };
    std::vector<std::vector<float>> frames((size_t)nf,std::vector<float>((size_t)cs));
    const float* src=buf.getReadPointer(0);
    for(int f=0;f<nf;++f) for(int s=0;s<cs;++s) frames[(size_t)f][(size_t)s]=q16(src[f*cs+s]);

    { juce::ScopedLock sl(wt[slot].lock);
      wt[slot].frames=std::move(frames); wt[slot].numFrames=nf; wt[slot].cycleSamples=cs;
      wt[slot].loaded=true; wt[slot].name=file.getFileNameWithoutExtension();
      wt[slot].filePath=file.getFullPathName(); }

    for(auto& v:voices){v.active=false;v.envStage=TranswaveVoice::Env::Idle;}
}

bool         TranswaveAudioProcessor::isWavetableLoaded(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].loaded; }
int          TranswaveAudioProcessor::getNumFrames(int s) const      { juce::ScopedLock l(wt[s].lock); return wt[s].numFrames; }
int          TranswaveAudioProcessor::getCycleSamples(int s) const   { juce::ScopedLock l(wt[s].lock); return wt[s].cycleSamples; }
juce::String TranswaveAudioProcessor::getWavetableName(int s) const  { juce::ScopedLock l(wt[s].lock); return wt[s].name; }
juce::String TranswaveAudioProcessor::getWavetableFilePath(int s) const { juce::ScopedLock l(wt[s].lock); return wt[s].filePath; }

bool TranswaveAudioProcessor::getFrameSamples(int slot,int fi,std::vector<float>& o) const {
    juce::ScopedLock l(wt[slot].lock);
    if(wt[slot].frames.empty()||fi<0||fi>=wt[slot].numFrames) return false;
    o=wt[slot].frames[(size_t)fi]; return true; }

bool TranswaveAudioProcessor::getWavetableOverview(int slot,int dw,int,std::vector<float>& o) const {
    juce::ScopedLock l(wt[slot].lock);
    if(wt[slot].frames.empty()||dw<=0) return false;
    o.resize((size_t)dw);
    for(int px=0;px<dw;++px){
        float t=(float)px/(float)(dw-1);
        int f0=juce::jlimit(0,wt[slot].numFrames-1,(int)(t*(wt[slot].numFrames-1)));
        float pk=0.f; for(float s:wt[slot].frames[(size_t)f0]) pk=juce::jmax(pk,std::abs(s));
        o[(size_t)px]=pk; }
    return true; }

//==============================================================================
float TranswaveAudioProcessor::sampleFrameRaw(const WavetableSlot& s,float fi,double ph) const {
    if(s.frames.empty()) return 0.f;
    int i0=juce::jlimit(0,s.numFrames-1,(int)fi), i1=juce::jlimit(0,s.numFrames-1,i0+1);
    float bl=fi-(float)i0;
    int si=juce::jlimit(0,s.cycleSamples-1,(int)(ph*s.cycleSamples)%s.cycleSamples);
    return s.frames[(size_t)i0][(size_t)si]+bl*(s.frames[(size_t)i1][(size_t)si]-s.frames[(size_t)i0][(size_t)si]); }

float TranswaveAudioProcessor::sampleFrameNearest(int slot,float fi,double ph) {
    juce::ScopedLock l(wt[slot].lock); return sampleFrameRaw(wt[slot],fi,ph); }

float TranswaveAudioProcessor::applyBitCrush(float s,float bits) {
    if(bits>=15.9f) return s;
    float lv=std::pow(2.f,bits)-1.f; return std::round(s*lv)/lv; }

//==============================================================================
void TranswaveAudioProcessor::synthesiseVoice(TranswaveVoice& v,int slot,
    float framePosNorm,float posLFOMod,double pitchMult,float& outL,float& outR)
{
    outL=outR=0.f; if(!v.active) return;
    float isr=(float)(1.0/currentSampleRate);
    float att=juce::jmax(0.001f,pAttack->load()), dec=juce::jmax(0.001f,pDecay->load());
    float sus=pSustain->load(), rel=juce::jmax(0.001f,pRelease->load());

    switch(v.envStage){
    case TranswaveVoice::Env::Attack:
        v.envLevel+=isr/att;
        if(v.envLevel>=1.f){v.envLevel=1.f;v.envStage=TranswaveVoice::Env::Decay;v.envTime=0.f;} break;
    case TranswaveVoice::Env::Decay:
        v.envLevel-=isr*(1.f-sus)/dec;
        if(v.envLevel<=sus){v.envLevel=sus;v.envStage=TranswaveVoice::Env::Sustain;} break;
    case TranswaveVoice::Env::Sustain: v.envLevel=sus; break;
    case TranswaveVoice::Env::Release:
        v.envLevel-=isr*v.releaseStartLevel/rel;
        if(v.envLevel<=0.f){v.envLevel=0.f;v.envStage=TranswaveVoice::Env::Idle;v.active=false;return;} break;
    case TranswaveVoice::Env::Idle: v.active=false; return; }

    double mf=440.0*std::pow(2.0,(v.midiNote-69.0)/12.0);
    double dm=std::pow(2.0,pDetune->load()/1200.0);
    float ur=pUniDetune->load();
    float uRand=(float)((v.midiNote*1664525+1013904223)&0x7FFFFFFF)/(float)0x7FFFFFFF;
    double um=std::pow(2.0,(uRand*2.f-1.f)*ur/1200.0);
    float sc=pSpread->load();
    double sA=std::pow(2.0,-sc/2400.0), sB=std::pow(2.0,sc/2400.0);
    double fA=mf*dm*um*pitchMult*sA, fB=mf*dm*um*pitchMult*sB;
    v.phaseA+=fA/currentSampleRate; v.phaseB+=fB/currentSampleRate;

    if(v.phaseA>=1.0){
        v.phaseA-=1.0;
        float jp=pJumpProb->load();
        if(jp>0.f&&((float)std::rand()/(float)RAND_MAX)<jp*0.5f)
            v.frameOffset=((float)std::rand()/(float)RAND_MAX)-framePosNorm; }
    if(v.phaseB>=1.0) v.phaseB-=1.0;

    float rv=framePosNorm+v.frameOffset+posLFOMod*0.5f;
    rv=std::fmod(rv,1.f); if(rv<0.f)rv+=1.f;
    const WavetableSlot& s=wt[slot];
    float fi=rv*(float)(s.numFrames-1);

    double lA=v.phaseA, lB=v.phaseB;
    float gr=pGrit->load();
    if(gr>0.f&&((float)std::rand()/(float)RAND_MAX)<gr*0.75f){
        lA=std::round(lA*s.cycleSamples)/(double)s.cycleSamples;
        lB=std::round(lB*s.cycleSamples)/(double)s.cycleSamples; }
    while(lA<0.0)lA+=1.0; while(lA>=1.0)lA-=1.0;
    while(lB<0.0)lB+=1.0; while(lB>=1.0)lB-=1.0;
    double phB=pStereoPhase->load();
    lB=std::fmod(lB+phB,1.0); if(lB<0.0)lB+=1.0;

    float smpA,smpB;
    { juce::ScopedLock sl(s.lock);
      smpA=sampleFrameRaw(s,fi,lA); smpB=sampleFrameRaw(s,fi,lB); }
    smpA=applyBitCrush(smpA,pBitCrush->load());
    smpB=applyBitCrush(smpB,pBitCrush->load());

    float ev=v.envLevel*v.velocity;
    float w=pStereoWidth->load();
    float mid=(smpA+smpB)*0.5f, side=(smpA-smpB)*0.5f;
    outL=(mid+side*w)*ev; outR=(mid-side*w)*ev;
}

//==============================================================================
void TranswaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();

    for(const auto md:midiMessages){
        auto msg=md.getMessage();
        if(msg.isNoteOn()){
            int si=-1; float minL=2.f; int minI=0;
            for(int i=0;i<MAX_VOICES;++i){
                if(!voices[i].active){si=i;break;}
                if(voices[i].envStage==TranswaveVoice::Env::Release&&voices[i].envLevel<minL)
                {minL=voices[i].envLevel;minI=i;} }
            if(si<0)si=minI;
            voices[si].noteOn(msg.getNoteNumber(),(float)msg.getVelocity()/127.f);
        } else if(msg.isNoteOff()){
            for(auto& v:voices)
                if(v.active&&v.midiNote==msg.getNoteNumber()&&v.envStage!=TranswaveVoice::Env::Release)
                    v.noteOff();
        } else if(msg.isAllNotesOff()||msg.isAllSoundOff()){
            for(auto& v:voices){v.active=false;v.envStage=TranswaveVoice::Env::Idle;} } }

    int curSlot=activeSlot.load();
    if(!wt[curSlot].loaded) return;

    int  N=buffer.getNumSamples();
    float* outL=buffer.getWritePointer(0), *outR=buffer.getWritePointer(1);
    gainSmooth.setTargetValue(pGain->load());

    // Read params once per block
    float evoTime     = juce::jmax(0.1f, pEvoTime->load());
    float evoLFORate  = pEvoLFORate->load();
    float evoLFODepth = pEvoLFODepth->load();
    float posLFORate  = pPosLFORate->load();
    float posLFODepth = pPosLFODepth->load();
    float pitchLFORate= pPitchLFORate->load();
    float pitchLFODep = pPitchLFO->load();
    auto  scanMode    = (ScanMode)(int)juce::jlimit(0.f,4.f,pScanStyle->load());
    float filterFreq  = pFilterFreq->load();
    float filterQ     = juce::jmax(0.1f,pFilterQ->load());

    filter.setLowpass(filterFreq,filterQ,currentSampleRate);

    { juce::Reverb::Parameters rp;
      rp.roomSize=pReverbSize->load(); rp.damping=pReverbDamp->load();
      rp.wetLevel=pReverbWet->load(); rp.dryLevel=1.f-pReverbWet->load();
      rp.width=1.f; rp.freezeMode=0.f; reverb.setParameters(rp); }

    float chRate=pChorusRate->load(), chDepth=pChorusDepth->load();
    // Increment per sample: one full curve cycle in evoTime seconds
    double phaseInc = 1.0 / (evoTime * currentSampleRate);

    for(int s=0;s<N;++s){
        // --- Advance curve playhead ---
        if(!curveFinished){
            switch(scanMode){
            case ScanMode::Forward:
                curvePhase+=phaseInc;
                if(curvePhase>=1.0) curvePhase-=1.0;
                break;
            case ScanMode::FwdStay:
                curvePhase+=phaseInc;
                if(curvePhase>=1.0){curvePhase=1.0;curveFinished=true;}
                break;
            case ScanMode::BackForth:
                curvePhase+=phaseInc*scanDir;
                if(curvePhase>=1.0){curvePhase=1.0;scanDir=-1;}
                else if(curvePhase<=0.0){curvePhase=0.0;scanDir=1;}
                break;
            case ScanMode::BwdStay:
                curvePhase-=phaseInc;
                if(curvePhase<=0.0){curvePhase=0.0;curveFinished=true;}
                break;
            case ScanMode::Backward:
                curvePhase-=phaseInc;
                if(curvePhase<=0.0) curvePhase+=1.0;
                break;
            }
        }

        // Publish playhead for display (every 64 samples to reduce atomic contention)
        if((s & 63) == 0) evoPlayhead.store((float)curvePhase);

        // --- LFOs ---
        evoLFOPhase  +=evoLFORate /currentSampleRate; if(evoLFOPhase >1.0)evoLFOPhase -=1.0;
        posLFOPhase  +=posLFORate /currentSampleRate; if(posLFOPhase >1.0)posLFOPhase -=1.0;
        pitchLFOPhase+=pitchLFORate/currentSampleRate; if(pitchLFOPhase>1.0)pitchLFOPhase-=1.0;
        float evoLFO  =(float)std::sin(juce::MathConstants<double>::twoPi*evoLFOPhase);
        float posLFO  =(float)std::sin(juce::MathConstants<double>::twoPi*posLFOPhase);
        float pitchLFO=(float)std::sin(juce::MathConstants<double>::twoPi*pitchLFOPhase);

        // Curve output + evo LFO modulation (LFO shakes the readhead, clamped 0..1)
        float curveOut = evalCurve((float)curvePhase);
        float rawEvoPos = juce::jlimit(0.f,1.f, curveOut + evoLFO*evoLFODepth*0.5f);

        double pitchMult=std::pow(2.0,(double)(pitchLFO*pitchLFODep)/12.0);

        float mixL=0.f,mixR=0.f;
        for(auto& v:voices){
            float vL=0.f,vR=0.f;
            synthesiseVoice(v,curSlot,rawEvoPos,posLFO*posLFODepth,pitchMult,vL,vR);
            mixL+=vL; mixR+=vR; }

        const float sc=0.3f;
        mixL=(mixL*sc)/(1.f+std::abs(mixL*sc));
        mixR=(mixR*sc)/(1.f+std::abs(mixR*sc));
        mixL=filter.processL(mixL); mixR=filter.processR(mixR);
        float g=gainSmooth.getNextValue();
        outL[s]=mixL*g; outR[s]=mixR*g; }

    if(chDepth>0.001f)
        for(int s=0;s<N;++s) chorus.process(outL[s],outR[s],chRate,chDepth,currentSampleRate);
    reverb.processStereo(outL,outR,N);
}

//==============================================================================
void TranswaveAudioProcessor::parameterChanged(const juce::String& id, float val)
{
    // Sync evoCurve atomics when APVTS is updated (e.g. on preset load)
    if(id.startsWith("evoPoint_")){
        int idx=id.substring(9).getIntValue();
        if(idx>=0&&idx<EVO_POINTS) evoCurve[idx].store(val); }
}

//==============================================================================
static std::unique_ptr<juce::XmlElement> buildFullStateXml(
    juce::AudioProcessorValueTreeState& apvts, int activeSlot,
    const juce::String& pA, int csA, const juce::String& pB, int csB)
{
    auto root=std::make_unique<juce::XmlElement>("PhizmOscState");
    auto state=apvts.copyState(); root->addChildElement(state.createXml());
    root->setAttribute("activeSlot",activeSlot);
    root->setAttribute("pathA",pA); root->setAttribute("cycleSizeA",csA);
    root->setAttribute("pathB",pB); root->setAttribute("cycleSizeB",csB);
    return root;
}

void TranswaveAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::String pA,pB; int csA=2048,csB=2048;
    { juce::ScopedLock la(wt[0].lock); pA=wt[0].filePath; csA=wt[0].cycleSamples; }
    { juce::ScopedLock lb(wt[1].lock); pB=wt[1].filePath; csB=wt[1].cycleSamples; }
    auto xml=buildFullStateXml(apvts,activeSlot.load(),pA,csA,pB,csB);
    copyXmlToBinary(*xml,destData);
}

void TranswaveAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data,sizeInBytes));
    if(!xml) return;
    if(auto* c=xml->getFirstChildElement()){ auto vt=juce::ValueTree::fromXml(*c); if(vt.isValid()) apvts.replaceState(vt); }
    // Sync curve atomics from restored APVTS
    for(int i=0;i<EVO_POINTS;++i){
        juce::String id="evoPoint_"+juce::String::formatted("%02d",i);
        if(auto* p=apvts.getRawParameterValue(id)) evoCurve[i].store(p->load()); }
    activeSlot.store(xml->getIntAttribute("activeSlot",0));
    curvePhase=0.0; curveFinished=false; scanDir=1;
    juce::String pA=xml->getStringAttribute("pathA"); int csA=xml->getIntAttribute("cycleSizeA",2048);
    juce::String pB=xml->getStringAttribute("pathB"); int csB=xml->getIntAttribute("cycleSizeB",2048);
    if(pA.isNotEmpty()){juce::File f(pA);if(f.existsAsFile())loadWavetable(f,csA,0);}
    if(pB.isNotEmpty()){juce::File f(pB);if(f.existsAsFile())loadWavetable(f,csB,1);}
}

juce::File TranswaveAudioProcessor::getPresetsDirectory()
{
    juce::File exe=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File c=exe;
    for(int i=0;i<5;++i){ c=c.getParentDirectory();
        if(c.getFileExtension().equalsIgnoreCase(".vst3")||
           c.getFileExtension().equalsIgnoreCase(".component")||
           c.getFileExtension().equalsIgnoreCase(".vst"))
        { auto d=c.getParentDirectory().getChildFile("PhizmOsc Presets"); d.createDirectory(); return d; } }
    auto fb=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("PhizmOsc Presets");
    fb.createDirectory(); return fb;
}

bool TranswaveAudioProcessor::savePreset(const juce::File& dest)
{
    juce::String pA,pB; int csA=2048,csB=2048;
    { juce::ScopedLock la(wt[0].lock); pA=wt[0].filePath; csA=wt[0].cycleSamples>0?wt[0].cycleSamples:2048; }
    { juce::ScopedLock lb(wt[1].lock); pB=wt[1].filePath; csB=wt[1].cycleSamples>0?wt[1].cycleSamples:2048; }
    auto xml=buildFullStateXml(apvts,activeSlot.load(),pA,csA,pB,csB);
    return xml->writeTo(dest);
}

bool TranswaveAudioProcessor::loadPreset(const juce::File& src)
{
    auto xml=juce::XmlDocument::parse(src);
    if(!xml||xml->getTagName()!="PhizmOscState") return false;
    if(auto* c=xml->getFirstChildElement()){ auto vt=juce::ValueTree::fromXml(*c); if(vt.isValid()) apvts.replaceState(vt); }
    for(int i=0;i<EVO_POINTS;++i){
        juce::String id="evoPoint_"+juce::String::formatted("%02d",i);
        if(auto* p=apvts.getRawParameterValue(id)) evoCurve[i].store(p->load()); }
    activeSlot.store(xml->getIntAttribute("activeSlot",0));
    curvePhase=0.0; curveFinished=false; scanDir=1;
    juce::String pA=xml->getStringAttribute("pathA"); int csA=xml->getIntAttribute("cycleSizeA",2048);
    juce::String pB=xml->getStringAttribute("pathB"); int csB=xml->getIntAttribute("cycleSizeB",2048);
    if(pA.isNotEmpty()){juce::File f(pA);if(f.existsAsFile())loadWavetable(f,csA,0);}
    if(pB.isNotEmpty()){juce::File f(pB);if(f.existsAsFile())loadWavetable(f,csB,1);}
    return true;
}

juce::AudioProcessorEditor* TranswaveAudioProcessor::createEditor()
{ return new TranswaveAudioProcessorEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{ return new TranswaveAudioProcessor(); }
