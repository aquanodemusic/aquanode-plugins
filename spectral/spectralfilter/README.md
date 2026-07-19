# SpectralFilter

**Latest version:** 2.1 — download builds from the [Releases](../../../../releases) page.

---

## 📖 Overview

SpectralFilter is a spectral audio processor that allows you to shape sound by directly manipulating its frequency content in real-time (minus the latency introduced by the FFT algorithm). It visualizes the incoming audio spectrum and lets you draw custom filter curves by clicking and dragging across the frequency display. 

You can boost or cut any frequency range from **20 Hz to 20 kHz**, quantized into the FFT bin widths, with up to **±24 dB** of gain. 

> ⚠️ **Impulse Response Note:** Exported WAV files will sound a little different and slightly more dull than the live plugin due to the technical method used to generate the impulse responses.

---

## ⏳ Version History

<Timeline>
  <TimelineEvent title="Version 1.1" time="v1.1">
    Added automatizable filter and bin position shifting, along with a **Wet Only** mode.
  </TimelineEvent>
  <TimelineEvent title="Version 1.2" time="v1.2">
    Introduced individual **frequency, panning, and phase shifting per bin**, making the plugin highly versatile for advanced sound design.
  </TimelineEvent>
  <TimelineEvent title="Version 2.0" time="v2.0">
    Added four dedicated **Strength Knobs** to scale the intensity of the respective filtering curves.
  </TimelineEvent>
  <TimelineEvent title="Version 2.1 (Latest)" time="v2.1">
    Added a **Frequency Window** to restrict where effects take place, and turned the automatic speed text boxes into easier-to-use **Knobs**.
  </TimelineEvent>
</Timeline>

---

## 🎛️ Controls & Parameters

| Control Name | Type | Function |
| :--- | :--- | :--- |
| **Main Display Area** | Interactive | Click and drag to manually draw your curves. Double-clicking clears the currently active curve. |
| **FFT Size** | Dropdown | Sets the Fast Fourier Transform processing resolution (**1024, 2048, 4096, or 8192**). Higher values give better frequency resolution but higher latency. |
| **Edit** | Dropdown | Selects which curve you are actively drawing on the main display: **Filter (EQ), Phase, Freq (Frequency Shift), or Pan**. |
| **Wet Only** | Toggle Button | When active, subtracts the dry signal so you only hear the isolated effect of your frequency, phase, and panning shifts. |
| **Export IR** | Button | Captures the current filter, phase, and shift states and exports them as a stereo WAV Impulse Response file *(Requires all "Auto" modes to be off)*. |
| **Rnd / Rst Filter** | Buttons | Randomizes the EQ gain curve or Resets the EQ curve to flat (0 dB). |
| **Rnd / Rst Phase** | Buttons | Randomizes the phase shift curve or Resets the phase curve to 0 radians. |
| **Rnd / Rst Freq** | Buttons | Randomizes the per-bin frequency offset or Resets the frequency shifts to 0. |
| **Rnd / Rst Pan** | Buttons | Randomizes the stereo panning curve or Resets the panning to the center (0). |
| **Shift** | Sliders | Manually offsets the respective curve (Filter, Phase, Freq, Pan, or Global) across the frequency bins. Represented as a percentage. |
| **Auto** | Toggle Button | Activates automatic, continuous scrolling of the slider/curve. Disables manual slider control while active. |
| **Speed Input** | Knobs | Sets the speed of the "Auto" scrolling effect, measured in bins per second. *(Upgraded from text boxes in v2.1)*. |
| **Wrap (Global Only)** | Toggle Button | Determines if audio frequencies shifted beyond the top (Nyquist) or bottom (DC) of the spectrum wrap around to the other side or get discarded. |
| **Color Inputs** | Text Fields | Allows you to input custom hex codes to change the colors of the background, grid, and specific curve lines. |
| **Rst Colors** | Button | Reverts all UI elements back to their default, hardcoded colors. |