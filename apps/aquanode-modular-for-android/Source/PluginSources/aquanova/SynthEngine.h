#pragma once
#include <JuceHeader.h>
#include "Voice.h"
#include "DSP/Effects.h"
#include "DSP/Vocoder.h"

namespace aquanova {

//==============================================================================
class SynthEngine
{
public:
    // 16 voices is plenty for a hand-played part and halves the worst-case
    // load compared with 32. Unison eats from the same pool.
    static constexpr int kMaxVoices = 16;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setTempo (double bpm) noexcept { hostBpm = bpm; }

    void handleMidi (const juce::MidiMessage& m);

    /** Stops everything dead and empties every effect tail. Used when the host
        transport stops, so the sound does not ring on after the DAW does. */
    void panic();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    ParamAccess params;

private:
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();

    void startVoices (int note, float velocity);
    void stopVoices (int note);

    // --- mono mode (PartPolyphony == Mono)
    struct MonoNote { int note; float velocity; };
    void startVoicesMono (int note, float velocity);
    void stopVoicesMono (int note);
    void retriggerMono (int note, float velocity);

    void renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void runEffects (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    void advanceArp (int numSamples, juce::MidiBuffer& out, int sampleOffset);
    double arpStepSeconds() const;

    //==============================================================================
    double sampleRate = 44100.0;
    double hostBpm = 120.0;

    std::array<Voice, kMaxVoices> voices;
    juce::uint32 voiceCounter = 0;

    float modWheel = 0.0f, aftertouch = 0.0f, pitchBend = 0.0f;

    // --- arpeggiator
    juce::Array<int> heldNotes, latchedNotes;
    juce::Array<MonoNote> monoStack;
    bool latchActive = false;
    double arpPhase = 0.0;
    int arpStep = 0, arpOctave = 0, arpDirection = 1;
    int arpSoundingNote = -1;
    double arpGateRemaining = 0.0;

    // --- effects
    EqSection eq;
    Distortion distortion;
    ChorusSection chorus;
    CombSection comb;
    DelaySection delay;
    ReverbSection reverb;
    PanSection panner;
    Vocoder vocoder;

    // One before the effects so nothing asymmetric gets fed into the feedback
    // lines, one on the output for whatever the effects add themselves.
    DcBlocker dcPreFx, dcOutput;

    // The last thing the signal touches: catches whatever the voice sum and
    // a hot resonant filter push past 0 dBFS, so it rounds off instead of
    // hard-clipping. Stateless, so it needs no prepare/reset.
    OutputLimiter limiter;

    juce::AudioBuffer<float> scratch;     // a copy of the input, for audio-in oscillators
    juce::AudioBuffer<float> modBuffer;   // the vocoder's modulator signal
};

} // namespace aquanova
