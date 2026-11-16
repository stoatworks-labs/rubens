# AGENTS.md: Rubens

Read this before changing the duct, the holes, the flames' accounting, the plume or composite
shaders, the Over's alpha, the fallback, the device menu or the clock. `CLAUDE.md` has the
commands; the spec is `~/Projects/resolume/specs/SPEC-rubens.md` (its As-built section records
where this repo departs from it).

## Status

**v0.1.0, local only, 2026-11-16.** `OXBOW=… tools/verify.sh` passes on this Mac (Apple
Silicon, macOS 26) against a fresh universal Release build: every check at 1280x720 and
320x180, five on Apple's software renderer, `--offline` with no GL context, the pipe for both
plugins, 13 of 13 negative controls, 25 of 25 mutants, 14 of 14 controls alive in the sweep
for each plugin, oxbow reading and rendering both bundles with devices disabled. **Never
loaded into Resolume; no live audio device ever opened**, by rubens or by earshot. Never built
on Windows. No remote, nothing released, not on the website. earshot is a snapshot of
`~/dev/earshot` at `8124191`, unpatched.

## The idea

**The music moves a piston; the propane in the pipe resonates; each hole's flow goes as the
square root of the pressure across it, outward only, into its flame; and the hot plumes above
the flames bend the light from what is behind them.** Audio drives the mechanism, never a
parameter wobble. The pressure at each hole is the sum of the duct's modes plus a closed-form
remainder; the hole's flow is integrated from that pressure every sample; a flame's length is
its low-passed outward flow over the quiet flow, times Flame. Everything the picture shows
moving (the standing wave, the dip, the gulping, the flare, a note off resonance doing
nothing) comes out of that chain.

## What the literature settled (the spec has the claim-by-claim table)

- **Confirmed** [GGD]: a plane-wave duct, rigid cap, loudspeaker; the holes are acoustic
  masses that push the resonances up, most at the lowest modes, so flame spacing exceeds c/2f
  there. Here: ω_n² = ω_H² + (nπc/L)², checked against their Table II (4.4%, 2.6%).
- **Corrected**: "end correction" in the IDEAS bullet. A closed–closed duct has no open end;
  the end corrections that move the resonances are the holes' (Rayleigh's 0.8217 r [BS]).
- **Confirmed second-hand** (Ficken & Stephenson via [Gee] and [GGD]; F&S itself paywalled):
  the √Δp Bernoulli efflux, normal operation tallest at pressure nodes, and the reversal with
  gulping at low gas pressure or loud sound, attributed by F&S to the swing exceeding the
  static pressure.
- **Contradicted as stated, derived instead**: the IDEAS bullet put the flare where the swing
  equals the overpressure. In the √Δp outward-only model that is where gulping **begins**
  (a = 1, flame still 10% short); the flare (mean outflow back to quiet) is at **a\* = 4.33**.
  No source states a crossover; [GGD] and [Gee] call the reversal not fully explained. It is
  tested as a property of this model and labelled derived.
- **Replaced**: "needs pitch". The waveform tap carries strictly more; pitch is not used.
- [GGD]'s and [Gee]'s citation of "C. Stephenson" is wrong: the publisher gives Francis C.
  Stephenson. Crossref dates [Gee]'s POMA volume 2010 (the talk was 2009).

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Rubens`, `RU01`, source, `com.stoatworks.ffgl.rubens`; `SW Rubens Over`,
  `RU02`, effect, `com.stoatworks.ffgl.rubens.over`; harness `rutest`. One OBJECT core, an
  `isEffect` flag (haze's shape). The source has Backdrop and no Mix; the Over has Mix and no
  Backdrop. The About block last.
- **earshot, snapshotted, not a submodule.** `external/earshot/` is `git archive 8124191` of
  the local-only `~/dev/earshot`, with `SNAPSHOT` saying so and "Patches: none" (verify.sh
  fails if that line changes). Configured with `tapRate = 16000` (the tap's 6.4 kHz cutoff is
  above the duct's 2.78 kHz cut-on, and the duct's cost is per sample), `maxChannels = 2`, four
  bands with edges {40, 160, 640, 2560, 8000} Hz (used by the fallback only).
- **The duct.** Closed–closed (the piston is a moving rigid wall), modes cos(nπx/L) from n = 0
  up to the first cross-mode cut-on f_c = j′₁₁c/(2πa): **35** for the default tube (n = 0…34;
  the spec said 34, miscounting n = 0), at most 64. The holes as a uniform shunt (each an
  inertance plus the jet's small-signal resistance 2P₀/q₀), the walls' Kirchhoff loss per mode
  [BBN]. Each mode is a 3-state linear system (a, y, w) discretised exactly (matrix
  exponential, ZOH on v) in double.
- **The left-out modes' remainder.** Truncating at f_c drops each high mode's quasi-static
  response, which the low-frequency amplitude needs: the cap's pressure was 2.6% off at 20 Hz
  and the piston's response had spurious zeros. Added, in closed form:
  p += R(x)·dv/dt, R(x) = (2ρL/π²)[S(θ, a) − Σ_{n=1}^{N−1} cos nθ/(n² + a²)],
  S(θ, a) = π cosh(a(π − θ))/(2a sinh πa) − 1/(2a²) (a → 0: π²/6 − πθ/2 + θ²/4),
  θ = πx/L, a = ω_H L/(πc); dv/dt as a first difference at the tap rate. With it the cap is
  within 2.3e-5 of the shunted line's wave solution below 400 Hz (check (2c)).
- **The holes.** M q̇ = (P₀ + p) − K q|q| per hole, backward Euler solved in closed form, two
  substeps per sample; the tube's mean pressure held at P₀ (a stiff regulator). The duct sees
  the holes only through the linearised shunt; the nonlinear flow does not feed back (Not in
  0.1.0).
- **The flames.** Fuel is the outward flow only, max(q, 0), low-passed per sample, τ = 50 ms.
  Length = Flame × fuel/q₀ [SHUN]. Aeration is the low-passed fraction of time gulping. Flame
  is the quiet height **at the current Gas**, so Gas moves the dip/flare balance without
  relighting the stage.
- **The plume.** An axisymmetric Gaussian column per hole (shape a look); its line-of-sight
  integral closed-form; ∇P by central difference over two pixels and the backdrop at
  `here + Distance·∇P` (schlieren's). The plume pass sums at most ±16 holes around a pixel.
- **The Over's alpha** (premultiplied): A = 1 − (1 − a_clip)(1 − a_tube)(1 − a_flame), a_flame
  the flames' brightest **encoded** channel; then A = max(A, the output's brightest encoded
  channel), so the result is always a valid premultiplied colour (without the lift a flame
  over a transparent clip came out 0.287 above its alpha). Mix 0 returns the clip by
  texelFetch, bit-exact. The source's alpha is 1.
- **The lamp** (spasis's states, earshot's): green Running; steady red Silent (the flames
  then burn at their quiet height: silence is no swing); blinking red Missing or Failed (the
  host FFT drives meanwhile); blinking amber Opening; amber Closed with the host's FFT. It is
  drawn last, over the flames' light, and slides in from the frame's edge when the housing is
  off it (Field narrower than the tube). The spec's "Silent: dark tube, dim pilot" was dropped:
  a red lamp says it without hiding the tube.
- **The device menu** (spasis's rule, woofer's wiring): "None (host FFT)" = 0, "System
  default" = deviceKey("(default)"), each device by deviceKey(name), "(none)" spares, 16 in
  all; a key not met in this process asks earshot for a name that cannot match, so it says
  Missing (never the default); Rescan re-lists (constructor and event only); `selectDevice`
  only when the value changes.
- **The fallback.** Band-limited noise per band (a deterministic xorshift through a 2-pole
  band-pass, normalised to unit RMS once), its amplitude ramped across the frame from the
  band's last level to this one, levels clamped to [0, 1].
- **Volume** is the SPL (dB re 20 µPa, RMS) a full-scale sine would make as a travelling wave
  in propane: v₀ = √2·p_ref·10^(Volume/20)/(ρc). Typed in the harness and checked to 1e-6.
- **No `.github/workflows/`.** BRIEF.md asks for ci.yml and release.yml; the machine's rule is
  GitHub Actions on public repos only, and this repo has no remote at all. Left out, as woofer
  did; add them (`rutest --offline`, the software subset) when it goes public.

## Defaults

Scaffolded at Volume 128 dB, Gas 30 Pa, Flame 8 cm, Field 1.8 m. Resolume's bundled demo
clips with their own music (`/Applications/Resolume Arena/media/AV`: Bass, Beat and Synth
001–006, each a `.mov` with its `.wav`; and Shop74's 15 clips, fed the Beat stems) went
through the Over with `--pipe --fps 30 --wav`, one still per clip at frame 140, as contact
sheets; the source was fed the same stems with `--wav` (stills, `--film` strips, and a Gas ×
Volume grid of how far each moved the flames). At the
scaffold's defaults the music moved the flames **1.1%** at most: invisible.

Now **Volume 145 dB, Gas 3 Pa, Flame 20 cm, Field 1.9 m** (Length 1.524 m, Pitch 2.2 cm, Hole
0.92 mm, Shimmer 0.5, Distance 6 m, Tube Height 0.2, Exposure 0, Backdrop Grid unchanged; the
flames' half-width cut from 0.09 to 0.05 of their length, which read as a hedge at 20 cm). On
the Bass and Beat stems the kicks flare the flames by **70–85%** at their peak, with a 10–25%
spread along the tube in each frame: the standing wave reads. The Synth stems sit at −34 to
−42 dB with no low end and barely move; left so, because in a set the clips are layered and
the full mix plays, and because quiet high notes are exactly what a real tube ignores (ωM/R:
the holes' mass resists the swing).

**These defaults sit in the reversal regime for loud music**: at 3 Pa almost any beat swings
the holes far past a\* = 4.33, so the antinodes flare. The textbook dip (normal operation)
needs Gas in the tens of pascals or a quieter Volume. An open question below.

Shop74's clips with real alpha (Trinity_09, BattleWeapon_Tank_09, Ethnik2, OrganicMotions_06)
went through the Over: transparent stays transparent away from the tube and flames. The DXV
decodes are mostly premultiplied-consistent but not entirely (some pixels have a channel above
alpha); the Over treats its input as premultiplied.

## Cost

`rutest --bench` in the three verify.sh runs of 2026-11-16, this Mac's GPU shared with other
builds; GL_TIME_ELAPSED median after a 600-frame warm-up and 240 frames per configuration;
wall time includes the duct, earshot's `update()` and the upload. The last run (the one whose
summary is reported) first, the range over the three after it.

| | 720p | 1080p | 4K |
| --- | --- | --- | --- |
| source, GPU median | 1.39 ms (0.72–1.39) | **0.59 ms** (0.57–0.59; 3.6% of 60 fps) | 2.28 ms (2.28) |
| Over, GPU median | 0.80 ms (0.48–0.80) | **0.61 ms** (0.59–0.61; 3.7%) | 2.37 ms (2.37–2.39) |
| wall per frame | 1.9–2.4 ms (1.5–2.4) | 1.6 ms (1.6–1.7) | 3.5–3.6 ms (3.5–3.7) |

160 holes (3 m at 1.9 cm): 0.62–1.18 ms GPU at 1080p. The duct alone: 7.5–7.7 ms per second
of audio (0.13 ms a 60 fps frame) for 69 holes and 35 modes. 720p measuring above 1080p is
the shared GPU's clock state, not the shaders: 720p doubled between two runs of the same
build.

## The traps actually hit

- **`--fps` was both a check and an option.** `rutest --fps` (the frame-rate check) collided
  with `--fps N`; the check is `--rates`.
- **`--set` takes the host's 0..1, not the unit.** A sweep that typed `Gas=3` set the top of
  the range every time and every render came out the same. Convert with the control's map.
- **zsh**: no `PIPESTATUS`, and `set -- $c` does not split words. Every script is bash.
- **ffmpeg in a `while read` loop eats the loop's stdin** (`-nostdin`), and SpaceUniverse_04 has
  only 121 frames (`-stream_loop -1`).
- **The lamp vanished under the flames' light**: it was drawn before the flames' screen blend.
  It is drawn last now, and `--device` (b2) reads it with the housing off the frame.
- **The Over's output was not valid premultiplied colour**: the flames' light added to a
  transparent clip came out 0.287 above its alpha. Fixed by the coverage term and the lift;
  `--alpha` counts pixels with a channel above alpha (must be 0).
- **Modal truncation looked fine at the resonances and was wrong between them.** The
  impulse-response check (1) compares the duct with itself in the frequency domain, so it
  could not see the missing modes; only comparing the amplitude with the shunted line's wave
  solution did (13% near f_c, 2.6% at 20 Hz, zeros at the piston). Hence the remainder and
  (2c). (2c) compares each end against the larger end's magnitude, because p(0) has true zeros
  where a relative error means nothing, and stops at f_c/2: above it, near the first mode left
  out, the truncation is 0.13 and 0.71 off, by construction.
- **Mode 3's flame period is 1% from the closed form, and that is the duct, not the
  picture.** At low Q the neighbours mix in; the duct's own pattern period is 0.5027 m against
  √(f_H² + f_n0²)'s 0.5080. The check reads it at Gas 3 Pa, which sharpens the mode.
- **The crossover lands at 4.43, not 4.33**: the holes' slug inertia leaves the flames ~0.6%
  under g(a) even at 5 Hz, and g's shallow slope near a\* turns that into +2.4% in a.
- **The gulp fraction acos(1/a)/π has an infinite slope at a = 1**, so `--orifice` (2b)
  compares only at a ≤ 0.9 or ≥ 1.2.
- **A mutant containing `||`** broke mutate.sh's `|`-separated table (conway's trap); that
  mutant was replaced.
- **sweep.py split "Source on GitHub event" wrongly** reading `--list`'s columns; it now
  matches on the kind column.
- **The bench's 720p is erratic on a shared GPU**: a 600-frame warm-up at 1080p, then 240
  frames per configuration, helps; it still wanders.
- **The software renderer is slow enough to fail a wall-time limit**: `--device` (d) allows a
  frame 100 ms while a device opens and Apple's software renderer took 113 ms. `--device` is
  left out of the software subset; it is state and one lamp colour, rasteriser-independent.
- **verify.sh's build was incremental**, so its "no warnings" step saw only recompiled files.
  It now clears the build directory unless `KEEP_BUILD=1`.
- **verify.sh filtered away the evidence of each caught mutant**: it kept only "ok … fails
  against the mutant", which a GLSL mutant that stopped the shader compiling would also print.
  It now keeps the check's own FAIL line under each mutant.

### earshot, as a consumer found it (nothing patched)

- **`feedBegin` sets the state Running and the source Device.** Under the harness the lamp is
  green and the tap path runs exactly as for a device; a check that wants the host path must
  feed nothing (`--host-fft`).
- **The tap holds back ~12 samples** of the resampler's lookahead: one second fed gives 15988
  samples, not 16000. It is chunk-invariant (Kaiser sinc at exact fractional positions), so
  `--rates` is bit-identical; a check that counts samples must allow for it.
- **`listDevices()` and `selectDevice(name)` both start CoreAudio** (`ma_context_init`), the
  second even for a name that cannot match: the backend's `open` initialises the context
  before it looks the name up. The `Engine` constructor does not. `RUBENS_NO_DEVICES=1`
  therefore gates both `Refresh` and `Select`: without the second, an oxbow run of a
  composition with a saved device name would talk to CoreAudio. Found by reading earshot
  (`src/Miniaudio.cpp`), not by a prompt.
- **Resolume's FFT buffer is always there**, so with no device chosen earshot's source is Host
  and the lamp is amber, not dark: amber is the normal state.
- **Any `selectDevice` ends a `feed()`** (woofer found it; confirmed): select only when the menu
  value changes.
- **The scripted backend delivers on its own thread**: the first frame after Running can have
  read no samples. `--device` (b) waits 30 ms before it renders.
- **Host band levels are not bounded by 1** (woofer's finding): clamped.
- `update()` runs all of earshot's analysis whatever is read; rubens reads only the tap and the
  bands. Its cost is inside the bench's wall time, not measured separately.

## Verified, and assumed

Verified, by `tools/verify.sh` on 16 November 2026 (numbers in README's Status): every claim
there, at 1280x720 and 320x180 on this Mac's GPU, five picture checks again on Apple's
software renderer; the physics checks with no GL context; the negative controls and mutants;
the sweep; both bundles universal, signed ad hoc, read and rendered by oxbow.

Assumed: propane at 20 °C filling the tube ([GGD] measured it warmer: 256 m/s); C_d = 1; P₀
held by a stiff regulator; holes uniform over the whole length (theirs start 12 cm in); an
ideal velocity piston; mode 0's thermal-only loss; the flame's 50 ms response; Flame
normalised to the current Gas; the plume's shape, the shimmer and the flame colour (looks);
Gas and Volume calibrated against nothing; Resolume's FFT levels anywhere near 0..1.

Not done: loading it into Resolume; a live device; a Windows build; a user guide, presets, an
OpenFX port, a browser demo.

## Would this hold on another rasteriser, at another raster?

One line per check.

- `--resonance`, `--orifice`, `--offres`: no GL; IEEE double on the CPU; run by `--offline`.
  Tolerances from the model (1e-4 against 1.4e-6; 1e-9 for exact algebra; 5% for [GGD]'s own
  4.4%; 2% for a ring-down over 13 windows).
- `--crossover`, `--rates`, `--fallback`: read the flame lengths the plugin uploads (CPU
  state), not the picture. `--rates` is bit-exact anywhere IEEE double is.
- `--spacing`: the flame heights are light-weighted column means off a flames-only render,
  each pixel weighted by its overlap with a one-pitch band; the period by least squares.
  1.5% = one hole pitch over the tube's length. Passes at 320x180 (0.15%) and on the software
  renderer.
- `--refraction`: P read as float from the plugin's own pass (1e-3 of its peak); the
  displacement within 0.1 px + 2% (the central difference over two pixels), only where columns
  are ≥ 8 px wide. The clip read-back within 0.02 px: five steps of the 8-bit sub-texel weight
  common to GPU bilinear filtering; a rasteriser with coarser weights would need it revisited.
  Passes at both rasters and on the software renderer.
- `--alpha`: Mix 0 bit-exact by texelFetch (no filtering anywhere on that path); the
  transparent clip within 1e-5 on RGBA32F; premultiplied validity at 1e-4. Passes on the
  software renderer.
- `--device`: state, and the lamp's colour read off the picture at its known position against
  colour codes with slack; one wall-time limit, which the software renderer's speed breaks (not
  run there).
- `--resize`: bit-exact, same context only; elsewhere the claim is the same frame, not the same
  bits.
- `--clock`: CPU double; `--state`: GL queries; `--names`, `--cues`: no GL.

## The negative controls and mutants

`rutest --negative`, 13 wrong models, each must fail its check:

| check | wrong model |
| --- | --- |
| `--resonance` | the modes without the holes' shunt |
| `--resonance` | the piston's forcing halved |
| `--orifice` | a linear efflux (q = Δp/R): no square root |
| `--offres` | the modes without the holes' shunt |
| `--crossover` | the flames burn the signed flow (inflow subtracted) |
| `--crossover` | a linear efflux |
| `--spacing` | the modes without the holes' shunt |
| `--rates` | the flames follow their fuel once a frame, not once a sample |
| `--fallback` | the fallback reads the (empty) tap |
| `--device` | menu values are positions, not keys |
| `--alpha` | the Over's alpha forced to 1 |
| `--refraction` | the deflection's sign flipped |
| `--cues` | every control ramped between keys |

`tools/mutate.sh`, 25 one-character mutants of the shipped code, each caught (a fresh arm64
build in its own temporary tree per mutant, 320x180, perl alarm 300 s):

| file | mutant | caught by |
| --- | --- | --- |
| Duct.cpp | the holes' end correction taken off the wall, + → − | `--resonance` |
| Duct.h | Rayleigh's end correction misread, 0.8217 → 0.8317 | `--resonance` |
| Duct.h | propane's sound speed misread, 246.59 → 246.69 | `--resonance` |
| Duct.cpp | the shunt's frequency from inertance plus pitch, * → + | `--resonance` |
| Duct.cpp | Kirchhoff's thermal loss with γ + 1, − → + | `--resonance` |
| Duct.cpp | the piston's drive on modes n ≥ 1 by 1/0.6, 0.5 → 0.6 | `--resonance` |
| Duct.cpp | the cap's pressure without the odd modes' sign, − → + | `--resonance` |
| Duct.cpp | the left-out modes' remainder by 3/2, 2 → 3 | `--resonance` |
| Duct.cpp | the quiet flow from 3P₀/ρ, 2 → 3 | `--orifice` |
| Duct.cpp | backward Euler's root scaled by 3/2, 2 → 3 | `--orifice` |
| Duct.cpp | gulping counted while the flow is outward, < → > | `--orifice` |
| Duct.cpp | the substeps' fuel summed, not averaged, / → * | `--crossover` |
| Duct.cpp | the flames' low-pass running away, − → + | `--crossover` |
| Rubens.cpp | Volume's peak velocity from √3 p_rms, 2 → 3 | `--rates` |
| Rubens.cpp | the lamp's blink always on | `--device` |
| Audio.cpp | a new menu choice ignored, == → != | `--device` |
| Audio.cpp | the host's band levels clamped to nothing, 1 → 0 | `--fallback` |
| Shaders.cpp (GLSL) | the column's √π misread, 1.7724539 → 1.8724539 | `--refraction` |
| Shaders.cpp (GLSL) | the column's deficit thinning as b^-2.5, 1.5 → 2.5 | `--refraction` |
| Shaders.cpp (GLSL) | the central difference over three pixels, 2 → 3 | `--refraction` |
| Shaders.cpp (GLSL) | the Over reading its clip against the displacement, + → − | `--refraction` |
| Shaders.cpp (GLSL) | hole i drawn at i / pitch, * → / | `--spacing` |
| Shaders.cpp (GLSL) | the flames' coverage combined with 1 + alpha, − → + | `--alpha` |
| Shaders.cpp (GLSL) | Mix 1 returning the clip untouched, 0 → 1 | `--alpha` |
| Shaders.cpp (GLSL) | the lamp slid in to nine radii, not two, 2 → 9 | `--device` |

## The sweep's honest list

`tools/sweep.py` renders every control at both ends for both plugins: 14 each, 0 dead, 0
barely alive. With `--bare` (no per-control context), **Channel** (needs a stereo WAV whose
channels differ) and **FFT Law** (needs the host FFT path) go dead; everything else acts on
the defaults. Audio Input and Rescan Inputs are not swept (they act on devices); `--device`
covers them.

## Open questions

1. **Which regime should the defaults show?** At Gas 3 Pa and Volume 145 dB loud music is in
   the reversal: antinodes flare. That moves, which is why it was picked on the demo clips;
   the textbook picture (dips at antinodes, normal operation) needs Gas in the tens of pascals.
   No source gives a working tube's overpressure or sound level, so neither is "right".
2. **Flame independent of Gas**: a real tube's quiet flames grow as √P₀. Kept normalised so
   Gas only moves the balance; it could be made physical.
3. **What alpha does Resolume hand an effect?** The Over assumes premultiplied input; the DXV
   demo clips are mostly but not entirely premultiplied-consistent. Needs a look in Arena.
4. **Modes stop at the (1,1) cut-on, 2.78 kHz.** A symmetric piston excites only axisymmetric
   cross-modes, whose first cut-on, (0,1), is 5.8 kHz; the 16 kHz tap (6.4 kHz cutoff) would
   carry the plane-wave modes up to there. Conservative as built.
5. **A pitch-driven end plunger** (Waetzmann's adjustable tubes) would be a second mechanism,
   not this one. Left out.
6. **CI**: BRIEF.md's workflows versus the public-repos-only rule; none until it has a public
   remote.
