/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (woofer's glue, which is
    mirrorball's and fountain's, with rubens' names, its two plugins and
    earshot's feed.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Rubens.cpp` and everything it calls -- the duct and its
    holes (Duct.cpp), the audio input (Audio.cpp) and earshot under it, the
    plume buffer -- compiled UNMODIFIED into the same WebAssembly module as
    this file (see demo/tools/build-wasm.sh for the list). This file only
    plays the host:

      - it constructs a `RubensPlugin`, as SourcePlugin.cpp / EffectPlugin.cpp's
        registrations would (the source, or with `effect` the Over), and calls
        InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, the Audio
        buffer's elements (Resolume's FFT) and ProcessOpenGL with a viewport
        (the source) or one input texture (the Over), as `tools/rutest`'s Rig
        does.

    One thing here is the plugin's HARNESS rather than a host: the audio.
    rutest never opens a device; it hands the plugin its track through
    earshot's feed() (`AudioInput::Feed`, reached by `AudioForTest()`, which
    is `Engine::feedBegin` and `Engine::feed`), the path a device's samples
    take inside earshot. So does this page. `musicAt` and the functions under
    it are rutest's own track, copied from tools/rutest/main.cpp character for
    character (and held to it by demo/tools/check_shaders.py), and
    `ru_host_fft` writes rutest --host-fft's synthetic kick spectrum into the
    Audio buffer (feedKicks' lines held to rutest's the same way). A file the
    visitor chooses is decoded by the page and reaches feed() through
    `ru_feed`, as `rutest --wav` does.

    No audio device exists in this module: earshot is built with
    EARSHOT_CAPTURE=OFF (NoCapture.cpp; miniaudio is not compiled), and
    `ru_prepare` sets RUBENS_NO_DEVICES=1, verify.sh's switch, so the plugin's
    AudioInput::Refresh never asks for a list. Nothing calls selectDevice:
    the page never sets Audio Input, and the plugin's own Select acts only
    when that value changes.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the five entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Duct.h"
#include "Rubens.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace rubens;

namespace
{
//---------------------------------------------------------------------------
// tools/rutest/main.cpp's track, verbatim (check_shaders.py compares).
//---------------------------------------------------------------------------
uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
double hash01( uint32_t a, uint32_t b = 0 )
{
	return pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

constexpr int kFeedRate = 48000;

double fadeIn( double t, double ramp )
{
	return t >= ramp ? 1.0 : t <= 0.0 ? 0.0 : 0.5 - 0.5 * std::cos( kPi * t / ramp );
}

/// A kick: an exponential sweep 150 -> 50 Hz, a 150 ms decay.
double kickAt( double t )
{
	if( t < 0.0 || t > 0.6 )
		return 0.0;
	const double f0 = 150.0, f1 = 50.0, tau = 0.04;
	const double phase = 2.0 * kPi * ( f1 * t + ( f0 - f1 ) * tau * ( 1.0 - std::exp( -t / tau ) ) );
	return std::sin( phase ) * std::exp( -t / 0.15 );
}

/// A plucked note: three partials, a 5 ms attack, a decay.
double noteAt( double t, double hz, double decay )
{
	if( t < 0.0 )
		return 0.0;
	const double env = fadeIn( t, 0.005 ) * std::exp( -t / decay );
	return env * ( std::sin( 2.0 * kPi * hz * t ) + 0.4 * std::sin( 4.0 * kPi * hz * t ) + 0.15 * std::sin( 6.0 * kPi * hz * t ) ) / 1.55;
}

/// The harness's track, 120 BPM: a kick on the beat, noise hat on the
/// off-beat, a bass line, and a lead on eighth notes through A minor
/// pentatonic between 220 and 880 Hz (the tube's busy octaves). Peaks near 1.
double musicAt( double t )
{
	if( t < 0.0 )
		return 0.0;
	const double beat = std::fmod( t, 0.5 );
	double v          = 0.55 * kickAt( beat );
	//The hat: hashed noise, 30 ms.
	const double off = std::fmod( t + 0.25, 0.5 );
	if( off < 0.08 )
	{
		const uint32_t i = static_cast< uint32_t >( std::llround( t * kFeedRate ) );
		v += 0.08 * ( hash01( i, 3 ) * 2.0 - 1.0 ) * std::exp( -off / 0.03 );
	}
	const double bass[] = { 55.0, 55.0, 65.41, 49.0 };
	const int bar       = static_cast< int >( std::floor( t / 2.0 ) ) % 4;
	v += 0.22 * noteAt( std::fmod( t, 1.0 ), bass[ bar ], 0.5 );
	const double lead[] = { 220.0, 261.63, 293.66, 329.63, 392.0, 440.0, 523.25, 587.33, 659.26, 783.99, 880.0 };
	const int step      = static_cast< int >( std::floor( t / 0.25 ) );
	const int pick      = static_cast< int >( pcg( static_cast< uint32_t >( step ) * 31u + 7u ) % 11u );
	v += 0.35 * noteAt( std::fmod( t, 0.25 ), lead[ pick ], 0.18 );
	return v;
}

//---------------------------------------------------------------------------
// One plugin instance and the host-side structs it is handed.
//---------------------------------------------------------------------------
struct Instance
{
	explicit Instance( bool effect ) : plugin( effect )
	{
	}

	RubensPlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	/// The feed's absolute sample clock at kFeedRate: samples [0, sent) have
	/// gone to feed(), as rutest's Feeder counts them.
	uint64_t sent = 0;
	std::vector< float > buffer;

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

/// The ParamId behind a host index of this instance, or PT_COUNT.
unsigned int idOf( Instance* instance, int index )
{
	for( unsigned int candidate = 0; candidate < PT_ABOUT_TEXT; ++candidate )
		if( instance->plugin.HostIndexOf( candidate ) == index )
			return candidate;
	return PT_COUNT;
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------

/// Before the first instance -- the declarations construct two: give the
/// plugin's log (Diag.cpp) somewhere to go, a directory in the page's
/// in-memory file system named through the override Diag.cpp reads (its own
/// `mkdir -p` goes through system(), which a browser does not have); and
/// RUBENS_NO_DEVICES=1, verify.sh's switch, so AudioInput::Refresh never asks
/// earshot for a device list (a build with no capture backend would return
/// none anyway).
EMSCRIPTEN_KEEPALIVE void ru_prepare()
{
	setenv( "RUBENS_LOG_DIR", "/rubens/logs", 1 );
	setenv( "RUBENS_NO_DEVICES", "1", 1 );
	mkdir( "/rubens", 0755 );
	mkdir( "/rubens/logs", 0755 );
}

/// The source (`effect` 0, SW Rubens) or the Over (1, SW Rubens Over).
EMSCRIPTEN_KEEPALIVE Instance* ru_new( int effect )
{
	return new Instance( effect != 0 );
}

EMSCRIPTEN_KEEPALIVE void ru_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int ru_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ru_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* ru_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int ru_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ru_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float ru_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ru_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ru_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float ru_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int ru_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ru_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int ru_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ru_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float ru_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound.
/// The source reads its size from the viewport the page set; the Over is
/// handed one input of clipWidth x clipHeight whose GL name is `clip` (a
/// texture the page registered with emscripten's GL tables). 1 on success.
EMSCRIPTEN_KEEPALIVE int ru_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	if( instance->plugin.IsEffect() )
	{
		instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
		instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
		instance->input.Handle                                 = static_cast< GLuint >( clip );
		instance->process.numInputTextures                     = 1;
		instance->process.inputTextures                        = instance->inputs;
	}
	else
	{
		instance->process.numInputTextures = 0;
		instance->process.inputTextures    = nullptr;
	}
	instance->process.HostFBO = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// The audio, as rutest hands it over. Each source is a fresh instance (the
// page's rule): a feed, once begun, is what earshot's Engine reads until a
// selectDevice, and nothing here selects.
//---------------------------------------------------------------------------

/// Begin the feed with no samples yet: earshot's feedBegin( 48000, 2 ), its
/// state Running and the frame's source the device path from here on.
EMSCRIPTEN_KEEPALIVE void ru_feed_begin( Instance* instance )
{
	static const float none[ 2 ] = { 0.0f, 0.0f };
	instance->plugin.AudioForTest().Feed( none, 0, kFeedRate, 2 );
}

/// rutest's track up to absolute sample `end` at kFeedRate on both channels
/// (`which` 0, rutest's default) or zeros (1, `--silent`): samples
/// [sent, end) of the signal, as Feeder::Frame makes them, into feed(). The
/// page's `end` is its clock times 48 000, rounded; rutest's is the frame's
/// end, (frame + 1) x 48 000 / fps.
EMSCRIPTEN_KEEPALIVE int ru_feed_track( Instance* instance, double end, int which )
{
	const double wanted = std::floor( end + 0.5 );
	if( !( wanted > static_cast< double >( instance->sent ) ) )
		return 0;
	const int n = static_cast< int >( std::min( wanted - static_cast< double >( instance->sent ), 4.0 * kFeedRate ) );
	instance->buffer.resize( static_cast< size_t >( 2 * n ) );
	for( int k = 0; k < n; ++k )
	{
		const uint64_t at = instance->sent + static_cast< uint64_t >( k );
		float& l          = instance->buffer[ static_cast< size_t >( 2 * k ) ];
		float& r          = instance->buffer[ static_cast< size_t >( 2 * k + 1 ) ];
		if( which == 1 )
			l = r = 0.0f;
		else
			l = r = static_cast< float >( musicAt( static_cast< double >( at ) / kFeedRate ) );
	}
	instance->plugin.AudioForTest().Feed( instance->buffer.data(), n, kFeedRate, 2 );
	instance->sent += static_cast< uint64_t >( n );
	return n;
}

/// A file the visitor chose, decoded by the page: `frames` interleaved frames
/// of `channels` at `rate`, into feed() as `rutest --wav` hands a WAV over.
EMSCRIPTEN_KEEPALIVE void ru_feed( Instance* instance, const float* interleaved, int frames, int rate, int channels )
{
	instance->plugin.AudioForTest().Feed( interleaved, frames, rate, channels );
}

/// rutest --host-fft: nothing fed; Resolume's FFT buffer only, with
/// feedKicks' spectrum -- a kick every half second over a floor, soft and
/// hard in turn -- written before frame `frame` (rutest's frame counter at
/// 60 fps; the page's is its clock times 60, rounded).
EMSCRIPTEN_KEEPALIVE void ru_host_fft( Instance* instance, int frame )
{
	const unsigned int host = static_cast< unsigned int >( instance->plugin.HostIndexOf( PT_AUDIO ) );
	const int since = ( frame - 20 ) % 30;
	const bool hard = ( ( frame - 20 ) / 30 ) % 2 == 1;
	for( int bin = 0; bin < 64; ++bin )
	{
		double v = bin > 40 ? 0.05 : 0.02;
		if( frame >= 20 && since < 4 && bin < 10 )
			v += ( hard ? 0.9 : 0.25 ) * std::exp( -since * 0.8 );
		instance->plugin.SetParamElementValue( host, static_cast< unsigned int >( bin ), static_cast< float >( v ) );
	}
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Rubens.cpp's UpdateTube and ProcessOpenGL apply to it. NaN where the
// plugin uses the host's 0..1 as it is (Mix), or an index.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ru_convert( Instance* instance, int index, float v )
{
	switch( idOf( instance, index ) )
	{
	case PT_VOLUME: return VolumeFromParam( v );
	case PT_LENGTH: return LengthFromParam( v );
	case PT_PITCH: return PitchFromParam( v );
	case PT_HOLE_SIZE: return HoleSizeFromParam( v );
	case PT_GAS: return GasFromParam( v );
	case PT_FLAME: return FlameFromParam( v );
	case PT_SHIMMER: return ShimmerFromParam( v );
	case PT_DISTANCE: return DistanceFromParam( v );
	case PT_FIELD: return FieldFromParam( v );
	case PT_TUBE_HEIGHT: return TubeHeightFromParam( v );
	case PT_EXPOSURE: return ExposureFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor's
/// table and the harness use). The page's presets are written in dB, metres,
/// pascals and stops and go through this. NaN for a parameter with no
/// inverse (Shimmer is the host's 0..1, so a preset gives it as that).
EMSCRIPTEN_KEEPALIVE double ru_param_from( Instance* instance, int index, double value )
{
	switch( idOf( instance, index ) )
	{
	case PT_VOLUME: return ParamFromVolume( value );
	case PT_LENGTH: return ParamFromLength( value );
	case PT_PITCH: return ParamFromPitch( value );
	case PT_HOLE_SIZE: return ParamFromHoleSize( value );
	case PT_GAS: return ParamFromGas( value );
	case PT_FLAME: return ParamFromFlame( value );
	case PT_DISTANCE: return ParamFromDistance( value );
	case PT_FIELD: return ParamFromField( value );
	case PT_TUBE_HEIGHT: return ParamFromTubeHeight( value );
	case PT_EXPOSURE: return ParamFromExposure( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (LastFrame(), TubeForTest(),
// AudioForTest(), HostNow()).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ru_holes( Instance* instance )
{
	return instance->plugin.LastFrame().holes;
}
/// The actual pitch, metres (the tube's length over the holes).
EMSCRIPTEN_KEEPALIVE double ru_pitch( Instance* instance )
{
	return instance->plugin.LastFrame().pitch;
}
/// The duct's modes, up to the first cross-mode's cut-on.
EMSCRIPTEN_KEEPALIVE int ru_modes( Instance* instance )
{
	return instance->plugin.TubeForTest().Modes();
}
/// The holes' Helmholtz frequency, Hz (each mode rings at sqrt( fH^2 + fn0^2 )).
EMSCRIPTEN_KEEPALIVE double ru_helmholtz_hz( Instance* instance )
{
	return instance->plugin.TubeForTest().HelmholtzHz();
}
EMSCRIPTEN_KEEPALIVE double ru_cut_on_hz( Instance* instance )
{
	return instance->plugin.TubeForTest().CutOnHz();
}
/// Mode n's frequency with the holes' shunt, lossless, Hz.
EMSCRIPTEN_KEEPALIVE double ru_mode_hz( Instance* instance, int n )
{
	return instance->plugin.TubeForTest().LosslessHz( n );
}
/// The quiet flame, metres (Flame), and the shortest, mean and tallest flame
/// the last frame drew, metres, over its holes.
EMSCRIPTEN_KEEPALIVE double ru_quiet_flame( Instance* instance )
{
	return instance->plugin.LastFrame().flame;
}
EMSCRIPTEN_KEEPALIVE double ru_flame_min( Instance* instance )
{
	const std::vector< float >& lengths = instance->plugin.LastFrame().lengths;
	return lengths.empty() ? 0.0 : *std::min_element( lengths.begin(), lengths.end() );
}
EMSCRIPTEN_KEEPALIVE double ru_flame_max( Instance* instance )
{
	const std::vector< float >& lengths = instance->plugin.LastFrame().lengths;
	return lengths.empty() ? 0.0 : *std::max_element( lengths.begin(), lengths.end() );
}
EMSCRIPTEN_KEEPALIVE double ru_flame_mean( Instance* instance )
{
	const std::vector< float >& lengths = instance->plugin.LastFrame().lengths;
	double sum = 0.0;
	for( float l : lengths )
		sum += l;
	return lengths.empty() ? 0.0 : sum / static_cast< double >( lengths.size() );
}
/// Hole i's flame the last frame drew, metres (0 past the last hole).
EMSCRIPTEN_KEEPALIVE double ru_flame( Instance* instance, int i )
{
	const std::vector< float >& lengths = instance->plugin.LastFrame().lengths;
	return i >= 0 && i < static_cast< int >( lengths.size() ) ? lengths[ static_cast< size_t >( i ) ] : 0.0;
}
/// The piston's speed for a full-scale sample, m/s (Volume).
EMSCRIPTEN_KEEPALIVE double ru_v0( Instance* instance )
{
	return instance->plugin.LastFrame().v0;
}
/// Samples the duct took last frame, at 16 kHz.
EMSCRIPTEN_KEEPALIVE int ru_tap_samples( Instance* instance )
{
	return instance->plugin.LastFrame().tapSamples;
}
/// 1 when the last frame's piston came from earshot's tap (the device path,
/// which feed() is), 0 when the fallback made it.
EMSCRIPTEN_KEEPALIVE int ru_from_device( Instance* instance )
{
	return instance->plugin.LastFrame().fromDevice ? 1 : 0;
}
/// earshot's CaptureState by name (Closed, Opening, Running, Silent, Missing,
/// Failed).
EMSCRIPTEN_KEEPALIVE const char* ru_capture_state_name( Instance* instance )
{
	return earshot::toString( instance->plugin.AudioForTest().State() );
}
/// The host's time as the plugin reads it, seconds (the wall clock until its
/// seconds-or-milliseconds vote settles).
EMSCRIPTEN_KEEPALIVE double ru_host_now( Instance* instance )
{
	return instance->plugin.HostNow();
}
/// Tap samples the duct has integrated since it was last built.
EMSCRIPTEN_KEEPALIVE double ru_duct_samples( Instance* instance )
{
	return static_cast< double >( instance->plugin.TubeForTest().Samples() );
}
/// Samples fed so far at 48 kHz, by ru_feed_track.
EMSCRIPTEN_KEEPALIVE double ru_fed( Instance* instance )
{
	return static_cast< double >( instance->sent );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* ru_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
