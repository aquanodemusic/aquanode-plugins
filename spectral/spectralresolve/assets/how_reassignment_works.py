"""
Spectral Reassignment Theory
-----------------------------
This is a python code. Rename the .txt to .py to run it!
I provide it as .txt file as some services flag .py files apparently.

The Short-Time Fourier Transform (STFT) places each bin's energy at the
*nominal* centre frequency  f_k = k * fs / N.  For a real sinusoid that
falls between two bins, energy smears across many neighbouring bins — the
well-known "bin-leakage" effect.

Spectral reassignment corrects this by computing *where* each bin's energy
actually comes from, using three parallel FFTs of the same windowed frame:

    X_h (k)   FFT of  h[n] * x[n]               (analysis window)
    X_dh(k)   FFT of  h'[n] * x[n]              (time-derivative of window)
    X_th(k)   FFT of  (n - N/2) * h[n] * x[n]   (time-ramp window)

From these we derive two reassignment operators per bin k:

  Frequency reassignment — instantaneous frequency estimate:

      f_hat(k)  =  f_k  -  (fs / 2*pi) * Im{ X_dh(k) / X_h(k) }

      Im{X_dh / X_h} is the time-derivative of the STFT phase, which equals
      the deviation of the local instantaneous frequency from the bin centre.

  Time reassignment — energy centroid within the frame:

      t_hat(k)  =  t_centre  +  Re{ X_th(k) / X_h(k) } / fs

      Re{X_th / X_h} gives the first moment of the windowed energy in time
      (in samples from the frame centre); dividing by fs converts to seconds.

Together (f_hat, t_hat) relocate each bin from the nominal grid point
(t_centre, k*fs/N) to the true energy centroid, yielding a sharper
time-frequency picture.
"""

import math
import cmath
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from   matplotlib.animation import FuncAnimation


# =============================================================================
# CONFIGURATION
# =============================================================================

FS               = 44100    # sample rate (Hz)
DURATION         = 4.0      # signal length (seconds)
N                = 1024     # analysis window / FFT size (must be power of two)
HOP              = 256      # hop size between frames  (N/4 = 75 % overlap)
F_START          = 80.0     # sweep start frequency (Hz)
F_END            = 8000.0   # sweep end  frequency (Hz)
MAG_FLOOR_DB     = -18.0    # bins below this are treated as noise and skipped
ANIM_SPEED_HOPS  = 2        # hop steps advanced per animation tick
SCROLL_WIDTH_PX  = 400      # number of time-columns in the scrolling spectrogram


# =============================================================================
# SECTION 1 — SIGNAL GENERATION  (pure Python, no library functions)
# =============================================================================

def generate_sweep(fs, duration, f_start, f_end):
    """
    Build a unit-amplitude linear sine sweep in pure Python.

    A linear chirp has instantaneous frequency

        f(t) = f_start + (f_end - f_start) * t / T

    Integrating gives the instantaneous phase

        phi(t) = 2*pi * [ f_start*t  +  (f_end - f_start) * t^2 / (2*T) ]

    We evaluate this analytically at each sample rather than accumulating a
    running phase sum, so there is no floating-point drift across the signal.

    Returns
    -------
    samples : list[float]   length = int(fs * duration)
    t_axis  : list[float]   time in seconds for each sample
    """
    n_samples  = int(fs * duration)
    T          = duration
    inv_fs     = 1.0 / fs
    sweep_rate = (f_end - f_start) / T      # Hz/s — constant for a linear chirp

    samples = []
    t_axis  = []
    for i in range(n_samples):
        t   = i * inv_fs
        phi = 2.0 * math.pi * (f_start * t + 0.5 * sweep_rate * t * t)
        samples.append(math.sin(phi))
        t_axis.append(t)

    return samples, t_axis


# =============================================================================
# SECTION 2 — PURE-PYTHON FFT  (Cooley-Tukey radix-2, recursive)
# =============================================================================

def fft(x):
    """
    Compute the Discrete Fourier Transform of sequence x using the
    Cooley-Tukey radix-2 divide-and-conquer algorithm.

    Complexity : O(N log N)   where N = len(x), N must be a power of two.

    Algorithm
    ---------
    Split x into even-indexed and odd-indexed sub-sequences, recurse on each,
    then combine the two N/2-point DFTs using "butterfly" operations:

        X[k]       = E[k] + W^k * O[k]
        X[k + N/2] = E[k] - W^k * O[k]

    where  W = e^{-j*2*pi/N}  is the principal N-th root of unity (twiddle
    factor), E[k] is the k-th output of the even sub-DFT, and O[k] likewise
    for the odd sub-DFT.

    Parameters
    ----------
    x : list[complex]   input sequence, length must be a power of two

    Returns
    -------
    list[complex]   N complex spectral coefficients X[0] ... X[N-1]
    """
    n = len(x)

    # Base case: the DFT of a single sample is the sample itself.
    if n <= 1:
        return list(x)

    # --- Divide --------------------------------------------------------------
    even = fft(x[0::2])   # recurse on even-indexed samples  x[0], x[2], ...
    odd  = fft(x[1::2])   # recurse on odd-indexed  samples  x[1], x[3], ...

    # --- Conquer: twiddle factors and butterfly merge -----------------------
    half   = n // 2
    result = [0j] * n
    for k in range(half):
        # Twiddle factor W_N^k = e^{-j * 2*pi * k / N}
        twiddle        = cmath.exp(-2j * math.pi * k / n) * odd[k]
        result[k]      = even[k] + twiddle   # upper butterfly output
        result[k+half] = even[k] - twiddle   # lower butterfly output

    return result


def windowed_fft(frame, window):
    """
    Apply window coefficients element-wise, then compute the FFT.
    Returns only the positive-frequency half X[0 ... N/2-1].

    For a real-valued input signal, X[N-k] = conj(X[k]), so the upper half
    of the spectrum is the conjugate mirror and carries no new information.

    Parameters
    ----------
    frame  : list[float]   N real audio samples
    window : list[float]   N window coefficients

    Returns
    -------
    list[complex]   length N // 2
    """
    n        = len(frame)
    windowed = [frame[i] * window[i] for i in range(n)]
    spectrum = fft(windowed)
    return spectrum[: n // 2]


# =============================================================================
# SECTION 3 — WINDOW FUNCTIONS  (Blackman-Harris 4-term + derivatives)
# =============================================================================

def build_windows(n):
    """
    Construct the three windows required for spectral reassignment.

    Blackman-Harris 4-term window (Harris, 1978)
    --------------------------------------------
    Chosen for ~92 dB side-lobe attenuation — far superior to Hann (~32 dB).
    This makes it possible to detect weak partials adjacent to strong ones.

    Coefficients  a0=0.35875, a1=0.48829, a2=0.14128, a3=0.01168

        h[n] = a0  -  a1*cos(w)  +  a2*cos(2w)  -  a3*cos(3w)
               where  w = 2*pi*n / N

    Derivative window  h'[n]
    ------------------------
    Differentiating h[n] analytically with respect to the continuous sample
    index n (treating n as a real variable) gives:

        h'[n] = (2*pi/N) * [ a1*sin(w)  -  2*a2*sin(2w)  +  3*a3*sin(3w) ]

    This is exact — no finite-difference approximation is needed.
    Im{ FFT(x * h') / FFT(x * h) } yields the instantaneous angular frequency
    deviation from the bin centre, in radians per sample.

    Time-ramp window  th[n]
    -----------------------
    Multiplying h[n] by the offset  (n - (N-1)/2)  centres the window so
    that index 0 corresponds to the physical midpoint of the frame:

        th[n] = (n - (N-1)/2) * h[n]

    Re{ FFT(x * th) / FFT(x * h) } gives the first temporal moment of the
    windowed energy (in samples from the frame centre); dividing by fs → seconds.

    Parameters
    ----------
    n : int   window length  (must equal the FFT size)

    Returns
    -------
    h, dh, th : list[float], list[float], list[float]   each of length n
    """
    a0, a1, a2, a3 = 0.35875, 0.48829, 0.14128, 0.01168
    scale_d = 2.0 * math.pi / n     # common prefactor for h'[n]
    centre  = (n - 1) / 2.0         # fractional midpoint index

    h, dh, th = [], [], []
    for i in range(n):
        w  = 2.0 * math.pi * i / n

        hi  = a0 - a1*math.cos(w) + a2*math.cos(2*w) - a3*math.cos(3*w)
        dhi = scale_d * (a1*math.sin(w) - 2*a2*math.sin(2*w) + 3*a3*math.sin(3*w))
        thi = (i - centre) * hi

        h .append(hi)
        dh.append(dhi)
        th.append(thi)

    return h, dh, th


# =============================================================================
# SECTION 4 — PER-FRAME SPECTRAL ANALYSIS  (STFT + reassignment)
# =============================================================================

def analyse_frame(frame, h, dh, th, fs):
    """
    Run the three parallel FFTs for one analysis frame and compute both
    reassignment operators for every positive-frequency bin.

    Parameters
    ----------
    frame : list[float]   N audio samples for this hop position
    h     : list[float]   analysis window
    dh    : list[float]   derivative window
    th    : list[float]   time-ramp window
    fs    : int           sample rate (Hz)

    Returns
    -------
    f_bins   : list[float]   nominal bin-centre frequencies  f_k = k * fs / N
    mag_db   : list[float]   |X_h(k)| in dBFS, length N/2
    f_reas   : list[float]   reassigned instantaneous frequencies (Hz)
    t_offset : list[float]   reassigned time offsets from frame centre (s)
    mask     : list[bool]    True for bins exceeding MAG_FLOOR_DB
    """
    n    = len(frame)
    half = n // 2
    inv2pi = 1.0 / (2.0 * math.pi)

    # Three parallel windowed FFTs — positive-frequency halves only.
    # All three operate on the *same* frame x[n]; only the window differs.
    X_h  = windowed_fft(frame, h )   # standard analysis window  → magnitude
    X_dh = windowed_fft(frame, dh)   # derivative window         → freq. reassignment
    X_th = windowed_fft(frame, th)   # time-ramp window          → time  reassignment

    f_bins   = []
    mag_db   = []
    f_reas   = []
    t_offset = []
    mask     = []

    bin_width = fs / n    # Hz per FFT bin = fs / N

    for k in range(half):
        f_k = k * bin_width

        # Magnitude of the analysis-window FFT in dBFS.
        # The 1e-6 floor prevents log10(0) = -inf.
        mag = 20.0 * math.log10(abs(X_h[k]) + 1e-6)

        f_bins.append(f_k)
        mag_db.append(mag)

        above_floor = mag > MAG_FLOOR_DB
        mask.append(above_floor)

        if above_floor:
            denom = X_h[k]

            # ── Frequency reassignment ──────────────────────────────────────
            # f_hat = f_k  -  (fs / 2*pi) * Im{ X_dh(k) / X_h(k) }
            #
            # X_dh(k) / X_h(k) gives  d(phi_STFT)/dt  at bin k.
            # Its imaginary part is the angular frequency deviation in rad/sample.
            # Multiplying by  fs/(2*pi)  converts to Hz.
            # The 1e-12 guard prevents division by near-zero for silent bins.
            ratio_dh    = X_dh[k] / (denom + 1e-12)
            f_corrected = f_k - fs * inv2pi * ratio_dh.imag

            # Clamp to [0, fs/2] — outliers from near-zero bins can exceed this.
            f_corrected = max(0.0, min(fs * 0.5, f_corrected))
            f_reas.append(f_corrected)

            # ── Time reassignment ───────────────────────────────────────────
            # t_hat = t_centre  +  Re{ X_th(k) / X_h(k) } / fs
            #
            # Re{X_th / X_h} is the first temporal moment of the windowed energy
            # in samples from the frame centre.  Dividing by fs → seconds.
            ratio_th   = X_th[k] / (denom + 1e-12)
            raw_offset = max(-n * 0.5, min(n * 0.5, ratio_th.real))
            t_offset.append(raw_offset / fs)

        else:
            # Below floor: store nominal values; these bins won't be displayed.
            f_reas.append(f_k)
            t_offset.append(0.0)

    return f_bins, mag_db, f_reas, t_offset, mask


# =============================================================================
# SECTION 5 — SCROLLING SPECTROGRAM BUFFER
# =============================================================================

def make_spectrogram_buffer(width, height):
    """
    Allocate a blank (black) 2-D image as a list-of-rows of RGB tuples.

    width  : int   number of time-columns to retain (history depth)
    height : int   number of frequency rows = N // 2
    """
    return [[(1.0, 1.0, 1.0)] * width for _ in range(height)]


def scroll_and_paint(buf, f_bins_col, f_reas_col, mag_col, mask_col, f_max):
    """
    Shift the spectrogram one column to the left and paint a new rightmost
    column with two overlaid layers:

      - Standard STFT bins  → faint green pixels  (nominal bin-centre freq)
      - Reassigned bins     → solid blue pixels    (instantaneous freq estimate)

    Parameters
    ----------
    buf        : list[list[tuple]]   image buffer (rows = freq, cols = time)
    f_bins_col : list[float]         nominal bin-centre frequencies (Hz)
    f_reas_col : list[float]         reassigned frequencies for this frame (Hz)
    mag_col    : list[float]         magnitudes in dBFS
    mask_col   : list[bool]          noise-floor mask
    f_max      : float               highest frequency displayed (Hz)
    """
    height = len(buf)
    width  = len(buf[0])

    # Scroll: shift every column one step to the left.
    for row in range(height):
        for col in range(width - 1):
            buf[row][col] = buf[row][col + 1]
        buf[row][width - 1] = (1.0, 1.0, 1.0)   # clear new rightmost column (white)

    mag_lo = MAG_FLOOR_DB
    mag_hi = 0.0   # 0 dBFS = full-scale reference

    # ── Pass 1: standard STFT bin-centre positions in faint green ────────────
    for k, (f_nominal, mag, active) in enumerate(zip(f_bins_col, mag_col, mask_col)):
        if not active:
            continue
        row_idx = int(round(f_nominal / f_max * (height - 1)))
        if not (0 <= row_idx < height):
            continue
        t = max(0.0, min(1.0, (mag - mag_lo) / (mag_hi - mag_lo)))
        # Faint green: fully desaturated at low mag, medium green at high mag
        green = 0.55 + t * 0.35          # 0.55 .. 0.90
        buf[row_idx][width - 1] = (1.0 - green * 0.25, 1.0, 1.0 - green * 0.25)

    # ── Pass 2: reassigned positions in solid blue (painted on top) ───────────
    for k, (f_r, mag, active) in enumerate(zip(f_reas_col, mag_col, mask_col)):
        if not active:
            continue
        row_idx = int(round(f_r / f_max * (height - 1)))
        if not (0 <= row_idx < height):
            continue
        t = max(0.0, min(1.0, (mag - mag_lo) / (mag_hi - mag_lo)))
        # Blue ramp: light steel blue (quiet) → strong blue (loud)
        blue  = 0.45 + t * 0.55          # 0.45 .. 1.0
        red   = 0.05 + (1.0 - t) * 0.55  # fades out as signal gets louder
        buf[row_idx][width - 1] = (red, red * 0.6, blue)


# =============================================================================
# SECTION 6 — ANIMATION AND PLOTTING
# =============================================================================

def build_animation(signal, t_axis, h, dh, th, fs, n, hop, f_max, scroll_w):
    """
    Construct a 3-row live animation figure:

      Row 1 — time-domain waveform with a sliding window highlight
      Row 2 — standard STFT magnitude (green, translucent) overlaid with
               reassigned instantaneous-frequency dots (blue)
      Row 3 — scrolling reassigned spectrogram (dark background, cyan/white)

    Parameters
    ----------
    signal   : list[float]   full audio signal
    t_axis   : list[float]   time axis (seconds) for each sample
    h, dh, th: list[float]   analysis, derivative, and time-ramp windows
    fs       : int           sample rate
    n        : int           FFT / window size
    hop      : int           hop size
    f_max    : float         maximum frequency shown in rows 2 and 3 (Hz)
    scroll_w : int           scrolling spectrogram width in pixels/columns

    Returns
    -------
    fig, ani   — keep ani alive in the caller to prevent garbage collection
    """
    half_n      = n // 2
    total_samps = len(signal)

    # Pre-build all valid frame start positions
    hop_starts = list(range(0, total_samps - n, hop))
    n_frames   = len(hop_starts) // ANIM_SPEED_HOPS

    # Precompute nominal bin-frequency axis (reused every frame)
    bin_freqs = [k * fs / n for k in range(half_n)]

    # ── Figure layout ────────────────────────────────────────────────────────
    fig, axes = plt.subplots(3, 1, figsize=(13, 10))
    fig.patch.set_facecolor('#f5f5f5')
    fig.suptitle(
        f"Spectral Reassignment  |  Blackman-Harris window"
        f"  |  N={n}  hop={hop}  fs={fs} Hz",
        fontsize=12, y=0.99
    )

    # ── Row 1: oscilloscope — current analysis frame only ────────────────────
    # Shows exactly the N samples being analysed this hop, on a 0..N sample
    # index axis. This is the classic live-buffer view: the waveform updates
    # each frame to whatever is inside the sliding analysis window.
    ax_wave = axes[0]
    ax_wave.set_facecolor('white')
    ax_wave.set_xlim(0, N)
    ax_wave.set_ylim(-1.15, 1.15)
    ax_wave.set_xlabel("Sample index (n)")
    ax_wave.set_ylabel("Amplitude")
    ax_wave.set_title(
        f"Current analysis frame  —  linear sine sweep  {int(F_START)} Hz \u2192 {int(F_END)} Hz"
    )
    ax_wave.tick_params(labelbottom=True)

    (waveform_line,) = ax_wave.plot([], [], color='#333333', lw=0.8, zorder=1)

    # ── Row 2: frequency-domain comparison ───────────────────────────────────
    ax_freq = axes[1]
    ax_freq.set_facecolor('white')
    # Logarithmic frequency axis: this is the key to showing the correct
    # perceptual width of STFT bins.  The window's side-lobe skirt has a
    # constant width in Hz (= fs/N per bin), but on a log scale that same
    # Hz width maps to a *wider* arc at low frequencies and a narrower one
    # at high frequencies — exactly matching how a spectrogram looks in any
    # DAW or analyser.  On a linear axis every bin appears equally wide,
    # which is misleading for audio work.
    ax_freq.set_xscale('log')
    ax_freq.set_xlim(max(F_START * 0.5, 20.0), f_max)
    ax_freq.set_ylim(MAG_FLOOR_DB - 5, 50.0)
    ax_freq.set_ylabel("Magnitude (dBFS)")
    ax_freq.set_xlabel("Frequency (Hz)")
    ax_freq.set_title(
        "Standard STFT magnitude (green) vs. reassigned instantaneous frequency (blue)"
    )
    ax_freq.tick_params(labelbottom=True)
    # Nice Hz tick labels on the log axis
    ax_freq.xaxis.set_major_formatter(
        ticker.FuncFormatter(
            lambda v, _: f"{int(v)}" if v < 1000 else f"{v/1000:.4g}k"
        )
    )

    # Standard STFT line — faint green shows the smeared bin-centre resolution
    (line_std,) = ax_freq.plot(
        [], [], color='#22aa44', alpha=0.35, lw=1.3,
        label="Standard STFT (bin-centre frequencies)"
    )

    # Reassigned scatter — solid blue dots at the sharpened instantaneous freq
    (line_reas,) = ax_freq.plot(
        [], [], color='#1a6fcc', marker='.', ls='',
        markersize=3.5, alpha=0.85,
        label="Reassigned instantaneous frequency"
    )
    ax_freq.legend(loc='upper right', fontsize=8)

    # ── Row 3: scrolling spectrogram ─────────────────────────────────────────
    ax_spec = axes[2]
    ax_spec.set_facecolor('white')
    ax_spec.set_ylabel("Frequency (Hz)")
    ax_spec.set_xlabel("\u2190 time scrolls left")
    ax_spec.set_title("Scrolling reassigned spectrogram")

    # Spectrogram pixel buffer: rows = frequency bins, cols = time history
    spec_buf = make_spectrogram_buffer(scroll_w, half_n)

    img = ax_spec.imshow(
        spec_buf,
        aspect='auto', origin='lower',
        extent=[0, scroll_w, 0, f_max],
        vmin=0.0, vmax=1.0,
        interpolation='nearest'
    )
    ax_spec.set_xlim(0, scroll_w)
    ax_spec.set_ylim(0, f_max)
    ax_spec.yaxis.set_major_formatter(
        ticker.FuncFormatter(
            lambda v, _: f"{v/1000:.1f}k" if v >= 1000 else f"{int(v)}"
        )
    )
    ax_spec.tick_params(labelbottom=False)

    plt.tight_layout(rect=[0, 0, 1, 0.97])

    # Mutable state shared across animation callbacks
    state = {'frame_idx': 0}

    def animate(_tick):
        """
        Animation callback.  Advances ANIM_SPEED_HOPS hops per tick,
        computes the spectral analysis for the new frame, and refreshes
        all three plot rows.
        """
        fi = state['frame_idx']
        state['frame_idx'] = fi + ANIM_SPEED_HOPS
        if fi >= len(hop_starts):
            return waveform_line, line_std, line_reas, img

        start = hop_starts[fi]
        frame = signal[start : start + n]

        # ── Spectral analysis (3 FFTs + reassignment) ─────────────────────────
        f_bins, mag_db, f_reas, t_offsets, active = analyse_frame(
            frame, h, dh, th, fs
        )

        # ── Row 1: slide the window highlight and draw active frame ───────────
        # Oscilloscope: plot the N-sample frame against sample indices 0..N-1
        waveform_line.set_data(list(range(n)), frame)

        # ── Row 2: update standard and reassigned frequency traces ────────────
        line_std.set_data(bin_freqs, mag_db)

        reas_f = [f_reas[k] for k in range(half_n) if active[k]]
        reas_m = [mag_db[k] for k in range(half_n) if active[k]]
        line_reas.set_data(reas_f, reas_m)

        # ── Row 3: scroll buffer and repaint the spectrogram image ────────────
        scroll_and_paint(spec_buf, bin_freqs, f_reas, mag_db, active, f_max)
        img.set_data(spec_buf)

        return waveform_line, line_std, line_reas, img

    ani = FuncAnimation(
        fig, animate,
        frames=n_frames,
        interval=30,
        blit=False,
        repeat=False
    )

    return fig, ani


# =============================================================================
# ENTRY POINT
# =============================================================================

def run():
    """
    Top-level entry point.  Generates the signal, builds the windows,
    and launches the animation.  Only this function should be called externally.
    """
    print(f"Generating sweep  {int(F_START)} Hz -> {int(F_END)} Hz"
          f"  ({DURATION}s @ {FS} Hz)...")
    signal, t_axis = generate_sweep(FS, DURATION, F_START, F_END)

    print(f"Building Blackman-Harris windows  (N={N})...")
    h, dh, th = build_windows(N)

    print("Launching animation...  (close the window to exit)")
    fig, ani = build_animation(
        signal, t_axis, h, dh, th,
        FS, N, HOP, F_END, SCROLL_WIDTH_PX
    )

    plt.show()


run()