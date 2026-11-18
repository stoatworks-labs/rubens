# rubens

Rubens' flame tube for Resolume Arena/Avenue, as two FFGL plugins from one core: a 1-D
propane duct driven at 16 kHz by the audio waveform from earshot (up to 64 modes, each an
exact three-state ZOH system, plus a closed-form remainder for the modes left out), the holes
as Bernoulli jets with inertia integrated per sample, the flames following their outward fuel
flow, and the plumes above them refracting the backdrop (source) or the clip (Over) by
line-of-sight sums, schlieren's way. C++/GLSL, CMake MODULE → universal `.bundle`s (macOS) +
Windows `.dll`s (built on GitHub's Windows runner). MIT. Public:
`github.com/stoatworks-labs/rubens`.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `RU01` | `SW Rubens` | `Rubens.bundle`, `com.stoatworks.ffgl.rubens` | source |
| `RU02` | `SW Rubens Over` | `Rubens Over.bundle`, `com.stoatworks.ffgl.rubens.over` | effect |

Read `AGENTS.md` before touching the duct (`Duct::build`, `Duct::Process`, `Duct::Residual`),
the holes' integrator, the flames' accounting (outward flow only), the plume and composite
shaders, the Over's alpha, the fallback (`AudioInput::Pull`), the device menu
(`AudioInput::Refresh`/`Select`) or the clock.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build`. Never from a shared session on this Mac: Allan
  may be using Arena. The release check loaded the CI's bundles once, under the `arena-mac`
  lock, and took them out again.
- Render offline: `./build/rutest --out /tmp/r.png` (the source at its defaults, fed the
  harness's track through earshot's `feed()`, 3 s in); `--over` for the Over on the harness's
  card, `--clip F` (raw RGBA at `--size`) for your own. The summary gives the holes, modes,
  f_H, the flames' range and how far the run moved them.
- Your own audio: `--wav song.wav` (16/24/32-bit PCM or float), `--silent`, `--host-fft`
  (nothing fed: Resolume's FFT buffer only, a synthetic kick spectrum)
- List parameters: `./build/rutest --list` (`--over`: the effect's)
- Set anything by name: `./build/rutest --set "Gas=0.5" --set "Backdrop=Dots"` (a standard
  control takes the host's 0..1, not its unit; an option takes its name). `--at N "Name=V"`
  sets it at frame N.
- Film: `./build/rutest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through the Over: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/rutest --over --pipe --fps 30 --size WxH --wav in.wav | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
  A partial last frame is dropped (said on stderr); a reader that hangs up ends it with exit
  1, not SIGPIPE's 141. How far a WAV moves the flames: `--out` with `--wav` prints it.
- The harness's track as a WAV: `./build/rutest --write-wav t.wav 4`
- Browser demo (`demo/`, AGENTS.md "Browser demo"): after ANY change under `source/` or
  `external/earshot/` rerun `demo/tools/build-wasm.sh` and commit its outputs, and after a
  shader change `demo/tools/check_shaders.py --write`; verify.sh fails until you do.

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` under bash (GLSL reserved words and
  WebGL2 gaps, `glslc`, the submodule pin, the earshot snapshot, a fresh universal build with
  no warnings outside `external/`, lipo, plist, ad-hoc signature, `oxbow probe` and
  `selftest` for both bundles -- oxbow is required, never skipped; every check at 1280x720 and
  320x180; five again on Apple's software renderer; `--offline`; `--pipe` for both plugins;
  the negative controls; 25 mutants; the sweep; the bench). The build directory (default
  `build-verify/`) is cleared first; `KEEP_BUILD=1` reuses it. About 5 minutes.
- One check at a time, each at both rasters (`--size WxH`: that raster alone):
  - **Physics, CPU only** (also under `--offline`, no GL context): `--resonance` (the
    duct's impulse response against its own continuous model through the ZOH; lossless poles
    against √(f_H² + f_n0²) and literal f_H; the amplitude against the shunted line's wave
    solution; [GGD]'s Table II; a ring-down; Kirchhoff's wall loss), `--orifice` (one hole's
    impedance; the mean outflow against g(a); the gulp fraction), `--offres` (between two
    modes against on one, against the derived dip).
  - **End to end** (the plugin, `feed()`): `--crossover` (5 Hz, Gas 30 Pa, Volume stepped:
    each flame against g(a) at its hole's swing; a\* = 4.33 ± 5%), `--rates` (24/30/60/240
    fps bit-identical; what Volume means), `--fallback` (Resolume's FFT only).
  - **Picture**: `--spacing` (the flames' period off the rendered frame at modes 3, 10, 14
    against c/(2f√(1 − f_H²/f²))), `--refraction` (the optical path against the columns'
    integral; the displacement against Distance × ∇P; where the Over reads its clip),
    `--alpha` (Mix 0 exact; a transparent clip stays transparent; premultiplied valid; the
    source opaque), `--resize` (a raster change mid-run equals a fresh instance), `--clock`
    (499 million ms), `--state` (the host's GL state given back).
  - **Audio**: `--device` (the menu, states, lamp and slide-in on a scripted backend).
  - **Plumbing, no GL**: `--names`, `--cues`.
- The checks can fail: `./build/rutest --negative` (13 wrong models), `tools/mutate.sh`
  (25 mutants, arm64, 320x180, perl alarm at 300 s; its own build tree, never a shared one).
- No dead controls: `python3 tools/sweep.py --build build` (`--bare` drops the contexts).
- Render cost: `./build/rutest --bench` (720p, 1080p, 4K, 160 holes, the duct alone).
- `RUTEST_RENDERER=software` runs any of it on Apple's software renderer.
- **No step opens an audio device.** rutest sets `RUBENS_NO_DEVICES=1` and installs earshot's
  scripted backend only for `--device`; verify.sh exports it for oxbow and the sweep. With it
  set the plugin neither lists devices nor looks a saved name up (earshot's lookup starts
  CoreAudio). A microphone prompt in a test run is a bug.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **The physics is on the CPU in double at the tap rate** (16 kHz): the modes discretised
  exactly, the holes by backward Euler solved in closed form, two substeps a sample, the
  flames' fuel low-passed per sample (τ 50 ms). Every tap sample is used once; the flames
  depend on the audio, never on how it was chunked into frames.
- **The GPU only draws**: the CPU uploads a holes×1 RGBA32F texture (flame length, aeration);
  the plume pass sums the Gaussian columns into an R32F optical path; the composite takes its
  gradient, samples the backdrop or clip displaced, adds the flames, draws the tube and lamp.
  Shader time is relative to the frame; phases are reduced in double on the CPU.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index,
  except Audio Input, whose values are `earshot::deviceKey(name)`.
- At most 160 holes (past that the pitch widens to L/160) and 64 modes (a 3 m tube stops at
  2.6 kHz, not the 2.78 kHz cut-on).
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `rubens_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- `external/earshot/` is a snapshot of earshot (`github.com/stoatworks-labs/earshot`) at
  `8124191`, unpatched (`SNAPSHOT`). Never edit it here; change earshot upstream and take a new
  snapshot. Its own CLAUDE.md and AGENTS.md are earshot's as of that commit, status lines and all.
- **Generated, never edited here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's projects.json), `ATTRIBUTIONS.md` (`sync-attributions.py`,
  from the backend's `attributions/derived.json`), `.github/FUNDING.yml` and
  `.github/ISSUE_TEMPLATE/` (the fleet's syncs). Change them there and re-sync. The About block
  has the User guide button, so each plugin declares 22 parameters.
- **CI** (`.github/workflows/ci.yml`): macOS builds arm64 with the tools and runs `--list`,
  `--offline` and `glslc`; Windows builds both DLLs with MSVC and loads each as a host does
  (`tools/wincheck.ps1`, whose `-Params` changes whenever a control or the About block does).
  `release.yml` builds the universal bundles and the DLLs with their zips, `.dmg` and
  installer; dispatched by hand it builds without publishing, and a `v*` tag publishes.
  Actions only because the repo is public: a private copy must never enable them.
- Checkout: `~/Projects/resolume/rubens`, public at `github.com/stoatworks-labs/rubens`. The
  spec is `~/Projects/resolume/specs/SPEC-rubens.md`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/rubens/rubens.YYYY-MM-DD.log
    %LOCALAPPDATA%\rubens\logs\rubens.YYYY-MM-DD.log

`RUBENS_LOG_DIR` overrides the folder.
