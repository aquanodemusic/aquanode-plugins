#include "SynthEngine.h"
#include <algorithm>
#include <limits>

namespace aquanova {

// Two arpeggiator parameters the shared P:: block does not declare yet. Kept
// local so nothing outside this file has to change; move them into
// Parameters.h whenever that header is next touched.
namespace ArpP
{
    enum
    {
        Velocity             = 250,
        FillInNoteOrdering   = 255
    };
}

//==============================================================================
void SynthEngine::prepare (double sr, int maxBlock)
{
    sampleRate = sr;

    for (auto& v : voices)
        v.prepare (sr, &params);

    eq.prepare (sr);
    chorus.prepare (sr, maxBlock);
    comb.prepare (sr);
    delay.prepare (sr);
    reverb.prepare (sr);
    panner.prepare (sr);
    vocoder.prepare (sr);
    dcPreFx.prepare (sr);
    dcOutput.prepare (sr);

    scratch.setSize (2, juce::jmax (64, maxBlock));
    modBuffer.setSize (1, juce::jmax (64, maxBlock));
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
        v.reset();

    heldNotes.clear();
    latchedNotes.clear();
    monoStack.clear();
    arpPhase = 0.0;
    arpStep = 0;
    arpOctave = 0;
    arpDirection = 1;
    arpSoundingNote = -1;
    arpGateRemaining = 0.0;
    eq.reset();
    dcPreFx.reset();
    dcOutput.reset();
}

//==============================================================================
void SynthEngine::panic()
{
    for (auto& v : voices)
        v.reset();

    heldNotes.clear();
    latchedNotes.clear();
    monoStack.clear();
    latchActive = false;
    arpPhase = 0.0;
    arpStep = 0;
    arpOctave = 0;
    arpSoundingNote = -1;
    arpGateRemaining = 0.0;

    eq.reset();
    chorus.reset();
    comb.reset();
    delay.reset();
    reverb.reset();
    panner.reset();
    vocoder.reset();
    dcPreFx.reset();
    dcOutput.reset();
}

//==============================================================================
void SynthEngine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();

        if (! heldNotes.contains (n))
            heldNotes.add (n);

        if (params.raw (P::ArpEnabled) != 0)
        {
            if (params.raw (P::ArpLatch) != 0 && ! latchActive)
            {
                latchedNotes.clear();
                latchActive = true;
            }

            if (params.raw (P::ArpLatch) != 0 && ! latchedNotes.contains (n))
                latchedNotes.add (n);
        }
        else
        {
            noteOn (n, m.getFloatVelocity());
        }
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        heldNotes.removeAllInstancesOf (n);

        if (params.raw (P::ArpEnabled) == 0)
            noteOff (n);

        if (heldNotes.isEmpty())
            latchActive = false;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        allNotesOff();
    }
    else if (m.isPitchWheel())
    {
        const float norm = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
        const int range = params.raw (P::Osc1BendRange) - 12;   // display is -12..+12
        pitchBend = norm * (float) range;

        for (auto& v : voices)
            v.setPitchBend (pitchBend);
    }
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1)
        {
            modWheel = (float) m.getControllerValue() / 127.0f;
            for (auto& v : voices) v.setModWheel (modWheel);
        }
    }
    else if (m.isAftertouch() || m.isChannelPressure())
    {
        aftertouch = (float) (m.isChannelPressure() ? m.getChannelPressureValue()
                                                    : m.getAfterTouchValue()) / 127.0f;
        for (auto& v : voices) v.setAftertouch (aftertouch);
    }
}

//==============================================================================
void SynthEngine::noteOn (int note, float velocity)
{
    startVoices (note, velocity);
}

void SynthEngine::noteOff (int note)
{
    stopVoices (note);
}

void SynthEngine::allNotesOff()
{
    for (auto& v : voices)
        if (v.isActive())
            v.noteOff();

    heldNotes.clear();
    latchedNotes.clear();
    monoStack.clear();
    latchActive = false;
}

void SynthEngine::startVoices (int note, float velocity)
{
    if (params.raw (P::PartPolyphony) == 1) // Mono
    {
        startVoicesMono (note, velocity);
        return;
    }

    // Unison: 0 = off, 1 = on (2 voices), 2..8 = that many voices.
    const int uni = params.raw (P::UnisonVoices);
    const int count = uni == 0 ? 1 : (uni == 1 ? 2 : uni);
    const float detuneAmount = params.uni (P::UnisonDetune) * 40.0f;   // cents

    for (int i = 0; i < count; ++i)
    {
        Voice* target = nullptr;

        for (auto& v : voices)
            if (! v.isActive()) { target = &v; break; }

        if (target == nullptr)
        {
            // Steal the oldest released voice, or failing that the oldest voice.
            juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
            for (auto& v : voices)
                if (! v.isHeld() && v.getStartOrder() < oldest)
                { oldest = v.getStartOrder(); target = &v; }

            if (target == nullptr)
            {
                oldest = std::numeric_limits<juce::uint32>::max();
                for (auto& v : voices)
                    if (v.getStartOrder() < oldest) { oldest = v.getStartOrder(); target = &v; }
            }
        }

        if (target == nullptr)
            return;

        const float spread = count > 1 ? ((float) i / (float) (count - 1)) * 2.0f - 1.0f : 0.0f;
        const float cents = count > 1 ? spread * detuneAmount : 0.0f;

        // Stacking unison voices must not stack loudness with them.
        target->setGainScale (1.0f / std::sqrt ((float) count));
        target->setStartOrder (++voiceCounter);
        target->setModWheel (modWheel);
        target->setPitchBend (pitchBend);
        target->setAftertouch (aftertouch);
        target->noteOn (note, velocity, cents, spread * 0.7f);
    }
}

void SynthEngine::stopVoices (int note)
{
    if (params.raw (P::PartPolyphony) == 1) // Mono
    {
        stopVoicesMono (note);
        return;
    }

    for (auto& v : voices)
        if (v.isActive() && v.isHeld() && v.getNote() == note)
            v.noteOff();
}

//==============================================================================
// Mono mode: only one musical note sounds at a time (its unison stack still
// gets `count` voices, same as poly). New notes retrigger the same voice(s)
// in place rather than stealing a fresh one, and releasing the current note
// falls back to the next most recently held note - standard last-note-priority
// mono behaviour, e.g. for basslines/leads played legato-style.
void SynthEngine::startVoicesMono (int note, float velocity)
{
    for (int i = monoStack.size(); --i >= 0;)
        if (monoStack.getReference (i).note == note)
            monoStack.remove (i);

    monoStack.add ({ note, velocity });
    retriggerMono (note, velocity);
}

void SynthEngine::stopVoicesMono (int note)
{
    for (int i = monoStack.size(); --i >= 0;)
        if (monoStack.getReference (i).note == note)
            monoStack.remove (i);

    if (monoStack.isEmpty())
    {
        // No held notes left: release whatever mono voices are currently
        // sounding, regardless of which note they were last set to (the
        // note may have changed under them since they were allocated).
        for (auto& v : voices)
            if (v.isActive() && v.isHeld())
                v.noteOff();
    }
    else
    {
        const auto& last = monoStack.getLast();
        retriggerMono (last.note, last.velocity);
    }
}

void SynthEngine::retriggerMono (int note, float velocity)
{
    const int uni = params.raw (P::UnisonVoices);
    const int count = uni == 0 ? 1 : (uni == 1 ? 2 : uni);
    const float detuneAmount = params.uni (P::UnisonDetune) * 40.0f;   // cents

    // Gather the voices already sounding for the previous mono note so they
    // can be reused in place; anything beyond `count` gets released.
    juce::Array<Voice*> active;
    for (auto& v : voices)
        if (v.isActive() && v.isHeld())
            active.add (&v);

    for (int i = 0; i < count; ++i)
    {
        Voice* target = i < active.size() ? active.getUnchecked (i) : nullptr;

        if (target == nullptr)
        {
            for (auto& v : voices)
                if (! v.isActive()) { target = &v; break; }

            if (target == nullptr)
            {
                juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
                for (auto& v : voices)
                    if (v.getStartOrder() < oldest) { oldest = v.getStartOrder(); target = &v; }
            }
        }

        if (target == nullptr)
            continue;

        const float spread = count > 1 ? ((float) i / (float) (count - 1)) * 2.0f - 1.0f : 0.0f;
        const float cents = count > 1 ? spread * detuneAmount : 0.0f;

        target->setGainScale (1.0f / std::sqrt ((float) count));
        target->setStartOrder (++voiceCounter);
        target->setModWheel (modWheel);
        target->setPitchBend (pitchBend);
        target->setAftertouch (aftertouch);
        target->noteOn (note, velocity, cents, spread * 0.7f);
    }

    // Unison count shrank since the last note: release the leftovers.
    for (int i = count; i < active.size(); ++i)
        active.getUnchecked (i)->noteOff();
}

//==============================================================================
double SynthEngine::arpStepSeconds() const
{
    const int sync = params.raw (P::ArpSync);

    // The sync list is "Off" followed by 32nd triplet .. 8 bar dotted. Turn
    // the index (1-based, since 0 is "Off") into a beat multiplier, then into
    // seconds at the host tempo.
    static const double beats[34] =
    {
        1.0/6, 0.125, 1.0/3, 0.25, 2.0/3, 0.375, 0.5, 4.0/3, 0.75, 1.0,
        8.0/3, 1.5, 2.0, 16.0/3, 3.0, 4.0, 32.0/3, 6.0, 8.0, 64.0/3,
        12.0, 80.0/3, 16.0, 18.0, 112.0/3, 20.0, 128.0/3, 24.0, 28.0, 30.0,
        32.0, 36.0, 42.0, 48.0
    };

    const double beat = 60.0 / juce::jmax (20.0, hostBpm);

    if (params.raw (P::ArpKeysync) != 0 || sync <= 0)
    {
        // Internal clock: the Speed parameter reads 64..191 BPM on the hardware.
        const double bpm = 64.0 + (double) params.raw (P::ArpSpeed);
        return (60.0 / bpm) * 0.25;
    }

    return beats[juce::jlimit (0, 33, sync - 1)] * beat;
}

void SynthEngine::advanceArp (int numSamples, juce::MidiBuffer& out, int sampleOffset)
{
    juce::ignoreUnused (out, sampleOffset);

    if (params.raw (P::ArpEnabled) == 0)
        return;

    const auto& source = (params.raw (P::ArpLatch) != 0 && ! latchedNotes.isEmpty())
                            ? latchedNotes : heldNotes;

    if (source.isEmpty())
    {
        if (arpSoundingNote >= 0) { stopVoices (arpSoundingNote); arpSoundingNote = -1; }
        arpStep = 0;
        arpOctave = 0;
        return;
    }

    juce::Array<int> notes (source);
    std::sort (notes.begin(), notes.end());

    const int ordering = params.raw (ArpP::FillInNoteOrdering);
    const bool downwards = ordering == 2 || ordering == 3;
    const bool asPlayed  = ordering == 4 || ordering == 5;

    if (asPlayed)
        notes = source;
    else if (downwards)
        std::reverse (notes.begin(), notes.end());

    const int range = params.raw (P::ArpOctaveRange) + 1;
    const double stepTime = arpStepSeconds();
    const double dt = (double) numSamples / sampleRate;

    // Gate handling: notes are released part way through the step.
    if (arpGateRemaining > 0.0)
    {
        arpGateRemaining -= dt;
        if (arpGateRemaining <= 0.0 && arpSoundingNote >= 0)
        {
            stopVoices (arpSoundingNote);
            arpSoundingNote = -1;
        }
    }

    arpPhase += dt;

    while (arpPhase >= stepTime)
    {
        arpPhase -= stepTime;

        if (arpSoundingNote >= 0)
        {
            stopVoices (arpSoundingNote);
            arpSoundingNote = -1;
        }

        if (arpStep >= notes.size())
        {
            arpStep = 0;
            if (++arpOctave >= range)
                arpOctave = 0;
        }

        const int note = juce::jlimit (0, 127, notes[arpStep] + arpOctave * 12);
        ++arpStep;

        float vel = 0.8f;
        switch (params.raw (ArpP::Velocity))
        {
            case 1: vel = 1.0f;  break;    // Full
            case 2: vel = 0.5f;  break;    // Half
            default: break;
        }

        startVoices (note, vel);
        arpSoundingNote = note;

        const float gate = juce::jmax (0.05f, params.uni (P::ArpGateTime));
        arpGateRemaining = stepTime * (double) gate;
    }
}

//==============================================================================
void SynthEngine::renderVoices (juce::AudioBuffer<float>& buffer, int start, int num)
{
    auto* l = buffer.getWritePointer (0, start);
    auto* r = buffer.getWritePointer (1, start);

    const auto* inL = buffer.getReadPointer (0, start);
    const auto* inR = buffer.getReadPointer (1, start);

    // Snapshot the input for the audio-in oscillator types before we overwrite it.
    scratch.setSize (2, num, false, false, true);
    juce::FloatVectorOperations::copy (scratch.getWritePointer (0), inL, num);
    juce::FloatVectorOperations::copy (scratch.getWritePointer (1), inR, num);

    juce::FloatVectorOperations::clear (l, num);
    juce::FloatVectorOperations::clear (r, num);

    const auto* sL = scratch.getReadPointer (0);
    const auto* sR = scratch.getReadPointer (1);

    modBuffer.setSize (1, juce::jmax (modBuffer.getNumSamples(), start + num), true, false, true);
    auto* mod = modBuffer.getWritePointer (0, start);
    juce::FloatVectorOperations::clear (mod, num);

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        for (int i = 0; i < num; ++i)
        {
            float vl = 0.0f, vr = 0.0f, vm = 0.0f;
            v.renderNextSample (vl, vr, sL[i], sR[i], vm);
            l[i] += vl;
            r[i] += vr;
            mod[i] += vm;
        }
    }

    // Audio In as the modulator does not come from the voices at all.
    if (params.raw (P::VocoderModSource) == 0)
    {
        const bool useRight = params.raw (P::VocoderAudioInput) != 0;
        juce::FloatVectorOperations::copy (mod, useRight ? sR : sL, num);
    }
}

//==============================================================================
void SynthEngine::runEffects (juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (params.flag (P::EffectsBypass))
        return;

    auto* l = buffer.getWritePointer (0, start);
    auto* r = buffer.getWritePointer (1, start);

    // --- vocoder first: it reshapes the synth before anything colours it
    const float vocBalance = params.uni (P::VocoderBalance);

    if (vocBalance > 0.001f && modBuffer.getNumSamples() >= start + num)
    {
        const auto* mod = modBuffer.getReadPointer (0, start);

        const float vocSpeed = 1.0f - params.uni (P::VocoderSpeed);
        const float vocWidth = (float) params.raw (P::VocoderWidth) / 15.0f;
        const float vocSib   = (float) params.raw (P::VocSibilanceLevel) / 15.0f;
        const bool  sibNoise = params.raw (P::VocSibilanceType) != 0;

        for (int i = 0; i < num; ++i)
            vocoder.process (l[i], r[i], mod[i], vocBalance, vocSpeed,
                             vocWidth, vocSib, sibNoise);
    }

    const float dry        = params.uni (P::EffectsDryLevel);
    const float morph      = params.uni (P::EffectsMorph);
    const int   order      = params.raw (P::FxOrder);

    const float distLevel  = params.uni (P::DistortionLevel);
    const float distOut    = params.uni (P::DistortionOutput);
    const float distComp   = params.uni (P::DistortionGainComp);
    const float distCurve  = params.bip (P::DistortionCurve);

    const float eqBass     = params.bip (P::EQBass);
    const float eqTreble   = params.bip (P::EQTreble);

    const float chSend     = params.uni (P::ChorusSendLevel);
    const float chSpeed    = params.uni (P::ChorusSpeed);
    const float chDepth    = params.uni (P::ChorusModDepth);
    const float chFb       = params.uni (P::ChorusFeedback);
    const float chDelay    = params.uni (P::ChorusDelay);
    const float chWidth    = params.uni (P::ChorusStereoWidth);
    const int   chType     = params.raw (P::ChorusType);

    const float combFreq   = params.uni (P::CombFrequency);
    const float combBoost  = params.uni (P::CombBoost);
    const float combSpeed  = params.uni (P::CombSpeed);
    const float combDepth  = params.uni (P::CombDepth);
    const float combSpread = params.uni (P::CombSpread);

    const float dlSend     = params.uni (P::DelaySendLevel);
    const float dlFb       = params.uni (P::DelayFeedback);
    const float dlDamp     = params.uni (P::DelayHFDamp);
    const float dlWidth    = params.uni (P::DelayWidth);
    const int   dlRatio    = params.raw (P::DelayRatio);
    const int   dlSync     = params.raw (P::DelaySync);

    double dlSeconds = 0.002 + (double) params.raw (P::DelayTime) / 127.0 * 2.4;
    if (dlSync > 0)
    {
        static const double beats[34] =
        {
            1.0/6, 0.125, 1.0/3, 0.25, 2.0/3, 0.375, 0.5, 4.0/3, 0.75, 1.0,
            8.0/3, 1.5, 2.0, 16.0/3, 3.0, 4.0, 32.0/3, 6.0, 8.0, 64.0/3,
            12.0, 80.0/3, 16.0, 18.0, 112.0/3, 20.0, 128.0/3, 24.0, 28.0, 30.0,
            32.0, 36.0, 42.0, 48.0
        };
        dlSeconds = beats[juce::jlimit (0, 33, dlSync - 1)] * (60.0 / juce::jmax (20.0, hostBpm));
    }

    const float rvSend     = params.uni (P::ReverbSendLevel);
    const float rvDecay    = params.uni (P::ReverbDecay);
    const float rvDamp     = params.uni (P::ReverbHFDamp);
    const float rvEarly    = (float) params.raw (P::ReverbEarlyRef) / 7.0f;
    const int   rvType     = params.raw (P::ReverbType);

    const float panPos     = params.bip (P::Pan);
    const int   panType    = params.raw (P::PanType);
    const float panSpeed   = params.uni (P::PanningSpeed);
    const float panDepth   = params.uni (P::PanningDepth);

    // The FxOrder list describes how Delay, Reverb and Chorus are wired to each
    // other: parallel sums, serial chains, and hybrids. The Configuration Morph
    // crossfades between the parallel reading and the chosen one, which is how
    // the hardware's Config knob behaves.
    const bool chorusIntoDelay  = order == 5 || order == 6  || order == 11 || order == 12 || order == 15;
    const bool delayIntoReverb  = order == 1 || order == 3  || order == 7  || order == 13;
    const bool reverbIntoDelay  = order == 4 || order == 10 || order == 14;

    for (int i = 0; i < num; ++i)
    {
        StereoSample s { l[i], r[i] };

        s = eq.process (s, eqBass, eqTreble);
        s = distortion.process (s, distLevel, distOut, distComp, distCurve);
        s = comb.process (s, combFreq, combBoost, combSpeed, combDepth, combSpread);

        StereoSample chorusOut = chorus.process (s, chSend, chSpeed, chDepth,
                                                 chFb, chDelay, chWidth, chType);

        StereoSample delayIn = s;
        if (chorusIntoDelay)
        {
            delayIn.l += chorusOut.l * morph;
            delayIn.r += chorusOut.r * morph;
        }

        StereoSample delayOut = delay.process (delayIn, dlSend, (float) dlSeconds,
                                               dlFb, dlDamp, dlWidth, dlRatio);

        StereoSample reverbIn = s;
        if (delayIntoReverb)
        {
            reverbIn.l += delayOut.l * morph;
            reverbIn.r += delayOut.r * morph;
        }

        StereoSample reverbOut = reverb.process (reverbIn, rvSend, rvDecay,
                                                 rvDamp, rvEarly, rvType);

        if (reverbIntoDelay)
        {
            delayOut.l += reverbOut.l * morph * 0.5f;
            delayOut.r += reverbOut.r * morph * 0.5f;
        }

        StereoSample mix
        {
            s.l * dry + chorusOut.l + delayOut.l + reverbOut.l,
            s.r * dry + chorusOut.r + delayOut.r + reverbOut.r
        };

        // Pan Effects decides whether the panner sits before or after the sends;
        // when it is off, only the dry path is panned.
        mix = panner.process (mix, panPos, panType, panSpeed, panDepth);

        l[i] = mix.l;
        r[i] = mix.r;
    }
}

//==============================================================================
void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2)
        return;

    // The LFOs' Sync controls need the host tempo to turn a note division
    // into Hz; keep every voice's copy current every block.
    for (auto& v : voices)
        v.setHostBpm (hostBpm);

    int lastEvent = 0;

    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);

        if (pos > lastEvent)
        {
            const int num = pos - lastEvent;
            advanceArp (num, midi, lastEvent);
            renderVoices (buffer, lastEvent, num);
            lastEvent = pos;
        }

        handleMidi (meta.getMessage());
    }

    if (lastEvent < numSamples)
    {
        const int num = numSamples - lastEvent;
        advanceArp (num, midi, lastEvent);
        renderVoices (buffer, lastEvent, num);
    }

    const bool blockDc = params.dcBlockEnabled();

    if (blockDc)
    {
        const float hz = params.dcBlockHz();
        dcPreFx.setCornerHz (hz);
        dcOutput.setCornerHz (hz);
        dcPreFx.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    }

    runEffects (buffer, 0, numSamples);

    const float vol = params.uni (P::MasterVolume) * params.uni (P::ProgramVolume);
    buffer.applyGain (vol);

    if (blockDc)
        dcOutput.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    else
    {
        // Nothing is running through them, so they must not come back with a
        // stale sample when the switch goes on again.
        dcPreFx.reset();
        dcOutput.reset();
    }

    limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
}

} // namespace aquanova
