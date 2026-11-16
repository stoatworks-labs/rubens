#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

/**
    The tube: a plane-wave duct driven by a piston, the holes along its top,
    and the flames the holes feed. CPU, double, one audio sample at a time
    (SPEC-rubens.md, "The one mechanism"). Nothing here knows about GL.

    **The duct.** Closed-closed for the pressure: the piston at x = 0 moves
    with the audio's velocity v, the cap at x = L is rigid. The holes are a
    uniform shunt (each an acoustic mass M_h = rho t_eff / S_h, t_eff the wall
    plus Rayleigh's end correction 0.8217 r_h at each end [BS], in series with
    the jet's linearised resistance R = 2 P0 / q0), the walls lose energy to
    Kirchhoff's boundary layers [BBN]. Modes cos( n pi x / L ) up to the first
    cross-mode cut-on, each a linear 3-state system (its amplitude, its rate,
    its holes' flow) discretised exactly (matrix exponential, zero-order hold
    on v). Lossless, each mode rings at sqrt( fH^2 + fn0^2 ).

    **The holes.** Each hole's flow obeys M_h dq/dt = ( P0 + p_i ) - K q|q|,
    K = rho / ( 2 S_h^2 ): Bernoulli, as Ficken & Stephenson modelled it (via
    Gee), integrated by backward Euler solved exactly, kSubsteps a sample. The
    duct's pressure drives the holes; the holes' nonlinear flow does not feed
    back into the duct (the duct's losses use the small-signal R: "Not in
    0.1.0").

    **The flames.** Each burns the OUTWARD flow only (inflow is gulping: air
    and burnt gas drawn in, which never reaches the flame), low-passed with
    tau = 50 ms; its length is Flame x that over the quiet flow q0 (laminar
    flame length goes as fuel mass flow, Sunderland et al. 2008).
*/
namespace rubens
{

namespace gas
{
//Propane at 20 C and 0.101325 MPa: the NIST WebBook's fluid tables (Lemmon,
//McLinden & Wagner 2009's equation of state), interpolated between 290 and
//295 K. Checked 2026-11-16; SPEC-rubens.md has the table's values.
constexpr double kSoundSpeed          = 246.59;   ///< m/s
constexpr double kDensity             = 1.8652;   ///< kg/m^3
constexpr double kGamma               = 1.1390;   ///< cp/cv = 73.376/64.423
constexpr double kKinematicViscosity  = 4.2825e-6;///< m^2/s (7.988 uPa s / rho)
constexpr double kPrandtl             = 0.7482;   ///< mu cp / k
/// Air at 20 C: the negative control only (ISO 9613-1, via pyfar).
constexpr double kAirSoundSpeed = 343.2;
} // namespace gas

namespace tube
{
constexpr double kRadius        = 0.026;///< m: Gardner, Gee & Dix's tubes
constexpr double kWall          = 0.004;///< m: theirs
constexpr double kEndCorrection = 0.8217;///< x r_h at each end: Rayleigh's long-orifice value (Brandao & Schnitzer 2020, sec. 1)
constexpr double kJ11           = 1.8411837813406593;///< first zero of J1': the (1,1) mode's cut-on, computed
constexpr int kMaxHoles         = 160;
constexpr int kMaxModes         = 64;
constexpr int kTapRate          = 16000;///< Hz: earshot's tap, the duct's sample rate
constexpr int kSubsteps         = 2;    ///< backward-Euler steps per sample for the holes
constexpr double kFlameTau      = 0.05; ///< s: a flame's length following its fuel (an assumption)
} // namespace tube

struct TubeSpec
{
	double length       = 1.524;  ///< m
	double pitch        = 0.022;  ///< m between holes (the actual pitch is length / holes)
	double holeRadius   = 0.92e-3;///< m
	double overpressure = 30.0;   ///< Pa, held by the regulator
	bool operator==( const TubeSpec& o ) const
	{
		return length == o.length && pitch == o.pitch && holeRadius == o.holeRadius && overpressure == o.overpressure;
	}
	bool operator!=( const TubeSpec& o ) const
	{
		return !( *this == o );
	}
};

/// The harness's hooks. A shipped instance sets 0.
enum DuctHook : int
{
	kDuctSignedFlow   = 1 << 0,///< negative: the flames burn the signed flow (inflow subtracted)
	kDuctLinearEfflux = 1 << 1,///< negative: q = dp / R, no square root
	kDuctPerFrame     = 1 << 2,///< negative: the flames follow their fuel once per Process call
	kDuctNoHoleShift  = 1 << 3,///< negative: the duct's modes without the holes' shunt
	kDuctHalfDrive    = 1 << 4,///< negative: the piston's forcing halved
	kDuctLossless     = 1 << 5 ///< analysis: no jet resistance, no wall loss (the closed form's limit)
};

/// Lock-in sums for one hole over a probe (StartProbe).
struct HoleProbe
{
	double outflow    = 0.0;///< sum of max( q, 0 ) / q0, per substep
	double signedFlow = 0.0;///< sum of q / q0, per substep
	double inflow     = 0.0;///< substeps with q < 0
	double c = 0.0, s = 0.0;///< sums of p cos, p sin at the probe's frequency
};

class Duct
{
public:
	Duct();

	/// A new tube. Geometry (length, pitch, holes) rebuilds and resets; the
	/// overpressure only rebuilds the matrices and keeps the state.
	void Configure( const TubeSpec& spec );
	void SetHooksForTest( int bits );
	int HooksForTest() const
	{
		return hooks;
	}
	void Reset();
	/// One sample per entry, m/s, at tube::kTapRate.
	void Process( const float* velocity, int count );

	const TubeSpec& Spec() const
	{
		return spec;
	}
	int Holes() const
	{
		return holes;
	}
	int Modes() const
	{
		return modes;
	}
	double Pitch() const
	{
		return pitch;
	}
	double HoleX( int i ) const;
	double Q0() const
	{
		return q0;
	}
	double HoleInertance() const
	{
		return inertance;
	}
	double JetResistance() const
	{
		return resistance;
	}
	double FlameFlow( int i ) const;///< the low-passed outward flow over q0
	double Aeration( int i ) const; ///< the low-passed fraction of time with inflow
	double HolePressure( int i ) const;
	double HoleFlow( int i ) const;///< q / q0, the last substep
	double CapPressure() const
	{
		return cap;
	}
	int64_t Samples() const
	{
		return samples;
	}

	//-- The closed forms (double) ------------------------------------------
	static double ShuntedHz( double helmholtzHz, double noHoleHz );///< sqrt( fH^2 + fn0^2 )
	double HelmholtzHz() const;
	double NoHoleHz( int n ) const;
	double LosslessHz( int n ) const;
	double WallDamping( int n ) const;///< sigma_n, 1/s
	double CutOnHz() const;
	/// p( x ) / v in Pa per m/s: the same modes, solved in the frequency
	/// domain, plus the left-out modes' quasi-static remainder (Residual).
	std::complex< double > Response( double hz, double x ) const;
	/// The modes above the cut-on, left out, still answer the piston below
	/// their own frequencies: p = Residual( x ) dv/dt, the sum over n >= Modes()
	/// of 2 rho c^2 cos( n pi x / L ) / ( L wn^2 ), in closed form.
	double Residual( double x ) const;
	/// Mode n's oscillatory pole (frequency, decay rate), from its 3x3 system.
	bool Pole( int n, double& hz, double& decay ) const;

	//-- The probe (the harness) --------------------------------------------
	void StartProbe( double hz );
	void StopProbe()
	{
		probing = false;
	}
	int64_t ProbeSamples() const
	{
		return probeCount;
	}
	const HoleProbe& Probe( int i ) const;
	std::complex< double > ProbeCap() const;      ///< complex amplitude of p( L ) at the probe frequency
	std::complex< double > ProbeHole( int i ) const;

private:
	struct Mode
	{
		double phi[ 3 ][ 3 ] = {};
		double gamma[ 3 ]    = {};
		double z[ 3 ]        = {};
		double a[ 3 ][ 3 ]   = {};///< continuous, clean (no negative hooks)
		double b[ 3 ]        = {};
		double omega         = 1.0;///< the state's scale: sqrt( w0^2 + wH^2 )
	};
	void build( bool resetState );
	void modeMatrix( int n, bool clean, double a[ 3 ][ 3 ], double b[ 3 ], double& omegaRef ) const;

	TubeSpec spec;
	bool built = false;
	int hooks  = 0;
	int holes = 0, modes = 0, holeStride = 0;
	double pitch = 0.0, q0 = 0.0, inertance = 0.0, resistance = 0.0, kQuad = 0.0;
	double helmholtz2 = 0.0;///< wH^2
	std::vector< Mode > mode;
	std::vector< float > cosTable;///< [ mode ][ hole ], holeStride apart
	std::vector< float > amp, pressure;
	std::vector< float > residual;///< per hole: the left-out modes' quasi-static p / ( dv/dt ), Pa s^2 / m
	double residualCap = 0.0;     ///< the same at x = L
	double lastVelocity = 0.0;    ///< v one sample back, for dv/dt
	std::vector< double > flow, flame, aeration;
	double cap      = 0.0;
	int64_t samples = 0;

	bool probing = false;
	double probeOmega = 0.0, probePhase = 0.0;
	int64_t probeCount = 0;
	std::vector< HoleProbe > probe;
	double capC = 0.0, capS = 0.0;
};

} // namespace rubens
