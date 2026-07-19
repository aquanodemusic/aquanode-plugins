# SpectralResolve

**Latest version:** 1.1 — download builds from the [Releases](../../../../releases) page.

## Manual

SpectralResolver is a real-time spectral analysis audio plugin that renders a live scrolling spectrogram with extreme resolution on the actual frequencies present in the signal.

Reassignment method: Uses three parallel window transforms to compute frequency and time reassignment operators, giving sharper spectral localization than a standard FFT.

Decimation method: Up to 16× downsampling via a cascade of 4-stage biquad lowpass filters, allowing high-frequency-resolution analysis of low-frequency content (high frequency cutoff to gain more resolution in the low frequency range).

Configurable FFT engine: Orders 10–14 (1024–16384 points), variable temporal resolution / hop size (down to FFT/256), and selectable frequency window that lets you analyze frequencies in any interval you wish, like 20 to 200 hertz or 10000 to 11000 hertz for example, and you can customize the colors in the GUI.

Sidechain input: A parallel processing path runs the same FFT pipeline on a sidechain signal and renders it as an overlay on the spectrogram. The sidechain spectrogram uses inverted colors to the main signal, meaning if the main signal has a gradient from blue for low-volume frequencies and green for loud frequencies, the sidechain will have green to blue.

Hide Controls: When you hide the control section using the button on the top right, you can freely resize the plugin.

Temporal interpolation: Cosine blending between consecutive spectral frames in pixel-row space for smooth visual scrolling without repeated horizontal bars.

## How Reassignment Works

A commented, runnable Python example of the spectral reassignment method used by this plugin is included at [`docs/how_reassignment_works.py`](docs/how_reassignment_works.py).
