#pragma once
#include <JuceHeader.h>
#include "Parameters.h"
#include "DSP/Oscillator.h"
#include "DSP/Filter.h"
#include "DSP/Envelope.h"
#include "DSP/Lfo.h"

namespace aquanova {

//==============================================================================
/** One Supernova II voice: three oscillators, noise, two ring modulators, the
    5 x 5 oscillator modulation matrix, the filter with its own matrix, three
    envelopes and two LFOs.
*/
class Voice
{
public:
    void prepare (double sampleRate, const ParamAccess* paramsIn);
    void reset();

    void noteOn (int midiNote, float velocity, float detuneCents, float panSpread);
    void noteOff();
    void setPitchBend (float semitones)   { pitchBend = semitones; }
    /** Unison gain compensation, set by the engine when the voice starts. */
    void setGainScale (float g) noexcept  { gainScale = g; }
    void setModWheel (float v)            { modWheel = v; }
    void setAftertouch (float v)          { aftertouch = v; }
    void setHostBpm (double bpm) noexcept { hostBpm = bpm; }

    bool isActive() const noexcept        { return env1.isActive(); }
    int  getNote() const noexcept         { return note; }
    bool isHeld() const noexcept          { return held; }
    juce::uint32 getStartOrder() const noexcept { return startOrder; }
    void setStartOrder (juce::uint32 o) noexcept { startOrder = o; }

    /** Renders one sample into l/r. Audio-in oscillator types read from inL/inR.

        modTap receives the oscillators selected as the vocoder's modulator,
        taken pre-filter and pre-amp so the vocoder hears the raw waveform. */
    void renderNextSample (float& l, float& r, float inL, float inR, float& modTap);

private:
    struct OscState
    {
        Oscillator osc;
        float currentHz = 440.0f;
    };

    float glideTowardsTarget();

    const ParamAccess* params = nullptr;
    double sampleRate = 44100.0;
    double hostBpm = 120.0;

    OscState oscs[3];
    NoiseSource noise;
    Filter filter;
    Envelope env1, env2, env3;
    Lfo lfo1, lfo2;

    int note = 60;
    float velocity = 1.0f;
    float detune = 0.0f;         // cents, for unison
    float pan = 0.0f;
    float pitchBend = 0.0f;
    float gainScale = 1.0f;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    bool held = false;
    juce::uint32 startOrder = 0;

    float glideCurrent = 60.0f, glideTarget = 60.0f;
    float ring1State = 0.0f, ring2State = 0.0f;
    float drift = 0.0f, driftPhase = 0.0f;
    juce::Random rng;
};

} // namespace aquanova
