# SampleField

**Latest version:** 1.0 — download builds from the [Releases](../../../../releases) page.

## Manual

SampleField is a 24-voice polyphonic, MIDI-triggered sampler plugin designed to continuously loop and chain random audio files from a user-loaded pool of up to 128 samples. Unlike standard samplers that play a single file per MIDI input, SampleField creates an uninterrupted generative chain for the entire duration of a held MIDI note.

### Core Playback Mechanism
*   When a MIDI Note On event is received, a voice is activated and a random sample is selected.
*   As soon as that sample reaches its end, hits a user-defined time limit, or triggers a skip event, the engine immediately triggers the next random sample or interval.
*   This cycle repeats indefinitely until a MIDI Note Off event winds down the voice.

### Controls

| Control | Function | Explanation |
| :--- | :--- | :--- |
| **Volume, Pan, & Rate** | Global Controls | Sets the baseline playback level, stereo placement, and playback speed/pitch transpose for the sample pool. |
| **Time** | Global Controls | Restricts playback duration (0.1 to 10s); triggers a 10ms fade-out before transitioning to the next sample. |
| **Tempo Lock** | Global Controls | Forces the "Time" duration to snap to host DAW BPM subdivisions (eighth-note increments). |
| **Pan Rnd, Rate Rnd, & Vol Rnd** | Randomisation | Introduces random, bipolar deviations to stereo position, pitch, and volume for each new sample trigger. |
| **Skip Prob** | Randomisation | Dictates the probability that a chain link will evaluate to silence instead of an audio sample. |
| **Delay Time & Delay Vol** | Delay Controls | Manages the timing and feedback level of an independent stereo delay line assigned to each voice. |
| **Delay Prob** | Delay Controls | Determines the probability that any single sample instance within the chain will feed into the delay loop. |