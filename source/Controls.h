#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index), except Audio Input, whose elements'
    values are earshot device keys (a device is remembered by name). Rescan is
    a press; Audio is Resolume's 64-bin FFT buffer, the fallback's only input.

    Units: metres for the tube and the view (Hole Size is a radius in mm and
    Hole Pitch is in cm on the CPU side too), pascals for the gas overpressure,
    dB re 20 uPa for Volume, centimetres for Flame, stops of exposure.
*/
namespace rubens
{
enum ParamId : unsigned int
{
	PT_INPUT = 0,///< the device menu
	PT_RESCAN,
	PT_CHANNEL,
	PT_FFT_LAW,
	PT_VOLUME,

	PT_LENGTH,
	PT_PITCH,
	PT_HOLE_SIZE,
	PT_GAS,

	PT_FLAME,
	PT_SHIMMER,
	PT_DISTANCE,

	PT_FIELD,
	PT_TUBE_HEIGHT,
	PT_EXPOSURE,
	PT_BACKDROP,///< the source only
	PT_MIX,     ///< the Over only
	PT_AUDIO,

	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer,
	Devices
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

const ControlInfo& InfoOf( unsigned int id );

/// Host index -> ParamId for one plugin: the source has no Mix, the Over no Backdrop.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's element index from whatever the host sent (index, rounded).
int OptionIndex( float value, int count );

/// The device menu: element 0 the host's FFT, element 1 the system default,
/// then devices by name, then spares, kInputSlots in all (spasis's shape).
constexpr int kInputSlots           = 16;
constexpr const char* kHostFftLabel = "None (host FFT)";
constexpr const char* kDefaultLabel = "System default";
constexpr const char* kEmptySlot    = "(none)";

enum class Channel
{
	Both = 0,
	Left,
	Right,
	Count
};

/// What Resolume's FFT bins are assumed to be (earshot's HostBins): nobody
/// has measured them, so the assumption is exposed.
enum class FftLaw
{
	LinearMagnitude = 0,
	LinearPower,
	LogMagnitude,
	LogPower,
	Count
};

enum class Backdrop
{
	Black = 0,
	Grid,///< thin lines every 10 cm: the refraction made visible
	Dots,///< a background-oriented-schlieren dot field
	Count
};

double VolumeFromParam( float value );///< dB re 20 uPa (RMS) of a full-scale sine as a travelling wave
float ParamFromVolume( double db );

double LengthFromParam( float value );///< m
float ParamFromLength( double metres );

double PitchFromParam( float value );///< m between holes
float ParamFromPitch( double metres );

double HoleSizeFromParam( float value );///< hole radius, m
float ParamFromHoleSize( double metres );

double GasFromParam( float value );///< overpressure, Pa
float ParamFromGas( double pascals );

double FlameFromParam( float value );///< quiet flame height, m
float ParamFromFlame( double metres );

double ShimmerFromParam( float value );///< 0..1
double DistanceFromParam( float value );///< m from the plume to the backdrop
float ParamFromDistance( double metres );

double FieldFromParam( float value );///< m across the frame
float ParamFromField( double metres );

double TubeHeightFromParam( float value );///< the tube's axis, fraction of the frame from the bottom
float ParamFromTubeHeight( double fraction );

double ExposureFromParam( float value );///< stops
float ParamFromExposure( double stops );

constexpr double kPi = 3.14159265358979323846;

} // namespace rubens
