#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# The build is universal on purpose. An arm64-only bundle builds and tests
# perfectly well here and then fails to load in an Intel Resolume, and the
# build log calls it a success either way -- so the architecture is checked
# with lipo, never with the log.
#
#     tools/verify.sh [BUILD_DIR]
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

# No step here may open an audio device. The plugin, oxbow's copy of it and
# rutest all read this: the plugin never lists the OS's devices and never opens
# or even looks one up (the lookup itself starts CoreAudio); rutest feeds its
# audio through earshot's feed() and tests the menu on a scripted backend.
export RUBENS_NO_DEVICES=1

# A host has to read the bundles, so oxbow is required, not optional: a run
# without it would print "skipped" and call itself green.
OXBOW="${OXBOW:-}"
[[ -n "$OXBOW" && -x "$OXBOW" ]] || fail "set OXBOW to a built oxbow binary (cmake -S ~/Projects/resolume/oxbow -B <scratch>/oxbow-build, never inside ~/Projects)"

#---------------------------------------------------------------------------
# GLSL reserved words, declared as identifiers.
#
# glslc catches these, but glslc is optional -- so the trap most likely to be
# walked into gets its own grep that always runs. The list is GLSL 4.10's
# keywords reserved for future use (3.6) plus the ones already bitten:
# `packed` passed Apple's compiler and glslc and failed only Mesa (atrac);
# noise1..noise4 are built-ins a user function is silently shadowed by (dice).
#---------------------------------------------------------------------------
reserved_words() {
	local words="common partition active asm class union enum typedef template this resource goto inline noinline
	             public static extern external interface long short half fixed unsigned superp input output filter
	             sizeof cast namespace using packed sample patch layout flat smooth noperspective precise
	             noise1 noise2 noise3 noise4"
	local bad=0 word
	for word in $words; do
		if grep -nE "(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])[[:space:]]+$word[[:space:]]*[;=,)(]" \
		            source/Shaders.cpp >/dev/null 2>&1; then
			printf '   "%s" is declared as an identifier and is a GLSL reserved word\n' "$word"
			bad=$(( bad + 1 ))
		fi
	done
	[ "$bad" -eq 0 ] && printf '   none of the reserved words is used as an identifier\n'
	return "$bad"
}

step "GLSL reserved words"
reserved_words || fail "a GLSL reserved word is used as an identifier"

#---------------------------------------------------------------------------
step "GLSL: nothing WebGL2 lacks"
#---------------------------------------------------------------------------
# The fleet's browser demos run the shaders unedited in WebGL2 (GLSL ES 3.00),
# and the kit's port() translates spellings, never maths. Rubens has no demo yet;
# the rule is kept so one can be made without rewriting a shader. These
# built-ins arrived in GLSL 4.00 / ES 3.10 and compile everywhere here but
# there: findMSB was in ferrofluid's panel shader until 2026-10-06.
if grep -nE '(^|[^A-Za-z_])(findMSB|findLSB|bitCount|bitfieldExtract|bitfieldInsert|bitfieldReverse|uaddCarry|usubBorrow|umulExtended|imulExtended|frexp|ldexp|fma|textureGather|textureQueryLevels|packUnorm4x8|unpackUnorm4x8)[[:space:]]*\(' source/Shaders.cpp | grep -vE '^[0-9]+:[[:space:]]*//' ; then
	fail "a built-in GLSL ES 3.00 does not have is in the GLSL"
fi
echo "   none"

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
tools/glslc.sh || fail "a shader does not compile"

#---------------------------------------------------------------------------
step "Submodule"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
pin="$(git -C external/ffgl rev-parse --short=7 HEAD)"
[[ "$pin" == "b1afaf9" ]] || fail "FFGL SDK at $pin, not the fleet's b1afaf9"
echo "ok   FFGL SDK pinned at $pin"
# earshot is a snapshot, not a submodule: the commit it came from and that it
# is unpatched are written in it, and AGENTS.md repeats both.
grep -q '^Commit:   8124191 ' external/earshot/SNAPSHOT || fail "external/earshot/SNAPSHOT is not earshot 8124191"
grep -q '^Patches:  none' external/earshot/SNAPSHOT || fail "external/earshot/SNAPSHOT records a patch: AGENTS.md must say what and why"
echo "ok   earshot snapshot 8124191, unpatched"

#---------------------------------------------------------------------------
step "Build (universal)"
#---------------------------------------------------------------------------
log="$( mktemp )"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$log" 2>&1 || { cat "$log"; fail "configure"; }
cmake --build "$BUILD" -j8 >"$log" 2>&1 || { tail -40 "$log"; fail "build"; }
# The SDK's own FFGLUtilities.cpp warns about RAND_MAX, and earshot's
# miniaudio is earshot's; anything else is ours.
if grep -E 'warning:' "$log" | grep -v 'external/' | grep -q .; then
	grep -E 'warning:' "$log" | grep -v 'external/' | sed 's/^/   /'
	fail "a warning in this repo's code"
fi
rm -f "$log"
echo "ok   built, no warnings outside external/"
RUTEST="$BUILD/rutest"

#---------------------------------------------------------------------------
step "Bundles"
#---------------------------------------------------------------------------
declared="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1 )"
grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h || fail "StoatworksAbout.h's versionFallback is not v$declared"
check_bundle() {
	local name="$1" id="$2" display="$3" ffid="$4" kind="$5"
	local bundle="$BUILD/$name.bundle" binary="$BUILD/$name.bundle/Contents/MacOS/$name"
	[[ -f "$binary" ]] || fail "no binary at $binary"
	local arches; arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$name: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$name: no x86_64 slice (got: $arches)"
	# Captured, then matched from a herestring -- never `nm ... | grep -q`: under
	# pipefail the grep's early exit SIGPIPEs nm and fails the pipeline.
	local symbols; symbols=$( nm -gU "$binary" 2>/dev/null || true )
	grep -q '_plugMain' <<<"$symbols" || fail "$name: plugMain not exported"
	local plist="$bundle/Contents/Info.plist"
	[[ -f "$plist" ]] || fail "$name: no Info.plist"
	read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }
	[[ "$( read_plist CFBundleIdentifier )" == "$id" ]] || fail "$name: bundle id is '$( read_plist CFBundleIdentifier )'"
	[[ "$( read_plist CFBundleExecutable )" == "$name" ]] || fail "$name: CFBundleExecutable is wrong"
	[[ "$( read_plist CFBundlePackageType )" == "BNDL" ]] || fail "$name: CFBundlePackageType is not BNDL"
	[[ "$( read_plist CFBundleVersion )" == "$declared" ]] || fail "$name: plist version != CMakeLists $declared"
	codesign --force --sign - --timestamp=none "$bundle" >/dev/null 2>&1 || fail "$name: could not be ad-hoc signed"
	codesign --verify --deep --strict "$bundle" >/dev/null 2>&1 || fail "$name: the ad-hoc signature does not verify"
	echo "ok   $name: $arches, plugMain exported, $id, BNDL, v$declared, ad-hoc signed and verified"
	# What a host reads: id, name, type. The FFGL name field is char[16] and not
	# null-terminated, so only something reading it back as a host does notices
	# a truncation.
	local probe; probe="$( "$OXBOW" probe "$bundle" 2>&1 || true )"
	printf '%s\n' "$probe" | sed -n '1,3p' | sed 's/^/   /'
	grep -q "id:          $ffid" <<<"$probe" || fail "oxbow did not read the id $ffid"
	grep -q "name:        $display\$" <<<"$probe" || fail "oxbow did not read the name '$display'"
	grep -q "type:        $kind" <<<"$probe" || fail "oxbow did not read the type as $kind"
	# Its selftest renders faster than real time, so a plugin that votes on
	# its host clock's units sees a fast clock here (AGENTS.md); it must still
	# render.
	local selftest; selftest="$( "$OXBOW" selftest "$bundle" 2>&1 || true )"
	grep -q "selftest:    PASS" <<<"$selftest" || { printf '%s\n' "$selftest" | tail -5; fail "oxbow selftest failed on $name"; }
	echo "ok   a host reads $display $ffid $kind, and oxbow selftest renders through it"
}
check_bundle "Rubens" "com.stoatworks.ffgl.rubens" "SW Rubens" "RU01" "source"
check_bundle "Rubens Over" "com.stoatworks.ffgl.rubens.over" "SW Rubens Over" "RU02" "effect"

#---------------------------------------------------------------------------
step "Checks (this Mac's GPU, 1280x720 and 320x180)"
#---------------------------------------------------------------------------
# Every claim the README makes, in the order the README makes them. The
# picture checks (crossover's flames as uploaded, spacing, alpha, refraction)
# run at both rasters themselves; rates, fallback, device, clock, resize and
# state measure the duct, the audio or the GL state, not the picture, and run
# at 320x180. resonance, orifice, offres, names and cues need no GL context.
CHECKS="resonance orifice offres crossover spacing rates fallback device alpha refraction clock resize state names cues"
for check in $CHECKS; do
	if out=$( "$RUTEST" --$check 2>&1 ); then
		printf '   ok   rutest --%s: %s checks\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
		fail "rutest --$check"
	fi
done

#---------------------------------------------------------------------------
step "Software renderer (CI's, at 320x180)"
#---------------------------------------------------------------------------
# The picture checks on Apple's software renderer, which is what a GPU-less CI
# runner falls back to: the GL state, the Over's alpha (Mix 0 bit for bit,
# premultiplied), the refraction (the plume against its closed form, the clip
# read where the displacement says), the clock and the flame spacing. About a
# minute. crossover (two minutes) and rates passed there on 2026-11-16 and run
# on the GPU above; device's 100 ms frame bound is a GPU number (a software
# frame alone took 113 ms during the slow open).
SOFTWARE="state alpha refraction clock spacing"
for check in $SOFTWARE; do
	if out=$( RUTEST_RENDERER=software "$RUTEST" --$check --size 320x180 2>&1 ); then
		printf '   ok   rutest --%s (software): %s checks\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
		fail "rutest --$check on the software renderer -- run: RUTEST_RENDERER=software $RUTEST --$check --size 320x180"
	fi
done

#---------------------------------------------------------------------------
step "Offline (what CI runs)"
#---------------------------------------------------------------------------
"$RUTEST" --offline >/dev/null || fail "rutest --offline"
echo "ok   rutest --offline"

#---------------------------------------------------------------------------
step "Pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, which the video renders through, for both
# plugins, with the music from a WAV (rutest --write-wav, the harness's track).
# Two and a half frames in must be exactly two out and a clean exit -- a
# partial frame is the end of the stream, never a frame -- --film N must be N
# frames, a cue naming no parameter and a --set that is not a number or an
# option must be refused, and a reader that hangs up must end the run with
# exit 1, not SIGPIPE's silent 141. Under bash: zsh has no PIPESTATUS.
frame=$(( 64 * 36 * 4 ))
raw=$( mktemp ); cues=$( mktemp ); out=$( mktemp ); wav="$raw.wav"
"$RUTEST" --write-wav "$wav" 2 >/dev/null || fail "--write-wav"
[[ "$( head -c 4 "$wav" )" == "RIFF" ]] || fail "--write-wav wrote no RIFF file"
for flavour in "" "--over"; do
	label=${flavour:-source}
	head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
	status=0
	"$RUTEST" $flavour --pipe --size 64x36 --wav "$wav" < "$raw" > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 2 ))" ]] \
		|| fail "$label: 2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
	status=0
	"$RUTEST" $flavour --film 3 --size 64x36 --wav "$wav" > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 3 ))" ]] || fail "$label: --film 3 gave $got bytes, exit $status"
	# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
	# pipeline whatever rutest did, and the refusal would pass for the wrong reason.
	printf '0 No Such Control 0.5\n' > "$cues"
	status=0
	"$RUTEST" $flavour --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "$label: a cue naming no parameter gave exit $status, not 2"
	status=0
	"$RUTEST" $flavour --film 1 --size 64x36 --set "Channel=Centre" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "$label: --set 'Channel=Centre' (no such option) gave exit $status, not 2"
	status=0
	"$RUTEST" $flavour --film 1 --size 64x36 --set "Channel=Right" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 0 ]] || fail "$label: --set 'Channel=Right' (an option by name) gave exit $status"
	head -c $(( frame * 20 )) /dev/zero > "$raw"
	set +e
	"$RUTEST" $flavour --pipe --size 64x36 --wav "$wav" < "$raw" 2>/dev/null | head -c 1 >/dev/null
	s1=${PIPESTATUS[0]}
	"$RUTEST" $flavour --film 20 --size 64x36 2>/dev/null | head -c 1 >/dev/null
	s2=${PIPESTATUS[0]}
	set -e
	[[ "$s1" -eq 1 && "$s2" -eq 1 ]] || fail "$label: a closed stdout gave exit $s1 (--pipe), $s2 (--film), not 1"
	echo "ok   $label: 2.5 frames in, exactly 2 out (--wav); --film 3 is 3; a bad cue and a bad option refused (exit 2); | head -c 1 exits 1, not 141"
done
rm -f "$raw" "$cues" "$out" "$wav"

#---------------------------------------------------------------------------
step "Negative controls"
#---------------------------------------------------------------------------
# Every check above, against a model that is deliberately wrong, required to
# FAIL. A check that cannot fail is not a check.
if out=$( "$RUTEST" --negative 2>&1 ); then
	printf '%s\n' "$out" | grep -E '^negative controls' | sed 's/^/   /'
else
	printf '%s\n' "$out" | grep -E 'PASSED against' | sed 's/^/      /'
	fail "a negative control went undetected"
fi

#---------------------------------------------------------------------------
step "Mutants"
#---------------------------------------------------------------------------
tools/mutate.sh | grep -E '^== mutant|^   (ok|FAIL)|^mutants' || fail "a mutant survived"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$BUILD" || fail "a dead control"

#---------------------------------------------------------------------------
step "Cost"
#---------------------------------------------------------------------------
"$RUTEST" --bench

printf '\n\033[32mall green\033[0m\n'
