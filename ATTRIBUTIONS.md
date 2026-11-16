# Attributions

Rubens is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-16).** Rubens is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without
their work.

### Live audio — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot> (intended home; on 2026-11-16 a local repository only)  
Licence: MIT  
Copyright: Stoatworks

Vendored at external/earshot as a snapshot of commit 8124191, unmodified (external/earshot/SNAPSHOT). The capture device worker, the device list and keys, the Silent state, the 16 kHz waveform tap the duct is integrated from, the scripted capture backend the harness drives, and the fallback to Resolume's FFT bins. earshot's own attributions (spasis, pattern, miniaudio) are in external/earshot/ATTRIBUTIONS.md.

### The device menu and the microphone-permission explanation — Stoatworks spasis

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The Audio Input option whose values are device-name keys, so a composition remembers the name and never the index; a missing device shown as missing, never swapped for the default; the "(none)" spare slots and Rescan Inputs (source/Audio.cpp, source/Controls.cpp, source/Rubens.cpp); and the README's account of macOS giving a host silence, not an error, when the microphone is refused. Adapted.

### The plugin shell, the harness and the tooling — Stoatworks haze, and woofer's earshot wiring

<https://github.com/stoatworks-labs/haze> (local only on 2026-11-16)  
Licence: MIT  
Copyright: Stoatworks Labs

Two plugins on one OBJECT core with an `isEffect` flag, the table of controls, the host-clock vote, the sRGB encode and the tone curve; the harness's rig, PNG writer, cue sheets, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout exits 1), `--negative` with its perturbation hooks, `--state`, `--names`, `--cues`, `--offline` and `--bench`; tools/verify.sh, tools/mutate.sh, tools/glslc.sh and tools/sweep.py are haze's (ferrofluid's before it), adapted. The WAV reader, the feed through earshot's `feed()` and the rule that only a changed menu value reaches `selectDevice` are woofer's (local only), read and adapted. scripts/release-lib.sh is the fleet's, unchanged.

### Background-oriented schlieren — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The Gladstone–Dale constant and the air's n₀ − 1 (checked there against Edlén), the deflection as a central difference of the optical path over two pixels, and the backdrop seen displaced by Distance times the deflection (schlieren's Background mode); the harness's look card. Adapted.

### The pass buffers, the log and the GL state guard — Stoatworks millpond and vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/millpond>, <https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's; source/GLState.h is radar's, which is vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back. The About block (source/StoatworksAbout*.h) is a provisional hand copy of haze's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### miniaudio — David Reid

<https://github.com/mackron/miniaudio>  
Licence: public domain (the Unlicense) or MIT No Attribution, at your choice  
Copyright: David Reid

Arrives inside the earshot snapshot at external/earshot/third_party/miniaudio, unmodified. Capture device enumeration and the audio callback.

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

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth
saying out loud. Every author list below was checked on 2026-11-16 against the paper, its
arXiv page or its Crossref record.

### The tube's resonances — Gardner, Gee & Dix 2009

Michael D. Gardner, Kent L. Gee, Gordon Dix, "An investigation of Rubens flame tube resonances", J. Acoust. Soc. Am. 125(3) (2009) 1285–1292, doi:10.1121/1.3075608. The holes as acoustic masses that shift the resonances upward, most for the lowest modes, and the flame spacing that exceeds c/2f there; their Table II is what `rutest --resonance` (3) compares the shunted closed form against.

### The tube's history — Gee, "The Rubens tube"

Kent L. Gee, "The Rubens tube", Proceedings of Meetings on Acoustics 8 (158th ASA meeting, 2009; published 2010) 025003, doi:10.1121/1.3636076. Rossing's yellow and blue flames and his 140–150 dB, and Ficken & Stephenson's Bernoulli model and gulping, both second-hand through it. Ficken & Stephenson themselves (George W. Ficken, Francis C. Stephenson, Phys. Teach. 17(5) (1979) 306–310, doi:10.1119/1.2340232) are paywalled and were not read.

### Flame length and fuel flow — Sunderland, Haylett, Urban & Nayagam 2008

Peter B. Sunderland, James E. Haylett, David L. Urban, Vedha Nayagam, "Lengths of laminar jet diffusion flames under elevated gravity", Combust. Flame 152 (2008) 60–68, doi:10.1016/j.combustflame.2007.08.011. A laminar jet flame's length goes as its fuel mass flow, so a flame's length here is its quiet length times its flow over the quiet flow.

### The holes' end correction — Brandão & Schnitzer 2020

Rodolfo Brandão, Ory Schnitzer, "Acoustic impedance of a cylindrical orifice", arXiv:2001.04904 (2020), §1: Rayleigh's long-orifice end correction 0.8217 r at each end, in the holes' inertance.

### The walls' losses — Berggren, Bernland & Noreland 2018

Martin Berggren, Anders Bernland, Daniel Noreland, "Acoustic boundary layers as boundary conditions", arXiv:1801.04177 (2018), Eqs. (39) and (48): Kirchhoff's visco-thermal loss in a circular duct, which `rutest --resonance` (4b) checks each mode's damping against.

### Propane — the NIST Chemistry WebBook

NIST Standard Reference Database 69, Thermophysical Properties of Fluid Systems, propane at 0.101325 MPa (Eric W. Lemmon, Mark O. McLinden, Wolfgang Wagner's equation of state, J. Chem. Eng. Data 54(12) (2009) 3141–3180, as NIST cites it; not read). The sound speed, density, heat capacities, viscosity and conductivity at 20 °C in source/Duct.h.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is linearised before the flames' light is added, then encoded again.
- **The decibel re 20 µPa** — Volume's meaning (source/Rubens.cpp).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
