//===========================================================================
// The checks. Each sets every control it depends on (honeydew's lesson), and
// every GL check runs at each raster in kRasters.
//===========================================================================

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks );
	rig.plugin.SetDuctHooksForTest( perturb.ductHooks );
	rig.plugin.AudioForTest().fallbackFromTap = perturb.fallbackFromTap;
	rig.plugin.AudioForTest().hostCentreOnly  = perturb.hostCentreOnly;
	rig.plugin.SetIgnoreHostRateForTest( perturb.ignoreHostRate );
	return true;
}

constexpr double kFs = tube::kTapRate;
constexpr double kDt = 1.0 / tube::kTapRate;

/// Volume, dB, whose v0 is `v` m/s (v0 = sqrt 2 p_ref 10^(dB/20) / (rho c)).
double volumeForVelocity( double v )
{
	return 20.0 * std::log10( v * gas::kDensity * gas::kSoundSpeed / ( std::sqrt( 2.0 ) * kReferencePressure ) );
}

/// Set Volume so a full-scale feed is `v` m/s; returns the feed amplitude
/// (1 unless v is past 150 dB's).
double setVelocity( Rig& rig, double v )
{
	const double db = std::min( volumeForVelocity( v ), 150.0 );
	rig.Set( PT_VOLUME, ParamFromVolume( db ) );
	const double v0 = std::sqrt( 2.0 ) * kReferencePressure * std::pow( 10.0, VolumeFromParam( ParamFromVolume( db ) ) / 20.0 ) / ( gas::kDensity * gas::kSoundSpeed );
	return v / v0;
}

/// The quiet tube, everything that shapes a picture pinned.
void tubeDefaults( Rig& rig )
{
	rig.Set( PT_LENGTH, ParamFromLength( 1.524 ) );
	rig.Set( PT_PITCH, ParamFromPitch( 0.022 ) );
	rig.Set( PT_HOLE_SIZE, ParamFromHoleSize( 0.92e-3 ) );
	rig.Set( PT_GAS, ParamFromGas( 30.0 ) );
	rig.Set( PT_FLAME, ParamFromFlame( 0.08 ) );
	rig.Set( PT_SHIMMER, 0.0f );
	rig.Set( PT_DISTANCE, ParamFromDistance( 6.0 ) );
	rig.Set( PT_FIELD, ParamFromField( 1.8 ) );
	rig.Set( PT_TUBE_HEIGHT, ParamFromTubeHeight( 0.2 ) );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
	rig.Set( PT_CHANNEL, 0.0f );
}

/// g( a ) = < sqrt( max( 1 + a sin theta, 0 ) ) >: the quasi-steady mean
/// outflow over q0, by the midpoint rule (independent of the duct).
double gClosed( double a )
{
	const int n = 60000;
	double sum  = 0.0;
	for( int k = 0; k < n; ++k )
		sum += std::sqrt( std::max( 1.0 + a * std::sin( 2.0 * kPi * ( k + 0.5 ) / n ), 0.0 ) );
	return sum / n;
}
double gulpClosed( double a )
{
	return a > 1.0 ? std::acos( 1.0 / a ) / kPi : 0.0;
}

/// In-place radix-2 FFT (forward, e^{-j}).
void fft( std::vector< Complex >& x )
{
	const size_t n = x.size();
	for( size_t i = 1, j = 0; i < n; ++i )
	{
		size_t bit = n >> 1;
		for( ; j & bit; bit >>= 1 )
			j ^= bit;
		j ^= bit;
		if( i < j )
			std::swap( x[ i ], x[ j ] );
	}
	for( size_t len = 2; len <= n; len <<= 1 )
	{
		const double ang = -2.0 * kPi / static_cast< double >( len );
		for( size_t i = 0; i < n; i += len )
			for( size_t k = 0; k < len / 2; ++k )
			{
				const Complex w = std::polar( 1.0, ang * static_cast< double >( k ) );
				const Complex u = x[ i + k ], v = x[ i + k + len / 2 ] * w;
				x[ i + k ]           = u + v;
				x[ i + k + len / 2 ] = u - v;
			}
	}
}

/// What the duct sampled at the tap rate must show at f for a sine (or the
/// DFT of its impulse response): its continuous model's response through the
/// zero-order hold, every alias summed. y[k] is read after sample k, so each
/// alias f_m = f + m fs carries sinc( f_m dt ) e^{j pi f_m dt}.
Complex zohResponse( const Duct& duct, double hz, double x, int aliases = 400 )
{
	//The modes through the hold, alias by alias; the left-out modes'
	//remainder R dv/dt is a first difference on the samples, (1 - z^-1) / dt.
	const double r = duct.Residual( x );
	Complex sum    = r * ( 1.0 - std::polar( 1.0, -2.0 * kPi * hz * kDt ) ) / kDt;
	for( int m = -aliases; m <= aliases; ++m )
	{
		const double fm = hz + m * kFs;
		const double u  = fm * kDt;
		const double sc = std::fabs( u ) < 1e-12 ? 1.0 : std::sin( kPi * u ) / ( kPi * u );
		sum += ( duct.Response( fm, x ) - Complex( 0.0, 2.0 * kPi * fm * r ) ) * sc * std::polar( 1.0, kPi * u );
	}
	return sum;
}

//===========================================================================
// --resonance (CPU): the duct against its closed forms.
//===========================================================================
int runResonance( const Perturb& perturb )
{
	std::printf( "\n=== resonance: the shipped Duct (double, 16 kHz, exact ZOH) against its own continuous model, the uniform-shunt closed form "
	             "sqrt( fH^2 + fn0^2 ) and Gardner, Gee & Dix's Table II\n" );
	const int before = g_failures;

	//(1) The impulse response's DFT against the continuous model.
	for( double gasPa : { 30.0, 3.0 } )
	{
		Duct d;
		TubeSpec spec;
		spec.overpressure = gasPa;
		d.Configure( spec );
		d.SetHooksForTest( perturb.ductHooks );
		const int n = 1 << 17;
		std::vector< Complex > y( static_cast< size_t >( n ) );
		const float one = 1.0f, zero = 0.0f;
		double peak     = 0.0;
		for( int k = 0; k < n; ++k )
		{
			d.Process( k == 0 ? &one : &zero, 1 );
			y[ static_cast< size_t >( k ) ] = d.CapPressure();
			peak                            = std::max( peak, std::fabs( d.CapPressure() ) );
		}
		double tail = 0.0;
		for( int k = n - n / 100; k < n; ++k )
			tail = std::max( tail, std::abs( y[ static_cast< size_t >( k ) ] ) );
		fft( y );
		const double binHz = kFs / n;
		std::vector< int > bins;
		for( int b = static_cast< int >( 20.0 / binHz ); b * binHz < 0.97 * d.CutOnHz(); b += 23 )
			bins.push_back( b );
		for( int m = 0; m < d.Modes(); ++m )
		{
			double hz = 0.0, decay = 0.0;
			if( d.Pole( m, hz, decay ) && hz > 20.0 && hz < 0.97 * d.CutOnHz() )
				bins.push_back( static_cast< int >( std::lround( hz / binHz ) ) );
		}
		double maxE = 0.0;
		std::vector< Complex > expect( bins.size() );
		for( size_t i = 0; i < bins.size(); ++i )
		{
			expect[ i ] = zohResponse( d, bins[ i ] * binHz, spec.length );
			maxE        = std::max( maxE, std::abs( expect[ i ] ) );
		}
		double worst   = 0.0;
		double worstHz = 0.0;
		for( size_t i = 0; i < bins.size(); ++i )
		{
			const double e = std::abs( y[ static_cast< size_t >( bins[ i ] ) ] - expect[ i ] ) / ( std::abs( expect[ i ] ) + 1e-4 * maxE );
			if( e > worst )
			{
				worst   = e;
				worstHz = bins[ i ] * binHz;
			}
		}
		Check( worst < 1e-4 && tail < 1e-7 * peak,
		       fmt( "(1) Gas %g Pa: the impulse response's DFT (2^17 samples, its tail %.1e of its peak) against the modal model in the frequency "
		            "domain through the ZOH (+-400 aliases), %zu frequencies 20 Hz to 0.97 f_c: worst %.1e (at %.1f Hz; <= 1e-4)",
		            gasPa, tail / peak, bins.size(), worst, worstHz ) );
	}

	//(2) Lossless: the poles and the time-domain peaks against the closed form.
	{
		Duct d;
		d.Configure( TubeSpec {} );
		d.SetHooksForTest( kDuctLossless | perturb.ductHooks );
		double worstPole = 0.0;
		for( int m = 0; m < d.Modes(); ++m )
		{
			double hz = 0.0, decay = 0.0;
			const bool found = d.Pole( m, hz, decay );
			worstPole        = std::max( worstPole, found ? std::fabs( hz / d.LosslessHz( m ) - 1.0 ) : 1.0 );
		}
		const int n = 1 << 18;
		std::vector< Complex > y( static_cast< size_t >( n ) );
		const float one = 1.0f, zero = 0.0f;
		for( int k = 0; k < n; ++k )
		{
			d.Process( k == 0 ? &one : &zero, 1 );
			const double hann               = 0.5 - 0.5 * std::cos( 2.0 * kPi * k / n );
			y[ static_cast< size_t >( k ) ] = d.CapPressure() * hann;
		}
		fft( y );
		const double binHz = kFs / n;
		double worstPeak   = 0.0;
		int worstMode      = 0;
		std::string list;
		for( int m = 0; m < d.Modes(); ++m )
		{
			const double want = d.LosslessHz( m );
			const int lo = static_cast< int >( ( want - std::max( 3.0, 0.01 * want ) ) / binHz ), hi = static_cast< int >( ( want + std::max( 3.0, 0.01 * want ) ) / binHz );
			int best = lo;
			for( int b = lo; b <= hi; ++b )
				if( std::abs( y[ static_cast< size_t >( b ) ] ) > std::abs( y[ static_cast< size_t >( best ) ] ) )
					best = b;
			const double la = std::log( std::abs( y[ static_cast< size_t >( best - 1 ) ] ) + 1e-300 ), lb = std::log( std::abs( y[ static_cast< size_t >( best ) ] ) + 1e-300 ),
			             lc = std::log( std::abs( y[ static_cast< size_t >( best + 1 ) ] ) + 1e-300 );
			const double den   = la - 2.0 * lb + lc;
			const double delta = den != 0.0 ? 0.5 * ( la - lc ) / den : 0.0;
			const double got   = ( best + delta ) * binHz;
			const double err   = std::fabs( got / want - 1.0 );
			if( err > worstPeak )
			{
				worstPeak = err;
				worstMode = m;
			}
			if( m <= 3 || m == 10 || m == d.Modes() - 1 )
				list += fmt( " n%d %.2f/%.2f", m, got, want );
		}
		//The closed form's own numbers, from the spec's table typed here (not
		//the plugin's constants): fH = c / 2 pi sqrt( S_h / ( S t_eff d ) ),
		//t_eff = t + 2 x 0.8217 r_h, d = L / round( L / pitch ); fn0 = n c / 2 L.
		const double cP = 246.59, rh = 0.92e-3, aT = 0.026, tW = 0.004, L = 1.524;
		const double dP = L / std::round( L / 0.022 );
		const double fH = cP / ( 2.0 * kPi ) * std::sqrt( rh * rh / ( aT * aT * ( tW + 2.0 * 0.8217 * rh ) * dP ) );
		double worstForm = std::fabs( d.HelmholtzHz() / fH - 1.0 );
		for( int m = 0; m < d.Modes(); ++m )
			worstForm = std::max( worstForm, std::fabs( d.LosslessHz( m ) / std::sqrt( fH * fH + std::pow( m * cP / ( 2.0 * L ), 2.0 ) ) - 1.0 ) );
		Check( worstPole < 1e-9 && worstForm < 1e-12,
		       fmt( "(2a) lossless (R -> 0, sigma -> 0): every mode's pole against sqrt( fH^2 + fn0^2 ), worst %.1e (<= 1e-9); fH %.4f Hz and fn0 "
		            "from the geometry (t_eff = 4 mm + 2 x 0.8217 r_h, propane's 246.59 m/s) typed here, worst %.1e (<= 1e-12)",
		            worstPole, d.HelmholtzHz(), worstForm ) );
		Check( worstPeak < 1e-4, fmt( "(2b) lossless, the time domain: %d peaks of a 2^18-sample impulse response (Hann, parabolic) against the closed "
		                              "form: worst %.1e at n = %d (<= 1e-4); Hz got/closed:%s",
		                              d.Modes(), worstPeak, worstMode, list.c_str() ) );
	}

	//(2c) Lossless: the amplitude, against the uniformly shunted line solved
	//as a wave, not as modes. p'' = -kappa^2 p with kappa^2 = ( w^2 - wH^2 ) /
	//c^2, u( 0 ) = v at the piston and u( L ) = 0 give
	//p( x ) / v = -j w rho cos( kappa ( L - x ) ) / ( kappa sin kappa L ): the
	//piston's drive, the shunt, the modes and the left-out modes' remainder in
	//one number, between the poles and below fH (where kappa is imaginary and
	//the wave decays from the piston). At the cap the modes' sum alternates
	//and converges fast; at the piston it does not, and the remainder's
	//quasi-static form is all that stands in for the modes left out.
	{
		Duct d;
		d.Configure( TubeSpec {} );
		d.SetHooksForTest( kDuctLossless | perturb.ductHooks );
		const double c = gas::kSoundSpeed, rho = gas::kDensity, L = d.Spec().length;
		const double wH = 2.0 * kPi * d.HelmholtzHz();
		std::vector< double > freqs = { 20.0, 50.0, 90.0, 0.5 * ( 90.0 + d.LosslessHz( 0 ) ) };
		for( int m = 0; m + 1 < d.Modes(); ++m )
			for( double at : { 0.3, 0.5 } )
				freqs.push_back( d.LosslessHz( m ) + at * ( d.LosslessHz( m + 1 ) - d.LosslessHz( m ) ) );
		//[end][band]: worst below 400 Hz, to f_c / 2, above it.
		double worst[ 2 ][ 3 ] = {};
		for( double hz : freqs )
		{
			const double w     = 2.0 * kPi * hz;
			const Complex kap  = std::sqrt( Complex( ( w * w - wH * wH ) / ( c * c ), 0.0 ) );
			const Complex atL  = Complex( 0.0, -w * rho ) / ( kap * std::sin( kap * L ) );
			const Complex at0  = atL * std::cos( kap * L );
			//Both against the larger end: p( 0 ) has its zeros between the poles.
			const double scale = std::max( std::abs( atL ), std::abs( at0 ) );
			const double err[ 2 ] = { std::abs( d.Response( hz, L ) - atL ) / scale, std::abs( d.Response( hz, 0.0 ) - at0 ) / scale };
			const int band = hz < 400.0 ? 0 : hz <= 0.5 * d.CutOnHz() ? 1 : 2;
			for( int e = 0; e < 2; ++e )
				for( int b = band; b < 3; ++b )
					if( b == band || ( b == 1 && band == 0 ) )
						worst[ e ][ b ] = std::max( worst[ e ][ b ], err[ e ] );
		}
		Check( worst[ 0 ][ 0 ] <= 1e-4 && worst[ 0 ][ 1 ] <= 2e-3 && worst[ 1 ][ 0 ] <= 1e-3 && worst[ 1 ][ 1 ] <= 5e-2,
		       fmt( "(2c) lossless, the amplitude against the shunted line's wave solution -j w rho cos( kappa ( L - x ) ) / ( kappa sin kappa L ) "
		            "(drive, shunt, modes and the left-out modes' remainder together), %zu frequencies from 20 Hz (evanescent below fH %.1f Hz), "
		            "0.3 and 0.5 of the way between poles, each end against the larger: at the cap %.1e below 400 Hz (<= 1e-4), %.1e to f_c / 2 "
		            "(<= 2e-3); at the piston %.1e (<= 1e-3), %.1e (<= 5e-2); above f_c / 2, near the first mode left out, %.1e and %.1e",
		            freqs.size(), d.HelmholtzHz(), worst[ 0 ][ 0 ], worst[ 0 ][ 1 ], worst[ 1 ][ 0 ], worst[ 1 ][ 1 ], worst[ 0 ][ 2 ], worst[ 1 ][ 2 ] ) );
	}

	//(3) Gardner, Gee & Dix 2009, Table II (propane, ideal velocity source):
	//their no-hole frequencies, their Helmholtz resonances, their results.
	{
		const double noHole[ 9 ] = { 75, 151, 228, 307, 387, 467, 548, 630, 711 };
		const double small[ 9 ]  = { 92, 162, 236, 314, 392, 472, 552, 643, 726 };
		const double large[ 9 ]  = { 127, 187, 255, 329, 405, 483, 562, 641, 722 };
		double worstSmall = 0.0, worstLarge = 0.0, worstNoShift = 0.0;
		for( int m = 0; m < 9; ++m )
		{
			worstSmall   = std::max( worstSmall, std::fabs( Duct::ShuntedHz( 60.0, noHole[ m ] ) / small[ m ] - 1.0 ) );
			worstLarge   = std::max( worstLarge, std::fabs( Duct::ShuntedHz( 102.0, noHole[ m ] ) / large[ m ] - 1.0 ) );
			worstNoShift = std::max( worstNoShift, std::fabs( noHole[ m ] / large[ m ] - 1.0 ) );
		}
		Check( worstSmall < 0.05 && worstLarge < 0.05 && worstNoShift > 0.2,
		       fmt( "(3) [GGD] Table II, modes 1-9, fed their own fH (60, 102 Hz) and no-hole frequencies: worst %.1f%% small holes, %.1f%% large "
		            "(<= 5%%); without the shunt %.0f%% off (must not match)",
		            100.0 * worstSmall, 100.0 * worstLarge, 100.0 * worstNoShift ) );
	}

	//(4) Damping: a high mode's ring-down against its pole; the walls'
	//Kirchhoff loss against [BBN]'s formula restated here.
	{
		Duct d;
		d.Configure( TubeSpec {} );
		d.SetHooksForTest( perturb.ductHooks );
		const int mode = 20;
		double hz = 0.0, decay = 0.0;
		d.Pole( mode, hz, decay );
		const double V = 0.01;
		const int drive = static_cast< int >( kFs );
		for( int k = 0; k < drive; ++k )
		{
			const float v = static_cast< float >( V * fadeIn( k * kDt, 0.05 ) * std::sin( 2.0 * kPi * hz * k * kDt ) );
			d.Process( &v, 1 );
		}
		const int window = static_cast< int >( std::lround( 40.0 * kFs / hz ) );
		const int count  = 14;
		std::vector< double > t, la;
		const float zero = 0.0f;
		for( int w = 0; w < count; ++w )
		{
			Complex acc = 0.0;
			for( int k = 0; k < window; ++k )
			{
				d.Process( &zero, 1 );
				const int at = w * window + k;
				acc += d.CapPressure() * std::polar( 1.0, -2.0 * kPi * hz * at * kDt );
			}
			const double amp = 2.0 * std::abs( acc ) / window;
			t.push_back( ( w + 0.5 ) * window * kDt );
			la.push_back( std::log( amp ) );
		}
		//Least squares over the windows within three time constants.
		double sx = 0, sy = 0, sxx = 0, sxy = 0;
		int used  = 0;
		for( size_t i = 0; i < t.size(); ++i )
			if( la[ 0 ] - la[ i ] < 3.0 )
			{
				sx += t[ i ];
				sy += la[ i ];
				sxx += t[ i ] * t[ i ];
				sxy += t[ i ] * la[ i ];
				++used;
			}
		const double slope = ( used * sxy - sx * sy ) / ( used * sxx - sx * sx );
		const double got   = -slope;
		Check( used >= 4 && std::fabs( got / decay - 1.0 ) < 0.02,
		       fmt( "(4a) mode %d at %.1f Hz: the ring-down's decay %.2f /s against its pole's %.2f /s (Q %.0f): %.2f%% over %d windows (<= 2%%)", mode, hz,
		            got, decay, kPi * hz / decay, 100.0 * std::fabs( got / decay - 1.0 ), used ) );
		double worstWall = 0.0;
		for( int m : { 1, 10, 30 } )
		{
			const double w      = 2.0 * kPi * d.LosslessHz( m );
			const double dv     = std::sqrt( 2.0 * gas::kKinematicViscosity / w );
			const double dth    = dv / std::sqrt( gas::kPrandtl );
			const double sigma  = w * ( dv + ( gas::kGamma - 1.0 ) * dth ) / ( 2.0 * tube::kRadius );
			worstWall           = std::max( worstWall, std::fabs( d.WallDamping( m ) / sigma - 1.0 ) );
		}
		auto qAt = []( double f ) {
			const double w  = 2.0 * kPi * f;
			const double dv = std::sqrt( 2.0 * gas::kKinematicViscosity / w );
			return tube::kRadius / ( dv + ( gas::kGamma - 1.0 ) * dv / std::sqrt( gas::kPrandtl ) );
		};
		Check( worstWall < 1e-9 && std::fabs( qAt( 100.0 ) - 192.0 ) < 1.0 && std::fabs( qAt( 1000.0 ) - 607.0 ) < 1.0,
		       fmt( "(4b) the walls: sigma_n against [BBN] Eq. 48's omega (delta_V + (gamma - 1) delta_T) / 2a at modes 1, 10, 30: worst %.1e; wall Q "
		            "%.0f at 100 Hz, %.0f at 1 kHz (the spec's 192, 607)",
		            worstWall, qAt( 100.0 ), qAt( 1000.0 ) ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --orifice (CPU): one hole's small-signal impedance, the quasi-steady mean
// outflow g( a ) and the gulping fraction.
//===========================================================================
int runOrifice( const Perturb& perturb )
{
	std::printf( "\n=== orifice: each hole's efflux (Bernoulli, sqrt of the pressure across it, with the slug's inertia) against its "
	             "small-signal impedance and the quasi-steady g( a ) by quadrature\n" );
	const int before = g_failures;

	//The closed form's landmarks (documentation: the spec's numbers).
	{
		double lo = 1.0, hi = 3.0;
		for( int i = 0; i < 60; ++i )
		{
			const double m1 = lo + ( hi - lo ) / 3.0, m2 = hi - ( hi - lo ) / 3.0;
			if( gClosed( m1 ) < gClosed( m2 ) )
				hi = m2;
			else
				lo = m1;
		}
		const double aMin = 0.5 * ( lo + hi );
		double a = 3.0, b = 6.0;
		for( int i = 0; i < 50; ++i )
		{
			const double c = 0.5 * ( a + b );
			( gClosed( c ) < 1.0 ? a : b ) = c;
		}
		const double aStar = 0.5 * ( a + b );
		Check( std::fabs( gClosed( 1.0 ) - 0.9003 ) < 1e-4 && std::fabs( gClosed( aMin ) - 0.8438 ) < 1e-4 && std::fabs( aMin - 1.533 ) < 2e-3
		           && std::fabs( aStar - 4.3308 ) < 1e-3 && std::fabs( gClosed( 400.0 ) / std::sqrt( 400.0 ) - 0.3814 ) < 2e-3,
		       fmt( "the closed form: g(1) = %.4f (2 sqrt2 / pi = %.4f), minimum %.4f at a = %.3f, g = 1 again at a* = %.4f, g(400)/20 = %.4f",
		            gClosed( 1.0 ), 2.0 * std::sqrt( 2.0 ) / kPi, gClosed( aMin ), aMin, aStar, gClosed( 400.0 ) / 20.0 ) );
	}

	//(1) Small signal: Z = p / q of one hole against the backward-Euler
	//scheme's exact transfer and against R + j omega M.
	{
		Duct d;
		d.Configure( TubeSpec {} );
		d.SetHooksForTest( perturb.ductHooks );
		const double M = d.HoleInertance(), R = d.JetResistance(), q0 = d.Q0(), h = kDt / tube::kSubsteps;
		const double alpha = M / ( M + h * R ), beta = h / ( M + h * R );
		double worstScheme = 0.0, worstLow = 0.0;
		std::string list;
		for( double f : { 20.0, 100.0, 400.0, 1000.0 } )
		{
			d.Reset();
			const double V   = 0.003 * d.Spec().overpressure / std::abs( d.Response( f, d.HoleX( 0 ) ) );
			const int settle = static_cast< int >( 0.3 * kFs ), n = 8000;
			Complex P = 0.0, Q = 0.0;
			for( int k = 0; k < settle + n; ++k )
			{
				const float v = static_cast< float >( V * fadeIn( k * kDt, 0.05 ) * std::sin( 2.0 * kPi * f * k * kDt ) );
				d.Process( &v, 1 );
				if( k >= settle )
				{
					const Complex e = std::polar( 1.0, -2.0 * kPi * f * k * kDt );
					P += d.HolePressure( 0 ) * e;
					Q += ( d.HoleFlow( 0 ) * q0 - q0 ) * e;
				}
			}
			const Complex Z  = P / Q;
			const Complex z  = std::polar( 1.0, 2.0 * kPi * f * kDt );
			const Complex Zd = ( z - alpha * alpha ) / ( z * ( 1.0 + alpha ) * beta );
			const Complex Zc( R, 2.0 * kPi * f * M );
			worstScheme      = std::max( worstScheme, std::abs( Z / Zd - 1.0 ) );
			const double dev = std::abs( Zd ) / std::abs( Zc ) - 1.0;
			if( f <= 100.0 )
				worstLow = std::max( worstLow, std::fabs( dev ) );
			list += fmt( " %g Hz %+.2f%%", f, 100.0 * dev );
		}
		Check( worstScheme < 2e-3, fmt( "(1a) hole 0, swing 0.3%% of the gas, 20 Hz to 1 kHz: Z = p/q against the 2-substep backward-Euler scheme's exact "
		                                "small-signal transfer (R = 2 P0 / q0 = %.3g, M = rho t_eff / S_h = %.4g): worst %.1e (<= 2e-3)",
		                                R, M, worstScheme ) );
		Check( worstLow < 0.01, fmt( "(1b) |Z| against |R + j omega M| (the duct's shunt): worst %.2f%% at <= 100 Hz (<= 1%%); the scheme's departure:%s",
		                             100.0 * worstLow, list.c_str() ) );
	}

	//(2) Quasi-steady: 5 Hz, far below every resonance (omega M / R = 0.03).
	{
		Duct d;
		d.Configure( TubeSpec {} );
		d.SetHooksForTest( perturb.ductHooks );
		const double P0 = d.Spec().overpressure, f = 5.0;
		const double mid = std::abs( d.Response( f, 0.5 * d.Spec().length ) );
		double worstG = 0.0, worstGulp = 0.0, maxSigned = 0.0, dipAt15 = 0.0, flareAt8 = 0.0;
		std::string list;
		for( double target : { 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.33, 6.0, 8.0 } )
		{
			d.Reset();
			const double V = target * P0 / mid;
			const int settle = static_cast< int >( kFs ), n = static_cast< int >( 5 * kFs / f );
			std::vector< float > chunk( 160 );
			int k = 0;
			auto run = [ & ]( int count ) {
				while( count > 0 )
				{
					const int c = std::min( count, 160 );
					for( int i = 0; i < c; ++i, ++k )
						chunk[ static_cast< size_t >( i ) ] = static_cast< float >( V * fadeIn( k * kDt, 0.2 ) * std::sin( 2.0 * kPi * f * k * kDt ) );
					d.Process( chunk.data(), c );
					count -= c;
				}
			};
			run( settle );
			d.StartProbe( f );
			run( n );
			const double steps = static_cast< double >( d.ProbeSamples() ) * tube::kSubsteps;
			double sumA = 0.0, sumG = 0.0;
			for( int i = 0; i < d.Holes(); ++i )
			{
				const double a = std::abs( d.ProbeHole( i ) ) / P0;
				const double g = d.Probe( i ).outflow / steps;
				const double want = gClosed( a );
				worstG    = std::max( worstG, std::fabs( g / want - 1.0 ) );
				//The fraction is singular at a = 1 (infinite slope), where the
				//slug's inertia delays the first reversal: compared away from it.
				if( a <= 0.9 || a >= 1.2 )
					worstGulp = std::max( worstGulp, std::fabs( d.Probe( i ).inflow / steps - gulpClosed( a ) ) );
				maxSigned = std::max( maxSigned, d.Probe( i ).signedFlow / steps );
				sumA += a;
				sumG += g;
			}
			const double meanA = sumA / d.Holes(), meanG = sumG / d.Holes();
			if( target == 1.5 )
				dipAt15 = meanG;
			if( target == 8.0 )
				flareAt8 = meanG;
			list += fmt( " a %.2f g %.4f(%.4f)", meanA, meanG, gClosed( meanA ) );
		}
		Check( worstG < 0.02, fmt( "(2a) 5 Hz, every hole, swings a = 0.25 to 8: mean outflow against g( a ): worst %.2f%% (<= 2%%; the slug's inertia "
		                           "costs ~0.6%% here):%s",
		                           100.0 * worstG, list.c_str() ) );
		Check( worstGulp < 0.01, fmt( "(2b) the fraction of time each hole gulps against acos( 1/a ) / pi, a <= 0.9 or >= 1.2: worst %.4f (<= 0.01)",
		                              worstGulp ) );
		Check( dipAt15 < 0.87 && flareAt8 > 1.0 && maxSigned < 1.0 + 1e-3,
		       fmt( "(2c) the dip (g %.3f at a = 1.5) and the flare (%.3f at a = 8) in the outflow; the signed flow never above 1 (max %.4f): the flare "
		            "needs the outward-only accounting",
		            dipAt15, flareAt8, maxSigned ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --offres (CPU): a note between two modes against one on a mode.
//===========================================================================
int runOffres( const Perturb& perturb )
{
	std::printf( "\n=== offres: a note between two modes barely moves the flames; one on a mode does. Each flame's dip against the derived "
	             "a^2 / ( 16 ( 1 + ( omega M / R )^2 ) ) with a from the transfer function\n" );
	const int before = g_failures;
	for( int mode : { 10, 3 } )
	{
		Duct d;
		d.Configure( TubeSpec {} );
		d.SetHooksForTest( perturb.ductHooks );
		double fOn = 0.0, fNext = 0.0, decay = 0.0;
		d.Pole( mode, fOn, decay );
		d.Pole( mode + 1, fNext, decay );
		const double fOff = 0.5 * ( fOn + fNext );
		const double P0 = d.Spec().overpressure, M = d.HoleInertance(), R = d.JetResistance();
		double peakOn = 0.0;
		for( int i = 0; i < d.Holes(); ++i )
			peakOn = std::max( peakOn, std::abs( d.Response( fOn, d.HoleX( i ) ) ) );
		const double V = 0.8 * P0 / peakOn;
		double got[ 2 ] = {}, want[ 2 ] = {};
		int idx = 0;
		for( double f : { fOn, fOff } )
		{
			d.Reset();
			std::vector< float > chunk( 160 );
			std::vector< double > mean( static_cast< size_t >( d.Holes() ), 0.0 );
			int k = 0, samples = 0;
			const int settle = static_cast< int >( kFs ), measure = static_cast< int >( 0.25 * kFs );
			while( k < settle + measure )
			{
				for( int i = 0; i < 160; ++i, ++k )
					chunk[ static_cast< size_t >( i ) ] = static_cast< float >( V * fadeIn( k * kDt, 0.05 ) * std::sin( 2.0 * kPi * f * k * kDt ) );
				d.Process( chunk.data(), 160 );
				if( k > settle )
				{
					for( int i = 0; i < d.Holes(); ++i )
						mean[ static_cast< size_t >( i ) ] += d.FlameFlow( i );
					++samples;
				}
			}
			const double w = 2.0 * kPi * f;
			for( int i = 0; i < d.Holes(); ++i )
			{
				got[ idx ]      = std::max( got[ idx ], std::fabs( mean[ static_cast< size_t >( i ) ] / samples - 1.0 ) );
				const double a  = V * std::abs( d.Response( f, d.HoleX( i ) ) ) / P0;
				want[ idx ]     = std::max( want[ idx ], a * a / ( 16.0 * ( 1.0 + w * M / R * ( w * M / R ) ) ) );
			}
			++idx;
		}
		const double ratio = got[ 1 ] / got[ 0 ];
		const bool agree   = std::fabs( got[ 0 ] / want[ 0 ] - 1.0 ) < 0.1 && std::fabs( got[ 1 ] / want[ 1 ] - 1.0 ) < 0.1;
		if( mode == 10 )
			Check( agree && ratio < 0.1,
			       fmt( "mode %d (%.1f Hz, Q %.0f) against %.1f Hz between it and the next, the same drive: deepest dip %.2e on, %.2e off (derived %.2e, "
			            "%.2e; <= 10%%): off/on %.4f (<= 0.1)",
			            mode, fOn, kPi * fOn / std::max( decay, 1e-9 ), fOff, got[ 0 ], got[ 1 ], want[ 0 ], want[ 1 ], ratio ) );
		else
			Check( agree, fmt( "mode %d (%.1f Hz, low Q at 30 Pa): dip %.2e on, %.2e between (derived %.2e, %.2e; <= 10%%): off/on %.3f -- Q-dependent, so "
			                   "reported, not bounded",
			                   mode, fOn, got[ 0 ], got[ 1 ], want[ 0 ], want[ 1 ], ratio ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --crossover (GL, end to end): the plugin fed a 5 Hz sine through earshot,
// Volume stepped; each flame against g( a ) at its hole's measured swing.
//===========================================================================
int runCrossover( const Perturb& perturb )
{
	std::printf( "\n=== crossover: end to end (feed(), the tap, the duct, the flames as uploaded): 5 Hz, Gas 30 Pa, Volume stepped; each flame "
	             "against g( a ) at its own hole's swing; where the flames cross the quiet height\n" );
	const int before = g_failures;
	const Raster raster = kRasters.back();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	tubeDefaults( rig );
	const double f = 5.0;
	double amplitude = 1.0;
	rig.feeder.signal = [ & ]( double t ) { return amplitude * fadeIn( t, 0.2 ) * std::sin( 2.0 * kPi * f * t ); };
	Duct& duct = rig.plugin.TubeForTest();

	struct Step
	{
		double a, g;
	};
	std::vector< Step > steps;
	double worst = 0.0;
	auto measure = [ & ]( double velocity ) -> bool {
		amplitude = setVelocity( rig, velocity );
		if( !rig.Render( 60 ) )
			return false;
		duct.StartProbe( f );
		std::vector< double > sum( static_cast< size_t >( duct.Holes() ), 0.0 );
		for( int k = 0; k < 60; ++k )
		{
			if( !rig.Render( 1 ) )
				return false;
			const FrameData& fd = rig.plugin.LastFrame();
			for( int i = 0; i < fd.holes; ++i )
				sum[ static_cast< size_t >( i ) ] += fd.lengths[ static_cast< size_t >( i ) ] / fd.flame;
		}
		double sa = 0.0, sg = 0.0;
		for( int i = 0; i < duct.Holes(); ++i )
		{
			const double a = std::abs( duct.ProbeHole( i ) ) / duct.Spec().overpressure;
			const double g = sum[ static_cast< size_t >( i ) ] / 60.0;
			worst          = std::max( worst, std::fabs( g / gClosed( a ) - 1.0 ) );
			sa += a;
			sg += g;
		}
		steps.push_back( { sa / duct.Holes(), sg / duct.Holes() } );
		return true;
	};
	//The first step calibrates a per m/s; the rest aim at swings.
	const double v1 = 0.3;
	if( !measure( v1 ) )
		return 1;
	const double aPerV = steps.front().a / v1;
	for( double target : { 1.0, 1.5, 2.5, 3.5, 4.0, 4.5, 5.0, 6.5 } )
		if( !measure( target / aPerV ) )
			return 1;
	std::sort( steps.begin(), steps.end(), []( const Step& x, const Step& y ) { return x.a < y.a; } );
	double aStar = -1.0, dip = 1.0;
	std::string list;
	for( size_t i = 0; i < steps.size(); ++i )
	{
		list += fmt( " %.2f:%.3f", steps[ i ].a, steps[ i ].g );
		dip = std::min( dip, steps[ i ].g );
		if( aStar < 0.0 && i > 0 && steps[ i - 1 ].g < 1.0 && steps[ i ].g >= 1.0 && steps[ i - 1 ].a > 1.0 )
			aStar = steps[ i - 1 ].a + ( 1.0 - steps[ i - 1 ].g ) * ( steps[ i ].a - steps[ i - 1 ].a ) / ( steps[ i ].g - steps[ i - 1 ].g );
	}
	Check( worst < 0.03, fmt( "%zu steps, every hole: the flame (as uploaded, averaged over 5 cycles) against g( a ) at its hole's swing: worst %.2f%% "
	                          "(<= 3%%); a:flame%s",
	                          steps.size(), 100.0 * worst, list.c_str() ) );
	Check( aStar > 0.0 && std::fabs( aStar / 4.3308 - 1.0 ) < 0.05 && dip < 0.9,
	       fmt( "the flames dip to %.3f, then cross the quiet height at a* = %.3f (derived 4.331 +- 5%%; swing = overpressure is a = 1, where gulping "
	            "starts, not the flare)",
	            dip, aStar ) );
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --spacing (GL): the flames' period read off the rendered picture.
//===========================================================================

/// Each hole's flame height: the light-weighted mean height of its column
/// (a band one pitch wide), from a raw flames-only render.
std::vector< double > flameHeights( const Floats& px, int w, int h, const FrameData& fd )
{
	const double pixel = fd.field / w;
	const double base  = fd.tubeY + tube::kRadius;
	std::vector< double > out( static_cast< size_t >( fd.holes ), 0.0 );
	for( int i = 0; i < fd.holes; ++i )
	{
		//The band in pixel units; each pixel weighted by the part of it inside
		//the band, so no hole's band is a pixel wider than its neighbour's.
		const double x  = -0.5 * fd.length + ( i + 0.5 ) * fd.pitch;
		const double u0 = ( x - 0.5 * fd.pitch + 0.5 * fd.field ) / pixel, u1 = ( x + 0.5 * fd.pitch + 0.5 * fd.field ) / pixel;
		double sum = 0.0, moment = 0.0;
		for( int yy = 0; yy < h; ++yy )
		{
			const double ym = ( yy + 0.5 ) * pixel - base;
			if( ym <= 0.0 )
				continue;
			for( int xx = std::max( static_cast< int >( std::floor( u0 ) ), 0 ); xx < std::min( static_cast< int >( std::ceil( u1 ) ), w ); ++xx )
			{
				const double cover = std::min( u1, xx + 1.0 ) - std::max( u0, static_cast< double >( xx ) );
				if( cover <= 0.0 )
					continue;
				const float* p = &px[ ( static_cast< size_t >( yy ) * w + xx ) * 4 ];
				const double e = cover * ( p[ 0 ] + p[ 1 ] + p[ 2 ] );
				sum += e;
				moment += e * ym;
			}
		}
		out[ static_cast< size_t >( i ) ] = sum > 0.0 ? moment / sum : 0.0;
	}
	return out;
}

/// The period P minimising the residual of y ~ c0 + c1 cos( 2 pi x / P ) + c2 sin( ... ).
double fitPeriod( const std::vector< double >& x, const std::vector< double >& y, double lo, double hi, double step )
{
	double best = lo, bestSS = 1e300;
	for( double P = lo; P <= hi; P += step )
	{
		double A[ 3 ][ 3 ] = {}, b[ 3 ] = {};
		for( size_t i = 0; i < x.size(); ++i )
		{
			const double r[ 3 ] = { 1.0, std::cos( 2.0 * kPi * x[ i ] / P ), std::sin( 2.0 * kPi * x[ i ] / P ) };
			for( int p = 0; p < 3; ++p )
			{
				b[ p ] += r[ p ] * y[ i ];
				for( int q = 0; q < 3; ++q )
					A[ p ][ q ] += r[ p ] * r[ q ];
			}
		}
		//Solve by Cramer's rule.
		auto det = []( const double m[ 3 ][ 3 ] ) {
			return m[ 0 ][ 0 ] * ( m[ 1 ][ 1 ] * m[ 2 ][ 2 ] - m[ 1 ][ 2 ] * m[ 2 ][ 1 ] ) - m[ 0 ][ 1 ] * ( m[ 1 ][ 0 ] * m[ 2 ][ 2 ] - m[ 1 ][ 2 ] * m[ 2 ][ 0 ] )
			       + m[ 0 ][ 2 ] * ( m[ 1 ][ 0 ] * m[ 2 ][ 1 ] - m[ 1 ][ 1 ] * m[ 2 ][ 0 ] );
		};
		const double D = det( A );
		if( std::fabs( D ) < 1e-30 )
			continue;
		double c[ 3 ];
		for( int k = 0; k < 3; ++k )
		{
			double m[ 3 ][ 3 ];
			for( int p = 0; p < 3; ++p )
				for( int q = 0; q < 3; ++q )
					m[ p ][ q ] = q == k ? b[ p ] : A[ p ][ q ];
			c[ k ] = det( m ) / D;
		}
		double ss = 0.0;
		for( size_t i = 0; i < x.size(); ++i )
		{
			const double e = y[ i ] - ( c[ 0 ] + c[ 1 ] * std::cos( 2.0 * kPi * x[ i ] / P ) + c[ 2 ] * std::sin( 2.0 * kPi * x[ i ] / P ) );
			ss += e * e;
		}
		if( ss < bestSS )
		{
			bestSS = ss;
			best   = P;
		}
	}
	return best;
}

int runSpacing( const Perturb& perturb )
{
	std::printf( "\n=== spacing: a sine on a mode, the flames' period read off the rendered picture, against c / ( 2 f sqrt( 1 - fH^2 / f^2 ) ) "
	             "for propane\n" );
	const int before = g_failures;
	struct Case
	{
		int mode;
		double gas;
	};
	const Case cases[] = { { 3, 3.0 }, { 10, 30.0 }, { 14, 30.0 } };
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			tubeDefaults( rig );
			rig.Set( PT_GAS, ParamFromGas( c.gas ) );
			rig.Set( PT_FLAME, ParamFromFlame( 0.15 ) );
			rig.Set( PT_FIELD, ParamFromField( 1.6 ) );
			rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookRaw | shaders::kHookFlamesOnly );
			//The drive: the frequency at which mode n's standing wave is exact.
			Duct probe;
			TubeSpec spec;
			spec.overpressure = c.gas;
			probe.Configure( spec );
			const double f  = probe.LosslessHz( c.mode );
			const double fH = probe.HelmholtzHz();
			//The swing that draws a clear pattern: tried on a scratch duct of
			//the same model (the holes' inertia hides high notes' dips).
			double peak = 0.0;
			for( int i = 0; i < probe.Holes(); ++i )
				peak = std::max( peak, std::abs( probe.Response( f, probe.HoleX( i ) ) ) );
			double velocity = 0.0;
			for( double target : { 1.5, 3.0, 6.0, 12.0, 24.0 } )
			{
				Duct scratch;
				scratch.Configure( spec );
				scratch.SetHooksForTest( perturb.ductHooks );
				velocity = target * c.gas / peak;
				std::vector< float > chunk( 160 );
				int k = 0;
				for( int s = 0; s < 100; ++s )
				{
					for( int i = 0; i < 160; ++i, ++k )
						chunk[ static_cast< size_t >( i ) ] = static_cast< float >( velocity * fadeIn( k * kDt, 0.05 ) * std::sin( 2.0 * kPi * f * k * kDt ) );
					scratch.Process( chunk.data(), 160 );
				}
				double lo = 1e9, hi = -1e9;
				for( int i = 0; i < scratch.Holes(); ++i )
				{
					lo = std::min( lo, scratch.FlameFlow( i ) );
					hi = std::max( hi, scratch.FlameFlow( i ) );
				}
				if( hi - lo >= 0.25 )
					break;
			}
			double amplitude  = setVelocity( rig, velocity );
			rig.feeder.signal = [ & ]( double t ) { return amplitude * fadeIn( t, 0.05 ) * std::sin( 2.0 * kPi * f * t ); };
			if( !rig.Render( 90 ) )
				return 1;
			const FrameData& fd = rig.plugin.LastFrame();
			const std::vector< double > y = flameHeights( rig.Output(), raster.w, raster.h, fd );
			std::vector< double > x( y.size() );
			for( size_t i = 0; i < x.size(); ++i )
				x[ i ] = ( i + 0.5 ) * fd.pitch;
			const double want   = gas::kSoundSpeed / ( 2.0 * f * std::sqrt( 1.0 - fH * fH / ( f * f ) ) );
			const double got    = fitPeriod( x, y, 0.7 * want, 1.4 * want, 0.0005 * want );
			std::vector< double > lengths( fd.lengths.begin(), fd.lengths.end() );
			const double duct   = fitPeriod( x, lengths, 0.7 * want, 1.4 * want, 0.0005 * want );
			const double plain  = gas::kSoundSpeed / ( 2.0 * f );
			const double air    = gas::kAirSoundSpeed / ( 2.0 * f );
			double lo = 1e9, hi = -1e9;
			for( double v : y )
			{
				lo = std::min( lo, v );
				hi = std::max( hi, v );
			}
			const double err = std::fabs( got / want - 1.0 );
			Check( err < 0.015 && std::fabs( got / air - 1.0 ) > 0.015 && ( c.mode > 4 || std::fabs( got / plain - 1.0 ) > 0.015 ),
			       fmt( "%dx%d mode %d at %.1f Hz (fH %.1f, Gas %g Pa, swing for a pattern %.2f m/s): flame heights %.1f-%.1f mm, period %.4f m against "
			            "%.4f: %.2f%% (<= 1.5%%; the duct's own lengths %.4f); c/2f %.4f (%.1f%% off), air's c/2f %.4f (%.0f%% off)",
			            raster.w, raster.h, c.mode, f, fH, c.gas, velocity, 1000.0 * lo, 1000.0 * hi, got, want, 100.0 * err, duct, plain,
			            100.0 * std::fabs( got / plain - 1.0 ), air, 100.0 * std::fabs( got / air - 1.0 ) ) );
		}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --rates (GL): the same audio at 24, 30, 60 and 240 fps, the flames at t = 1 s;
// and what Volume means (the piston velocity of a full-scale sample).
//===========================================================================
int runRates( const Perturb& perturb )
{
	std::printf( "\n=== rates: the harness's track fed at 24, 30, 60 and 240 fps: the flames at t = 1 s are the same (the duct integrates at the tap "
	             "rate, not the frame rate); and what Volume means\n" );
	const Raster raster = kRasters.back();
	std::vector< std::vector< float > > results;
	std::string list;
	double moved = 0.0, v0 = 0.0;
	for( double fps : { 24.0, 30.0, 60.0, 240.0 } )
	{
		Rig rig;
		rig.fps = fps;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		tubeDefaults( rig );
		rig.Set( PT_VOLUME, ParamFromVolume( 150.0 ) );
		rig.feeder.signal = musicAt;
		if( !rig.Render( static_cast< int >( fps ) ) )
			return 1;
		results.push_back( rig.plugin.LastFrame().lengths );
		v0 = rig.plugin.LastFrame().v0;
		for( float l : results.back() )
			moved = std::max( moved, std::fabs( l / rig.plugin.LastFrame().flame - 1.0 ) );
		list += fmt( " %g fps: %lld samples;", fps, static_cast< long long >( rig.plugin.TubeForTest().Samples() ) );
	}
	double worst = 0.0;
	for( size_t r = 1; r < results.size(); ++r )
		for( size_t i = 0; i < results[ r ].size() && i < results[ 0 ].size(); ++i )
			worst = std::max( worst, static_cast< double >( std::fabs( results[ r ][ i ] - results[ 0 ][ i ] ) ) );
	Check( worst == 0.0 && moved > 0.03,
	       fmt( "the largest difference between rates %.2e m (must be 0); the flames moved up to %.0f%% from quiet;%s", worst, 100.0 * moved, list.c_str() ) );
	//Volume: the SPL a full-scale sine would make as a travelling wave in
	//propane, so its peak velocity is sqrt 2 p_rms / ( rho c ). The numbers
	//are the spec's, typed here, not read from the plugin.
	const double want = std::sqrt( 2.0 ) * 20e-6 * std::pow( 10.0, 150.0 / 20.0 ) / ( 1.8652 * 246.59 );
	Check( std::fabs( v0 / want - 1.0 ) < 1e-6,
	       fmt( "Volume 150 dB: a full-scale sample drives the piston at %.6f m/s, sqrt 2 x 20 uPa x 10^(150/20) / (1.8652 x 246.59) = %.6f "
	            "(within 1e-6)",
	            v0, want ) );
	return Verdict();
}

//===========================================================================
// --fallback (GL): no device, only Resolume's FFT.
//===========================================================================
int runFallback( const Perturb& perturb )
{
	std::printf( "\n=== fallback: no audio fed, only Resolume's FFT buffer: band-limited noise drives the piston, louder bands move the flames more\n" );
	const Raster raster = kRasters.back();
	double motion[ 3 ] = {};
	bool host = true, device = false;
	int idx = 0;
	for( double level : { 0.0, 0.05, 0.3 } )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		tubeDefaults( rig );
		rig.Set( PT_VOLUME, ParamFromVolume( 140.0 ) );
		rig.Bins( [ & ]( int ) { return level; } );
		if( !rig.Render( 60 ) )
			return 1;
		double sum = 0.0;
		int n      = 0;
		for( int f = 0; f < 30; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const FrameData& fd = rig.plugin.LastFrame();
			for( float l : fd.lengths )
			{
				sum += std::fabs( l / fd.flame - 1.0 );
				++n;
			}
			device = device || fd.fromDevice;
		}
		if( level > 0.0 )
			host = host && rig.plugin.AudioForTest().Frame().source == earshot::Source::Host;
		motion[ idx++ ] = sum / n;
	}
	Check( motion[ 0 ] < 1e-6 && motion[ 1 ] > 1e-4 && motion[ 2 ] > 2.0 * motion[ 1 ] && host && !device,
	       fmt( "flat bins at 0, 0.05, 0.3: mean |flame/quiet - 1| %.2e, %.2e, %.2e (0, moving, more); earshot's source Host: %s; the tap never read: %s",
	            motion[ 0 ], motion[ 1 ], motion[ 2 ], host ? "yes" : "NO", device ? "NO" : "yes" ) );
	return Verdict();
}

//===========================================================================
// --arenafft (GL): the fallback on Resolume's own FFT, as Arena 7.29.0 sends it
// (earshot's AGENTS.md, "Resolume's 64 bins, measured"). Audio is made into
// the 64 bins Arena was measured to make and handed to the plugin as its
// FF_USAGE_FFT buffer, element by element, as a host does; nothing goes
// through feed(), and SetSampleRate is called as Arena calls it. Under earshot
// 8124191 (bin j centred at (j + 0.5)/64 of 22.05 kHz, each bin whole to the
// band holding its centre) the 40-160 Hz band got no bin and its noise was
// never driven.
//===========================================================================

/// Arena's FFT buffer as measured: every 2048 samples of the engine's rate, a
/// 2048-point Hann-windowed DFT of the newest 2048 samples (the mono sum; the
/// signals here are the same on both sides), and bin k the sum of |X_i| / 2N
/// over the 16 fine bins 16k .. 16k + 15: linear, fs / 128 a bin (375 Hz at
/// 48 kHz) to fs / 2, a magnitude, a sine of amplitude A reading about A / 4 in
/// its bin. Held between updates, as Arena holds it. (snowglobe's sgtest
/// builds its bins the same way.)
struct HostSpectrum
{
	static constexpr int kN = 2048;
	int rate;
	std::function< double( double ) > signal;
	std::vector< float > bins = std::vector< float >( 64, 0.0f );
	int64_t updated           = -1;

	HostSpectrum( int r, std::function< double( double ) > s ) : rate( r ), signal( std::move( s ) ) {}

	const std::vector< float >& at( double t )
	{
		const int64_t u = static_cast< int64_t >( std::floor( t * rate / kN + 1e-9 ) );
		if( u == updated )
			return bins;
		updated = u;
		std::vector< std::complex< double > > x( kN );
		for( int n = 0; n < kN; ++n )
		{
			const int64_t sample = u * kN - kN + n;
			const double hann    = 0.5 - 0.5 * std::cos( 2.0 * kPi * n / kN );
			x[ static_cast< size_t >( n ) ] = hann * signal( static_cast< double >( sample ) / rate );
		}
		//Radix-2, in place.
		for( int i = 1, j = 0; i < kN; ++i )
		{
			int bit = kN >> 1;
			for( ; j & bit; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if( i < j )
				std::swap( x[ static_cast< size_t >( i ) ], x[ static_cast< size_t >( j ) ] );
		}
		for( int len = 2; len <= kN; len <<= 1 )
		{
			const std::complex< double > w = std::polar( 1.0, -2.0 * kPi / len );
			for( int i = 0; i < kN; i += len )
			{
				std::complex< double > wk( 1.0, 0.0 );
				for( int k = 0; k < len / 2; ++k )
				{
					const auto a = x[ static_cast< size_t >( i + k ) ], b = x[ static_cast< size_t >( i + k + len / 2 ) ] * wk;
					x[ static_cast< size_t >( i + k ) ]           = a + b;
					x[ static_cast< size_t >( i + k + len / 2 ) ] = a - b;
					wk *= w;
				}
			}
		}
		for( int k = 0; k < 64; ++k )
		{
			double sum = 0.0;
			for( int i = 16 * k; i < 16 * k + 16; ++i )
				sum += std::abs( x[ static_cast< size_t >( i ) ] );
			bins[ static_cast< size_t >( k ) ] = static_cast< float >( sum / ( 2.0 * kN ) );
		}
		return bins;
	}
};

/// 128 BPM (Arena's Beat 001), so the kicks fall at every phase of the
/// 2048-sample updates. The parts: the harness's kick (150 -> 50 Hz) on every
/// beat; a clap on two and four and hats on the eighths between, as noise
/// twice differenced (-52 dB at 375 Hz), so they put next to nothing in bin 0.
constexpr double kLoopBeat = 60.0 / 128.0;
enum LoopPart : int
{
	kLoopKick = 1,
	kLoopTop  = 2///< claps and hats
};

double drumLoopAt( double t, int parts, int rate )
{
	if( t < 0.0 )
		return 0.0;
	//The sample's own index, rounded: truncating t * rate repeats and skips
	//samples, and a differenced noise that does is no longer high-passed.
	const uint32_t n  = static_cast< uint32_t >( std::llround( t * rate ) );
	const double beat = std::fmod( t, kLoopBeat );
	double s          = 0.0;
	if( parts & kLoopKick )
		s += 0.6 * kickAt( beat );
	if( parts & kLoopTop )
	{
		auto hiss = [ n ]( uint32_t seed ) { return hash01( n, seed ) - 2.0 * hash01( n - 1u, seed ) + hash01( n - 2u, seed ); };
		const double clap = std::fmod( t, 2.0 * kLoopBeat ) - kLoopBeat;
		if( clap >= 0.0 )
			s += 0.3 * std::exp( -clap / 0.03 ) * hiss( 11u );
		const double hat = std::fmod( t + kLoopBeat / 2.0, kLoopBeat );
		s += 0.2 * std::exp( -hat / 0.01 ) * hiss( 7u );
	}
	return s;
}

int runArenaFft( const Perturb& perturb )
{
	std::printf( "\n=== arenafft: the fallback on Resolume's FFT alone (the bins as Arena 7.29.0 sends them, nothing fed), at 48 and 44.1 kHz: "
	             "a tone in bin 0 drives the 40-160 Hz noise band at its share of the bin, 120 Hz of fs/128; a drum loop's kicks drive it "
	             "every time, within two of the host's updates and a frame; claps and hats above bin 0 leave it quiet; earshot reads the "
	             "bins at the host's rate under FFT Law's default, Linear Mag, the measured law\n" );
	const Raster raster = kRasters.back();
	auto start          = [ & ]( Rig& rig, int rate ) {
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return false;
		tubeDefaults( rig );
		rig.plugin.SetSampleRate( static_cast< unsigned int >( rate ) );
		return true;
	};
	auto feedHost = [ & ]( Rig& rig, HostSpectrum& host ) {
		rig.beforeFrame = [ &rig, &host ]( int f ) {
			const std::vector< float >& b = host.at( rig.Seconds( f ) );
			rig.Bins( [ &b ]( int bin ) { return static_cast< double >( b[ static_cast< size_t >( bin ) ] ); } );
		};
	};

	for( int rate : { 48000, 44100 } )
	{
		//(a) A 70 Hz tone: all of it in bin 0, which spans 0 to fs/128. The
		//40-160 Hz band takes 120/(fs/128) of the bin's energy, 160-640 Hz the
		//rest; the noise of each is driven at that level.
		{
			Rig rig;
			if( !start( rig, rate ) )
				return 1;
			HostSpectrum host( rate, []( double t ) { return 0.4 * std::sin( 2.0 * kPi * 70.0 * t ); } );
			feedHost( rig, host );
			if( !rig.Render( 90 ) )
				return 1;
			AudioInput& audio = rig.plugin.AudioForTest();
			const double width      = rate / 128.0;
			const double m0 = host.bins[ 0 ], m1 = host.bins[ 1 ];
			const double want0 = std::sqrt( 120.0 / width ) * m0, want1 = std::sqrt( ( width - 160.0 ) / width ) * m0;
			const double l0 = audio.FallbackLevelForTest( 0 ), l1 = audio.FallbackLevelForTest( 1 );
			const double l2 = audio.FallbackLevelForTest( 2 ), l3 = audio.FallbackLevelForTest( 3 );
			Check( std::fabs( l0 / want0 - 1.0 ) < 1e-4 && std::fabs( l1 / want1 - 1.0 ) < 1e-4 && l2 < 1e-3 * m0 && l3 < 1e-3 * m0 && m1 < 1e-3 * m0,
			       fmt( "a 70 Hz tone, %d Hz: bin 0 %.5f (bin 1 %.2e); the 40-160 Hz noise driven at %.5f, want sqrt(120/%.2f) x bin 0 = %.5f; "
			            "160-640 Hz at %.5f, want sqrt(%.2f/%.2f) x bin 0 = %.5f (each within 1e-4); 640-2560 and 2560-8000 Hz %.1e and %.1e "
			            "(want under 1e-3 of bin 0)",
			            rate, m0, m1, l0, width, want0, l1, width - 160.0, width, want1, l2, l3 ) );
			const double read = audio.HostRateForTest();
			Check( std::fabs( read - rate ) < 1e-9, fmt( "the host says %d Hz: earshot reads its bins at %.0f Hz", rate, read ) );
			if( rate == 48000 )
			{
				const earshot::HostBins law = audio.Engine().config().hostBins;
				const earshot::HostBins measured;
				Check( rig.plugin.GetById( PT_FFT_LAW ) == 0.0f && law.value == earshot::HostBins::Value::Magnitude && law.layout == earshot::HostBins::Layout::Linear
				           && measured.value == law.value && measured.layout == law.layout,
				       fmt( "FFT Law at its default (Linear Mag): earshot reads %s, %s, earshot's measured default",
				            law.value == earshot::HostBins::Value::Magnitude ? "magnitudes" : "powers",
				            law.layout == earshot::HostBins::Layout::Linear ? "linear" : "log" ) );
			}
		}

		//(b) The drum loop: every kick drives the 40-160 Hz noise up from where
		//it stood just before the kick, by the second update after it and the
		//next frame at the latest.
		const double late = 2.0 * HostSpectrum::kN / rate + 1.0 / 60.0 + 1e-6;
		double loopPeak   = 0.0;
		{
			Rig rig;
			if( !start( rig, rate ) )
				return 1;
			HostSpectrum host( rate, [ rate ]( double t ) { return drumLoopAt( t, kLoopKick | kLoopTop, rate ); } );
			feedHost( rig, host );
			std::vector< double > level, moved;
			for( int f = 0; f < 360; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				level.push_back( rig.plugin.AudioForTest().FallbackLevelForTest( 0 ) );
				const FrameData& fd = rig.plugin.LastFrame();
				double sum          = 0.0;
				for( float l : fd.lengths )
					sum += std::fabs( l / fd.flame - 1.0 );
				moved.push_back( fd.lengths.empty() ? 0.0 : sum / fd.lengths.size() );
			}
			int kicks = 0, driven = 0;
			double worstRatio = 1e30, worstLag = 0.0, moving = 0.0;
			for( int k = 0; k * kLoopBeat < 6.0; ++k )
			{
				const double at = k * kLoopBeat;
				if( at < 1.0 || at + late > 6.0 - 1.0 / 60.0 )
					continue;//the first second primes earshot's floors
				++kicks;
				const int before = static_cast< int >( std::floor( at * 60.0 - 1e-9 ) );//the last frame before the kick
				double peak = 0.0, lag = 0.0;
				for( int f = before + 1; f < static_cast< int >( level.size() ) && f / 60.0 <= at + late; ++f )
					if( level[ static_cast< size_t >( f ) ] > peak )
					{
						peak = level[ static_cast< size_t >( f ) ];
						lag  = f / 60.0 - at;
					}
				const double was   = level[ static_cast< size_t >( before ) ];
				const double ratio = peak / std::max( was, 1e-30 );
				if( peak > 1e-3 && ratio > 4.0 )
				{
					++driven;
					worstLag = std::max( worstLag, lag );
				}
				worstRatio = std::min( worstRatio, ratio );
				loopPeak   = std::max( loopPeak, peak );
			}
			for( size_t f = 60; f < moved.size(); ++f )
				moving = std::max( moving, moved[ f ] );
			Check( kicks > 0 && driven == kicks,
			       fmt( "the drum loop, %d Hz, 1..6 s: %d of %d kicks drive the 40-160 Hz noise up (peak over 1e-3 and over 4x its level the frame "
			            "before the kick; the least %.1fx), the latest peak %.1f ms after its kick (limit %.1f); loudest %.4f",
			            rate, driven, kicks, worstRatio, worstLag * 1e3, late * 1e3, loopPeak ) );
			Check( moving > 0.03, fmt( "the drum loop, %d Hz: the flames move (mean |flame/quiet - 1| up to %.3f, want over 0.03)", rate, moving ) );
		}

		//(c) Claps and hats alone: next to nothing in bin 0, so the 40-160 Hz
		//noise stays quiet while the bands above it move.
		{
			Rig rig;
			if( !start( rig, rate ) )
				return 1;
			HostSpectrum host( rate, [ rate ]( double t ) { return drumLoopAt( t, kLoopTop, rate ); } );
			feedHost( rig, host );
			if( !rig.Render( 60 ) )
				return 1;
			double low = 0.0, top = 0.0;
			for( int f = 60; f < 360; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				low = std::max( low, rig.plugin.AudioForTest().FallbackLevelForTest( 0 ) );
				top = std::max( top, rig.plugin.AudioForTest().FallbackLevelForTest( 3 ) );
			}
			Check( loopPeak > 0.0 && low < 0.05 * loopPeak && top > 10.0 * low,
			       fmt( "claps and hats alone, %d Hz, 1..6 s: the 40-160 Hz noise at most %.2e (want under 5%% of the kicks' %.4f); 2560-8000 Hz up to %.4f",
			            rate, low, loopPeak, top ) );
		}
	}

	//A rate no engine runs at is not one: earshot keeps its 48 kHz.
	{
		Rig rig;
		if( !start( rig, 0 ) )
			return 1;
		if( !rig.Render( 2 ) )
			return 1;
		const double read = rig.plugin.AudioForTest().HostRateForTest();
		Check( std::fabs( read - 48000.0 ) < 1e-9, fmt( "a host rate of 0: earshot keeps %.0f Hz (want 48000)", read ) );
	}

	//FFT Law and the host's rate changed in the same frame, then each alone:
	//neither may drop the other. earshot applies a configure() at the next
	//update() and a second one before it replaces the first, so a configure
	//built from config() (the applied one) loses whichever came first.
	{
		Rig rig;
		if( !start( rig, 48000 ) )
			return 1;
		if( !rig.Render( 30 ) )
			return 1;
		using L     = earshot::HostBins::Layout;
		using V     = earshot::HostBins::Value;
		auto reads  = [ & ]( const char* what, L layout, V value, double rate ) {
			const earshot::HostBins h = rig.plugin.AudioForTest().Engine().config().hostBins;
			auto name                 = []( L l, V v ) { return std::string( l == L::Log ? "log " : "linear " ) + ( v == V::Power ? "power" : "magnitude" ); };
			Check( h.layout == layout && h.value == value && std::fabs( h.sampleRate - rate ) < 1e-9,
			       fmt( "%s: earshot reads %s at %.0f Hz (want %s at %.0f)", what, name( h.layout, h.value ).c_str(), h.sampleRate,
			            name( layout, value ).c_str(), rate ) );
		};
		rig.Set( PT_FFT_LAW, static_cast< float >( FftLaw::LogMagnitude ) );
		rig.plugin.SetSampleRate( 44100u );
		if( !rig.Render( 2 ) )
			return 1;
		reads( "FFT Law to Log Mag and the rate to 44.1 kHz in one frame", L::Log, V::Magnitude, 44100.0 );
		rig.Set( PT_FFT_LAW, static_cast< float >( FftLaw::LinearPower ) );
		if( !rig.Render( 2 ) )
			return 1;
		reads( "then FFT Law alone, to Linear Power", L::Linear, V::Power, 44100.0 );
		rig.plugin.SetSampleRate( 48000u );
		if( !rig.Render( 2 ) )
			return 1;
		reads( "then the rate alone, to 48 kHz", L::Linear, V::Power, 48000.0 );
		rig.plugin.SetSampleRate( 96000u );
		rig.Set( PT_FFT_LAW, static_cast< float >( FftLaw::LogPower ) );
		if( !rig.Render( 2 ) )
			return 1;
		reads( "the rate to 96 kHz and FFT Law to Log Power in one frame", L::Log, V::Power, 96000.0 );
	}
	return Verdict();
}

//===========================================================================
// --device (GL): the menu, the states and the lamp, on a scripted backend.
//===========================================================================
bool lampIs( const double c[ 3 ], double r, double g, double b )
{
	return std::fabs( c[ 0 ] - r ) < 0.12 && std::fabs( c[ 1 ] - g ) < 0.12 && std::fabs( c[ 2 ] - b ) < 0.12;
}

/// The colour code a linear light becomes.
double codeOf( double light )
{
	return light <= 0.0031308 ? light * 12.92 : 1.055 * std::pow( light, 1.0 / 2.4 ) - 0.055;
}

int runDevice( const Perturb& perturb )
{
	std::printf( "\n=== device: the menu, the states and the pilot lamp, on a scripted capture backend (no audio device is touched)\n"
	             "    (100 ms: a third of the scripted open; a wall-time limit measures the scheduler, so it is generous)\n" );
	const int before = g_failures;
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		g_deviceNames = { "Test Mic", "Test Silent", "Test Slow" };
	}
	AudioInput::SetBackendFactoryForTest( scriptedBackend );
	struct Restore
	{
		~Restore()
		{
			AudioInput::SetBackendFactoryForTest( nullptr );
		}
	} restore;
	const Raster raster = kRasters.front();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	tubeDefaults( rig );
	if( perturb.menuByIndex )
	{
		rig.plugin.AudioForTest().menuByIndex = true;
		rig.Press( PT_RESCAN );
	}
	const unsigned int menu = static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_INPUT ) );
	auto lamp = [ & ]( double out[ 3 ] ) {
		const FrameData& fd = rig.plugin.LastFrame();
		const double pixel  = fd.field / rig.width;
		//Shaders.cpp's LampAt: in the housing, or in from the left edge.
		const double radius = std::max( 0.006, 3.0 * pixel );
		const int x         = static_cast< int >( ( std::max( -0.5 * fd.length - 0.08, -0.5 * fd.field + 2.0 * radius ) + 0.5 * fd.field ) / pixel );
		const int y         = static_cast< int >( ( fd.tubeY + 0.035 ) / pixel );
		const Floats p      = rig.Output();
		for( int c = 0; c < 3; ++c )
			out[ c ] = p[ ( static_cast< size_t >( y ) * rig.width + x ) * 4 + c ];
	};
	auto waitFor = [ & ]( std::function< bool() > done, double seconds ) {
		const auto start = std::chrono::steady_clock::now();
		while( std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count() < seconds )
		{
			rig.Render( 1 );
			if( done() )
				return true;
			std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
		}
		return false;
	};
	auto state = [ & ]() { return rig.plugin.AudioForTest().State(); };
	auto opened = [ & ]() {
		earshot::Status status;
		rig.plugin.AudioForTest().Engine().status( status );
		return std::string( status.opened );
	};
	const double green[ 3 ] = { codeOf( 0.05 ), codeOf( 0.9 ), codeOf( 0.15 ) };
	const double red[ 3 ]   = { codeOf( 0.9 ), codeOf( 0.03 ), codeOf( 0.02 ) };
	const double amber[ 3 ] = { codeOf( 1.0 ), codeOf( 0.45 ), codeOf( 0.03 ) };

	//(a) The menu.
	{
		const char* names[] = { kHostFftLabel, kDefaultLabel, "Test Mic", "Test Silent", "Test Slow" };
		const float keys[]  = { 0.0f, earshot::deviceKey( earshot::kDefaultDevice ), earshot::deviceKey( "Test Mic" ), earshot::deviceKey( "Test Silent" ),
                               earshot::deviceKey( "Test Slow" ) };
		bool ok             = rig.plugin.GetNumParamElements( menu ) == static_cast< unsigned int >( kInputSlots );
		std::string shown;
		for( unsigned int e = 0; e < rig.plugin.GetNumParamElements( menu ); ++e )
		{
			const std::string label = rig.plugin.GetParamElementName( menu, e );
			const float value       = elementValue( rig.plugin, menu, e );
			if( e < 5 )
			{
				ok = ok && label == names[ e ] && value == keys[ e ];
				shown += fmt( " \"%s\"=%.0f", label.c_str(), value );
			}
			else
				ok = ok && label == kEmptySlot && value == 0.0f;
		}
		rig.Render( 1 );
		double c[ 3 ];
		lamp( c );
		Check( ok && lampIs( c, amber[ 0 ], amber[ 1 ], amber[ 2 ] ) && state() == earshot::CaptureState::Closed,
		       fmt( "(a) the menu, by name and key:%s, then %d \"%s\" = 0; nothing open (%s), the lamp amber: the host's FFT (%.2f %.2f %.2f)",
		            shown.c_str(), kInputSlots - 5, kEmptySlot, earshot::toString( state() ), c[ 0 ], c[ 1 ], c[ 2 ] ) );
	}
	//(b) A device that delivers.
	float value = 0.0f;
	optionValue( rig.plugin, menu, "Test Mic", value );
	rig.Set( PT_INPUT, value );
	{
		const bool running = waitFor( [ & ] { return state() == earshot::CaptureState::Running && rig.plugin.AudioForTest().FromDevice(); }, 3.0 );
		//The device delivers on its own thread: give it a block before the frame.
		std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
		rig.Render( 1 );
		double c[ 3 ];
		lamp( c );
		Check( running && lampIs( c, green[ 0 ], green[ 1 ], green[ 2 ] ) && rig.plugin.LastFrame().tapSamples > 0,
		       fmt( "(b) Test Mic: %s, the duct fed from the tap (%d samples last frame), the lamp green (%.2f %.2f %.2f)", earshot::toString( state() ),
		            rig.plugin.LastFrame().tapSamples, c[ 0 ], c[ 1 ], c[ 2 ] ) );
	}
	//(b2) A field too narrow for the housing: the lamp slides in, still lit.
	{
		const float field = rig.plugin.GetFloatParameter( static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_FIELD ) ) );
		rig.Set( PT_FIELD, ParamFromField( 1.0 ) );
		rig.Render( 2 );
		double c[ 3 ];
		lamp( c );
		const bool housingOff = -0.5 * rig.plugin.LastFrame().length - 0.08 < -0.5 * rig.plugin.LastFrame().field;
		Check( housingOff && lampIs( c, green[ 0 ], green[ 1 ], green[ 2 ] ),
		       fmt( "(b2) Field 1.0 m, the housing off the frame (%s): the lamp in from the edge, still green (%.2f %.2f %.2f)", housingOff ? "yes" : "NO", c[ 0 ],
		            c[ 1 ], c[ 2 ] ) );
		rig.plugin.SetFloatParameter( static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_FIELD ) ), field );
		rig.Render( 1 );
	}
	//(c) The list reordered under it: Rescan keeps the device by name.
	{
		{
			std::lock_guard< std::mutex > lock( g_deviceMutex );
			g_deviceNames = { "Test Slow", "Test Silent", "Test Mic" };
		}
		rig.Press( PT_RESCAN );
		const bool same = waitFor( [ & ] { return state() == earshot::CaptureState::Running && opened() == "Test Mic"; }, 3.0 );
		Check( same, fmt( "(c) the device list reordered, then Rescan: still \"%s\" (%s)", opened().c_str(), earshot::toString( state() ) ) );
	}
	//(d) A slow open never holds up the render thread.
	optionValue( rig.plugin, menu, "Test Slow", value );
	rig.Set( PT_INPUT, value );
	{
		double longest   = 0.0;
		const auto start = std::chrono::steady_clock::now();
		while( std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count() < 0.6 )
		{
			const auto t0 = std::chrono::steady_clock::now();
			rig.Render( 1 );
			glFinish();
			longest = std::max( longest, std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - t0 ).count() );
			std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
		}
		const bool open = waitFor( [ & ] { return state() == earshot::CaptureState::Running; }, 3.0 );
		Check( longest <= 100.0 && open && g_opensOnMain.load() == 0,
		       fmt( "(d) Test Slow (a 300 ms open): the longest frame meanwhile took %.1f ms (<= 100); then %s; opens on the render thread: %d", longest,
		            earshot::toString( state() ), g_opensOnMain.load() ) );
	}
	//(e) A device that delivers zeros: Silent, shown, not papered over.
	optionValue( rig.plugin, menu, "Test Silent", value );
	rig.Set( PT_INPUT, value );
	{
		const bool silent = waitFor( [ & ] { return state() == earshot::CaptureState::Silent; }, 6.0 );
		double c1[ 3 ], c2[ 3 ];
		rig.Render( 1 );
		lamp( c1 );
		rig.Render( 15 );
		lamp( c2 );
		double still = 0.0;
		for( float l : rig.plugin.LastFrame().lengths )
			still = std::max( still, std::fabs( l / rig.plugin.LastFrame().flame - 1.0 ) );
		Check( silent && lampIs( c1, red[ 0 ], red[ 1 ], red[ 2 ] ) && lampIs( c2, red[ 0 ], red[ 1 ], red[ 2 ] ) && still < 0.01,
		       fmt( "(e) Test Silent: %s, the lamp steady red (%.2f %.2f %.2f, then %.2f %.2f %.2f), the flames quiet (%.3f)", earshot::toString( state() ),
		            c1[ 0 ], c1[ 1 ], c1[ 2 ], c2[ 0 ], c2[ 1 ], c2[ 2 ], still ) );
	}
	//(f) A saved device that is not here: Missing, never the default.
	const float goneKey   = earshot::deviceKey( "Gone Mic" );
	const int opensBefore = g_opens.load();
	rig.Set( PT_INPUT, goneKey );
	{
		const bool missing = waitFor( [ & ] { return state() == earshot::CaptureState::Missing; }, 3.0 );
		double c1[ 3 ], c2[ 3 ];
		rig.Render( 1 );
		lamp( c1 );
		rig.Render( 15 );
		lamp( c2 );
		const bool blinks = std::fabs( c1[ 0 ] - c2[ 0 ] ) >= 0.4;
		Check( missing && opened().empty() && g_opens.load() == opensBefore && blinks,
		       fmt( "(f) a saved key with no device: %s, nothing opened in its place (\"%s\"), the lamp blinking red (R %.2f then %.2f)",
		            earshot::toString( state() ), opened().c_str(), c1[ 0 ], c2[ 0 ] ) );
	}
	//(g) Rescan with it back reopens it by name.
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		g_deviceNames.push_back( "Gone Mic" );
	}
	rig.Press( PT_RESCAN );
	{
		bool listed = false;
		for( unsigned int e = 0; e < rig.plugin.GetNumParamElements( menu ); ++e )
			listed = listed || ( std::string( rig.plugin.GetParamElementName( menu, e ) ) == "Gone Mic" && elementValue( rig.plugin, menu, e ) == goneKey );
		const bool running = waitFor( [ & ] { return state() == earshot::CaptureState::Running && opened() == "Gone Mic"; }, 3.0 );
		Check( listed && running, fmt( "(g) Rescan with Gone Mic connected: listed with its key (%s), reopened by name (%s, \"%s\")", listed ? "yes" : "no",
		                               earshot::toString( state() ), opened().c_str() ) );
	}
	//(h) Back to the host FFT: amber while its buffer is written.
	rig.Set( PT_INPUT, 0.0f );
	rig.Bins( []( int ) { return 0.1; } );
	{
		const bool closed = waitFor( [ & ] { return state() == earshot::CaptureState::Closed; }, 3.0 );
		rig.Render( 2 );
		double c[ 3 ];
		lamp( c );
		Check( closed && lampIs( c, amber[ 0 ], amber[ 1 ], amber[ 2 ] ),
		       fmt( "(h) None (host FFT) with the buffer written: %s, the lamp amber (%.2f %.2f %.2f)", earshot::toString( state() ), c[ 0 ], c[ 1 ], c[ 2 ] ) );
	}
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		g_deviceNames = { "Test Mic", "Test Silent", "Test Slow" };
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --alpha (GL): the Over's alpha, decided: A = 1 - (1 - a_clip)(1 - a_flame)(1 - a_tube),
// a_flame the flames' largest encoded channel, then lifted to the largest
// encoded channel so the output is a valid premultiplied colour.
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: the Over at Mix 0 is the clip; a clip with alpha keeps it away from the flames and the tube; the tube is opaque, the "
	             "flames add coverage; the source is opaque\n" );
	for( const Raster& raster : kRasters )
	{
		{
			const Floats clip = noiseCard( raster.w, raster.h, true );
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &clip ) )
				return 1;
			tubeDefaults( rig );
			rig.Set( PT_MIX, 0.0f );
			rig.feeder.signal = musicAt;
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			int differ       = 0;
			for( size_t i = 0; i < out.size(); ++i )
				differ += out[ i ] != clip[ i ];
			Check( differ == 0, fmt( "%dx%d: Mix 0 against a per-pixel noise card, alpha too: %d of %zu floats differ", raster.w, raster.h, differ, out.size() ) );
		}
		for( float a : { 0.0f, 0.4f } )
		{
			const Floats clip = flatCard( raster.w, raster.h, 0.3f * a, 0.2f * a, 0.1f * a, a );
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &clip ) )
				return 1;
			tubeDefaults( rig );
			rig.Set( PT_MIX, 1.0f );
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out    = rig.Output();
			const FrameData& fd = rig.plugin.LastFrame();
			const double pixel  = fd.field / raster.w;
			double worstAway = 0.0, tubeAlpha = 1.0, flameAlpha = 0.0, worstOver = 0.0;
			int away = 0, overAlpha = 0;
			for( int y = 0; y < raster.h; ++y )
				for( int x = 0; x < raster.w; ++x )
				{
					const double my = ( y + 0.5 ) * pixel, mx = ( x + 0.5 ) * pixel - 0.5 * fd.field;
					const float* p  = &out[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
					//Premultiplied: no channel above the alpha, anywhere.
					const double excess = std::max( { p[ 0 ], p[ 1 ], p[ 2 ] } ) - p[ 3 ];
					worstOver           = std::max( worstOver, excess );
					overAlpha += excess > 1e-4;
					//Away: below the tube's housing, or above the tallest flame.
					if( my < fd.tubeY - 0.08 || my > fd.tubeY + tube::kRadius + 2.0 * fd.flame + 0.02 )
					{
						++away;
						worstAway = std::max( worstAway, static_cast< double >( std::fabs( p[ 3 ] - a ) ) );
						for( int c = 0; c < 3; ++c )
							worstAway = std::max( worstAway, static_cast< double >( std::fabs( p[ c ] - clip[ static_cast< size_t >( c ) ] ) ) );
					}
					if( std::fabs( my - fd.tubeY ) < 0.5 * tube::kRadius && std::fabs( mx ) < 0.4 * fd.length )
						tubeAlpha = std::min( tubeAlpha, static_cast< double >( p[ 3 ] ) );
					if( std::fabs( my - ( fd.tubeY + tube::kRadius + 0.5 * fd.flame ) ) < pixel && std::fabs( mx ) < 0.4 * fd.length )
						flameAlpha = std::max( flameAlpha, static_cast< double >( p[ 3 ] ) );
				}
			Check( away > 0 && worstAway < 1e-5 && tubeAlpha > 1.0 - 1e-6 && flameAlpha > a + 0.2 && overAlpha == 0,
			       fmt( "%dx%d: a clip at alpha %.1f (premultiplied): away from tube and flames (%d px) colour and alpha off the clip by at most %.1e "
			            "(<= 1e-5); the tube's alpha %.4f (1); the flames' up to %.3f; a channel above its alpha at %d px (0; worst by %.4f)",
			            raster.w, raster.h, a, away, worstAway, tubeAlpha, flameAlpha, overAlpha, std::max( worstOver, 0.0 ) ) );
		}
		{
			Rig rig( false );
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			tubeDefaults( rig );
			rig.feeder.signal = musicAt;
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			int notOpaque    = 0;
			for( size_t i = 3; i < out.size(); i += 4 )
				notOpaque += out[ i ] != 1.0f;
			Check( notOpaque == 0, fmt( "%dx%d: the source's alpha is 1 at %zu of %zu pixels", raster.w, raster.h, out.size() / 4 - notOpaque, out.size() / 4 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --refraction (GL): the plume's optical path and the backdrop's
// displacement against the line-of-sight integral in closed form.
//===========================================================================

/// P( x, y ) in metres of optical path (negative) and its gradient, from the
/// spec's column model and the Gladstone-Dale index deficit, every hole.
void plumeClosed( const FrameData& fd, double mx, double my, double& P, double& gx, double& gy )
{
	const double n0 = kGladstoneDale * kAirDensity;
	const double h  = my - ( fd.tubeY + tube::kRadius );
	P = gx = gy = 0.0;
	if( h <= 0.0 )
		return;
	const double bMax = ( shaders::kMaxWindow - 1 ) * fd.pitch / 3.5;
	for( int i = 0; i < fd.holes; ++i )
	{
		const double Lf = fd.lengths[ static_cast< size_t >( i ) ];
		if( Lf <= 1e-4 )
			continue;
		const double dx   = mx - ( -0.5 * fd.length + ( i + 0.5 ) * fd.pitch );
		const double bTip = 0.002 + 0.12 * Lf;
		double b, db, th, dth;
		if( h < Lf )
		{
			b   = 0.002 + 0.12 * Lf * std::sqrt( h / Lf );
			db  = 0.06 * Lf / std::sqrt( h * Lf );
			th  = 0.7 * ( 1.0 - std::exp( -h / 0.003 ) );
			dth = 0.7 * std::exp( -h / 0.003 ) / 0.003;
		}
		else
		{
			b   = bTip + 0.12 * ( h - Lf );
			db  = 0.12;
			th  = 0.7 * std::pow( bTip / b, 1.5 );
			dth = -1.5 * th / b * db;
		}
		if( b > bMax )
		{
			b  = bMax;
			db = 0.0;
		}
		if( std::fabs( dx ) > 3.5 * b )
			continue;
		const double e = std::exp( -dx * dx / ( b * b ) );
		const double k = -n0 * std::sqrt( kPi );
		P += k * th * b * e;
		gx += k * th * b * e * ( -2.0 * dx / ( b * b ) );
		//d/dh of th b e^{-dx^2/b^2}: dth b e + th db e + th b e (2 dx^2 / b^3) db
		gy += k * ( dth * b * e + th * db * e + th * b * e * 2.0 * dx * dx / ( b * b * b ) * db );
	}
}

int runRefraction( const Perturb& perturb )
{
	std::printf( "\n=== refraction: the plume's optical path P against the Gaussian columns' line-of-sight integral, and the backdrop's displacement "
	             "against Distance x grad P (the ray bends toward the denser, cooler air)\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		tubeDefaults( rig );
		rig.Set( PT_FLAME, ParamFromFlame( 0.2 ) );
		rig.Set( PT_DISTANCE, ParamFromDistance( 30.0 ) );
		rig.Set( PT_FIELD, ParamFromField( 0.8 ) );
		rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookDisplacement );
		if( !rig.Render( 2 ) )
			return 1;
		const Floats out    = rig.Output();
		const FrameData& fd = rig.plugin.LastFrame();
		const double pixel  = fd.field / raster.w;
		double worstShift = 0.0, worstP = 0.0, maxP = 0.0, maxShift = 0.0;
		int compared = 0;
		for( int y = 2; y < raster.h - 2; ++y )
			for( int x = 2; x < raster.w - 2; ++x )
			{
				const double mx = ( x + 0.5 ) * pixel - 0.5 * fd.field, my = ( y + 0.5 ) * pixel;
				double P, gx, gy;
				plumeClosed( fd, mx, my, P, gx, gy );
				const float* p = &out[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
				maxP           = std::max( maxP, std::fabs( P * 1e6 ) );
				worstP         = std::max( worstP, std::fabs( p[ 2 ] - P * 1e6 ) );
				//Where every column is at least 8 pixels wide, the 2-pixel central
				//difference is the gradient to ~1.6%.
				const double h = my - ( fd.tubeY + tube::kRadius );
				if( h <= 0.0 )
					continue;
				const double bMin = h < fd.flame ? 0.002 + 0.12 * fd.flame * std::sqrt( h / fd.flame ) : 0.002 + 0.12 * fd.flame + 0.12 * ( h - fd.flame );
				if( bMin < 8.0 * pixel )
					continue;
				//Nor across the column model's two kinds (its slope changes at
				//the flame's tip and where the width is capped): a central
				//difference there is the mean of two slopes.
				const double bMax  = ( shaders::kMaxWindow - 1 ) * fd.pitch / 3.5;
				const double hCap  = fd.flame + ( bMax - ( 0.002 + 0.12 * fd.flame ) ) / 0.12;
				if( std::fabs( h - fd.flame ) < 1.5 * pixel || std::fabs( h - hCap ) < 1.5 * pixel )
					continue;
				const double ex = fd.distance * gx / pixel, ey = fd.distance * gy / pixel;
				const double err = std::hypot( p[ 0 ] - ex, p[ 1 ] - ey ) / ( 0.1 + 0.02 * std::hypot( ex, ey ) );
				worstShift       = std::max( worstShift, err );
				maxShift         = std::max( maxShift, std::hypot( ex, ey ) );
				++compared;
			}
		Check( worstP < 1e-3 * maxP + 1e-4 && compared > 1000 && worstShift <= 1.0 && maxShift > 0.5,
		       fmt( "%dx%d, Flame 20 cm, Distance 30 m, Field 0.8 m, Shimmer 0: P off the closed form by %.1e um of %.2f um; the displacement (up to %.1f px) within "
		            "0.1 px + 2%% at %d px where columns are >= 8 px wide: worst %.2f of the tolerance",
		            raster.w, raster.h, worstP, maxP, maxShift, compared, worstShift ) );

		//The Over samples its clip where the displacement says: a clip that is
		//its own coordinates (R = x, G = y, float, linearly filtered) read back
		//above the flames' tips, where nothing but the clip is drawn.
		Floats ramp( static_cast< size_t >( raster.w ) * raster.h * 4 );
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
			{
				float* q = &ramp[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
				q[ 0 ]   = ( x + 0.5f ) / raster.w;
				q[ 1 ]   = ( y + 0.5f ) / raster.h;
				q[ 2 ]   = 0.0f;
				q[ 3 ]   = 1.0f;
			}
		Rig over( true );
		if( !startRig( over, raster.w, raster.h, perturb, &ramp ) )
			return 1;
		tubeDefaults( over );
		over.Set( PT_FLAME, ParamFromFlame( 0.2 ) );
		over.Set( PT_DISTANCE, ParamFromDistance( 30.0 ) );
		over.Set( PT_FIELD, ParamFromField( 0.8 ) );
		over.Set( PT_MIX, 1.0f );
		over.plugin.SetHooksForTest( perturb.hooks | shaders::kHookDisplacement );
		if( !over.Render( 2 ) )
			return 1;
		const Floats shiftField = over.Output();
		over.plugin.SetHooksForTest( perturb.hooks );
		if( !over.Render( 1 ) )
			return 1;
		const Floats seen    = over.Output();
		const FrameData& od  = over.plugin.LastFrame();
		double worstRead = 0.0, largest = 0.0;
		int read = 0;
		for( int y = 2; y < raster.h - 2; ++y )
			for( int x = 2; x < raster.w - 2; ++x )
			{
				const double h = ( y + 0.5 ) * pixel - ( od.tubeY + tube::kRadius );
				if( h < od.flame * 1.2 + 3.0 * pixel )
					continue;
				const size_t at  = ( static_cast< size_t >( y ) * raster.w + x ) * 4;
				const double sx  = shiftField[ at ], sy = shiftField[ at + 1 ];
				if( std::hypot( sx, sy ) < 0.1 )
					continue;
				const double rx = seen[ at ] * raster.w - 0.5 - x, ry = seen[ at + 1 ] * raster.h - 0.5 - y;
				worstRead       = std::max( worstRead, std::hypot( rx - sx, ry - sy ) );
				largest         = std::max( largest, std::hypot( sx, sy ) );
				++read;
			}
		Check( read > 50 && worstRead <= 0.02,
		       fmt( "%dx%d: the Over reads its clip at the pixel plus the displacement: a coordinate ramp read back above the tips at %d px "
		            "(shifts 0.1 to %.2f px): off by at most %.4f px (<= 0.02)",
		            raster.w, raster.h, read, largest, worstRead ) );
	}
	return Verdict();
}

//===========================================================================
// --clock, --resize, --state.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: at 499,000,000 ms the frames are still 1/60 s apart and the shimmer still rises 0.8 m/s x dt a frame\n" );
	const Raster raster = kRasters.back();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	rig.plugin.SetClockScaleForTest( 0.001 );
	double worstDt = 0.0, worstRise = 0.0;
	double lastNow = 0.0, lastY = 0.0;
	for( int f = 0; f < 20; ++f )
	{
		rig.plugin.SetTime( 499000000.0 + f * 1000.0 / 60.0 );
		glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
		glViewport( 0, 0, raster.w, raster.h );
		if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
			return 1;
		const double now = rig.plugin.HostNow();
		const double y   = rig.plugin.ShimmerOffsets()[ 1 ];
		if( f > 1 )
		{
			worstDt     = std::max( worstDt, std::fabs( ( now - lastNow ) - 1.0 / 60.0 ) );
			double step = ( lastY - y ) * kShimmerCell;//metres risen: the lattice moves down
			if( step < 0.0 )
				step += kLatticePeriod * kShimmerCell;
			worstRise = std::max( worstRise, std::fabs( step - kShimmerRise / 60.0 ) );
		}
		lastNow = now;
		lastY   = y;
	}
	Check( worstDt < 1e-9 && worstRise < 1e-6,
	       fmt( "20 frames: dt off 1/60 by at most %.1e s; the rise off 0.8 m/s x dt by at most %.1e m", worstDt, worstRise ) );
	return Verdict();
}

int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes mid-run (and its shape), the picture is exactly a fresh instance's that heard the "
	             "same audio\n" );
	const Raster small = kRasters.back(), big = { 2 * small.w, 2 * small.h }, tall = { small.w, small.w * 3 / 4 };
	for( const Raster& to : { small, tall } )
		for( bool over : { false, true } )
		{
			Rig resized( over );
			if( !startRig( resized, big.w, big.h, perturb ) )
				return 1;
			tubeDefaults( resized );
			resized.Set( PT_SHIMMER, 0.5f );
			resized.feeder.signal = musicAt;
			if( !resized.Render( 70 ) || !resized.Resize( to.w, to.h ) || !resized.Render( 3 ) )
				return 1;
			Rig fresh( over );
			if( !startRig( fresh, to.w, to.h, Perturb {} ) )
				return 1;
			tubeDefaults( fresh );
			fresh.Set( PT_SHIMMER, 0.5f );
			fresh.feeder.signal = musicAt;
			if( !fresh.Render( 73 ) )
				return 1;
			const Floats x = resized.Output(), y = fresh.Output();
			int differ = 0;
			double lit = 0.0;
			for( size_t i = 0; i < x.size() && i < y.size(); ++i )
			{
				differ += byteOf( x[ i ] ) != byteOf( y[ i ] );
				lit = std::max( lit, static_cast< double >( y[ i ] ) );
			}
			Check( differ == 0 && lit > 0.1 && x.size() == y.size(),
			       fmt( "%s: %dx%d for 70 frames, then %dx%d for 3, against %dx%d for all 73: %d bytes differ", over ? "Over" : "source", big.w, big.h, to.w,
			            to.h, to.w, to.h, differ ) );
		}
	return Verdict();
}

int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back, both plugins\n" );
	for( bool over : { false, true } )
	{
		Rig rig( over );
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.feeder.signal = musicAt;
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		int problems = 0;
		std::string what;
		for( int frame = 0; frame < 6; ++frame )
		{
			rig.feeder.Frame( rig.plugin, frame, 60.0 );
			rig.Set( PT_RESCAN, frame == 1 ? 1.0f : 0.0f );
			rig.Set( PT_BACKDROP, static_cast< float >( frame % 3 ) );
			rig.Set( PT_SHIMMER, frame % 2 ? 1.0f : 0.0f );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 0, 0, 320, 180 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0, unpack = 0;
			GLfloat clear[ 4 ]   = {};
			GLboolean mask[ 4 ]  = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_UNPACK_ALIGNMENT, &unpack );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			glGetBooleanv( GL_COLOR_WRITEMASK, mask );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
			expect( unpack == 4, "unpack-alignment" );
			for( int u = 0; u < 8; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		Check( problems == 0, fmt( "%s, six frames, a Rescan, every backdrop, shimmer on and off: viewport, vertex array, array buffer, program, active unit, "
		                           "framebuffer, blend, scissor, clear colour, colour mask, unpack alignment, eight texture units (%d wrong:%s)",
		                           over ? "SW Rubens Over" : "SW Rubens", problems, what.empty() ? " none" : what.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --names and --cues (no GL).
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too), within FFGL's 16 characters, the About block last\n" );
	for( bool over : { false, true } )
	{
		RubensPlugin plugin( over );
		std::map< std::string, int > seen, address;
		int longNames = 0, dupes = 0, clashes = 0, slashes = 0;
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
			slashes += name.find( '/' ) != std::string::npos;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( address[ key ]++ > 0 )
				++clashes;
		}
		const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
		const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
		const bool right              = over ? plugin.HostIndexOf( PT_BACKDROP ) < 0 && plugin.HostIndexOf( PT_MIX ) >= 0
		                                     : plugin.HostIndexOf( PT_MIX ) < 0 && plugin.HostIndexOf( PT_BACKDROP ) >= 0;
		Check( longNames == 0 && dupes == 0 && clashes == 0 && slashes == 0 && aboutLast && right,
		       fmt( "%s: %u parameters, %d too long, %d duplicated, %d clashing addresses, %d with a '/'; the About block is last (%s); %s", over ? "SW Rubens Over" : "SW Rubens",
		            plugin.GetNumParams(), longNames, dupes, clashes, slashes, aboutLast ? "yes" : "NO",
		            over ? "Mix, no Backdrop" : "Backdrop, no Mix" ) );
	}
	return Verdict();
}

int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options and events, ramps standard controls, and names an option by its name\n" );
	RubensPlugin plugin( false );
	std::istringstream sheet( "0 Backdrop Black\n60 Backdrop Dots\n0 Rescan Inputs 0\n60 Rescan Inputs 1\n0 Volume 0\n60 Volume 1\n0 Channel L+R\n"
	                          "60 Channel Right\n" );
	std::map< unsigned int, Cue > cues;
	std::string error;
	bindScript( plugin, sheet, "cues", cues, error );
	int bad = 0;
	std::string what;
	for( const auto& entry : cues )
	{
		const unsigned int type = plugin.GetParamType( entry.first );
		const bool ramp         = perturb.cuesRamp ? true : entry.second.ramp;
		const float mid         = valueAt( entry.second.track, 29, ramp );
		const float a = entry.second.track.front().second, b = entry.second.track.back().second;
		const bool expectRamp = type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( entry.second.track, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", plugin.GetParamName( entry.first ), expectRamp ? "ramps " : "steps ", mid );
	}
	const auto backdrop = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_BACKDROP ) ) );
	const auto channel  = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_CHANNEL ) ) );
	const bool named    = backdrop != cues.end() && backdrop->second.track.back().second == static_cast< float >( Backdrop::Dots ) && channel != cues.end()
	                   && channel->second.track.back().second == static_cast< float >( Channel::Right );
	Check( error.empty() && bad == 0 && cues.size() == 4 && named,
	       fmt( "%d wrong:%s; 'Dots' and 'Right' read as their options: %s", bad, what.c_str(), named ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "resonance", runResonance, true }, { "orifice", runOrifice, true },   { "offres", runOffres, true },         { "crossover", runCrossover, false },
		{ "spacing", runSpacing, false },    { "rates", runRates, false },         { "fallback", runFallback, false },    { "arenafft", runArenaFft, false },
		{ "device", runDevice, false },
		{ "alpha", runAlpha, false },        { "refraction", runRefraction, false }, { "clock", runClock, false },     { "resize", runResize, false },
		{ "state", runState, false },        { "names", runNames, true },       { "cues", runCues, true },
	};
	return list;
}

bool isOffline( const char* name )
{
	for( const CheckEntry& c : checks() )
		if( std::strcmp( c.flag, name ) == 0 )
			return c.offline;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	using namespace shaders;
	add( "resonance", runResonance, "the duct's modes without the holes' shunt", []( Perturb& p ) { p.ductHooks = kDuctNoHoleShift; } );
	add( "resonance", runResonance, "the piston's forcing halved", []( Perturb& p ) { p.ductHooks = kDuctHalfDrive; } );
	add( "orifice", runOrifice, "a linear efflux (q = dp / R): no square root", []( Perturb& p ) { p.ductHooks = kDuctLinearEfflux; } );
	add( "offres", runOffres, "the duct's modes without the holes' shunt", []( Perturb& p ) { p.ductHooks = kDuctNoHoleShift; } );
	add( "crossover", runCrossover, "the flames burn the signed flow (inflow subtracted)", []( Perturb& p ) { p.ductHooks = kDuctSignedFlow; } );
	add( "crossover", runCrossover, "a linear efflux (q = dp / R): no square root", []( Perturb& p ) { p.ductHooks = kDuctLinearEfflux; } );
	add( "spacing", runSpacing, "the duct's modes without the holes' shunt", []( Perturb& p ) { p.ductHooks = kDuctNoHoleShift; } );
	add( "rates", runRates, "the flames follow their fuel once a frame, not once a sample", []( Perturb& p ) { p.ductHooks = kDuctPerFrame; } );
	add( "fallback", runFallback, "the fallback reads the (empty) tap", []( Perturb& p ) { p.fallbackFromTap = true; } );
	add( "arenafft", runArenaFft, "Resolume's bins filed whole under the band holding each one's centre (earshot 8124191's rule): the 40-160 Hz band gets none",
	     []( Perturb& p ) { p.hostCentreOnly = true; } );
	add( "arenafft", runArenaFft, "the host's sample rate never reaches earshot", []( Perturb& p ) { p.ignoreHostRate = true; } );
	add( "device", runDevice, "menu values are positions, not keys", []( Perturb& p ) { p.menuByIndex = true; } );
	add( "alpha", runAlpha, "the Over's alpha forced to 1", []( Perturb& p ) { p.hooks = kHookAlphaOne; } );
	add( "refraction", runRefraction, "the deflection's sign flipped", []( Perturb& p ) { p.hooks = kHookFlipDeflection; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 && g_failures == 0 ? "PASS" : "FAIL" );
	return failed == 0 && g_failures == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED, and the CPU's duct.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		bool over;
		std::function< void( Rig& ) > set;
		bool at4K;///< 4K once, short (a shared machine: no 4K loops)
	};
	const Case cases[] = {
		{ "source, defaults", false, []( Rig& ) {}, true },
		{ "source, 160 holes (3 m, 1.9 cm)", false,
		  []( Rig& r ) {
			  r.Set( PT_LENGTH, 1.0f );
			  r.Set( PT_PITCH, ParamFromPitch( 0.0188 ) );
			  r.Set( PT_FIELD, ParamFromField( 3.4 ) );
		  },
		  false },
		{ "Over, defaults", true, []( Rig& ) {}, true },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) and wall time per frame (the duct on the CPU included), the harness's track "
	             "fed; after 240 frames of warm-up, median and worst of 120 (4K: 30 and 30, defaults only)\n" );
	//Wake the GPU first, so the first case is not measured from idle. The
	//machine is shared: the numbers still wander between runs (AGENTS.md).
	{
		Rig warm( false );
		if( !warm.Init( 1920, 1080 ) )
			return 1;
		warm.feeder.signal = musicAt;
		if( !warm.Render( 600 ) )
			return 1;
	}
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			const bool fourK = size.w == 3840;
			if( fourK && !c.at4K )
				continue;
			Rig rig( c.over );
			if( !rig.Init( size.w, size.h ) )
				return 1;
			rig.feeder.signal = musicAt;
			c.set( rig );
			if( !rig.Render( fourK ? 30 : 240 ) )
				return 1;
			const int kTimed = fourK ? 30 : 120;
			std::vector< double > times, cpu;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-34s %-6s GPU median %6.2f ms, worst %6.2f  (%5.1f%% of a 60 fps frame); wall %5.2f ms; %d holes\n", c.what, size.name,
			             times[ kTimed / 2 ], times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ], rig.plugin.LastFrame().holes );
		}
	glDeleteQueries( 1, &query );
	//The duct alone: a second of the track at the tap rate.
	{
		Duct d;
		d.Configure( TubeSpec {} );
		std::vector< float > v( static_cast< size_t >( kFs ) );
		for( size_t k = 0; k < v.size(); ++k )
			v[ k ] = static_cast< float >( 0.15 * musicAt( k * kDt ) );
		const auto start = std::chrono::steady_clock::now();
		for( int r = 0; r < 5; ++r )
			d.Process( v.data(), static_cast< int >( v.size() ) );
		const double ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() / 5.0;
		std::printf( "  the duct alone (default tube, %d holes, %d modes): %.2f ms per second of audio (%.3f ms per 60 fps frame)\n", d.Holes(), d.Modes(), ms,
		             ms / 60.0 );
	}
	return 0;
}
