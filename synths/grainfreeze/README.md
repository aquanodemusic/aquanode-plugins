# Grainfreeze

![Grainfreeze banner](assets/Banner.webp)

**Latest version:** 1.1 — download builds from the [Releases](../../../../releases) page.

Grainfreeze is a real-time phase vocoder–based time-stretching and freeze processor. It loads an audio file into memory and resynthesizes it using FFT analysis and overlap-add techniques. The plugin features a spectral viewer that highlights the 10 loudest frequencies present in the current grain with their note name. Please note that the plugin is CPU-intensive and designed for tonality preservation rather than transients, which will be smeared.

The source code is open source and written in JUCE/C++.

---

## Manual

Grainfreeze operates in two primary modes: normal playback and freeze mode.

### Core Processing Overview
The plugin uses a Short-Time Fourier Transform (STFT) process:
*   A windowed block of audio is read from the loaded file.
*   The block is transformed into the frequency domain using an FFT.
*   Magnitude and phase are extracted for each frequency bin.
*   Phase differences are tracked to calculate true frequency.
*   Phases are advanced based on the time-stretch factor.
*   The signal is transformed back to the time domain using an inverse FFT and overlap-added into an output buffer.

### Controls

| Control | Function |
| :--- | :--- |
| **Load Audio** | Loads an audio file into the plugin. |
| **Play / Stop** | Starts or stops audio playback. |
| **Freeze** | Enables freeze mode, holding a spectral snapshot. |
| **Time Stretch** | Controls playback speed ( < 1.0 is faster, > 1.0 is slower). |
| **FFT Size** | Sets FFT resolution; higher values provide smoother sound but increase latency. |
| **Hop Div** | Controls overlap between FFT frames; lower values are smoother but use more CPU. |
| **Freeze Glide** | Smooths transitions when moving the playhead in freeze mode. |
| **HF Boost** | Compensates for energy loss in high frequencies during phase vocoder processing. |
| **Micro Move** | Adds subtle random motion to grains to prevent static phasing artifacts. |
| **Window** | Selects FFT window type (Hann for softer sound, Blackman-Harris for cleaner highs). |
| **X-Fade Len** | Sets the length of crossfades between grains to help avoid clicks. |
| **Waveform Display** | Visualizes the loaded audio; click or drag to move the playhead. |
