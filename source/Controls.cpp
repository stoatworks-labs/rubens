#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace rubens
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

//No source read gives a working tube's sound level or gas pressure
//(SPEC-rubens.md, "What could not be read"): the ranges are wide on purpose.
constexpr double kVolumeLow = 80.0, kVolumeHigh = 150.0;
constexpr double kLengthLow = 0.5, kLengthHigh = 3.0;
constexpr double kPitchLow = 0.01, kPitchHigh = 0.05;
constexpr double kHoleLow = 0.3e-3, kHoleHigh = 2.0e-3;
constexpr double kGasLow = 1.0, kGasHigh = 1000.0;
constexpr double kFlameLow = 0.01, kFlameHigh = 0.30;
constexpr double kDistanceHigh = 30.0;
constexpr double kFieldLow = 0.5, kFieldHigh = 6.0;
constexpr double kTubeHeightHigh = 0.6;
constexpr double kStops = 4.0;

const char* const kChannelNames[]  = { "L+R", "Left", "Right" };
const char* const kLawNames[]      = { "Linear Mag", "Linear Power", "Log Mag", "Log Power" };
const char* const kBackdropNames[] = { "Black", "Grid", "Dots" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: Gardner, Gee & Dix's large-holed tube (1.524 m, 2.2 cm pitch,
// 0.92 mm holes), the rest set on Resolume's demo clips with music (AGENTS.md,
// "Defaults").
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	{ PT_INPUT, "Audio Input", "Audio", Kind::Devices, 0.0f, nullptr, 0 },
	event( PT_RESCAN, "Rescan Inputs", "Audio" ),
	option( PT_CHANNEL, "Channel", "Audio", 0.0f, kChannelNames, static_cast< int >( Channel::Count ) ),
	option( PT_FFT_LAW, "FFT Law", "Audio", 0.0f, kLawNames, static_cast< int >( FftLaw::Count ) ),
	standard( PT_VOLUME, "Volume", "Audio", ParamFromVolume( 128.0 ) ),

	standard( PT_LENGTH, "Length", "Tube", ParamFromLength( 1.524 ) ),
	standard( PT_PITCH, "Hole Pitch", "Tube", ParamFromPitch( 0.022 ) ),
	standard( PT_HOLE_SIZE, "Hole Size", "Tube", ParamFromHoleSize( 0.92e-3 ) ),
	standard( PT_GAS, "Gas", "Tube", ParamFromGas( 30.0 ) ),

	standard( PT_FLAME, "Flame", "Flames", ParamFromFlame( 0.08 ) ),
	standard( PT_SHIMMER, "Shimmer", "Flames", 0.5f ),
	standard( PT_DISTANCE, "Distance", "Flames", ParamFromDistance( 6.0 ) ),

	standard( PT_FIELD, "Field Width", "View", ParamFromField( 1.8 ) ),
	standard( PT_TUBE_HEIGHT, "Tube Height", "View", ParamFromTubeHeight( 0.2 ) ),
	standard( PT_EXPOSURE, "Exposure", "View", ParamFromExposure( 0.0 ) ),
	option( PT_BACKDROP, "Backdrop", "View", 1.0f, kBackdropNames, static_cast< int >( Backdrop::Count ) ),
	standard( PT_MIX, "Mix", "View", 1.0f ),
	{ PT_AUDIO, "Audio", "Audio", Kind::Buffer, 0.0f, nullptr, 0 },
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			if( !over && id == PT_MIX )
				continue;
			if( over && id == PT_BACKDROP )
				continue;
			order.push_back( id );
		}
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	if( !std::isfinite( value ) )
		return 0;
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double VolumeFromParam( float v )
{
	return linear( v, kVolumeLow, kVolumeHigh );
}
float ParamFromVolume( double db )
{
	return inverseLinear( db, kVolumeLow, kVolumeHigh );
}

double LengthFromParam( float v )
{
	return linear( v, kLengthLow, kLengthHigh );
}
float ParamFromLength( double metres )
{
	return inverseLinear( metres, kLengthLow, kLengthHigh );
}

double PitchFromParam( float v )
{
	return linear( v, kPitchLow, kPitchHigh );
}
float ParamFromPitch( double metres )
{
	return inverseLinear( metres, kPitchLow, kPitchHigh );
}

double HoleSizeFromParam( float v )
{
	return geometric( v, kHoleLow, kHoleHigh );
}
float ParamFromHoleSize( double metres )
{
	return inverseGeometric( metres, kHoleLow, kHoleHigh );
}

double GasFromParam( float v )
{
	return geometric( v, kGasLow, kGasHigh );
}
float ParamFromGas( double pascals )
{
	return inverseGeometric( pascals, kGasLow, kGasHigh );
}

double FlameFromParam( float v )
{
	return geometric( v, kFlameLow, kFlameHigh );
}
float ParamFromFlame( double metres )
{
	return inverseGeometric( metres, kFlameLow, kFlameHigh );
}

double ShimmerFromParam( float v )
{
	return clamp01( v );
}

double DistanceFromParam( float v )
{
	return linear( v, 0.0, kDistanceHigh );
}
float ParamFromDistance( double metres )
{
	return inverseLinear( metres, 0.0, kDistanceHigh );
}

double FieldFromParam( float v )
{
	return geometric( v, kFieldLow, kFieldHigh );
}
float ParamFromField( double metres )
{
	return inverseGeometric( metres, kFieldLow, kFieldHigh );
}

double TubeHeightFromParam( float v )
{
	return linear( v, 0.0, kTubeHeightHigh );
}
float ParamFromTubeHeight( double fraction )
{
	return inverseLinear( fraction, 0.0, kTubeHeightHigh );
}

double ExposureFromParam( float v )
{
	return linear( v, -kStops, kStops );
}
float ParamFromExposure( double stops )
{
	return inverseLinear( stops, -kStops, kStops );
}

} // namespace rubens
