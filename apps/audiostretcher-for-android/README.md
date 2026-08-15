# AudioStretcher for Android

An improved Android port of the AudioStretcher VST.

AudioStretcher uses the Rubber Band Library to time-stretch audio files. The Rubber Band Library is distributed under the GNU General Public License (GPL).

## Features

- Time-stretch audio from 0.2x to 4.0x
- Fine-tune pitch by +/- 36 cents
- Set custom start and end points
- Load MP3, FLAC and WAV files
- Export processed audio as FLAC or WAV (MP3 are exported as FLAC, due to JUCE limitations)

## Time Stretching

The time-stretch factor can be chosen between 0.2x to 4.0x:

- 0.2x - 5 times slower
- 1.0x - original speed
- 4.0x - 4 times faster

Stretching is performed using the Rubber Band Library.

Audio stretching can be computationally intensive and may take some time to complete on Android, especially for longer audio files.

A status information text is shown at the bottom of the GUI.
