#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# CI's raster, under a time limit (a mutant can hang: honeydew's lesson; a
# timeout counts as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The scratch tree goes under TMPDIR when it is set (a heredoc into python
# hung once with the system's /var/folders TMPDIR: the fleet's trap).
WORK="$(mktemp -d "${TMPDIR:-/tmp}/rumutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Duct.cpp|const double tEff     = tube::kWall + 2.0 * tube::kEndCorrection * spec.holeRadius;|const double tEff     = tube::kWall - 2.0 * tube::kEndCorrection * spec.holeRadius;|--resonance|C++: the holes' end correction taken off the wall, + -> -"
	"source/Duct.h|constexpr double kEndCorrection = 0.8217;|constexpr double kEndCorrection = 0.8317;|--resonance|C++: Rayleigh's end correction misread, 2 -> 3"
	"source/Duct.h|constexpr double kSoundSpeed          = 246.59;|constexpr double kSoundSpeed          = 246.69;|--resonance|C++: propane's sound speed misread from NIST's table, 5 -> 6"
	"source/Duct.cpp|helmholtz2            = rho * c * c / ( area * inertance * pitch );|helmholtz2            = rho * c * c / ( area * inertance + pitch );|--resonance|C++: the shunt's frequency from inertance plus pitch, * -> +"
	"source/Duct.cpp|( gas::kGamma - 1.0 ) * deltaT|( gas::kGamma + 1.0 ) * deltaT|--resonance|C++: Kirchhoff's thermal loss with gamma + 1, - -> +"
	"source/Duct.cpp|double drive = rho * c * c / ( spec.length * ( n == 0 ? 1.0 : 0.5 ) );|double drive = rho * c * c / ( spec.length * ( n == 0 ? 1.0 : 0.6 ) );|--resonance|C++: the piston's drive on modes n >= 1 by 1 / 0.6, 5 -> 6"
	"source/Duct.cpp|capSum += ( n & 1 ) ? -m.z[ 0 ] : m.z[ 0 ];|capSum += ( n & 1 ) ? +m.z[ 0 ] : m.z[ 0 ];|--resonance|C++: the cap's pressure without the odd modes' sign, - -> +"
	"source/Duct.cpp|return 2.0 * gas::kDensity * L / ( kPiD * kPiD ) * sum;|return 3.0 * gas::kDensity * L / ( kPiD * kPiD ) * sum;|--resonance|C++: the left-out modes' remainder by 3/2, 2 -> 3"
	"source/Duct.cpp|q0                    = holeArea * std::sqrt( 2.0 * std::max( spec.overpressure, 1e-6 ) / rho );|q0                    = holeArea * std::sqrt( 3.0 * std::max( spec.overpressure, 1e-6 ) / rho );|--orifice|C++: the quiet flow from 3 P0 / rho, 2 -> 3"
	"source/Duct.cpp|q               = 2.0 * cc / ( M + std::sqrt( M2 + fourKh * std::fabs( cc ) ) );|q               = 3.0 * cc / ( M + std::sqrt( M2 + fourKh * std::fabs( cc ) ) );|--orifice|C++: backward Euler's root scaled by 3/2, 2 -> 3"
	"source/Duct.cpp|inflow += q < 0.0 ? 1.0 : 0.0;|inflow += q > 0.0 ? 1.0 : 0.0;|--orifice|C++: gulping counted while the flow is outward, < -> >"
	"source/Duct.cpp|const double fuel = ( signedQ ? sum : out ) * invQ0 / tube::kSubsteps;|const double fuel = ( signedQ ? sum : out ) * invQ0 * tube::kSubsteps;|--crossover|C++: the substeps' fuel summed, not averaged, / -> *"
	"source/Duct.cpp|const double alpha  = 1.0 - std::exp( -dt / tube::kFlameTau );|const double alpha  = 1.0 - std::exp( +dt / tube::kFlameTau );|--crossover|C++: the flames' low-pass running away, - -> +"
	"source/Rubens.cpp|const double v0   = std::sqrt( 2.0 ) * prms / ( gas::kDensity * gas::kSoundSpeed );|const double v0   = std::sqrt( 3.0 ) * prms / ( gas::kDensity * gas::kSoundSpeed );|--rates|C++: Volume's peak velocity from sqrt 3 p_rms, 2 -> 3"
	"source/Rubens.cpp|const bool blink        = std::fmod( now, 0.5 ) < 0.25;|const bool blink        = std::fmod( now, 0.5 ) < 0.75;|--device|C++: the lamp's blink always on, 2 -> 7"
	"source/Audio.cpp|	if( value == applied )|	if( value != applied )|--device|C++: a new menu choice ignored, = -> !"
	"source/Audio.cpp|std::clamp( static_cast< double >( f.bands[ b ].level ), 0.0, 1.0 );|std::clamp( static_cast< double >( f.bands[ b ].level ), 0.0, 0.0 );|--fallback|C++: the host's band levels clamped to nothing, 1 -> 0"
	"source/Shaders.cpp|P -= IndexDeficit * theta * 1.7724539 * b * exp( -dx * dx / ( b * b ) );|P -= IndexDeficit * theta * 1.8724539 * b * exp( -dx * dx / ( b * b ) );|--refraction|GLSL: the column's sqrt(pi) misread, 7 -> 8"
	"source/Shaders.cpp|theta = kThetaFlame * pow( bTip / b, 1.5 );|theta = kThetaFlame * pow( bTip / b, 2.5 );|--refraction|GLSL: the column's deficit thinning as b^-2.5, 1 -> 2"
	"source/Shaders.cpp|* ( 1e-6 / ( 2.0 * px ) );|* ( 1e-6 / ( 3.0 * px ) );|--refraction|GLSL: the central difference over 3 pixels' width, 2 -> 3"
	"source/Shaders.cpp|vec2 q         = clamp( ( vec2( ip ) + 0.5 + shiftPx ) / size, halfTexel, vec2( 1.0 ) - halfTexel );|vec2 q         = clamp( ( vec2( ip ) + 0.5 - shiftPx ) / size, halfTexel, vec2( 1.0 ) - halfTexel );|--refraction|GLSL: the Over reading its clip against the displacement, + -> -"
	"source/Shaders.cpp|return -0.5 * TubeLength + ( float( i ) + 0.5 ) * HolePitch;|return -0.5 * TubeLength + ( float( i ) + 0.5 ) / HolePitch;|--spacing|GLSL: hole i drawn at i / pitch, * -> /"
	"source/Shaders.cpp|alpha   = 1.0 - ( 1.0 - alpha ) * ( 1.0 - max( fc.r, max( fc.g, fc.b ) ) );|alpha   = 1.0 - ( 1.0 + alpha ) * ( 1.0 - max( fc.r, max( fc.g, fc.b ) ) );|--alpha|GLSL: the flames' coverage combined with 1 + alpha, - -> +"
	"source/Shaders.cpp|if( MixAmount <= 0.0 )|if( MixAmount <= 1.0 )|--alpha|GLSL: Mix 1 returning the clip untouched, 0 -> 1"
	"source/Shaders.cpp|return vec2( max( -0.5 * TubeLength - 0.08, -0.5 * Field + 2.0 * lr ), TubeY + 0.035 );|return vec2( max( -0.5 * TubeLength - 0.08, -0.5 * Field + 9.0 * lr ), TubeY + 0.035 );|--device|GLSL: the lamp slid in to nine radii, not two, 2 -> 9"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
	ln -s "$REPO/external/earshot" "$tree/external/earshot"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target rutest -j6 >/dev/null 2>&1
	status=0
	RUBENS_NO_DEVICES=1 limit 300 "$tree/build/rutest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
