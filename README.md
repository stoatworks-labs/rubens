# rubens

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has run in Resolume only briefly,
> in Arena 7.27.1 on Windows (software rendering, no sound device) and Arena 7.29.0 on a Mac,
> where Resolume's own sound analysis moves the flames with a demo drum loop playing; it has
> **never been in a show**, and **it has never opened a live audio device**. Everything else below is established
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

> **Photosensitivity warning.** The flames jump with the music, and at the defaults the kicks
> flare them. With loud music that is **more than three flashes a second** in part of the
> picture, the line broadcast and web guidance draw: through SW Rubens Over at the defaults,
> 2 of Resolume's 33 demo clips went over on their own raw stems, **24 of 33** with the same
> music mastered to an ordinary release level (about −11 dBFS RMS) and all 33 with a louder
> master, up to 12 rises and falls in a second in a third of the picture where the line is
> six. **Gas 30 Pa** or **Volume 130 dB** brought every clip back under it. The whole picture
> never went over at the defaults. The row of evenly spaced flames is also a regular pattern.
> See [Flashing](#flashing) and the user guide before you put it in front of an audience.

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

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/rubens/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`rubens-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/rubens/releases/download/v0.1.0/rubens-0.1.0-macos-universal.dmg) | 1.2 MB |
| Universal (Apple Silicon + Intel) · .zip archive | [`rubens-macos-universal.zip`](https://github.com/stoatworks-labs/rubens/releases/latest/download/rubens-macos-universal.zip) | 1.1 MB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`rubens-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/rubens/releases/download/v0.1.0/rubens-0.1.0-windows-x86_64-setup.exe) | 370 KB |
| x64 · .zip archive | [`rubens-windows-x86_64.zip`](https://github.com/stoatworks-labs/rubens/releases/latest/download/rubens-windows-x86_64.zip) | 488 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/rubens/releases](https://github.com/stoatworks-labs/rubens/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Video

> **Photosensitivity warning:** the video shows flames that flare with the music, and regular
> patterns: a row of evenly spaced flames, and a fine grid behind them.

A video of both plugins, rendered by `rutest` with Resolume's demo stems and some synthesised
tones as the sound, is scheduled on YouTube for 07:50 London time on 19 November 2026. Its link
goes here once it is public.

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

**v0.1.0, and early days.** It has run in Resolume only briefly, in Arena on Windows and
on a Mac (below), never in a show, and it has never opened a live audio device: every sound it
has heard was fed to it. `oxbow probe` reads the bundles as a host does (`SW Rubens` / `RU01` /
source, `SW Rubens Over` / `RU02` / effect) and `oxbow selftest` renders through each. On Windows
both DLLs build under MSVC in CI and load as a host loads them (`tools/wincheck.ps1`: name, id,
type, 22 parameters each). The [user guide](docs/USER-GUIDE.md) is also on the
[project page](https://stoatworks-labs.com/software/rubens/guide/), and there's a
[browser demo](https://rubens-demo.stoatworks-labs.com). No OpenFX port.

### In Resolume

**On macOS**, in Arena 7.29.0 on an Apple Silicon Mac (macOS 26), with the release workflow's
universal bundles (signed ad hoc), driven over Arena's REST API in a new composition of its own:
both register (`SW Rubens`, RU01, a source; `SW Rubens Over`, RU02, an effect), with all 22 and
23 parameters shown (the Over's after Arena's own Opacity) and every default as declared. Audio
Input listed "None (host FFT)", "System default" and the inputs by name and stayed on None (host
FFT), so no device was opened, and the lamp was amber. With nothing playing and Shimmer at 0, two
snapshots 1.5 s apart are identical; Mix 0 is identical to the bare clip. Length, Hole Pitch,
Flame, Distance, Field Width, Tube Height, Exposure, Shimmer and Mix change the picture in the
Over, and the same and Backdrop in the source; with no sound Volume, Gas, Channel and FFT Law
change nothing, as they shouldn't, and Hole Size changes it by a hundredth of a level. Setting
the source's controls back gives the first picture again, identical. **The Over's alpha:** over a
test clip whose left third is opaque, middle third white at 50% alpha and right third
transparent, on a blue layer, with Distance at 0 every third above the flames is identical to
the bare clip, the half-transparent band included, and the tube and flames stand solid over the
transparent third: Arena's premultiplied input and the Over's premultiplied output agree.
**Resolume's sound analysis moves the flames.** That first check (18 November) played
`Beat 001.mov` on the layer below, and the flames held their quiet height, because Arena's demo
`.mov` clips are picture only: their sound is in the matching `.wav` files. Re-tested on
19 November 2026 with the v0.1.0 release's own bundles, Audio Input on None (host FFT), the Audio
buffer on Composition and `Beat 001.wav` playing on the layer below, read off the picture in
snapshots about a fifth of a second apart: the Over's flames (over an opaque card) stood at a
median 115 px with nothing playing, flickering only by Shimmer; with the loop playing at the
default Volume (145 dB) they dropped and went uneven along the tube, to a median of 101 px and
as low as 61 px, with the flames out over part of it; at 150 dB, the most Volume gives, to a
median of 82 px and as low as 59 px. The source's flames went from 142 px to a median of 135 px
(as low as 117 px), and at 150 dB to 123 px. Once the loop was cleared the flames went back to
their quiet height. Arena's quieter `Bass 001.wav` (−40 dBFS RMS, 18 dB under Beat 001) left them
at their quiet height even at 150 dB. **Arena's Audio setting decides what they hear:** on
Composition, every clip in the composition; on Local, only the clip the plugin is on (here the
Over's card, or the source's own clip, so nothing, and the flames stayed quiet); on External,
Arena's own audio input, which heard nothing here either. Arena's clock reached the plugin in
milliseconds, its log has no error from either plugin, and no microphone prompt appeared.

**On Windows**, in Arena 7.27.1 on win-lab (software rendering, Mesa llvmpipe, no GPU and no
sound device), the fleet's Arena gate passed 15 of 15 with 4 skipped twice: with the release's
own x64 DLLs, from the v0.1.0 zip, and before that with a pre-tag build of the release workflow
(whose DLLs differ in hash, as every build's do). Both load and register (`SW Rubens`, RU01, a source; `SW
Rubens Over`, RU02, an effect), list in Arena's source and effect browsers with all 22 and 23
parameters as declared (the Over's after Arena's own Opacity; name, order, type, range and
default), and render. With Shimmer at 0 to hold the flames still, 9 of the source's controls
and 10 of the Over's (Arena's Opacity among them) move the picture. The seven that need sound
(Audio Input, Channel, FFT Law, Volume, Hole Size, Gas and the Audio buffer) weren't tried, as
that machine has none; Rescan Inputs wasn't pressed. Arena's log is clean and it stayed up
throughout.

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
  3.5–3.7 ms at 4K. The duct alone: 7.5–7.7 ms per second of audio. Re-measured on the released
  tree on 2026-11-18: the source 0.57 ms at 1080p, 2.28 ms at 4K; the Over 0.59 and 2.38 ms;
  the duct 7.57 ms per second of audio.

### Flashing

Measured from the plugins' own output with xenon's flash check (a rise or fall of a tenth of
white, counted over the whole picture and over every third-of-the-picture patch, in linear
light; more than six in a second is over the line), 60 fps, 7.5 s each, fed through earshot's
`feed()`. The music is Resolume's own demo stems as they come (layered, about −20 dBFS RMS),
the same stems layered and mastered to about −11 dBFS RMS, and limited to about −9.5.

- **The Over at its defaults, over all 33 demo clips:** with each clip's own audio (an AV
  clip's stem, the layered stems for a silent one) 2 of 33 over, at 7 and 8 (those two clips
  measure 4 and 5 bare); mastered, 24 of 33 (up to 11); loud, 33 of 33 (up to 12). The plugin
  raised 23 of the 33 clips' own rate.
- **The source at its defaults:** the 18 raw stems 0 over (worst 6), the 6 raw layered mixes
  1 over (7); mastered 4 of 6 (up to 11), loud 6 of 6 (up to 12). `rutest`'s own track, which
  the browser demo plays: 8. The whole picture never went over at the defaults (6 at most).
- **Turning things up, the Over over the raw stems:** Volume 150 dB, 8 of 33 over (up to 10);
  Gas 1 Pa, 18 (10); Flame 30 cm, 8 (10); Exposure +4 stops, 13 (11); Field Width 0.5 m, 12 (10);
  all of those at once, 11 (12). Shimmer 1 changes nothing. With the loud master, Gas 1 Pa,
  Flame 30 cm and Field Width 0.5 m take the whole picture over too (7).
- **What brings it back:** Gas 30 Pa, or Volume 130 dB. Either kept all 33 clips under with
  the mastered and the loud music (worst 6, from a clip that measures 6 bare), and the source
  over those 12 mixes at 2. One at a time on the source with those mixes: Volume 135 dB, 6;
  Gas 10 Pa, 7; Gas 100 Pa, 0; Flame 5 cm, 2; Field Width 6 m, 5. Exposure −2 stops still
  measured 8, and the Black backdrop is no better than Grid.

No default was changed for this. The flash check counts flashes only: the row of flames is also
a regular pattern, 69 evenly spaced stripes across about a quarter of the picture at the
defaults (80% of it at Field Width 0.5 m), and the Grid backdrop is a faint grid over the whole
frame (4% of white at most), which broadcast guidance (ITU-R BT.1702, Ofcom) also counts. The
user guide's "Flashing, and keeping it safe" has the table.

### Not established

- **Which way up the defaults are.** At Gas 3 Pa and Volume 145 dB loud music is in the
  reversal: the antinodes flare. The pattern is upside down against the textbook's, where the
  flames are lowest where the pressure swings most. The textbook picture needs Gas in the tens
  of pascals or a quieter Volume. The defaults were set on Resolume's demo clips because they
  move; neither way is anchored.
- **Gas and Volume are anchored to no source.** No source read gives an absolute gas
  overpressure, flow rate or sound level for a working tube, so both are calibrated against
  nothing; the default 3 Pa is low.
- **Whether Flame should follow Gas.** Flame is the quiet height at whatever the Gas is, so Gas
  moves the dip-to-flare balance without relighting the stage; a real tube's quiet flames grow
  with the gas pressure.
- **The Over's alpha.** Arena hands an effect premultiplied input, and the Over assumes
  premultiplied input and gives premultiplied output, which match. In Arena 7.29.0 on a Mac a
  half-transparent band of a test clip came through the Over unchanged and the tube and flames
  stood solid over a transparent one; a clip with soft edges in a show hasn't been tried.
- **Whether the modes should run past the 2.78 kHz cut-on.** The modes stop below the pipe's
  first cross-mode for the default tube (2.75 kHz is the last). A loudspeaker driving the pipe
  evenly can't excite that one, so the plane-wave modes could run on to the first one it can
  (5.8 kHz); stopping early is conservative.
- **A pitch-driven plunger.** Some tubes are tuned by an adjustable end. That would be a second
  mechanism, not in this one.
- **Ficken & Stephenson 1979 is known only second-hand** (paywalled): everything about the
  reversal is through Gee and Gardner, Gee & Dix, who both call it not fully explained. The
  crossover at a\* = 4.33 is a property of the √Δp, outward-only model, labelled derived;
  swing = overpressure is where gulping starts, not where the flames flare.
- The flames' shape and colour, the plume's growth and the shimmer are looks. The flame's
  50 ms response to its fuel is an assumption.
- Not modelled: the jets' nonlinear resistance feeding back into the duct, temperature along
  the tube, the loudspeaker's own impedance, holes over part of the tube, air drawn in changing
  the gas, flames going out.
- **Resolume's FFT is unmeasured:** the fallback's level depends on FFT Law, and its bands can
  read above full scale (they are clipped). In Arena 7.29.0 on a Mac one of Arena's demo drum
  loops, at its own level, moved the flames at the defaults (above); nothing louder, and no full
  mix, has been played to it there.
- **Audio inputs, from earshot's behaviour as this snapshot has it:** with no input chosen the
  lamp is amber, which is normal; a device that has gone stays missing (blinking red) until it
  is back or another is chosen; and an open input that delivers exact zeros for 3 s turns the
  lamp steady red. None of these has been seen with a real device.
- **The inspector shows slider positions**, not decibels or metres: the user guide gives each
  range and the middle of each slider.
- **Resolume in a show.** Run in Arena only briefly, never in a composition of several layers,
  at 4K, on a GPU under Windows, or through a show; its cost was measured only offline.

## Browser demo

[rubens-demo.stoatworks-labs.com](https://rubens-demo.stoatworks-labs.com/) runs both plugins
in your browser: their own C++, the plugin class with the duct and its holes integrated in
double at 16 kHz, its audio input and earshot under it, compiled unmodified to WebAssembly and
drawing with its own GLSL in WebGL2. The page is the host, and it never opens or lists an audio
device or asks for a microphone. It feeds the plugin through earshot's `feed()` as `rutest`
does: `rutest`'s own track, silence, or an audio file of yours decoded in the page and never
uploaded; or nothing at all, with `rutest --host-fft`'s made-up kick spectrum in the Audio
buffer for the plugin's own fallback. Audio Input, Rescan Inputs and the live-device path are
not in the browser, and nothing is played aloud. A frame of the page matched `rutest`'s on the
same settings and audio to one 8-bit code in seven cases at Shimmer 0 (the shimmer and the
fallback do not match, and [AGENTS.md](AGENTS.md#browser-demo) says why). At the defaults the
flames flare with loud music rather than dip, and the page says so and offers the presets that
turn it the right way up; the flames jump with the beats, and the page says that at the top. It
is a demo, not the plugin, and the page lists what it leaves out. Its track at the defaults
measures 8 in the flash check (over the line in a third of the picture); its Gas 30 Pa and
Volume 130 dB presets measure 0.

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

Windows needs Visual Studio 2022 and vcpkg (for GLEW); CI builds it on GitHub's Windows runner
(`.github/workflows/ci.yml`), and the release installer and zip come from `release.yml`.

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
