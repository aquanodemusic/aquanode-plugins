#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
// PhismOscLookAndFeel
//==============================================================================
PhismOscLookAndFeel::PhismOscLookAndFeel()
{
    setColour(juce::Label::textColourId,              juce::Colours::white.withAlpha(0.85f));
    setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white.withAlpha(0.7f));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x33000000));
    setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0x00000000));
    setColour(juce::TextButton::buttonColourId,        juce::Colour(0xff3a1a6e));
    setColour(juce::TextButton::textColourOffId,       juce::Colour(0xffff6ec7));
    setColour(juce::TextEditor::backgroundColourId,    juce::Colour(0xff1a0a2e));
    setColour(juce::TextEditor::textColourId,          juce::Colour(0xff6ec6ff));
    setColour(juce::TextEditor::outlineColourId,       juce::Colour(0xff6040a0));
}

void PhismOscLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x,int y,int w,int h,float sliderPos,float startA,float endA,juce::Slider&)
{
    auto b=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h).reduced(4.f);
    float cx=b.getCentreX(), cy=b.getCentreY(), r=b.getWidth()*0.5f;
    juce::ColourGradient gl(accent1.withAlpha(0.35f),cx,cy,accent2.withAlpha(0.f),cx+r,cy,true);
    g.setGradientFill(gl); g.fillEllipse(b.expanded(4.f));
    juce::Colour rc=accent1.interpolatedWith(accent2,sliderPos);
    g.setColour(rc); g.drawEllipse(b,2.f);
    juce::ColourGradient bg2(knobFace.brighter(0.15f),cx-r*0.3f,cy-r*0.4f,knobFace.darker(0.3f),cx+r*0.3f,cy+r*0.4f,true);
    g.setGradientFill(bg2); g.fillEllipse(b.reduced(2.f));
    float ang=startA+sliderPos*(endA-startA);
    float px=cx+(r-6.f)*std::sin(ang), py=cy-(r-6.f)*std::cos(ang);
    juce::Path ptr; ptr.startNewSubPath(cx+(r*0.15f)*std::sin(ang),cy-(r*0.15f)*std::cos(ang)); ptr.lineTo(px,py);
    g.setColour(rc.brighter(0.5f)); g.strokePath(ptr,juce::PathStrokeType(2.5f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.9f)); g.fillEllipse(px-2.5f,py-2.5f,5.f,5.f);
    juce::Path arc; arc.addCentredArc(cx,cy,r+3.f,r+3.f,0.f,startA,endA,true);
    g.setColour(juce::Colours::white.withAlpha(0.12f)); g.strokePath(arc,juce::PathStrokeType(1.5f));
    juce::Path af; af.addCentredArc(cx,cy,r+3.f,r+3.f,0.f,startA,startA+sliderPos*(endA-startA),true);
    g.setColour(rc.withAlpha(0.6f)); g.strokePath(af,juce::PathStrokeType(2.5f));
}

void PhismOscLookAndFeel::drawLabel(juce::Graphics& g,juce::Label& label){
    g.fillAll(juce::Colour(0x00000000));
    auto font=getLabelFont(label); g.setFont(font);
    g.setColour(label.findColour(juce::Label::textColourId));
    g.drawFittedText(label.getText(),label.getLocalBounds(),label.getJustificationType(),
        juce::jmax(1,(int)((float)label.getHeight()/font.getHeight()))); }

juce::Font PhismOscLookAndFeel::getLabelFont(juce::Label&)
{ return juce::Font(juce::FontOptions().withHeight(11.f)); }

void PhismOscLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,
    const juce::Colour&,bool hi,bool dn){
    auto bounds=b.getLocalBounds().toFloat().reduced(1.f);
    float a=dn?0.9f:(hi?0.7f:0.5f);
    juce::ColourGradient gr(accent1.withAlpha(a),bounds.getTopLeft(),accent2.withAlpha(a),bounds.getBottomRight(),false);
    g.setGradientFill(gr); g.fillRoundedRectangle(bounds,4.f);
    g.setColour(accent1.withAlpha(0.6f)); g.drawRoundedRectangle(bounds,4.f,1.f); }

void PhismOscLookAndFeel::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool){
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawFittedText(b.getButtonText(),b.getLocalBounds(),juce::Justification::centred,1); }

//==============================================================================
// PhismOscKnob
//==============================================================================
PhismOscKnob::PhismOscKnob(const juce::String& lbl,
    juce::AudioProcessorValueTreeState& apvts,const juce::String& id,PhismOscLookAndFeel& laf)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,58,14);
    slider.setLookAndFeel(&laf); addAndMakeVisible(slider);
    label.setText(lbl,juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setLookAndFeel(&laf); addAndMakeVisible(label);
    attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts,id,slider);
}
void PhismOscKnob::resized(){ auto b=getLocalBounds(); label.setBounds(b.removeFromBottom(16)); slider.setBounds(b); }

//==============================================================================
// EvoCurveEditor
//==============================================================================
EvoCurveEditor::EvoCurveEditor(TranswaveAudioProcessor& p) : proc(p)
{
    startTimerHz(30);

    btnReset.onClick = [this]{
        for(int i=0;i<EVO_POINTS;++i) proc.setCurvePoint(i,0.5f);
        repaint(); };
    btnRamp.onClick = [this]{
        for(int i=0;i<EVO_POINTS;++i) proc.setCurvePoint(i,(float)i/(float)(EVO_POINTS-1));
        repaint(); };
    btnStepped.onClick = [this]{
        // Toggle stepped param via APVTS
        if(auto* p=proc.apvts.getParameter("evoStepped"))
            p->setValueNotifyingHost(p->getValue()>0.5f ? 0.f : 1.f);
        repaint(); };

    addAndMakeVisible(btnReset);
    addAndMakeVisible(btnRamp);
    addAndMakeVisible(btnStepped);
}

EvoCurveEditor::~EvoCurveEditor() { stopTimer(); }

juce::Rectangle<float> EvoCurveEditor::drawArea() const
{
    // Reserve 24px top for buttons, 4px bottom margin, 4px side margins
    auto b = getLocalBounds().toFloat();
    return b.reduced(4.f, 0.f).withTrimmedTop(28.f).withTrimmedBottom(4.f);
}

float EvoCurveEditor::pointToPixelX(int idx) const {
    auto da=drawArea();
    return da.getX() + (float)idx/(float)(EVO_POINTS-1) * da.getWidth(); }

float EvoCurveEditor::pointToPixelY(float val) const {
    auto da=drawArea();
    // val 0=bottom, 1=top
    return da.getBottom() - val*da.getHeight(); }

int EvoCurveEditor::xToPointIndex(float px) const {
    auto da=drawArea();
    float t=(px-da.getX())/da.getWidth();
    return juce::jlimit(0,EVO_POINTS-1,(int)std::round(t*(float)(EVO_POINTS-1))); }

float EvoCurveEditor::yToVal(float py) const {
    auto da=drawArea();
    float v=(da.getBottom()-py)/da.getHeight();
    return juce::jlimit(0.f,1.f,v); }

void EvoCurveEditor::paint(juce::Graphics& g)
{
    auto b=getLocalBounds().toFloat();
    auto da=drawArea();

    // Background
    juce::ColourGradient bg(juce::Colour(0xff0d0520),b.getTopLeft(),
                             juce::Colour(0xff051030),b.getBottomRight(),false);
    g.setGradientFill(bg); g.fillRoundedRectangle(b,6.f);
    g.setColour(juce::Colour(0xff6040a0).withAlpha(0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f),6.f,1.f);

    // Grid lines (horizontal at 0, 0.25, 0.5, 0.75, 1.0)
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for(int i=0;i<=4;++i){
        float y=pointToPixelY((float)i/4.f);
        g.drawHorizontalLine((int)y,da.getX(),da.getRight());
    }
    // Vertical grid (every 8 points)
    for(int i=0;i<EVO_POINTS;i+=8){
        float x=pointToPixelX(i);
        g.drawVerticalLine((int)x,da.getY(),da.getBottom());
    }

    bool stepped=(proc.apvts.getRawParameterValue("evoStepped")
                  && proc.apvts.getRawParameterValue("evoStepped")->load()>0.5f);

    // Curve fill (gradient under line)
    juce::Path fillPath;
    fillPath.startNewSubPath(pointToPixelX(0), da.getBottom());
    for(int i=0;i<EVO_POINTS;++i){
        float x=pointToPixelX(i), y=pointToPixelY(proc.getCurvePoint(i));
        if(stepped && i>0){
            float px=pointToPixelX(i-1), py=pointToPixelY(proc.getCurvePoint(i-1));
            fillPath.lineTo(x,py); // horizontal step
        }
        fillPath.lineTo(x,y);
    }
    fillPath.lineTo(pointToPixelX(EVO_POINTS-1), da.getBottom());
    fillPath.closeSubPath();
    juce::ColourGradient fillGrad(juce::Colour(PhismOscLookAndFeel::col_accent1).withAlpha(0.18f),0.f,da.getY(),
                                   juce::Colour(PhismOscLookAndFeel::col_accent2).withAlpha(0.04f),0.f,da.getBottom(),false);
    g.setGradientFill(fillGrad); g.fillPath(fillPath);

    // Curve line
    juce::Path linePath;
    for(int i=0;i<EVO_POINTS;++i){
        float x=pointToPixelX(i), y=pointToPixelY(proc.getCurvePoint(i));
        if(i==0) linePath.startNewSubPath(x,y);
        else {
            if(stepped){ float px=pointToPixelX(i-1),py=pointToPixelY(proc.getCurvePoint(i-1)); linePath.lineTo(x,py); }
            linePath.lineTo(x,y); }
    }
    juce::ColourGradient lineGrad(juce::Colour(PhismOscLookAndFeel::col_accent1),da.getTopLeft(),
                                   juce::Colour(PhismOscLookAndFeel::col_accent2),da.getTopRight(),false);
    g.setGradientFill(lineGrad);
    g.strokePath(linePath,juce::PathStrokeType(2.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));

    // Point handles
    for(int i=0;i<EVO_POINTS;++i){
        float x=pointToPixelX(i), y=pointToPixelY(proc.getCurvePoint(i));
        bool  hovered=(i==dragIndex);
        g.setColour(hovered ? juce::Colour(PhismOscLookAndFeel::col_accent1)
                             : juce::Colours::white.withAlpha(0.55f));
        g.fillEllipse(x-3.5f,y-3.5f,7.f,7.f);
        if(hovered){ g.setColour(juce::Colour(PhismOscLookAndFeel::col_accent1).withAlpha(0.6f));
                     g.drawEllipse(x-5.f,y-5.f,10.f,10.f,1.5f); }
    }

    // Playhead
    float ph=proc.evoPlayhead.load();
    float phX=da.getX()+ph*da.getWidth();
    float phVal=proc.evalCurve(ph);
    float phY=pointToPixelY(phVal);

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawVerticalLine((int)phX,da.getY(),da.getBottom());
    g.setColour(juce::Colour(PhismOscLookAndFeel::col_accent1));
    g.fillEllipse(phX-4.f,phY-4.f,8.f,8.f);

    // Labels
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.f)));
    g.setColour(juce::Colour(PhismOscLookAndFeel::col_accent1).withAlpha(0.8f));
    g.drawText("EVOLUTION CURVE",b.reduced(6.f,3.f).withHeight(16.f).toNearestInt(),
               juce::Justification::centredLeft);
    // Y axis labels
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawText("1.0",juce::Rectangle<int>((int)da.getX()-2,(int)da.getY(),26,12),juce::Justification::centredRight);
    g.drawText("0.0",juce::Rectangle<int>((int)da.getX()-2,(int)da.getBottom()-12,26,12),juce::Justification::centredRight);
}

void EvoCurveEditor::handleDrag(const juce::MouseEvent& e)
{
    int idx=xToPointIndex((float)e.x);
    float val=yToVal((float)e.y);
    dragIndex=idx;
    proc.setCurvePoint(idx,val);
    repaint();
}

void EvoCurveEditor::mouseDown(const juce::MouseEvent& e) { handleDrag(e); }
void EvoCurveEditor::mouseDrag(const juce::MouseEvent& e) { handleDrag(e); }
void EvoCurveEditor::mouseUp  (const juce::MouseEvent&)   { dragIndex=-1; repaint(); }

//==============================================================================
// WavetableDisplay
//==============================================================================
WavetableDisplay::WavetableDisplay(TranswaveAudioProcessor& p,int slot,const juce::String& lbl)
    : proc(p),slotIndex(slot),slotLabel(lbl) { startTimerHz(30); }
WavetableDisplay::~WavetableDisplay() { stopTimer(); }

void WavetableDisplay::paint(juce::Graphics& g)
{
    auto b=getLocalBounds().toFloat();
    juce::ColourGradient bg(juce::Colour(0xff0d0520),b.getTopLeft(),juce::Colour(0xff051030),b.getBottomRight(),false);
    g.setGradientFill(bg); g.fillRoundedRectangle(b,6.f);

    bool isActive=(proc.activeSlot.load()==slotIndex);
    juce::Colour bc=isActive?juce::Colour(0xffff6ec7):juce::Colour(0xff6040a0);
    g.setColour(bc.withAlpha(isActive?0.9f:0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f),6.f,isActive?1.5f:1.f);

    juce::Rectangle<float> badge(b.getX()+6.f,b.getY()+5.f,42.f,15.f);
    juce::ColourGradient bg2(isActive?juce::Colour(0xffff6ec7):juce::Colour(0xff6040a0),badge.getTopLeft(),
                              isActive?juce::Colour(0xff6ec6ff):juce::Colour(0xff2a1060),badge.getBottomRight(),false);
    g.setGradientFill(bg2); g.fillRoundedRectangle(badge,3.f);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    g.drawText(slotLabel,badge.toNearestInt(),juce::Justification::centred);

    if(!proc.isWavetableLoaded(slotIndex)){
        g.setColour(juce::Colour(0xff6040a0).withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.f)));
        g.drawText("Load a .wav wavetable",b,juce::Justification::centred); return; }

    g.setColour(juce::Colour(0xaaff6ec7));
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.f)));
    g.drawText(proc.getWavetableName(slotIndex),
               b.reduced(54.f,4.f).withTop(b.getY()+5.f).withHeight(15.f).toNearestInt(),
               juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xaa6ec6ff));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.f)));
    juce::String info=juce::String(proc.getNumFrames(slotIndex))+" fr  "+juce::String(proc.getCycleSamples(slotIndex))+" smp";
    g.drawText(info,b.reduced(4.f,5.f).withHeight(14.f).toNearestInt(),juce::Justification::topRight);

    int nf=proc.getNumFrames(slotIndex), w=(int)b.getWidth();
    if(nf>0&&w>0){
        juce::Path wp;
        float midY=b.getCentreY(), halfH=b.getHeight()*0.38f;
        float fp=proc.getCurrentEvoFramePos();
        float fi=fp*(float)(nf-1);
        for(int px=0;px<w;++px){
            double ph=(double)px/(double)(w-1);
            float s=proc.sampleFrameNearest(slotIndex,fi,ph);
            float y=midY-s*halfH;
            if(px==0)wp.startNewSubPath((float)px+b.getX(),y);
            else      wp.lineTo((float)px+b.getX(),y); }
        if(isActive){ juce::ColourGradient wg(juce::Colour(0xffff6ec7),b.getTopLeft(),juce::Colour(0xff6ec6ff),b.getBottomRight(),false); g.setGradientFill(wg); }
        else g.setColour(juce::Colour(0xff6040a0).withAlpha(0.6f));
        g.strokePath(wp,juce::PathStrokeType(1.8f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded)); }
}

//==============================================================================
// TranswaveAudioProcessorEditor
//==============================================================================
TranswaveAudioProcessorEditor::TranswaveAudioProcessorEditor(TranswaveAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      // ROW 1
      knobGain        ("Gain",       p.apvts, "gain",        laf),
      knobEvoTime     ("Evo Time",   p.apvts, "evoTime",     laf),
      knobEvoLFORate  ("Evo LFO Hz", p.apvts, "evoLFORate",  laf),
      knobEvoLFODepth ("Evo LFO D",  p.apvts, "evoLFODepth", laf),
      knobPosLFORate  ("Pos LFO Hz", p.apvts, "posLFORate",  laf),
      knobPosLFODepth ("Pos LFO D",  p.apvts, "posLFODepth", laf),
      knobDetune      ("Detune ct",  p.apvts, "detune",      laf),
      knobPitchLFO    ("Pitch LFO",  p.apvts, "pitchLFO",    laf),
      knobPitchLFORate("Ptch LFO Hz",p.apvts, "pitchLFORate",laf),
      // ROW 2
      knobAttack      ("Attack",     p.apvts, "attack",      laf),
      knobDecay       ("Decay",      p.apvts, "decay",       laf),
      knobSustain     ("Sustain",    p.apvts, "sustain",     laf),
      knobRelease     ("Release",    p.apvts, "release",     laf),
      knobBitCrush    ("Bit Depth",  p.apvts, "bitCrush",    laf),
      knobGrit        ("Grit",       p.apvts, "grit",        laf),
      knobScanStyle   ("Scan Dir",   p.apvts, "scanStyle",   laf),
      knobScanJump    ("Rnd Jump",   p.apvts, "jumpProb",    laf),
      knobFilterFreq  ("Cutoff",     p.apvts, "filterFreq",  laf),
      knobFilterRes   ("Resonance",  p.apvts, "filterQ",     laf),
      // ROW 3
      knobSpread      ("Spread ct",  p.apvts, "spread",      laf),
      knobStereoWidth ("Width",      p.apvts, "stereoWidth", laf),
      knobUniDetune   ("Uni Detune", p.apvts, "uniDetune",   laf),
      knobStereoPhase ("B Phase",    p.apvts, "stereoPhase", laf),
      knobChorusRate  ("Chr Rate",   p.apvts, "chorusRate",  laf),
      knobChorusDepth ("Chr Depth",  p.apvts, "chorusDepth", laf),
      knobReverbSize  ("Rvb Size",   p.apvts, "reverbSize",  laf),
      knobReverbDamp  ("Rvb Damp",   p.apvts, "reverbDamp",  laf),
      knobReverbWet   ("Rvb Wet",    p.apvts, "reverbWet",   laf),
      // Displays + curve
      evoCurveEditor(p),
      wtDisplayA(p,0,"OSC A"),
      wtDisplayB(p,1,"OSC B")
{
    setLookAndFeel(&laf);
    setSize(1000, 590);

    auto setupSec=[&](juce::Label& l,const juce::String& t){
        l.setText(t,juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
        l.setColour(juce::Label::textColourId,juce::Colour(0xffff6ec7));
        l.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(l); };

    setupSec(sectionWT,    "WAVETABLE");
    setupSec(sectionEvo,   "EVOLUTION");
    setupSec(sectionPitch, "PITCH");
    setupSec(sectionADSR,  "ENVELOPE");
    setupSec(sectionGrit,  "CHARACTER");
    setupSec(sectionScan,  "SCAN STYLE");
    setupSec(sectionFilter,"FILTER");
    setupSec(sectionStereo,"STEREO");
    setupSec(sectionFX,    "FX");
    setupSec(sectionCurve, "");  // text drawn inside EvoCurveEditor

    infoLabel.setText("Transwave Synthesizer",juce::dontSendNotification);
    infoLabel.setColour(juce::Label::textColourId,juce::Colour(0xaaff6ec7));
    infoLabel.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    infoLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(infoLabel);

    presetNameLabel.setText("— init —",juce::dontSendNotification);
    presetNameLabel.setColour(juce::Label::textColourId,juce::Colour(0xffb0d0ff));
    presetNameLabel.setColour(juce::Label::backgroundColourId,juce::Colour(0x22ffffff));
    presetNameLabel.setColour(juce::Label::outlineColourId,juce::Colour(0x33ff6ec7));
    presetNameLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.f)));
    presetNameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(presetNameLabel);

    savePresetButton.setButtonText("SAVE"); savePresetButton.setLookAndFeel(&laf);
    savePresetButton.onClick=[this]{savePresetClicked();};
    addAndMakeVisible(savePresetButton);
    loadPresetButton.setButtonText("LOAD"); loadPresetButton.setLookAndFeel(&laf);
    loadPresetButton.onClick=[this]{loadPresetClicked();};
    addAndMakeVisible(loadPresetButton);

    // STEPPED button in scan panel
    btnStepped.setButtonText("STEPPED"); btnStepped.setLookAndFeel(&laf);
    btnStepped.onClick=[this]{
        if(auto* p=audioProcessor.apvts.getParameter("evoStepped"))
            p->setValueNotifyingHost(p->getValue()>0.5f?0.f:1.f); };
    addAndMakeVisible(btnStepped);

    // Load A
    loadButtonA.setButtonText("LOAD A"); loadButtonA.setLookAndFeel(&laf);
    loadButtonA.onClick=[this]{loadWavetableClicked(0);}; addAndMakeVisible(loadButtonA);
    cycleSizeLabelA.setText("Cycle:",juce::dontSendNotification);
    cycleSizeLabelA.setColour(juce::Label::textColourId,juce::Colour(0xff6ec6ff));
    cycleSizeLabelA.setFont(juce::Font(juce::FontOptions().withHeight(10.5f))); addAndMakeVisible(cycleSizeLabelA);
    cycleSizeEditorA.setText("2048"); cycleSizeEditorA.setInputRestrictions(6,"0123456789");
    cycleSizeEditorA.setLookAndFeel(&laf); addAndMakeVisible(cycleSizeEditorA);
    filenameLabelA.setText("No file loaded",juce::dontSendNotification);
    filenameLabelA.setColour(juce::Label::textColourId,juce::Colour(0x88ff6ec7));
    filenameLabelA.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    filenameLabelA.setJustificationType(juce::Justification::centredLeft); addAndMakeVisible(filenameLabelA);

    // Load B
    loadButtonB.setButtonText("LOAD B"); loadButtonB.setLookAndFeel(&laf);
    loadButtonB.onClick=[this]{loadWavetableClicked(1);}; addAndMakeVisible(loadButtonB);
    cycleSizeLabelB.setText("Cycle:",juce::dontSendNotification);
    cycleSizeLabelB.setColour(juce::Label::textColourId,juce::Colour(0xff6ec6ff));
    cycleSizeLabelB.setFont(juce::Font(juce::FontOptions().withHeight(10.5f))); addAndMakeVisible(cycleSizeLabelB);
    cycleSizeEditorB.setText("2048"); cycleSizeEditorB.setInputRestrictions(6,"0123456789");
    cycleSizeEditorB.setLookAndFeel(&laf); addAndMakeVisible(cycleSizeEditorB);
    filenameLabelB.setText("No file loaded",juce::dontSendNotification);
    filenameLabelB.setColour(juce::Label::textColourId,juce::Colour(0x886ec6ff));
    filenameLabelB.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    filenameLabelB.setJustificationType(juce::Justification::centredLeft); addAndMakeVisible(filenameLabelB);

    abToggleButton.setButtonText("ACTIVE: OSC A"); abToggleButton.setLookAndFeel(&laf);
    abToggleButton.onClick=[this]{
        int c=audioProcessor.activeSlot.load(); audioProcessor.activeSlot.store(c==0?1:0);
        updateABToggleLabel(); };
    addAndMakeVisible(abToggleButton);

    addAndMakeVisible(evoCurveEditor);
    addAndMakeVisible(wtDisplayA); addAndMakeVisible(wtDisplayB);

    // All knobs
    addAndMakeVisible(knobGain);
    addAndMakeVisible(knobEvoTime); addAndMakeVisible(knobEvoLFORate); addAndMakeVisible(knobEvoLFODepth);
    addAndMakeVisible(knobPosLFORate); addAndMakeVisible(knobPosLFODepth);
    addAndMakeVisible(knobDetune); addAndMakeVisible(knobPitchLFO); addAndMakeVisible(knobPitchLFORate);
    addAndMakeVisible(knobAttack); addAndMakeVisible(knobDecay); addAndMakeVisible(knobSustain); addAndMakeVisible(knobRelease);
    addAndMakeVisible(knobBitCrush); addAndMakeVisible(knobGrit);
    addAndMakeVisible(knobScanStyle); addAndMakeVisible(knobScanJump);
    addAndMakeVisible(knobFilterFreq); addAndMakeVisible(knobFilterRes);
    addAndMakeVisible(knobSpread); addAndMakeVisible(knobStereoWidth); addAndMakeVisible(knobUniDetune); addAndMakeVisible(knobStereoPhase);
    addAndMakeVisible(knobChorusRate); addAndMakeVisible(knobChorusDepth);
    addAndMakeVisible(knobReverbSize); addAndMakeVisible(knobReverbDamp); addAndMakeVisible(knobReverbWet);

    startTimerHz(20);
}

TranswaveAudioProcessorEditor::~TranswaveAudioProcessorEditor()
{ stopTimer(); setLookAndFeel(nullptr); }

void TranswaveAudioProcessorEditor::updateABToggleLabel(){
    abToggleButton.setButtonText(audioProcessor.activeSlot.load()==0?"ACTIVE: OSC A":"ACTIVE: OSC B"); }

void TranswaveAudioProcessorEditor::loadWavetableClicked(int slot)
{
    fileChooser=std::make_unique<juce::FileChooser>("Select wavetable .wav",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),"*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
        [this,slot](const juce::FileChooser& ch){
            auto f=ch.getResult(); if(!f.existsAsFile()) return;
            int cs=(slot==0)?cycleSizeEditorA.getText().getIntValue():cycleSizeEditorB.getText().getIntValue();
            if(cs<16)cs=2048;
            audioProcessor.loadWavetable(f,cs,slot);
            if(slot==0) filenameLabelA.setText(f.getFileNameWithoutExtension(),juce::dontSendNotification);
            else        filenameLabelB.setText(f.getFileNameWithoutExtension(),juce::dontSendNotification); });
}

void TranswaveAudioProcessorEditor::savePresetClicked()
{
    fileChooser=std::make_unique<juce::FileChooser>("Save preset as...",
        TranswaveAudioProcessor::getPresetsDirectory().getChildFile("New Preset.phism"),"*.phism");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|
                              juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& ch){
            auto dest=ch.getResult(); if(dest==juce::File{}) return;
            if(dest.getFileExtension().toLowerCase()!=".phism") dest=dest.withFileExtension(".phism");
            presetNameLabel.setText(audioProcessor.savePreset(dest)?dest.getFileNameWithoutExtension():"Save failed!",
                                    juce::dontSendNotification); });
}

void TranswaveAudioProcessorEditor::loadPresetClicked()
{
    fileChooser=std::make_unique<juce::FileChooser>("Open preset...",
        TranswaveAudioProcessor::getPresetsDirectory(),"*.phism");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& ch){
            auto src=ch.getResult(); if(!src.existsAsFile()) return;
            if(audioProcessor.loadPreset(src)){
                presetNameLabel.setText(src.getFileNameWithoutExtension(),juce::dontSendNotification);
                filenameLabelA.setText(audioProcessor.isWavetableLoaded(0)?audioProcessor.getWavetableName(0):"No file loaded",juce::dontSendNotification);
                filenameLabelB.setText(audioProcessor.isWavetableLoaded(1)?audioProcessor.getWavetableName(1):"No file loaded",juce::dontSendNotification);
                if(audioProcessor.isWavetableLoaded(0)) cycleSizeEditorA.setText(juce::String(audioProcessor.getCycleSamples(0)),false);
                if(audioProcessor.isWavetableLoaded(1)) cycleSizeEditorB.setText(juce::String(audioProcessor.getCycleSamples(1)),false);
                updateABToggleLabel();
            } else presetNameLabel.setText("Load failed!",juce::dontSendNotification); });
}

//==============================================================================
void TranswaveAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto b=getLocalBounds().toFloat();
    juce::ColourGradient bg(juce::Colour(0xff120820),0.f,0.f,juce::Colour(0xff051030),b.getWidth(),b.getHeight(),false);
    g.setGradientFill(bg); g.fillAll();
    for(int y=0;y<(int)b.getHeight();y+=3){ g.setColour(juce::Colours::black.withAlpha(0.07f)); g.drawHorizontalLine(y,0.f,b.getWidth()); }
    juce::ColourGradient strip(juce::Colour(0xffff6ec7),0.f,0.f,juce::Colour(0xff6ec6ff),b.getWidth(),0.f,false);
    g.setGradientFill(strip); g.fillRect(0.f,0.f,b.getWidth(),3.f); g.fillRect(0.f,b.getHeight()-3.f,b.getWidth(),3.f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(22.f)));
    juce::ColourGradient tg(juce::Colour(0xffff6ec7),18.f,14.f,juce::Colour(0xff6ec6ff),200.f,14.f,false);
    g.setGradientFill(tg); g.drawText("PhizmOsc",18,8,280,28,juce::Justification::centredLeft);

    auto dp=[&](int px,int py,int pw,int ph){
        auto r=juce::Rectangle<int>(px,py,pw,ph);
        g.setColour(juce::Colour(0x22ffffff)); g.fillRoundedRectangle(r.toFloat(),6.f);
        g.setColour(juce::Colour(0x33ff6ec7)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f),6.f,0.8f); };

    // ROW 1 panels
    dp(10,  50,  76,  114);   // WT (1 knob)
    dp(94,  50,  360, 114);   // EVO (5 knobs)
    dp(462, 50,  292, 114);   // PITCH (3 knobs)
    // Curve panel drawn by EvoCurveEditor itself (762,50,228,360)

    // ROW 2 panels
    dp(10,  174, 280, 114);   // ADSR
    dp(300, 174, 144, 114);   // CHAR
    dp(454, 174, 144, 114);   // SCAN
    dp(608, 174, 144, 114);   // FILTER

    // ROW 3 panels
    dp(10,  298, 280, 114);   // STEREO
    dp(300, 298, 452, 114);   // FX

    // Row dividers
    g.setColour(juce::Colour(0x22ff6ec7));
    g.drawHorizontalLine(294,10.f,754.f);
    g.drawHorizontalLine(418,10.f,754.f);
}

//==============================================================================
void TranswaveAudioProcessorEditor::resized()
{
    const int kw=68, kh=80;
    const int ky1=68, ky2=192, ky3=316;

    // --- Title bar ---
    infoLabel.setBounds(590,13,160,18);
    presetNameLabel.setBounds(185,9,388,26);
    loadPresetButton.setBounds(754,10,58,24);
    savePresetButton.setBounds(818,10,58,24);

    // --- ROW 1 labels ---
    sectionWT.setBounds   (14, 52,  70,  14);
    sectionEvo.setBounds  (98, 52,  130, 14);
    sectionPitch.setBounds(466,52,  130, 14);

    // --- ROW 1 knobs ---
    knobGain.setBounds(14, ky1, kw, kh);   // WT panel (1 knob)

    // EVO panel (5 knobs starting at x=98)
    knobEvoTime.setBounds    (98,       ky1, kw, kh);
    knobEvoLFORate.setBounds (98+  kw,  ky1, kw, kh);
    knobEvoLFODepth.setBounds(98+2*kw,  ky1, kw, kh);
    knobPosLFORate.setBounds (98+3*kw,  ky1, kw, kh);
    knobPosLFODepth.setBounds(98+4*kw,  ky1, kw, kh);

    // PITCH panel (3 knobs starting at x=466)
    knobDetune.setBounds      (466,      ky1, kw, kh);
    knobPitchLFO.setBounds    (466+  kw, ky1, kw, kh);
    knobPitchLFORate.setBounds(466+2*kw, ky1, kw, kh);

    // --- EVOLUTION CURVE EDITOR (spans rows 1+2+3) ---
    // The curve editor occupies x=762, y=50, w=228, h=360
    // Its own buttons (FLAT/RAMP/STEP) are placed inside it in resized via child layout
    evoCurveEditor.setBounds(762, 50, 228, 360);
    // Place the 3 curve buttons inside the curve editor at top
    evoCurveEditor.btnReset.setBounds  (4,  3, 66, 22);
    evoCurveEditor.btnRamp.setBounds   (74, 3, 66, 22);
    evoCurveEditor.btnStepped.setBounds(144,3, 78, 22);

    // --- ROW 2 labels ---
    sectionADSR.setBounds  (14,  176, 130, 14);
    sectionGrit.setBounds  (304, 176, 130, 14);
    sectionScan.setBounds  (458, 176, 130, 14);
    sectionFilter.setBounds(612, 176, 130, 14);

    // --- ROW 2 knobs ---
    knobAttack.setBounds (14,        ky2, kw, kh);
    knobDecay.setBounds  (14+  kw,   ky2, kw, kh);
    knobSustain.setBounds(14+2*kw,   ky2, kw, kh);
    knobRelease.setBounds(14+3*kw,   ky2, kw, kh);

    knobBitCrush.setBounds(304,     ky2, kw, kh);
    knobGrit.setBounds    (304+kw,  ky2, kw, kh);

    knobScanStyle.setBounds(458,     ky2, kw, kh);
    knobScanJump.setBounds (458+kw,  ky2, kw, kh);
    // STEPPED button below the 2 scan knobs
    btnStepped.setBounds(459, ky2+kh-2, 130, 20);

    knobFilterFreq.setBounds(612,     ky2, kw, kh);
    knobFilterRes.setBounds (612+kw,  ky2, kw, kh);

    // --- ROW 3 labels ---
    sectionStereo.setBounds(14,  300, 130, 14);
    sectionFX.setBounds    (304, 300, 130, 14);

    // --- ROW 3 knobs ---
    knobSpread.setBounds      (14,       ky3, kw, kh);
    knobStereoWidth.setBounds (14+  kw,  ky3, kw, kh);
    knobUniDetune.setBounds   (14+2*kw,  ky3, kw, kh);
    knobStereoPhase.setBounds (14+3*kw,  ky3, kw, kh);

    knobChorusRate.setBounds  (304,      ky3, kw, kh);
    knobChorusDepth.setBounds (304+  kw, ky3, kw, kh);
    knobReverbSize.setBounds  (304+2*kw, ky3, kw, kh);
    knobReverbDamp.setBounds  (304+3*kw, ky3, kw, kh);
    knobReverbWet.setBounds   (304+4*kw, ky3, kw, kh);

    // --- BOTTOM (y=422..590) ---
    // Two displays side by side: each 485px wide, 8px gap, 10px margins
    wtDisplayA.setBounds(10,  422, 485, 110);
    wtDisplayB.setBounds(505, 422, 485, 110);

    const int ctrlY=542, ctrlH=26, lblYOff=ctrlY+4, lblH=18;

    // Left zone (Slot A)
    loadButtonA.setBounds     (10,  ctrlY, 72, ctrlH);
    cycleSizeLabelA.setBounds (88,  lblYOff, 38, lblH);
    cycleSizeEditorA.setBounds(128, ctrlY, 54, ctrlH);
    filenameLabelA.setBounds  (188, lblYOff, 220, lblH);

    // Centre toggle
    abToggleButton.setBounds(420, ctrlY, 160, ctrlH);

    // Right zone (Slot B)
    filenameLabelB.setBounds  (592, lblYOff, 220, lblH);
    cycleSizeLabelB.setBounds (820, lblYOff, 38, lblH);
    cycleSizeEditorB.setBounds(860, ctrlY, 54, ctrlH);
    loadButtonB.setBounds     (918, ctrlY, 72, ctrlH);
}
