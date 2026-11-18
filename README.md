# rubens

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume, and it has never opened a live audio device.** What's established is established
> by an offline harness, `rutest`, that drives the real plugin class through the same
> `ProcessOpenGL` a host calls, in a headless GL context on a synthetic clock, on this Mac's GPU
> at two rasters, and feeds it audio through the same earshot `feed()` path a device would use.
> The shipped duct's impulse response matches its own continuous model, through the zero-order
> hold, to **1.4e-6** over 988 frequencies; with the losses taken out its resonances sit on the
> closed form √(f_H² + f_n0²), the holes' end correction included, to **4.4e-16**. The period of
> the flames, read off the rendered picture, matches c / (2f √(1 − f_H²/f²)) with propane's
> 246.59 m/s to **0.15%** at modes 10 and 14 (air's sound speed is 27% off). Fed a slow sine
> through earshot, the flames dip to **0.838** of their quiet height and cross it again at a
> swing of **4.43** times the gas overpressure, against **4.33** from the √Δp model. The same
> audio at 24, 30, 60 and 240 fps gives **bit-identical** flames. **13** deliberately wrong
> models and **25** one-character mutants of the shipped code are all caught. What the flames
> and the hot air above them look like is **chosen**, not measured, and no source gives a
> working tube's gas pressure or sound level: see [Status](#status).

Rubens' flame tube for [Resolume](https://resolume.com) Arena and Avenue, as a pair of FFGL
plugins: **SW Rubens**, a source, and **SW Rubens Over**, an effect in which your clip is seen
through the heat above the flames.

![A long horizontal steel tube near the bottom of a dark frame, a speaker housing on its left
end with a small green lamp, and a row of 69 tall yellow-orange flames with blue bases along
its top. The flames rise and fall in broad humps along the tube: a standing wave drawn in
flame. Behind them a grey grid is wobbled and bent by the hot air above the
flames](docs/hero.png)

<sub>One frame rendered by `rutest`, not captured from Resolume: SW Rubens at its defaults on
the Grid backdrop, 5.5 s into the harness's track (`--frames 330`).</sub>

## The one idea

**The music moves a piston; the propane in the pipe resonates; each hole's flow goes as the
square root of the pressure across it, outward only, into its flame; and the hot plumes above
the flames bend the light from what is behind them.**

Nothing else listens to the audio, and no parameter is modulated by it. The tube is a 1-D
duct closed at both ends (a rigid cap, and the loudspeaker as a moving rigid wall) with up to
64 modes below its first cross-mode, each a three-state linear system discretised exactly at
16 kHz, plus a closed-form remainder for the modes left out. Every hole is an acoustic mass
(its wall thickness plus Rayleigh's end correction at each end) and a Bernoulli jet,
integrated per audio sample. A flame's length follows its outward fuel flow, as a laminar jet
flame's does. The hot gas above each flame is a column whose line-of-sight integral is
closed-form, and the backdrop or clip is seen displaced by its gradient, as background-oriented
schlieren sees it.

## What falls out

- **A standing wave drawn in flame**, its peaks L/n apart. That is c/2f for propane at the
  higher modes, closer together than an air calculation says; at the lowest modes the holes
  push the resonances up and the peaks sit further apart than c/2f, as Gardner, Gee & Dix
  measured.
- **Quiet sound dips the flames where the pressure swings most.** Louder, past the point
  where the swing beats the gas pressure, the holes there briefly draw air in ("gulping");
  louder still, at about 4.3 times the gas pressure, those same places burn taller than the
  rest: the reversal Ficken & Stephenson reported (as Gee relates it). Which way round the
  pattern sits depends on **Gas** against **Volume**.
- **Off-resonance notes barely move it**: halfway between two sharp modes, a hundred times
  less than on one (a fifth as much between the broad low modes).
- **The gas damps the low modes.** A hole's jet resistance grows with the gas pressure, so
  turning Gas up blurs the lowest modes and leaves the high ones sharp. High notes also dip
  the flames less: the holes' own mass resists the swing.
- **The heat shimmers.** The clip or backdrop behind the flames is bent by the plumes, most
  just above the tips, more the further away it is (**Distance**).

## Where the audio comes from

From an audio input, through [earshot](https://github.com/stoatworks-labs/earshot), the fleet's
live-audio front end ([a snapshot](external/earshot/SNAPSHOT) of commit `8124191`, unmodified). Its 16 kHz waveform tap is the
loudspeaker's velocity, every sample once: the tube picks its own resonances out of whatever
is played, chords and noise included. Pitch is not used.

Pick the input in **Audio Input**: it is remembered by name, so a composition reopened on
another day finds the same device, and a device that has gone shows as missing rather than
quietly becoming another one. **Rescan Inputs** lists what is plugged in now. The device is
opened off the render thread.

**On macOS the microphone permission belongs to Resolume, not to this plugin.** The first time
any plugin inside Resolume opens an input, macOS asks whether *Resolume* may use the
microphone. If that was refused, macOS hands Resolume silence rather than an error: after 3 s
of exact zeros the lamp goes **steady red** and the flames burn at their quiet height. The fix is System Settings →
Privacy & Security → Microphone → Resolume Arena. An unpatched Dante receiver or an empty
loopback looks the same.

**With no input chosen ("None (host FFT)"), rubens falls back to Resolume's own FFT, and that
is much less.** The FFT gives four band levels a frame and no waveform, so rubens drives the
piston with band-limited noise in each band (40–160, 160–640, 640–2560 and 2560–8000 Hz)
whose level follows the band. The tube still picks its resonances out of the noise, and the
loud bands still dip or flare the flames; but **the melody is gone** (which notes, so which
modes), **so are the transients inside a frame**, **the level is not calibrated** (nobody has
measured what Resolume's bins hold; **FFT Law** chooses how to read them), and **it steps once
a frame**, so unlike the audio-input path it depends on the frame rate. The fallback also
drives while a chosen input is opening, missing or failed.

The status lamp, a small LED on the speaker housing (slid in from the edge when the housing
is off the frame): **green** an input is delivering; **amber** Resolume's FFT (the usual state
with no input chosen); **blinking amber** an input is opening; **blinking red** the chosen
input is missing or failed (the FFT drives meanwhile); **steady red** the input is open but
silent.

## The Over and its alpha

The clip is the backdrop, behind the tube, seen through the plumes; the flames add their light
and the tube is opaque in front. The output is premultiplied: alpha is
1 − (1 − clip alpha)(1 − tube)(1 − flames), the flames' coverage being their brightest encoded
channel, then raised where needed so no channel exceeds alpha. A transparent clip (Resolume's
DXV demo clips carry alpha) stays transparent everywhere but the tube and the flames. **Mix**
blends that with the untouched clip, and **Mix 0 returns the clip bit-exactly**. The source's
alpha is 1.

## Controls

| Group | Control | Range | Default |
| --- | --- | --- | --- |
| Audio | Audio Input | the device menu: None (host FFT), System default, each input by name | None (host FFT) |
| | Rescan Inputs | event | |
| | Channel | L+R, Left, Right | L+R |
| | FFT Law | how the host's bins are read: Linear Mag, Linear Power, Log Mag, Log Power | Linear Mag |
| | Volume | 80–150 dB re 20 µPa: the level a full-scale sine makes in the gas | 145 dB |
| Tube | Length | 0.5–3 m | 1.524 m |
| | Hole Pitch | 1–5 cm (at most 160 holes) | 2.2 cm |
| | Hole Size | hole radius 0.3–2 mm | 0.92 mm |
| | Gas | the gas overpressure, 1–1000 Pa | 3 Pa |
| Flames | Flame | the quiet flame's height, 1–30 cm | 20 cm |
| | Shimmer | the plumes' turbulence, 0–1 | 0.5 |
| | Distance | how far behind the plumes the backdrop is, 0–30 m | 6 m |
| View | Field Width | the width of the scene across the frame, 0.5–6 m | 1.9 m |
| | Tube Height | the tube's height up from the bottom of the frame, 0–0.6 | 0.2 |
| | Exposure | ±4 stops | 0 |
| | Backdrop (source) | Black, Grid, Dots | Grid |
| | Mix (Over) | 0–1 | 1 |

The tube's bore (2.6 cm radius) and wall (4 mm) are Gardner, Gee & Dix's and fixed; so is the
gas, propane at 20 °C (NIST). The default tube is their large-holed one. **Flame** is the quiet
height at whatever the Gas is, so turning Gas moves the dip-to-flare balance without
relighting the stage (a real tube's quiet flames grow with the gas pressure). Gas and Volume
are not anchored by any source read: the defaults were set on Resolume's bundled demo clips
and their music, where 3 Pa and 145 dB make the kicks flare the flames by 70–85%.

## Status

**v0.1.0, 2026-11-16; public from 2026-11-18.** Built and verified offline on one Mac (Apple Silicon);
never loaded into Resolume, never run against a real audio device, never built on Windows.
No OpenFX port and no browser demo.

### Measured offline, on macOS

- **The duct.** The shipped `Duct`'s impulse response against the same model solved in the
  frequency domain through the zero-order hold: 1.4e-6 worst over 20 Hz to 0.97 f_c. Lossless,
  every pole against √(f_H² + f_n0²): 4.4e-16, with f_H (125.86 Hz) and the mode frequencies
  computed from literals typed in the harness. The amplitude against the shunted line's wave
  solution: 2.3e-5 at the cap below 400 Hz, 1.3e-3 to f_c/2. A high mode's ring-down against
  its pole: 0.07%. Each mode's wall loss against Kirchhoff's: 2.2e-16 (Q 192 at 100 Hz, 607 at
  1 kHz).
- **Against the paper.** The shunted closed form, fed Gardner, Gee & Dix's own f_H and no-hole
  frequencies, against their Table II: 4.4% worst (small holes), 2.6% (large); without the
  holes' shunt, 41% off.
- **The holes.** One hole's small-signal impedance against |R + jωM|: 0.43% to 100 Hz. The
  slow-swing mean outflow against the quasi-steady √Δp average g(a): 0.71% worst over a = 0.25
  to 8. The fraction of time a hole gulps against acos(1/a)/π: 0.0014.
- **Dip and flare, end to end** (earshot `feed()`, 5 Hz, Gas 30 Pa): every flame against g(a)
  at its own hole's measured swing, 0.79% worst; the crossing at a\* = 4.434 against the
  derived 4.331 (the holes' inertia, ~2%). A note between two modes moves the flames 0.0099 as
  much as one on mode 10.
- **The picture.** The flames' period by least squares off the rendered frame: 0.10% and 0.05%
  at modes 10 and 14 (1280x720), 0.15% (320x180); 1.0% at mode 3, where the duct's own
  pattern, mixed with its neighbours, is 0.5027 m against the closed form's 0.5080. The plume's
  optical path against the Gaussian columns' integral: 2.1e-5 µm of 23 µm; the displacement
  against Distance × ∇P within a quarter of its 0.1 px + 2% tolerance; the Over reads its clip
  where the displacement says, to 0.0027 px.
- **Rates, clock, raster.** 24, 30, 60 and 240 fps: 0 difference. At Resolume's 499 million ms
  the frames stay 1/60 s apart to 6e-11 s. A raster change mid-run gives exactly a fresh
  instance's picture (0 bytes differ, both plugins, both a size and a shape change).
- **Alpha.** Mix 0: 0 of 3.7 million floats differ. A transparent clip off the tube and flames:
  0 error; no channel above alpha anywhere.
- **The device menu**, on earshot's scripted backend: by name, a reorder survived, Missing
  never swapped for the default, Silent, Rescan, a 300 ms open costing the render thread at
  most 6 ms a frame.
- **Cost** (this Mac's GPU, shared with other builds; GL_TIME_ELAPSED medians over three
  verify runs on 2026-11-16): the source **0.57–0.59 ms at 1080p (3.4–3.6% of a 60 fps
  frame)**, 2.28 ms at 4K, 0.72–1.39 ms at 720p; the Over 0.59–0.61, 2.37–2.39 and 0.48–0.80
  ms. 720p is the noisy one, and reading above 1080p is the shared GPU's clock, not the
  shaders. Wall time per frame, the duct and earshot included: 1.5–2.4 ms at 720p and 1080p,
  3.5–3.7 ms at 4K. The duct alone: 7.5–7.7 ms per second of audio.

### Not established

- Whether a real Rubens tube burns like this at these settings. No source read gives an
  absolute gas overpressure, flow rate or sound level for a working tube, so Gas and Volume are
  calibrated against nothing; the default 3 Pa is low.
- Ficken & Stephenson 1979 itself (paywalled): everything about the reversal is through Gee and
  Gardner, Gee & Dix, who both call it not fully explained. The crossover at a\* = 4.33 is a
  property of the √Δp, outward-only model, labelled derived; swing = overpressure is where
  gulping starts, not where the flames flare.
- The flames' shape and colour, the plume's growth and the shimmer are looks. The flame's
  50 ms response to its fuel is an assumption.
- Not modelled: the jets' nonlinear resistance feeding back into the duct, temperature along
  the tube, the loudspeaker's own impedance, holes over part of the tube, air drawn in changing
  the gas, flames going out.
- Resolume's FFT bins are unmeasured: the fallback's level depends on FFT Law.
- Windows: never built.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Rubens.bundle` and `Rubens Over.bundle` into
`~/Documents/Resolume Arena/Extra Effects`. Restart Arena: **SW Rubens** is under Sources and
**SW Rubens Over** under Effects.

Windows needs Visual Studio 2022 and vcpkg (for GLEW). It has never been built.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot, under bash: shader hygiene and `glslc`, the submodule pin and the
earshot snapshot, a fresh universal build with no warnings, both bundles' architecture, plist
and ad-hoc signature, a host's view of them through `oxbow` (required), every check at two
rasters and five on the software renderer, the checks that need no GL context, the pipe, the
negative controls, the mutants, the sweep for dead controls and the render cost. No step opens
an audio device. `CLAUDE.md` lists the checks one by one.

A clip through the Over, with its soundtrack:

```bash
ffmpeg -i clip.mov -f rawvideo -pix_fmt rgba - \
  | ./build/rutest --over --pipe --fps 30 --size 1280x720 --wav clip.wav \
  | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mp4
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
