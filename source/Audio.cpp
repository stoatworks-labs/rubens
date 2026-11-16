#include "Audio.h"

#include "Diag.h"
#include "Duct.h"

#include <earshot/testing.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace rubens
{
namespace
{
BackendFactory g_factory = nullptr;

/// The bands, Hz: the fallback's noise and earshot's envelopes. Two octaves
/// each from 40 Hz, the last to the tap's Nyquist; the tube's modes stop at
/// the cross-mode cut-on, 2.78 kHz for the default bore.
constexpr float kBandEdges[ 5 ] = { 40.0f, 160.0f, 640.0f, 2560.0f, 8000.0f };
constexpr int kBands            = 4;
/// Most samples one frame takes from the tap (a second); older ones go.
constexpr int kMaxPull = tube::kTapRate;
/// The fallback's longest frame, in seconds.
constexpr double kMaxFallbackStep = 0.25;

/// RUBENS_NO_DEVICES=1: never ask the OS for its devices (verify.sh sets it
/// for oxbow, so no step of verification talks to CoreAudio at all).
bool devicesDisabled()
{
	const char* v = std::getenv( "RUBENS_NO_DEVICES" );
	return v != nullptr && *v != '\0' && std::strcmp( v, "0" ) != 0;
}

earshot::Config makeConfig()
{
	earshot::Config c;
	c.maxChannels = 2;
	c.tapRate     = tube::kTapRate;
	c.bandCount   = kBands;
	for( int i = 0; i <= kBands; ++i )
		c.bandEdges[ i ] = kBandEdges[ i ];
	return c;
}

uint32_t xorshift( uint32_t& s )
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}
} // namespace

void AudioInput::SetBackendFactoryForTest( BackendFactory factory )
{
	g_factory = factory;
}

AudioInput::AudioInput() :
	left( kMaxPull ), right( kMaxPull )
{
	if( g_factory != nullptr )
		engine = std::make_unique< earshot::Engine >( makeConfig(), g_factory() );
	else
		engine = std::make_unique< earshot::Engine >( makeConfig() );
	if( !engine->ok() )
		diag::error( "earshot could not allocate: the tube will not hear anything" );

	//The fallback's band-passes (two poles, unit peak gain), each normalised
	//so its noise has unit RMS: measured once on the same generator.
	bands.resize( kBands );
	const double fs = tube::kTapRate;
	for( int b = 0; b < kBands; ++b )
	{
		Band& band       = bands[ static_cast< size_t >( b ) ];
		const double lo  = kBandEdges[ b ], hi = std::min< double >( kBandEdges[ b + 1 ], 0.49 * fs );
		const double f0  = std::sqrt( lo * hi );
		const double q   = f0 / ( hi - lo );
		const double w0  = 2.0 * kPi * f0 / fs;
		const double al  = std::sin( w0 ) / ( 2.0 * q );
		const double a0  = 1.0 + al;
		band.b0          = al / a0;
		band.b2          = -al / a0;
		band.a1          = -2.0 * std::cos( w0 ) / a0;
		band.a2          = ( 1.0 - al ) / a0;
		uint32_t s       = 0x1234567u + static_cast< uint32_t >( b );
		double x1 = 0, x2 = 0, y1 = 0, y2 = 0, sum = 0.0;
		const int kCount = 1 << 16;
		for( int k = 0; k < kCount + 4096; ++k )
		{
			const double x = xorshift( s ) * ( 2.0 / 4294967296.0 ) - 1.0;
			const double y = band.b0 * x + band.b2 * x2 - band.a1 * y1 - band.a2 * y2;
			x2 = x1;
			x1 = x;
			y2 = y1;
			y1 = y;
			if( k >= 4096 )
				sum += y * y;
		}
		band.gain = 1.0 / std::sqrt( std::max( sum / kCount, 1e-30 ) );
	}
	Refresh();
}

AudioInput::~AudioInput() = default;

double AudioInput::nextNoise()
{
	return xorshift( noiseState ) * ( 2.0 / 4294967296.0 ) - 1.0;
}

void AudioInput::Refresh()
{
	slots.clear();
	slots.push_back( { kHostFftLabel, "", 0.0f } );
	slots.push_back( { kDefaultLabel, earshot::kDefaultDevice, earshot::deviceKey( earshot::kDefaultDevice ) } );
	std::vector< earshot::DeviceInfo > devices;
	if( g_factory != nullptr )
	{
		//A throwaway backend of the harness's kind: it lists, it opens nothing.
		if( auto lister = g_factory() )
			devices = lister->enumerate();
	}
	else if( !devicesDisabled() )
		devices = earshot::listDevices();
	for( const earshot::DeviceInfo& d : devices )
	{
		if( static_cast< int >( slots.size() ) >= kInputSlots )
			break;
		const float key = earshot::deviceKey( d.name.c_str() );
		seen[ key ]     = d.name;
		slots.push_back( { d.name.substr( 0, 30 ), d.name, key } );
	}
	while( static_cast< int >( slots.size() ) < kInputSlots )
		slots.push_back( { kEmptySlot, "", 0.0f } );
	if( menuByIndex )
		for( size_t i = 0; i < slots.size(); ++i )
			slots[ i ].value = static_cast< float >( i );
	diag::info( "audio inputs: " + std::to_string( devices.size() ) + " device(s) listed" );
}

void AudioInput::Select( float value )
{
	if( value == applied )
		return;
	applied = value;
	feeding = false;//any select ends a feed
	std::string name;
	if( menuByIndex )
	{
		const int index = std::clamp( static_cast< int >( std::lround( value ) ), 0, static_cast< int >( slots.size() ) - 1 );
		name            = slots[ static_cast< size_t >( index ) ].name;
	}
	else if( value == 0.0f )
		name.clear();
	else if( value == earshot::deviceKey( earshot::kDefaultDevice ) )
		name = earshot::kDefaultDevice;
	else
	{
		const auto known = seen.find( value );
		//A key with no device met in this process: ask for a name that cannot
		//match, so earshot says Missing (never the default). Rescan resolves it
		//once the device is back.
		name = known != seen.end() ? known->second : "(device " + std::to_string( static_cast< long >( value ) ) + " not present)";
	}
	//RUBENS_NO_DEVICES=1 (verification): a device is never opened, nor even
	//looked up -- earshot's lookup itself starts CoreAudio. The host's FFT.
	if( !name.empty() && g_factory == nullptr && devicesDisabled() )
	{
		diag::info( "audio input: " + name + " not opened (RUBENS_NO_DEVICES): host FFT" );
		name.clear();
	}
	engine->selectDevice( name.c_str() );
	diag::info( "audio input: " + ( name.empty() ? std::string( "host FFT" ) : name ) );
}

const earshot::Frame& AudioInput::Frame() const
{
	return engine->frame();
}

earshot::CaptureState AudioInput::State() const
{
	return engine->captureState();
}

void AudioInput::Feed( const float* interleaved, int frames, int rate, int channels )
{
	if( !feeding )
	{
		engine->feedBegin( rate, channels );
		feeding = true;
	}
	engine->feed( interleaved, frames );
}

int AudioInput::Pull( double hostSeconds, double dt, const float* bins, int binCount, Channel channel, FftLaw law, std::vector< float >& out )
{
	if( static_cast< int >( law ) != appliedLaw )
	{
		earshot::Config c      = engine->config();
		const bool power       = law == FftLaw::LinearPower || law == FftLaw::LogPower;
		const bool logarithmic = law == FftLaw::LogMagnitude || law == FftLaw::LogPower;
		c.hostBins.value       = power ? earshot::HostBins::Value::Power : earshot::HostBins::Value::Magnitude;
		c.hostBins.layout      = logarithmic ? earshot::HostBins::Layout::Log : earshot::HostBins::Layout::Linear;
		engine->configure( c );
		appliedLaw = static_cast< int >( law );
	}

	const earshot::Frame& f = engine->update( hostSeconds, bins, binCount );
	out.clear();
	lastFromDevice = f.source == earshot::Source::Device;

	if( lastFromDevice || fallbackFromTap )
	{
		//The real path: every tap sample since the last frame, once. If more
		//than a second queued (a stalled host), the oldest are dropped.
		int n = engine->readTap( left.data(), right.data(), kMaxPull );
		while( f.tapAvailable > 0 && n == kMaxPull )
		{
			const int more = engine->readTap( left.data(), right.data(), kMaxPull );
			if( more <= 0 )
				break;
			n = more;
		}
		out.resize( static_cast< size_t >( n ) );
		for( int k = 0; k < n; ++k )
			out[ static_cast< size_t >( k ) ] = channel == Channel::Left ? left[ k ] : channel == Channel::Right ? right[ k ] : 0.5f * ( left[ k ] + right[ k ] );
		if( lastFromDevice )
		{
			for( Band& b : bands )
				b.level = 0.0;
			return n;
		}
	}

	//The host FFT, or nothing: the host clock says how many samples.
	carry += std::clamp( dt, 0.0, kMaxFallbackStep ) * tube::kTapRate;
	const int n = static_cast< int >( std::floor( carry ) );
	carry -= n;
	if( fallbackFromTap )
		return static_cast< int >( out.size() );
	out.assign( static_cast< size_t >( n ), 0.0f );
	if( f.source != earshot::Source::Host || n <= 0 )
	{
		for( Band& b : bands )
			b.level = 0.0;
		return n;
	}
	const double kRmsOfSine = 1.0 / std::sqrt( 2.0 );
	std::vector< double > target( static_cast< size_t >( kBands ), 0.0 );
	for( int b = 0; b < std::min( kBands, f.bandCount ); ++b )
		target[ static_cast< size_t >( b ) ] = std::clamp( static_cast< double >( f.bands[ b ].level ), 0.0, 1.0 );
	for( int k = 0; k < n; ++k )
	{
		const double x = nextNoise();
		double sum     = 0.0;
		for( int b = 0; b < kBands; ++b )
		{
			Band& band       = bands[ static_cast< size_t >( b ) ];
			const double y   = band.b0 * x + band.b2 * band.x2 - band.a1 * band.y1 - band.a2 * band.y2;
			band.x2          = band.x1;
			band.x1          = x;
			band.y2          = band.y1;
			band.y1          = y;
			const double amp = band.level + ( target[ static_cast< size_t >( b ) ] - band.level ) * ( k + 1.0 ) / n;
			sum += amp * kRmsOfSine * band.gain * y;
		}
		out[ static_cast< size_t >( k ) ] = static_cast< float >( sum );
	}
	for( int b = 0; b < kBands; ++b )
		bands[ static_cast< size_t >( b ) ].level = target[ static_cast< size_t >( b ) ];
	return n;
}

} // namespace rubens
