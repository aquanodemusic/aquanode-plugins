# CombWeave

**Latest version:** 1.0 — download builds from the [Releases](../../../../releases) page.

## Manual

CombWeave is an experimental synth that produces additive timbres, where each sine wave is spaced equidistantly, exponentially or harmonically from the next.

Controls:
You have 4 rows of controls, two per oscillator.

Attack, Release, High Pass Cutoff, Volume and Fine Tune are the standard usual controls.
The unique ones are:

Harmonics: The amount of added sine waves.
Spread: The distance in Hertz (Linear) or the multiplicator (Exponential) by which the next sine wave is moved from the previous one.
Rolloff: Dampens down the velocity of higher waves directly, additionally to the high pass filter -  a unique feature of additive synths.

Direction Button: If it is in bidirectional mode, each sine wave is mirrored along the fundamental note you play, creating bass notes.

Note Lock: Will lock each sine wave to the nearest note frequency.

Spread mode: Linear spreads in Hz, sounding noisy, Exponential spreads as a multiplier, sounding metallic, harmonic spreads by musical overtones.

Freq mode: Wrap will wrap frequencies that exceed 22000 Hertz, moving them in from the left again. Mirror will mirror them at the edges. Regular will simply delete them from the buffer.

Harmonic filter: Lets either all sine waves sound or only all the even or odd ones (similar to a square wave).
