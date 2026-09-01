# Spectral Smoothe

Spectral Smoothe is a real-time spectral freeze and smearing processor built around cepstral (homomorphic) magnitude smoothing, which keeps its held/stretched textures noticeably smoother and less "buzzy" or phasey than ordinary spectral smoothing. It offers several freeze behaviours (from a fully static hold to a slowly evolving drone) plus a granular Stretch mode.

## Manual

Spectral Smoothe processes the incoming signal continuously through an FFT, using a cepstral lifter to smooth the magnitude spectrum before resynthesis, and optionally freezes or stretches that spectrum on command.

### Core Processing Overview
The plugin uses a Short-Time Fourier Transform (STFT) process with a cepstral smoothing stage:
*   A windowed block of the live input is transformed into the frequency domain via FFT.
*   The log-magnitude spectrum is converted to its real cepstrum, low-quefrency-liftered (per the Character control), and converted back, replacing the source's fine spectral detail with a crisp but smooth, frozen envelope.
*   Depending on Freeze Mode, that smoothed spectrum is held static, left to slowly evolve, or continuously re-analyzed from the live input.
*   The signal is transformed back to the time domain via inverse FFT and overlap-added, then blended with the dry signal per the Mix control.

### Controls

| Control | Function |
| :--- | :--- |
| **Freeze** | Engages/releases the spectral freeze. |
| **Freeze Mode** | Static/Evolving/Continuous magnitude behaviour, each in a Hold variant (frozen level stays fixed) or Follow variant (frozen level keeps tracking the live dry signal), plus a granular **Stretch** mode. |
| **Character** | Cepstral lifter cutoff. Low = only the coarse spectral envelope survives (very coarse and "spectral-y" like an untuned vocoder); high = closer to the source's own harmonic detail (smoother, more resonant). |
| **Evolve Rate** | How quickly the spectrum drifts in Evolving freeze modes. |
| **Diffusion** | Amount of randomized spectral smearing applied while frozen. |
| **Stretch Time** | Grain period (in seconds) used by Stretch mode. |
| **FFT Size** | Sets FFT resolution; higher values give smoother/more stable results but increase latency. Retrigger Freeze to make it sound correct, if necessary. |
| **Mix** | Dry/wet balance between the live input and the frozen/smoothed signal. |
| **Gain Match** | Automatically compensates level so freezing/smoothing doesn't jump the perceived loudness. |
