"""
ALIVE - Biometric Acquisition Unit (BAU)
Quantitative analysis + figures for Section 3.1

Covers the two channels analysed in the report:
  - 3.1.3 Heart-Rate / PPG filter design
  - 3.1.4 Motion / yaw drift (MPU-6050 datasheet-grounded)

Running this script:
  - prints every quantitative figure used in Section 3.1
  - saves two figures:  fig_ppg_filter.png  and  fig_yaw_drift.png

Reproducible:   python bau_analysis.py
Requires:       numpy, scipy, matplotlib
"""
import numpy as np
from scipy import signal
import matplotlib
matplotlib.use("Agg")          # save to file, no display window needed
import matplotlib.pyplot as plt

# =====================================================================
# 3.1.3  HEART-RATE / PPG CHANNEL
# =====================================================================
print("=" * 64)
print("3.1.3  HEART-RATE / PPG FILTER DESIGN")
print("=" * 64)

fs = 25.0                 # PPG sample rate (Hz), N2
f_lo, f_hi = 0.5, 4.0     # band-pass passband (Hz)

print("Cardiac fundamental band (F1, 50-180 bpm):")
for bpm in (50, 180):
    print(f"  {bpm} bpm -> {bpm/60:.3f} Hz")
print(f"  Nyquist = fs/2 = {fs/2:.1f} Hz  (> 3.0 Hz first harmonic -> no aliasing)")
print()

w1, w2 = 2*np.pi*f_lo, 2*np.pi*f_hi
w0 = np.sqrt(w1*w2); B = w2 - w1
print("Band-pass transform parameters:")
print(f"  w0 = sqrt(w1*w2) = {w0:.3f} rad/s  -> f0 = {w0/2/np.pi:.3f} Hz (geometric mean)")
print(f"  B  = w2 - w1     = {B:.2f} rad/s")
print(f"  Q  = w0/B        = {w0/B:.3f}")
print()

noise = {"baseline 0.1 Hz": 0.1, "respiration 0.25 Hz": 0.25,
         "cardiac center 1.2 Hz": 1.2, "motion 6 Hz": 6.0, "flicker 10 Hz": 10.0}
print("Attenuation by filter order (Butterworth band-pass 0.5-4 Hz):")
print(f"  {'disturbance':22s} {'2nd':>8s} {'4th':>8s}")
for name, f in noise.items():
    row = []
    for order in (1, 2):
        sos = signal.butter(order, [f_lo, f_hi], btype='band', fs=fs, output='sos')
        w, h = signal.sosfreqz(sos, worN=16384, fs=fs)
        row.append(20*np.log10(abs(h[np.argmin(np.abs(w-f))]) + 1e-12))
    print(f"  {name:22s} {row[0]:7.1f}dB {row[1]:7.1f}dB")

sos = signal.butter(2, [f_lo, f_hi], btype='band', fs=fs, output='sos')
b, a = signal.sos2tf(sos)
wg, gd = signal.group_delay((b, a), w=np.linspace(0.3, fs/2, 8000), fs=fs)
card = (wg >= 0.83) & (wg <= 3.0)
print(f"  4th-order group delay over cardiac band: "
      f"{(gd[card]/fs*1000).min():.0f}-{(gd[card]/fs*1000).max():.0f} ms")
print()

z, p, k = signal.sos2zpk(sos)
print(f"Stability: max |pole| = {np.max(np.abs(p)):.3f}  (< 1 => stable)")
print()

print("F1 accuracy (+/-5 bpm -> beat-interval tolerance):")
for bpm in (60, 120, 180):
    print(f"  {bpm} bpm: interval {60000/bpm:.0f} ms, +/-5 bpm = +/-{abs(-60000/bpm**2)*5:.1f} ms")
print(f"  Sample period @ {fs} Hz = {1000/fs:.0f} ms")
print(f"  => 40 ms > 9.3 ms tolerance: sub-sample peak interpolation REQUIRED")
print()

# ---------------------------------------------------------------------
# FIGURE 1: PPG band-pass magnitude + group delay
# ---------------------------------------------------------------------
w, h = signal.sosfreqz(sos, worN=16384, fs=fs)
mag = 20*np.log10(abs(h) + 1e-12)
wg_full, gd_full = signal.group_delay((b, a), w=8192, fs=fs)
gd_ms = gd_full/fs*1000

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7.0, 6.2), sharex=True)
ax1.semilogx(w, mag, color="#2E75B6", lw=1.8)
ax1.axvspan(f_lo, f_hi, color="#9EC3E6", alpha=0.22, label="Passband 0.5-4 Hz")
ax1.axvspan(0.83, 3.0, color="#A9D18E", alpha=0.35, label="Cardiac 50-180 bpm")
for f in (0.1, 0.25, 10):
    ax1.axvline(f, color="#C00000", ls=":", lw=1)
ax1.set_ylim(-90, 5); ax1.set_ylabel("Magnitude (dB)")
ax1.set_title("PPG band-pass - 4th-order Butterworth, fs = 25 Hz")
ax1.grid(True, which="both", ls=":", alpha=0.5)
ax1.legend(fontsize=8, loc="lower center")

ax2.semilogx(wg_full, gd_ms, color="#7030A0", lw=1.8)
ax2.axvspan(0.83, 3.0, color="#A9D18E", alpha=0.35)
ax2.set_ylim(0, 600); ax2.set_xlim(0.05, 12.5)
ax2.set_xlabel("Frequency (Hz)"); ax2.set_ylabel("Group delay (ms)")
ax2.set_title("Group delay (HR latency budget = 5 s, N1)")
ax2.grid(True, which="both", ls=":", alpha=0.5)
plt.tight_layout()
plt.savefig("fig_ppg_filter.png", dpi=140)
plt.close()
print("Saved fig_ppg_filter.png")
print()

fs = 25.0
f_lo, f_hi = 0.5, 4.0
sos = signal.butter(2, [f_lo, f_hi], btype='band', fs=fs, output='sos')
b, a = signal.sos2tf(sos)

# ---------------------------------------------------------------------
# 3.1.4  PEAK DETECTION AND RATE ESTIMATION  (proves F1)
# ---------------------------------------------------------------------
print("=" * 64)
print("3.1.4  PEAK DETECTION AND RATE ESTIMATION (F1)")
print("=" * 64)

F1_TOL   = 5.0     # bpm, F1 accuracy requirement
SNR_DB   = 30.0    # assumed PPG SNR (good optical contact)
Ts       = 1000/fs # sample period, ms

# --- the problem: sample period vs required interval tolerance --------
print("Interval tolerance for +/-5 bpm  (dHR = 60000/T^2 * dT):")
for bpm in (60, 120, 180):
    T = 60000/bpm
    print(f"  {bpm:3d} bpm: T = {T:4.0f} ms, tolerance = +/-{F1_TOL*T**2/60000:.1f} ms")
print(f"  Sample period Ts = {Ts:.0f} ms -> naive peak-pick error = +/-{Ts/2:.0f} ms")
print(f"  => +/-20 ms > +/-9.3 ms at 180 bpm: naive peak-picking FAILS F1")
print()

# --- (i) parabolic sub-sample interpolation: measure the jitter -------
def ppg(t, f, ph):
    """Realistic PPG: fundamental + 2nd/3rd harmonics (dicrotic notch)."""
    return (1.00*np.sin(2*np.pi*f*t + ph)
          + 0.35*np.sin(2*np.pi*2*f*t + ph + 0.6)
          + 0.15*np.sin(2*np.pi*3*f*t + ph + 1.2))

def peak_jitter(bpm, snr_db, trials=3000, seed=1):
    """sigma of parabolic-interpolated peak time (ms) vs true peak."""
    rng = np.random.default_rng(seed)
    f, namp, err = bpm/60, 10**(-snr_db/20), []
    for _ in range(trials):
        ph = rng.uniform(0, 2*np.pi)
        t  = np.arange(0, 6, 1/fs)
        x  = ppg(t, f, ph) + namp*rng.standard_normal(len(t))
        k  = np.argmax(x[3:len(x)-3]) + 3
        ym, y0, yp = x[k-1], x[k], x[k+1]
        den = ym - 2*y0 + yp
        if abs(den) < 1e-12:
            continue
        delta = np.clip(0.5*(ym - yp)/den, -1, 1)      # sub-sample offset
        t_est = (k + delta)/fs
        td = np.arange(0, 6, 1/2000)                    # dense ground truth
        w  = (td > t[k] - 0.5/f) & (td < t[k] + 0.5/f)
        t_true = td[w][np.argmax(ppg(td[w], f, ph))]
        err.append(abs(t_est - t_true)*1000)
    return np.percentile(err, 95)/1.645                 # p95 -> sigma

sigma_t = {bpm: peak_jitter(bpm, SNR_DB) for bpm in (60, 120, 180)}
print(f"(i) Parabolic interpolation, {SNR_DB:.0f} dB SNR -> per-peak jitter:")
for bpm, s in sigma_t.items():
    print(f"  {bpm:3d} bpm: sigma_t = {s:4.1f} ms")
print(f"  => 180 bpm sigma_t = {sigma_t[180]:.1f} ms, still > 9.3 ms: NOT sufficient alone")
print()

# --- (ii) M-beat averaging: sigma_HR = sqrt(2)*sigma_t*HR^2/(60000*M) --
def sigma_hr(bpm, M):
    return np.sqrt(2)*sigma_t[bpm]*bpm**2/(60000*M)

print("(ii) M-beat rolling average -> 2-sigma HR error (bpm):")
print(f"  {'HR':>5s} " + " ".join(f"{'M='+str(M):>9s}" for M in (1,2,4)))
for bpm in (60, 120, 180):
    cells = []
    for M in (1, 2, 4):
        e = 2*sigma_hr(bpm, M)
        cells.append(f"{e:7.2f}{'ok' if e <= F1_TOL else 'XX'}")
    print(f"  {bpm:5d} " + " ".join(f"{c:>9s}" for c in cells))

M_req = max(next(M for M in range(1, 33) if 2*sigma_hr(bpm, M) <= F1_TOL)
            for bpm in (60, 120, 180))
print(f"  => smallest M meeting +/-5 bpm at every rate: M = {M_req}")
print()

# --- group-delay variation term (from 3.1.3, worst realistic HR jump) --
wg, gd = signal.group_delay((b, a), w=np.linspace(0.3, fs/2-0.01, 20000), fs=fs)
gd_ms  = gd/fs*1000
tau    = lambda bpm: gd_ms[np.argmin(np.abs(wg - bpm/60))]
print("Group-delay variation under a beat-to-beat HR change:")
eps_delay = 0.0
for b1, b2 in ((70, 75), (120, 126), (150, 158)):
    d   = abs(tau(b2) - tau(b1))                  # differential delay, ms
    T   = 60000/b2
    err = abs(60000/(T + d) - 60000/T)            # induced HR error, bpm
    eps_delay = max(eps_delay, err)
    print(f"  {b1}->{b2} bpm: d(tau_g) = {d:5.1f} ms -> HR error = {err:.2f} bpm")
print(f"  => worst case = {eps_delay:.2f} bpm")
print()

# --- F1 error budget --------------------------------------------------
eps_timing = 2*sigma_hr(180, M_req)               # worst rate, 2-sigma
eps_total  = np.hypot(eps_timing, eps_delay)      # independent -> RSS
print(f"F1 ERROR BUDGET (worst case, 180 bpm, M = {M_req}):")
print(f"  peak-timing jitter (2 sigma)  = {eps_timing:.2f} bpm")
print(f"  group-delay variation         = {eps_delay:.2f} bpm")
print(f"  total (RSS)                   = {eps_total:.2f} bpm")
print(f"  F1 requirement                = +/-{F1_TOL:.1f} bpm"
      f"  -> {'MET' if eps_total <= F1_TOL else 'NOT MET'}"
      f" ({(1-eps_total/F1_TOL)*100:.0f}% margin)")

# =====================================================================
# 3.1.5  MOTION / YAW (MPU-6050 datasheet-grounded)
# =====================================================================
print("=" * 64)
print("3.1.4  MOTION / YAW DRIFT  (MPU-6050 datasheet figures)")
print("=" * 64)

ZRO          = 20.0    # deg/s, initial zero-rate output tolerance [6.1]
noise_rms    = 0.05    # deg/s-rms total noise, 100 Hz BW          [6.1]
lin_acc_sens = 0.1     # deg/s per g, linear accel sensitivity      [6.1]
accel_zerog  = 0.050   # g, accel zero-g tolerance X/Y (50 mg)      [6.2]
fs_imu       = 100.0   # Hz
F3_limit     = 5.0     # deg, F3 yaw accuracy requirement

tilt_err = np.degrees(np.arcsin(accel_zerog/1.0))
print(f"Roll/pitch: accel zero-g tolerance {accel_zerog*1000:.0f} mg")
print(f"  worst-case static tilt error = arcsin(0.050/1.0) = {tilt_err:.1f} deg  (< 5 deg, meets F3)")
print()

print(f"Raw uncalibrated bias (ZRO = {ZRO} deg/s):")
print(f"  {ZRO} deg/s x 60 s = {ZRO*60:.0f} deg/min  => calibration MANDATORY")
print()

dt = 1/fs_imu
print(f"Gyro noise random-walk ({noise_rms} deg/s-rms):")
print(f"  yaw std after 60 s = {noise_rms}*sqrt({dt}*60) = {noise_rms*np.sqrt(dt*60):.3f} deg  (negligible)")
print()

print("Residual bias drift after calibration (the dominant term):")
for resid in (0.05, 0.10, 0.50):
    print(f"  {resid:>4} deg/s -> {resid*30:.1f} deg @30s, {resid*60:.1f} deg @60s")
print(f"  => at 0.1 deg/s, yaw error = 6 deg @60s EXCEEDS +/-5 deg (F3)")
print(f"  => yaw is short-term-relative only")
print()
print(f"Acceleration cross-coupling ({lin_acc_sens} deg/s/g):")
print(f"  sustained 1g -> {lin_acc_sens*60:.1f} deg/min false yaw (compounds bias drift)")
print()

# ---------------------------------------------------------------------
# FIGURE 2: yaw drift vs time for several residual biases
# ---------------------------------------------------------------------
t = np.linspace(0, 120, 500)               # seconds
biases = [0.05, 0.10, 0.20, 0.50]          # deg/s residual after calibration
colors = ["#2E9E75", "#2E75B6", "#BF9000", "#C00000"]

fig, ax = plt.subplots(figsize=(7.0, 4.6))
for bdeg, col in zip(biases, colors):
    ax.plot(t, bdeg*t, color=col, lw=1.8, label=f"{bdeg} deg/s residual bias")
ax.axhline(F3_limit, color="black", ls="--", lw=1.4)
ax.text(2, F3_limit+0.6, "F3 limit = +/-5 deg", fontsize=9)
ax.plot(50, 5, "o", color="#2E75B6", ms=6)
ax.annotate("0.1 deg/s crosses\n5 deg at t = 50 s",
            xy=(50, 5), xytext=(60, 12), fontsize=8,
            arrowprops=dict(arrowstyle="->", color="#2E75B6"))
ax.set_xlim(0, 120); ax.set_ylim(0, 30)
ax.set_xlabel("Time since bias re-estimate (s)")
ax.set_ylabel("Accumulated yaw error (deg)")
ax.set_title("Yaw drift from residual gyro bias (MPU-6050) vs F3 limit")
ax.grid(True, ls=":", alpha=0.5)
ax.legend(fontsize=8, loc="upper left")
plt.tight_layout()
plt.savefig("fig_yaw_drift.png", dpi=140)
plt.close()
print("Saved fig_yaw_drift.png")
