#include "Duct.h"

#include <algorithm>
#include <cmath>

namespace rubens
{
namespace
{
constexpr double kPiD = 3.14159265358979323846;

using Mat4 = std::array< std::array< double, 4 >, 4 >;

Mat4 multiply( const Mat4& x, const Mat4& y )
{
	Mat4 r {};
	for( int i = 0; i < 4; ++i )
		for( int k = 0; k < 4; ++k )
		{
			const double v = x[ i ][ k ];
			if( v == 0.0 )
				continue;
			for( int j = 0; j < 4; ++j )
				r[ i ][ j ] += v * y[ k ][ j ];
		}
	return r;
}

/// exp( m ): scaling and squaring, Taylor to the 20th term on a matrix scaled
/// to a 1-norm under 1/4. The modes' states are balanced (Duct::modeMatrix),
/// so a handful of squarings is all it takes.
Mat4 expm( Mat4 m )
{
	double norm = 0.0;
	for( int j = 0; j < 4; ++j )
	{
		double col = 0.0;
		for( int i = 0; i < 4; ++i )
			col += std::fabs( m[ i ][ j ] );
		norm = std::max( norm, col );
	}
	int squarings = 0;
	while( norm > 0.25 )
	{
		norm *= 0.5;
		++squarings;
	}
	const double scale = std::ldexp( 1.0, -squarings );
	for( auto& row : m )
		for( double& v : row )
			v *= scale;
	Mat4 sum {}, term {};
	for( int i = 0; i < 4; ++i )
		sum[ i ][ i ] = term[ i ][ i ] = 1.0;
	for( int k = 1; k <= 20; ++k )
	{
		term = multiply( term, m );
		for( auto& row : term )
			for( double& v : row )
				v /= k;
		for( int i = 0; i < 4; ++i )
			for( int j = 0; j < 4; ++j )
				sum[ i ][ j ] += term[ i ][ j ];
	}
	for( int s = 0; s < squarings; ++s )
		sum = multiply( sum, sum );
	return sum;
}

/// The roots of z^3 + c2 z^2 + c1 z + c0 (Durand-Kerner).
std::array< std::complex< double >, 3 > cubicRoots( double c2, double c1, double c0 )
{
	using C = std::complex< double >;
	const double scale = std::max( { 1.0, std::fabs( c2 ), std::sqrt( std::fabs( c1 ) ), std::cbrt( std::fabs( c0 ) ) } );
	std::array< C, 3 > z = { C( 0.4, 0.9 ) * scale, C( 0.4, 0.9 ) * C( 0.4, 0.9 ) * scale, C( 0.4, 0.9 ) * C( 0.4, 0.9 ) * C( 0.4, 0.9 ) * scale };
	auto f = [ & ]( C x ) { return ( ( x + c2 ) * x + c1 ) * x + c0; };
	for( int iter = 0; iter < 500; ++iter )
	{
		double moved = 0.0;
		for( int i = 0; i < 3; ++i )
		{
			C den = 1.0;
			for( int j = 0; j < 3; ++j )
				if( j != i )
					den *= z[ i ] - z[ j ];
			if( std::abs( den ) == 0.0 )
				den = 1e-300;
			const C step = f( z[ i ] ) / den;
			z[ i ] -= step;
			moved = std::max( moved, std::abs( step ) / scale );
		}
		if( moved < 1e-15 )
			break;
	}
	return z;
}
} // namespace

Duct::Duct()
{
	Configure( TubeSpec {} );
}

void Duct::SetHooksForTest( int bits )
{
	if( bits == hooks )
		return;
	hooks = bits;
	build( false );
}

void Duct::Configure( const TubeSpec& next )
{
	if( built && next == spec )
		return;
	const bool geometry = !built || next.length != spec.length || next.pitch != spec.pitch || next.holeRadius != spec.holeRadius;
	spec                = next;
	build( geometry );
}

void Duct::modeMatrix( int n, bool clean, double a[ 3 ][ 3 ], double b[ 3 ], double& omegaRef ) const
{
	const double c    = gas::kSoundSpeed;
	const double rho  = gas::kDensity;
	const double w0   = n * kPiD * c / spec.length;
	const bool noHole = !clean && ( hooks & kDuctNoHoleShift ) != 0;
	const bool lossless = ( hooks & kDuctLossless ) != 0;
	const double wH2  = noHole ? 0.0 : helmholtz2;
	const double r    = ( noHole || lossless ) ? 0.0 : resistance / inertance;
	const double wn   = std::sqrt( w0 * w0 + wH2 );
	omegaRef          = std::max( wn, 2.0 * kPiD * 10.0 );

	//Kirchhoff's wall loss [BBN Eq. 48], at the mode's own frequency. Mode 0
	//has no axial flow and keeps only the thermal part.
	double sigma = 0.0;
	if( !lossless && wn > 0.0 )
	{
		const double deltaV = std::sqrt( 2.0 * gas::kKinematicViscosity / wn );
		const double deltaT = deltaV / std::sqrt( gas::kPrandtl );
		sigma = wn * ( ( n == 0 ? 0.0 : deltaV ) + ( gas::kGamma - 1.0 ) * deltaT ) / ( 2.0 * tube::kRadius );
	}
	double drive = rho * c * c / ( spec.length * ( n == 0 ? 1.0 : 0.5 ) );
	if( !clean && ( hooks & kDuctHalfDrive ) != 0 )
		drive *= 0.5;

	//State ( a, Y = ( da/dt - b v ) / w, U = ( rho c^2 / S ) w_holes / w ):
	//balanced, so every entry is a rate of the order of the mode's own.
	const double w = omegaRef;
	a[ 0 ][ 0 ] = 0.0;
	a[ 0 ][ 1 ] = w;
	a[ 0 ][ 2 ] = 0.0;
	a[ 1 ][ 0 ] = -( w0 * w0 + wH2 ) / w;
	a[ 1 ][ 1 ] = -2.0 * sigma;
	a[ 1 ][ 2 ] = r;
	a[ 2 ][ 0 ] = wH2 / w;
	a[ 2 ][ 1 ] = 0.0;
	a[ 2 ][ 2 ] = -r;
	b[ 0 ]      = drive;
	b[ 1 ]      = -2.0 * sigma * drive / w;
	b[ 2 ]      = 0.0;
}

void Duct::build( bool resetState )
{
	built            = true;
	const double c   = gas::kSoundSpeed;
	const double rho = gas::kDensity;
	const double L   = spec.length;
	holes            = std::clamp( static_cast< int >( std::lround( L / std::max( spec.pitch, 1e-4 ) ) ), 1, tube::kMaxHoles );
	pitch            = L / holes;
	modes            = std::clamp( static_cast< int >( std::floor( CutOnHz() / ( c / ( 2.0 * L ) ) ) ) + 1, 1, tube::kMaxModes );

	const double area     = kPiD * tube::kRadius * tube::kRadius;
	const double holeArea = kPiD * spec.holeRadius * spec.holeRadius;
	const double tEff     = tube::kWall + 2.0 * tube::kEndCorrection * spec.holeRadius;
	inertance             = rho * tEff / holeArea;
	kQuad                 = rho / ( 2.0 * holeArea * holeArea );
	q0                    = holeArea * std::sqrt( 2.0 * std::max( spec.overpressure, 1e-6 ) / rho );
	resistance            = 2.0 * std::max( spec.overpressure, 1e-6 ) / q0;
	helmholtz2            = rho * c * c / ( area * inertance * pitch );

	const double dt = 1.0 / tube::kTapRate;
	std::vector< Mode > next( static_cast< size_t >( modes ) );
	for( int n = 0; n < modes; ++n )
	{
		Mode& m = next[ static_cast< size_t >( n ) ];
		double ca[ 3 ][ 3 ], cb[ 3 ], cw = 1.0;
		modeMatrix( n, true, m.a, m.b, m.omega );
		modeMatrix( n, false, ca, cb, cw );
		//The discrete system is in the hooked model's own coordinates.
		m.omega = cw;
		Mat4 aug {};
		for( int i = 0; i < 3; ++i )
		{
			for( int j = 0; j < 3; ++j )
				aug[ i ][ j ] = ca[ i ][ j ] * dt;
			aug[ i ][ 3 ] = cb[ i ] * dt;
		}
		const Mat4 e = expm( aug );
		for( int i = 0; i < 3; ++i )
		{
			for( int j = 0; j < 3; ++j )
				m.phi[ i ][ j ] = e[ i ][ j ];
			m.gamma[ i ] = e[ i ][ 3 ];
		}
		//Mode 0 with no holes at all is a pure integrator (the negative's
		//duct only): leave it out rather than let it drift.
		if( n == 0 && ( hooks & kDuctNoHoleShift ) != 0 )
		{
			for( auto& row : m.phi )
				for( double& v : row )
					v = 0.0;
			for( double& g : m.gamma )
				g = 0.0;
		}
	}
	//Keep the state across a change of overpressure (the same coordinates
	//up to the mode's scale, which does not depend on it).
	if( !resetState && mode.size() == next.size() )
		for( size_t n = 0; n < next.size(); ++n )
			for( int k = 0; k < 3; ++k )
				next[ n ].z[ k ] = mode[ n ].z[ k ] * ( k == 0 ? 1.0 : mode[ n ].omega / next[ n ].omega );
	mode.swap( next );

	holeStride = ( holes + 7 ) & ~7;
	cosTable.assign( static_cast< size_t >( modes ) * holeStride, 0.0f );
	for( int n = 0; n < modes; ++n )
		for( int i = 0; i < holes; ++i )
			cosTable[ static_cast< size_t >( n ) * holeStride + i ] = static_cast< float >( std::cos( n * kPiD * HoleX( i ) / L ) );
	amp.assign( static_cast< size_t >( modes ), 0.0f );
	pressure.assign( static_cast< size_t >( holeStride ), 0.0f );
	if( resetState || flow.size() != static_cast< size_t >( holes ) )
	{
		flow.assign( static_cast< size_t >( holes ), q0 );
		flame.assign( static_cast< size_t >( holes ), 1.0 );
		aeration.assign( static_cast< size_t >( holes ), 0.0 );
		cap = 0.0;
		for( Mode& m : mode )
			m.z[ 0 ] = m.z[ 1 ] = m.z[ 2 ] = 0.0;
	}
	probe.assign( static_cast< size_t >( holes ), HoleProbe {} );
	probing = false;
}

void Duct::Reset()
{
	for( Mode& m : mode )
		m.z[ 0 ] = m.z[ 1 ] = m.z[ 2 ] = 0.0;
	std::fill( flow.begin(), flow.end(), q0 );
	std::fill( flame.begin(), flame.end(), 1.0 );
	std::fill( aeration.begin(), aeration.end(), 0.0 );
	std::fill( pressure.begin(), pressure.end(), 0.0f );
	cap     = 0.0;
	samples = 0;
}

double Duct::HoleX( int i ) const
{
	return ( i + 0.5 ) * pitch;
}

double Duct::FlameFlow( int i ) const
{
	return ( i >= 0 && i < holes ) ? flame[ static_cast< size_t >( i ) ] : 0.0;
}

double Duct::Aeration( int i ) const
{
	return ( i >= 0 && i < holes ) ? aeration[ static_cast< size_t >( i ) ] : 0.0;
}

double Duct::HolePressure( int i ) const
{
	return ( i >= 0 && i < holes ) ? pressure[ static_cast< size_t >( i ) ] : 0.0;
}

double Duct::HoleFlow( int i ) const
{
	return ( i >= 0 && i < holes ) ? flow[ static_cast< size_t >( i ) ] / q0 : 0.0;
}

void Duct::Process( const float* velocity, int count )
{
	if( count <= 0 )
		return;
	const double dt     = 1.0 / tube::kTapRate;
	const double h      = dt / tube::kSubsteps;
	const double alpha  = 1.0 - std::exp( -dt / tube::kFlameTau );
	const double P0     = spec.overpressure;
	const double M      = inertance;
	const double invQ0  = 1.0 / q0;
	const bool linear   = ( hooks & kDuctLinearEfflux ) != 0;
	const bool signedQ  = ( hooks & kDuctSignedFlow ) != 0;
	const bool perFrame = ( hooks & kDuctPerFrame ) != 0;
	const double fourKh = 4.0 * kQuad * h;
	const double M2     = M * M;
	const double linDen = 1.0 / ( M + h * resistance );

	for( int k = 0; k < count; ++k )
	{
		const double v = velocity[ k ];
		double capSum  = 0.0;
		for( int n = 0; n < modes; ++n )
		{
			Mode& m          = mode[ static_cast< size_t >( n ) ];
			const double z0 = m.z[ 0 ], z1 = m.z[ 1 ], z2 = m.z[ 2 ];
			m.z[ 0 ]         = m.phi[ 0 ][ 0 ] * z0 + m.phi[ 0 ][ 1 ] * z1 + m.phi[ 0 ][ 2 ] * z2 + m.gamma[ 0 ] * v;
			m.z[ 1 ]         = m.phi[ 1 ][ 0 ] * z0 + m.phi[ 1 ][ 1 ] * z1 + m.phi[ 1 ][ 2 ] * z2 + m.gamma[ 1 ] * v;
			m.z[ 2 ]         = m.phi[ 2 ][ 0 ] * z0 + m.phi[ 2 ][ 1 ] * z1 + m.phi[ 2 ][ 2 ] * z2 + m.gamma[ 2 ] * v;
			amp[ static_cast< size_t >( n ) ] = static_cast< float >( m.z[ 0 ] );
			capSum += ( n & 1 ) ? -m.z[ 0 ] : m.z[ 0 ];
		}
		cap = capSum;

		//p_i = sum_n a_n cos( n pi x_i / L ): over the holes in the inner
		//loop, so it vectorises without reassociating a sum.
		float* p = pressure.data();
		std::fill( p, p + holeStride, 0.0f );
		for( int n = 0; n < modes; ++n )
		{
			const float an   = amp[ static_cast< size_t >( n ) ];
			const float* row = &cosTable[ static_cast< size_t >( n ) * holeStride ];
			for( int i = 0; i < holeStride; ++i )
				p[ i ] += row[ i ] * an;
		}

		double cosP = 0.0, sinP = 0.0;
		if( probing )
		{
			cosP = std::cos( probePhase );
			sinP = std::sin( probePhase );
			capC += cap * cosP;
			capS += cap * sinP;
			probePhase += probeOmega * dt;
			if( probePhase > 2.0 * kPiD )
				probePhase -= 2.0 * kPiD;
			++probeCount;
		}

		for( int i = 0; i < holes; ++i )
		{
			const double dp = P0 + p[ i ];
			double q        = flow[ static_cast< size_t >( i ) ];
			double out = 0.0, sum = 0.0, inflow = 0.0;
			for( int s = 0; s < tube::kSubsteps; ++s )
			{
				if( linear )
					q = ( M * q + h * dp ) * linDen;
				else
				{
					//M ( q - q_k ) / h = dp - K q|q|, solved exactly for q.
					const double cc = M * q + h * dp;
					q               = 2.0 * cc / ( M + std::sqrt( M2 + fourKh * std::fabs( cc ) ) );
				}
				out += std::max( q, 0.0 );
				sum += q;
				inflow += q < 0.0 ? 1.0 : 0.0;
			}
			flow[ static_cast< size_t >( i ) ] = q;
			const double fuel = ( signedQ ? sum : out ) * invQ0 / tube::kSubsteps;
			const double in   = inflow / tube::kSubsteps;
			if( !perFrame )
			{
				flame[ static_cast< size_t >( i ) ] += alpha * ( fuel - flame[ static_cast< size_t >( i ) ] );
				aeration[ static_cast< size_t >( i ) ] += alpha * ( in - aeration[ static_cast< size_t >( i ) ] );
			}
			if( probing )
			{
				HoleProbe& pr = probe[ static_cast< size_t >( i ) ];
				pr.outflow += out * invQ0;
				pr.signedFlow += sum * invQ0;
				pr.inflow += inflow;
				pr.c += p[ i ] * cosP;
				pr.s += p[ i ] * sinP;
			}
		}
		++samples;
	}

	//The negative: the flames follow their fuel once a call, from its last
	//sample, with the call's whole duration as the step.
	if( perFrame )
	{
		const double a = 1.0 - std::exp( -count * dt / tube::kFlameTau );
		for( int i = 0; i < holes; ++i )
		{
			const double dp   = std::max( 0.0, flow[ static_cast< size_t >( i ) ] ) * invQ0;
			const double fuel = signedQ ? flow[ static_cast< size_t >( i ) ] * invQ0 : dp;
			flame[ static_cast< size_t >( i ) ] += a * ( fuel - flame[ static_cast< size_t >( i ) ] );
			aeration[ static_cast< size_t >( i ) ] += a * ( ( flow[ static_cast< size_t >( i ) ] < 0.0 ? 1.0 : 0.0 ) - aeration[ static_cast< size_t >( i ) ] );
		}
	}
}

//---------------------------------------------------------------------------
// The closed forms.
//---------------------------------------------------------------------------
double Duct::ShuntedHz( double helmholtzHz, double noHoleHz )
{
	return std::sqrt( helmholtzHz * helmholtzHz + noHoleHz * noHoleHz );
}

double Duct::HelmholtzHz() const
{
	return std::sqrt( helmholtz2 ) / ( 2.0 * kPiD );
}

double Duct::NoHoleHz( int n ) const
{
	return n * gas::kSoundSpeed / ( 2.0 * spec.length );
}

double Duct::LosslessHz( int n ) const
{
	return ShuntedHz( HelmholtzHz(), NoHoleHz( n ) );
}

double Duct::WallDamping( int n ) const
{
	if( n < 0 || n >= modes )
		return 0.0;
	return -0.5 * mode[ static_cast< size_t >( n ) ].a[ 1 ][ 1 ];
}

double Duct::CutOnHz() const
{
	return tube::kJ11 * gas::kSoundSpeed / ( 2.0 * kPiD * tube::kRadius );
}

std::complex< double > Duct::Response( double hz, double x ) const
{
	using C       = std::complex< double >;
	const C s     = C( 0.0, 2.0 * kPiD * hz );
	C total       = 0.0;
	for( int n = 0; n < modes; ++n )
	{
		const Mode& m = mode[ static_cast< size_t >( n ) ];
		//( s I - A ) X = B, by Cramer's rule.
		C k[ 3 ][ 3 ];
		for( int i = 0; i < 3; ++i )
			for( int j = 0; j < 3; ++j )
				k[ i ][ j ] = ( i == j ? s : C( 0.0 ) ) - m.a[ i ][ j ];
		auto det3 = []( const C q[ 3 ][ 3 ] ) {
			return q[ 0 ][ 0 ] * ( q[ 1 ][ 1 ] * q[ 2 ][ 2 ] - q[ 1 ][ 2 ] * q[ 2 ][ 1 ] ) - q[ 0 ][ 1 ] * ( q[ 1 ][ 0 ] * q[ 2 ][ 2 ] - q[ 1 ][ 2 ] * q[ 2 ][ 0 ] )
			       + q[ 0 ][ 2 ] * ( q[ 1 ][ 0 ] * q[ 2 ][ 1 ] - q[ 1 ][ 1 ] * q[ 2 ][ 0 ] );
		};
		const C d = det3( k );
		if( std::abs( d ) == 0.0 )
			continue;
		C k0[ 3 ][ 3 ];
		for( int i = 0; i < 3; ++i )
			for( int j = 0; j < 3; ++j )
				k0[ i ][ j ] = j == 0 ? C( m.b[ i ] ) : k[ i ][ j ];
		total += det3( k0 ) / d * std::cos( n * kPiD * x / spec.length );
	}
	return total;
}

bool Duct::Pole( int n, double& hz, double& decay ) const
{
	if( n < 0 || n >= modes )
		return false;
	const auto& a = mode[ static_cast< size_t >( n ) ].a;
	const double tr = a[ 0 ][ 0 ] + a[ 1 ][ 1 ] + a[ 2 ][ 2 ];
	const double minors = a[ 0 ][ 0 ] * a[ 1 ][ 1 ] - a[ 0 ][ 1 ] * a[ 1 ][ 0 ] + a[ 0 ][ 0 ] * a[ 2 ][ 2 ] - a[ 0 ][ 2 ] * a[ 2 ][ 0 ] + a[ 1 ][ 1 ] * a[ 2 ][ 2 ]
	                      - a[ 1 ][ 2 ] * a[ 2 ][ 1 ];
	const double det = a[ 0 ][ 0 ] * ( a[ 1 ][ 1 ] * a[ 2 ][ 2 ] - a[ 1 ][ 2 ] * a[ 2 ][ 1 ] ) - a[ 0 ][ 1 ] * ( a[ 1 ][ 0 ] * a[ 2 ][ 2 ] - a[ 1 ][ 2 ] * a[ 2 ][ 0 ] )
	                   + a[ 0 ][ 2 ] * ( a[ 1 ][ 0 ] * a[ 2 ][ 1 ] - a[ 1 ][ 1 ] * a[ 2 ][ 0 ] );
	const auto roots = cubicRoots( -tr, minors, -det );
	double best = 0.0;
	bool found  = false;
	for( const auto& r : roots )
		if( r.imag() > best )
		{
			best  = r.imag();
			hz    = r.imag() / ( 2.0 * kPiD );
			decay = -r.real();
			found = true;
		}
	return found;
}

//---------------------------------------------------------------------------
// The probe.
//---------------------------------------------------------------------------
void Duct::StartProbe( double hz )
{
	probing    = true;
	probeOmega = 2.0 * kPiD * hz;
	probePhase = 0.0;
	probeCount = 0;
	capC = capS = 0.0;
	std::fill( probe.begin(), probe.end(), HoleProbe {} );
}

const HoleProbe& Duct::Probe( int i ) const
{
	static const HoleProbe none;
	return ( i >= 0 && i < holes ) ? probe[ static_cast< size_t >( i ) ] : none;
}

std::complex< double > Duct::ProbeCap() const
{
	if( probeCount == 0 )
		return 0.0;
	return std::complex< double >( capC, -capS ) * ( 2.0 / static_cast< double >( probeCount ) );
}

std::complex< double > Duct::ProbeHole( int i ) const
{
	if( probeCount == 0 || i < 0 || i >= holes )
		return 0.0;
	const HoleProbe& pr = probe[ static_cast< size_t >( i ) ];
	return std::complex< double >( pr.c, -pr.s ) * ( 2.0 / static_cast< double >( probeCount ) );
}

} // namespace rubens
