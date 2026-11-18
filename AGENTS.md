# AGENTS.md: Rubens

Read this before changing the duct, the holes, the flames' accounting, the plume or composite
shaders, the Over's alpha, the fallback, the device menu or the clock. `CLAUDE.md` has the
commands; the spec is `~/Projects/resolume/specs/SPEC-rubens.md` (its As-built section records
where this repo departs from it).

## Status

**v0.1.0, 2026-11-16; public at `github.com/stoatworks-labs/rubens` from 2026-11-18.**
`OXBOW=… tools/verify.sh` passes on this Mac (Apple
Silicon, macOS 26) against a fresh universal Release build: every check at 1280x720 and
320x180, five on Apple's software renderer, `--offline` with no GL context, the pipe for both
plugins, 13 of 13 negative controls, 25 of 25 mutants, 14 of 14 controls alive in the sweep
for each plugin, oxbow reading and rendering both bundles with devices disabled. **Never
loaded into Resolume; no live audio device ever opened**, by rubens or by earshot. Never built
on Windows before its CI. earshot is a snapshot of `github.com/stoatworks-labs/earshot` at
`8124191`, unpatched.

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
  earshot (`github.com/stoatworks-labs/earshot`), with `SNAPSHOT` saying so and "Patches: none" (verify.sh
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
- **Workflows only once public.** BRIEF.md asks for ci.yml and release.yml; the machine's rule
  is GitHub Actions on public repos only. Until it was public rubens shipped none, so nothing
  could run by accident; `ci.yml` and `release.yml` came with the public repo (mirrorball's,
  in haze's shape for two plugins, with woofer's `wincheck.ps1`). The software-renderer subset
  stays in verify.sh: a runner's GL is not Apple's software renderer.

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
6. ~~**CI**~~: settled when the repo went public (2026-11-18): `ci.yml` and `release.yml`.

## Browser demo

https://rubens-demo.stoatworks-labs.com/ (2026-11-18), from `demo/`, deployed by
`wrangler.toml` (a Worker route: the zone is at Cloudflare's 100 custom domains, so
`rubens-demo` is a proxied AAAA `100::` record made through the API) and
`.github/workflows/deploy.yml` on every push to main. woofer's pattern, with mirrorball's two
variants (SW Rubens and SW Rubens Over on one page, `?plugin=over` for the Over).

- **It runs the plugin, it doesn't port it.** `demo/rubens-core.wasm` is `Rubens.cpp` and
  everything it calls (Diag, Controls, Duct, Audio, PassBuffer, Shaders), earshot's snapshot
  (every `src/*.cpp` but `Miniaudio.cpp`/`MiniaudioImpl.cpp`, with `NoCapture.cpp`: its own
  `EARSHOT_CAPTURE=OFF` build, so the module has no device backend at all) and the FFGL SDK's
  plugin classes, FFGLLog, FFGLShader, FFGLFBO, the screen quad, the scoped bindings and
  FFGLUtilities, compiled unmodified by emscripten (`demo/tools/build-wasm.sh`). The duct, the
  holes' jets, the flames, the fallback and earshot's resampler and tap are that C++. Left out:
  `SourcePlugin.cpp` and `EffectPlugin.cpp` (the registrations; the glue constructs
  `RubensPlugin(false/true)` itself). The script fails if a `source/*.cpp` or an earshot
  `src/*.cpp` it doesn't list appears.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs either plugin, reads
  its declarations back through the SDK so the panel is built from them, forwards
  SetFloatParameter, SetTime, the Audio buffer's elements and ProcessOpenGL as rutest's rig
  does, and hands the page the harness's accessors: holes, pitch, modes, f_H, cut-on, the
  flames' min/max/mean and each one, v0, tap samples a frame, earshot's state, the plugin's
  clock); `demo/wasm/gl_shim.cpp` takes five entry points: glShaderSource (refusing any text
  that isn't byte for byte one of the page's four assemblies of `demo/shaders.js`, then the
  kit's `port()`), glEnable/glDisable/glIsEnabled (GL_PROGRAM_POINT_SIZE, which WebGL2 lacks)
  and glTexImage2D (see the traps); the host row; the read-outs' units, tooltips, the nine
  presets (in dB, metres, pascals and stops, through the plugin's own inverses via
  `ru_param_from`), the regime line, the status line and Diag's log from emscripten's memory
  file system (`RUBENS_LOG_DIR=/rubens/logs`). Off the panel: Audio Input, Rescan Inputs, the
  Audio buffer and the About block (the panel's note says so). Channel and FFT Law stay on it,
  with tooltips saying when each acts (a stereo file whose channels differ; "None (host FFT)").
- **The audio, never a device.** The page never calls a device API (`check_shaders.py` fails if
  a served file names getUserMedia, enumerateDevices or mediaDevices), the module cannot open
  one, and `ru_prepare` sets `RUBENS_NO_DEVICES=1` so `AudioInput::Refresh` never asks. The
  page never sends Audio Input either: SetFloatParameter on it calls `audio.Select`, which ends
  a feed. The host row's choices are rutest's: the track (`musicAt` and its five helpers copied
  from `tools/rutest/main.cpp` into the glue, held character for character by
  `check_shaders.py`, with kFeedRate and the `--host-fft` loop's lines), `--silent` (earshot
  goes Silent after 3 s, the lamp steady red) and `--host-fft` (nothing fed; the fallback on
  the synthetic spectrum, the lamp amber); plus a file of the visitor's, decoded by
  `decodeAudioData` in an `OfflineAudioContext` at 48 kHz (no output device), mono to both
  channels, fed through `ru_feed` and looped (rutest --wav plays once). Each frame feeds the
  samples up to the page's clock × 48 000. Nothing is played aloud. Every change of audio or
  of plugin is a new instance from time 0 (a feed lasts until a select, which nothing here
  calls), and the page's default is the track where the plugin's own is "None (host FFT)".
- **The clock.** The page sends seconds from frame deltas (capped at 0.1 s). Each new instance
  (load, a change of plugin or audio, Restart) is primed with five frames about a millisecond
  apart so the plugin's seconds-or-milliseconds vote settles before the first frame shown
  (mirrorball's fix): nothing is fed during them, so the flames start from rest and a paused
  page stands still. Until the vote settles the plugin's clock is the wall clock, and the
  shimmer's clock is the sum of its frame steps, so each instance's shimmer starts 3-4 ms on,
  never the same twice: two Restarts at the defaults differ in the plumes' noise (18,587 bytes
  at 640×360, up to 218 codes); at Shimmer 0 they are identical. rutest presets its clock and
  never takes those steps. On the host-FFT choice the fallback's band noise runs on the same
  clock.
- **The regime, said plainly.** At the defaults (Gas 3 Pa, Volume 145 dB) loud music flares the
  flames where the pressure swings most (rutest, 320×180, 330 frames of the track: 13.5-28.1 cm
  against a quiet 20; the page's frame at 5.5 s gives the same 13.45-28.11), the reversal
  Ficken & Stephenson reported as [Gee] relates it. The presets "Gas 30 Pa" (16.6-19.5 cm) and
  "Volume 130 dB" (16.2-19.8) are the right way up, chosen with rutest; Gas 100 and 300 Pa and
  Volume 120 and 125 dB barely move the flames. A line under the picture says so, and the
  status line reports which way round the last two seconds were. No default was changed.
- **Photosensitivity.** The page says at the top that the flames jump with the music's beats
  and that the measured figures are in the user guide, and that Silence or Pause stops it. No
  default was changed for it, and there is no open-paused rule. Embed mode (`?embed=1`) does
  not show the note.
- **Checked by `tools/verify.sh`** ("Browser demo's copies", `demo/tools/check_shaders.py`):
  the five GLSL pieces character for character and the order InitGL and `Assemble()` join them
  in for the four stages; every uniform set by the setter its type takes and nothing undeclared
  set (plume 14 by 14 calls, composite 21 by 22: MaxUV is set twice; `geometryUniforms( shader,
  ... )`'s calls are counted for each shader that calls it); rutest's track and host-FFT lines
  in the glue; no served file naming a device API; the hashes of every input the .wasm was
  built from (`demo/wasm/inputs.sha256`, 64 files) and the SDK pin. Any change under `source/`
  or `external/earshot/` needs `demo/tools/build-wasm.sh` rerun and its outputs committed (mode
  644).
- **Compared with rutest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built rutest). Headless Chrome on
  the real GPU (ANGLE on Metal), the page paused, the settings applied, Restart, then N presses
  of Step (800 N samples fed, the clock at N/60); for the Over, Mix 0 hands the clip back so the
  canvas is the exact input frame; `rutest --pipe --fps 60 [--over]` gets that frame (black for
  the source) N times with the same host values, and its last frame is compared. Apple M4 Max,
  640×360 (691,200 colour values a case):

  | case | clip | N | colour values off | max |
  | --- | --- | --- | --- | --- |
  | source, Shimmer 0, the track | scene | 330 | 11 | 1 code |
  | Over, Shimmer 0 | scene | 240 | 1,668 | 1 code |
  | source, Gas 30 Pa, Dots, Shimmer 0 | scene | 300 | 3 | 1 code |
  | Over, 130 dB, Distance 15 m, Mix 50%, Shimmer 0 | bars | 200 | 18 | 1 code |
  | source, 3 m tube, 0.5 mm holes, Black, +1 stop, Shimmer 0 | scene | 260 | 0 | 0 |
  | Over, shape on transparency, Field 0.8 m, Tube Height 0.3, Shimmer 0 | alpha | 180 | 1,610 | 1 code |
  | source, silence, Shimmer 0 | scene | 120 | 5 | 1 code |
  | source, the defaults, Shimmer 0.5 (reported, not judged) | scene | 330 | 66,463 | 195 codes |
  | source, None (host FFT), Shimmer 0 (reported, not judged) | scene | 120 | 99,761 | 201 codes |

  Alpha identical but for one code on the alpha clip. The negative control (rutest with Gas one
  slider step, 0.001, off on the first case) puts 33,363 values off, up to 199 codes. So on the
  feed path the wasm duct lands on the Mac's to within the GPU's last code, and the page's
  flames on the first case (13.45-28.11 cm) are rutest's (13.5-28.1). The two unjudged cases differ for the clock reason above.
- **Also checked on the page** (CDP, the real GPU): the defaults read back as the plugin's
  (L+R, Linear Mag, 145.0 dB, 1.524 m, 2.20 cm, 0.92 mm, 3.00 Pa, 20.0 cm, 0.50, 6.0 m, 1.90 m,
  20% up, 0.00 stops, Grid; the Over has Mix 100% in place of Backdrop); the flames move with
  the track (the tallest swinging between about 19 and 64 cm a few times a second at the defaults), the
  lamp is green on the feed, steady red on zeros, amber on the host FFT; Flame changes the
  picture paused, Gas changes it playing (the negative control above; paused it cannot, since
  the flames follow the samples fed); a WAV chosen through the file input decodes and drives
  the flames; all nine presets render on both plugins with no GL error, and a Restart after
  "No shimmer" gives the same first frame before and after another preset; the Plugin menu
  starts a new instance from time 0 at that plugin's defaults; Copy link carries
  `plugin=over`; all four stages the compiled plugin handed to glShaderSource were the page's
  (none refused); no horizontal scroll at 390 px; no console errors.
- **Every gap, as the page discloses it** (banner, the panel's note, the host row's tooltips,
  the "does not reproduce" list): it runs the plugin, not a port, and the units, tooltips and
  presets are the page's; no audio device or microphone, earshot built without capture, Audio
  Input, Rescan Inputs and the Audio buffer off the panel, the live-device path absent; the
  audio is the harness's track or a file decoded, resampled to 48 kHz and looped in the page,
  never played aloud; Channel acts only on a stereo file, FFT Law only on the host FFT; "None
  (host FFT)" runs on a synthetic spectrum, not a real FFT, and is uncalibrated; every change of
  audio or plugin is a new instance from time 0, and the page's default audio is not the
  plugin's; the defaults flare rather than dip; GL is emscripten's WebGL2 with five shimmed
  entry points and the kit's GLSL port; the clock is the page's and primed, the shimmer carries
  the prime, and a paused control move feeds nothing; a frame is a display refresh (the feed
  path does not depend on it, the fallback does); WebAssembly has no fused multiply-add and
  musl's libm; the Over's clip is the kit's, premultiplied, its whole texture; the warning is
  the page's; the About block is off the panel; nothing is measured, and the plugin has never
  been in Resolume or opened a device.
- **Traps hit**: the FFGL SDK's FFGLFBO allocates the R32F plume with a `GL_RGBA` client format
  and no data, which desktop GL takes and WebGL2 refuses (OpenGL ES 3.0 pairs R32F only with
  `GL_RED`/`GL_FLOAT`), so the shim's glTexImage2D gives R32F `GL_RED` when there is no
  data, passes the hole table's RGBA32F upload through, and aborts on anything else;
  `needFloat` makes the kit enable EXT_color_buffer_float or refuse to start, since R32F is a
  render target. Rubens.cpp sets its geometry uniforms in a helper that takes the shader as a
  parameter, so the uniform check expands that helper into each caller. A paused Gas move
  changes nothing on screen (the jets act on samples as they are fed), which looks like a dead
  control and is not: Flame, which sets the quiet height directly, does change a paused frame.
  The kit's Composition select is blank with `?size=` (not one of its presets), which is the
  kit's, not a fault.
