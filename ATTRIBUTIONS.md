# Attributions

Rubens is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### earshot — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot>  
Licence: MIT  
Copyright: Stoatworks Labs

external/earshot/ is earshot, the fleet's shared audio front end, as a snapshot: one commit taken with git archive and left unmodified, named in external/earshot/SNAPSHOT. It is built into the plugin by add_subdirectory and supplies the capture device, chosen by name with a Silent state, and the audio analysis the plugin reads every frame. It brings miniaudio with it, credited below.

### The device menu and the microphone-permission explanation — Stoatworks spasis

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The Audio Input option whose values are device-name keys, so a composition remembers the name and never the index; a missing device shown as missing, never swapped for the default; the "(none)" spare slots and Rescan Inputs (source/Audio.cpp, source/Controls.cpp, source/Rubens.cpp); and the README's account of macOS giving a host silence, not an error, when the microphone is refused. Adapted.

### The plugins' shape, the harness and the tooling — Stoatworks haze, from ferrofluid

<https://github.com/stoatworks-labs/haze>  
Licence: MIT  
Copyright: Stoatworks Labs

Two plugins on one OBJECT core with an isEffect flag, the table of controls, the host-clock vote, the sRGB encode and the tone curve; the harness's rig, PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout exits 1), --negative with its perturbation hooks, --state, --names, --cues, --offline and --bench; tools/verify.sh, tools/mutate.sh, tools/glslc.sh and tools/sweep.py are haze's (ferrofluid's before it), adapted. scripts/release-lib.sh is the fleet's, unchanged.

### The WAV reader and the feed through earshot — Stoatworks woofer

<https://github.com/stoatworks-labs/woofer>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's WAV reader, the feed through earshot's feed(), and the rule that only a changed menu value reaches selectDevice are woofer's, read and adapted.

### Background-oriented schlieren — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The Gladstone–Dale constant and the air's n₀ − 1 (checked there against Edlén), the deflection as a central difference of the optical path over two pixels, and the backdrop seen displaced by Distance times the deflection (schlieren's Background mode); the harness's look card. Adapted.

### The pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's. Adapted.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is radar's, which is vectrix's, which lifted it from resolume-scopes' Scopes.cpp: the host's GL state captured and put back.

### The release workflow and the DLL check — Stoatworks mirrorball, haze and woofer

<https://github.com/stoatworks-labs/mirrorball>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are mirrorball's, which are haze's (xenon's, with isohypse's two-plugin parts): universal bundles and Windows DLLs, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads each DLL as a host loads it and reads back its name, id, type and parameters, is woofer's (mirrorball's, pinhole's before them). Adapted.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### miniaudio, inside earshot

<https://github.com/mackron/miniaudio>  
Licence: Public Domain (Unlicense) or MIT-0, at your choice  
Copyright: David Reid

Arrives inside the vendored earshot snapshot, at external/earshot/third_party/miniaudio/miniaudio.h, unmodified. Not vendored separately.

earshot's capture backend: it lists the capture devices and runs the audio callback, on CoreAudio and WASAPI. The plugin never calls miniaudio itself; it arrives only with earshot.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The tube's resonances: Gardner, Gee & Dix 2009 — Michael D. Gardner, Kent L. Gee and Gordon Dix

<https://doi.org/10.1121/1.3075608>

"An investigation of Rubens flame tube resonances", J. Acoust. Soc. Am. 125(3) (2009) 1285–1292. The holes as acoustic masses that shift the resonances upward, most for the lowest modes, and the flame spacing that exceeds c/2f there; their Table II is what rutest --resonance (3) compares the shunted closed form against. The tube's bore and wall are theirs.

### The tube's history: Gee, "The Rubens tube" — Kent L. Gee

<https://doi.org/10.1121/1.3636076>

Proceedings of Meetings on Acoustics 8 (158th ASA meeting, 2009; published 2010) 025003. Rossing's yellow and blue flames and his 140–150 dB, and Ficken & Stephenson's Bernoulli model and gulping, both second-hand through it. Ficken & Stephenson themselves (George W. Ficken and Francis C. Stephenson, Phys. Teach. 17(5) (1979) 306–310, doi:10.1119/1.2340232) are paywalled and were not read.

### Flame length and fuel flow: Sunderland, Haylett, Urban & Nayagam 2008 — Peter B. Sunderland, James E. Haylett, David L. Urban and Vedha Nayagam

<https://doi.org/10.1016/j.combustflame.2007.08.011>

"Lengths of laminar jet diffusion flames under elevated gravity", Combust. Flame 152 (2008) 60–68. A laminar jet flame's length goes as its fuel mass flow, so a flame's length here is its quiet length times its flow over the quiet flow.

### The holes' end correction: Brandão & Schnitzer 2020 — Rodolfo Brandão and Ory Schnitzer

<https://arxiv.org/abs/2001.04904>

"Acoustic impedance of a cylindrical orifice", arXiv:2001.04904 (2020), §1: Rayleigh's long-orifice end correction 0.8217 r at each end, in the holes' inertance.

### The walls' losses: Berggren, Bernland & Noreland 2018 — Martin Berggren, Anders Bernland and Daniel Noreland

<https://arxiv.org/abs/1801.04177>

"Acoustic boundary layers as boundary conditions", arXiv:1801.04177 (2018), Eqs. (39) and (48): Kirchhoff's visco-thermal loss in a circular duct, which rutest --resonance (4b) checks each mode's damping against.

### Propane: the NIST Chemistry WebBook — NIST

<https://webbook.nist.gov/chemistry/fluid/>

NIST Standard Reference Database 69, Thermophysical Properties of Fluid Systems, propane at 0.101325 MPa (Eric W. Lemmon, Mark O. McLinden and Wolfgang Wagner's equation of state, J. Chem. Eng. Data 54(12) (2009) 3141–3180, as NIST cites it; not read). The sound speed, density, heat capacities, viscosity and conductivity at 20 °C in source/Duct.h.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Rubens' flame tube

The physics classroom's standing wave drawn in flame: a pipe of propane with a row of holes along its top and a loudspeaker at one end. Built from how the thing works and from the papers above; nothing is copied from anyone's software, visualiser or demonstration.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is linearised before the flames' light is added, then encoded again.
- **The decibel re 20 µPa** — Volume's meaning (source/Rubens.cpp, source/Controls.h): the RMS sound pressure level a full-scale sine makes as a travelling wave in the gas.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
