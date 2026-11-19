#include "Rubens.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace rubens
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Host seconds a single frame may advance by (the fallback's noise and the
/// shimmer; the device path takes every tap sample whatever the frame).
constexpr double kMaxFrameDelta = 0.25;
/// Resolume's FFT buffer.
constexpr int kBins = 64;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// Every pass here writes every texel of its target, so what the target held
/// does not matter -- but Apple's GL runs on Metal, where binding a
/// framebuffer LOADS the attachment unless it is cleared first. A clear
/// becomes the pass's load action, which costs nothing.
void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}

double positiveMod( double x, double period )
{
	const double m = std::fmod( x, period );
	return m < 0.0 ? m + period : m;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
RubensPlugin::RubensPlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );

	//The fallback's sample count and the shimmer run on the host's clock.
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Devices:
		{
			//The device menu (spasis's shape, earshot's keys). The range covers
			//the keys (23 bits) for any host that clamps an option to it.
			SetOptionParamInfo( host, info.name, kInputSlots, 0.0f );
			const auto& slots = audio.Slots();
			for( int i = 0; i < kInputSlots; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			SetParamRange( host, 0.0f, 8388608.0f );
			break;
		}
		case Kind::Event: SetParamInfo( host, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//Resolume's FFT: an audio-source picker, 64 bins written every
			//frame. The fallback's only input.
			SetBufferParamInfo( host, info.name, kBins, FF_USAGE_FFT );
			for( int i = 0; i < kBins; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( host, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint RubensPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void RubensPlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void RubensPlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}
void RubensPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b )
{
	glUniform2f( where( shader, name ), a, b );
}

//---------------------------------------------------------------------------
FFResult RubensPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &plumeShader, shaders::Pass::Plume, "plume" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Rubens: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void RubensPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (rosette's, by way of haze).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

void RubensPlugin::UpdateTube( double dt )
{
	TubeSpec spec;
	spec.length       = LengthFromParam( params[ PT_LENGTH ] );
	spec.pitch        = PitchFromParam( params[ PT_PITCH ] );
	spec.holeRadius   = HoleSizeFromParam( params[ PT_HOLE_SIZE ] );
	spec.overpressure = GasFromParam( params[ PT_GAS ] );
	duct.Configure( spec );

	float bins[ kBins ] = {};
	int binCount        = 0;
	const int host      = idToHost[ PT_AUDIO ];
	if( const ParamInfo* info = FindParamInfo( static_cast< FFUInt32 >( host ) ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	const Channel channel = static_cast< Channel >( OptionIndex( params[ PT_CHANNEL ], static_cast< int >( Channel::Count ) ) );
	const FftLaw law      = static_cast< FftLaw >( OptionIndex( params[ PT_FFT_LAW ], static_cast< int >( FftLaw::Count ) ) );
	//Resolume's 64 bins are linear to half the host's audio engine rate, 375 Hz
	//each at 48 kHz, and earshot shares each among the bands it overlaps
	//(earshot 0.1.1). Until the host sends a rate, earshot's 48 kHz.
	if( !ignoreHostRate )
		audio.SetHostRate( hostRate.load( std::memory_order_relaxed ) );
	audio.Pull( now, dt, bins, binCount, channel, law, tap );

	//Volume: the SPL a full-scale sine makes as a travelling wave in the gas,
	//p = rho c v.
	const double prms = kReferencePressure * std::pow( 10.0, VolumeFromParam( params[ PT_VOLUME ] ) / 20.0 );
	const double v0   = std::sqrt( 2.0 ) * prms / ( gas::kDensity * gas::kSoundSpeed );
	velocity.resize( tap.size() );
	for( size_t k = 0; k < tap.size(); ++k )
		velocity[ k ] = static_cast< float >( v0 * tap[ k ] );
	duct.Process( velocity.data(), static_cast< int >( velocity.size() ) );

	frame.holes      = duct.Holes();
	frame.pitch      = duct.Pitch();
	frame.length     = spec.length;
	frame.flame      = FlameFromParam( params[ PT_FLAME ] );
	frame.v0         = v0;
	frame.tapSamples = static_cast< int >( tap.size() );
	frame.fromDevice = audio.FromDevice();
	frame.lengths.resize( static_cast< size_t >( frame.holes ) );
	for( int i = 0; i < frame.holes; ++i )
		frame.lengths[ static_cast< size_t >( i ) ] = static_cast< float >( frame.flame * std::max( 0.0, duct.FlameFlow( i ) ) );
}

void RubensPlugin::uploadHoles()
{
	const int n = std::max( frame.holes, 1 );
	std::vector< float > texels( static_cast< size_t >( n ) * 4, 0.0f );
	for( int i = 0; i < frame.holes; ++i )
	{
		texels[ static_cast< size_t >( i ) * 4 + 0 ] = frame.lengths[ static_cast< size_t >( i ) ];
		texels[ static_cast< size_t >( i ) * 4 + 1 ] = static_cast< float >( duct.Aeration( i ) );
	}
	if( holeTexture == 0 )
		glGenTextures( 1, &holeTexture );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, holeTexture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	if( n != holeTextureWidth )
	{
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, n, 1, 0, GL_RGBA, GL_FLOAT, texels.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		holeTextureWidth = n;
	}
	else
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RGBA, GL_FLOAT, texels.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void RubensPlugin::geometryUniforms( const FFGLShader& shader, int width, int height )
{
	( void )height;
	uniform( shader, "Hooks", hooks );
	uniform( shader, "Field", static_cast< float >( frame.field ) );
	uniform( shader, "TubeY", static_cast< float >( frame.tubeY ) );
	uniform( shader, "TubeRadius", static_cast< float >( tube::kRadius ) );
	uniform( shader, "TubeLength", static_cast< float >( frame.length ) );
	uniform( shader, "HoleCount", frame.holes );
	uniform( shader, "HolePitch", static_cast< float >( frame.pitch ) );
	uniform( shader, "HoleRadius", static_cast< float >( duct.Spec().holeRadius ) );
	( void )width;
}

void RubensPlugin::drawPlume( int width, int height )
{
	ScopedFBOBinding target( plume.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, width, height );
	discardTarget();
	glUseProgram( plumeShader.GetGLID() );
	bindUnit( 0, holeTexture );
	uniform( plumeShader, "Holes", 0 );
	geometryUniforms( plumeShader, width, height );
	glUniform4i( where( plumeShader, "Viewport" ), 0, 0, width, height );
	uniform( plumeShader, "Shimmer", static_cast< float >( ShimmerFromParam( params[ PT_SHIMMER ] ) ) );
	glUniform3f( where( plumeShader, "ShimmerOffset" ), static_cast< float >( shimmerOffset[ 0 ] ), static_cast< float >( shimmerOffset[ 1 ] ),
	             static_cast< float >( shimmerOffset[ 2 ] ) );
	uniform( plumeShader, "ShimmerCell", static_cast< float >( kShimmerCell ) );
	uniform( plumeShader, "IndexDeficit", static_cast< float >( kGladstoneDale * kAirDensity ) );
	quad.Draw();
	unbindTextureUnits( 1 );
	glUseProgram( 0 );
}

void RubensPlugin::lampColour( float rgb[ 3 ] ) const
{
	using earshot::CaptureState;
	const earshot::Frame& f = audio.Frame();
	const bool blink        = std::fmod( now, 0.5 ) < 0.25;
	float r = 0.0f, g = 0.0f, b = 0.0f;
	switch( f.capture )
	{
	case CaptureState::Running:
		r = 0.05f;
		g = 0.9f;
		b = 0.15f;
		break;
	case CaptureState::Silent:
		r = 0.9f;
		g = 0.03f;
		b = 0.02f;
		break;
	case CaptureState::Missing:
	case CaptureState::Failed:
		if( blink )
		{
			r = 0.9f;
			g = 0.03f;
			b = 0.02f;
		}
		break;
	case CaptureState::Opening:
		if( blink )
		{
			r = 1.0f;
			g = 0.45f;
			b = 0.03f;
		}
		break;
	case CaptureState::Closed:
		if( f.source == earshot::Source::Host )
		{
			r = 1.0f;
			g = 0.45f;
			b = 0.03f;
		}
		break;
	}
	rgb[ 0 ] = r;
	rgb[ 1 ] = g;
	rgb[ 2 ] = b;
}

void RubensPlugin::drawComposite( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( 0, plume.TextureID() );
	//Always a real texture on the clip's unit, even in the source: a sampler
	//on texture 0 is "unloadable" to Apple's GL (boreal's trap).
	bindUnit( 1, input != nullptr ? input->Handle : plume.TextureID() );
	bindUnit( 2, holeTexture );
	uniform( compositeShader, "Plume", 0 );
	uniform( compositeShader, "InputTexture", 1 );
	uniform( compositeShader, "Holes", 2 );
	geometryUniforms( compositeShader, width, height );
	glUniform4i( where( compositeShader, "Viewport" ), hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	if( input != nullptr )
	{
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
		uniform( compositeShader, "MaxUV", maxCoords.s, maxCoords.t );
	}
	else
		uniform( compositeShader, "MaxUV", 1.0f, 1.0f );
	uniform( compositeShader, "IsOver", isEffect ? 1 : 0 );
	uniform( compositeShader, "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	uniform( compositeShader, "Gain", static_cast< float >( std::pow( 2.0, ExposureFromParam( params[ PT_EXPOSURE ] ) ) ) );
	uniform( compositeShader, "Distance", static_cast< float >( frame.distance ) );
	uniform( compositeShader, "Backdrop", isEffect ? 0 : OptionIndex( params[ PT_BACKDROP ], static_cast< int >( Backdrop::Count ) ) );
	float lamp[ 3 ];
	lampColour( lamp );
	glUniform3f( where( compositeShader, "Lamp" ), lamp[ 0 ], lamp[ 1 ], lamp[ 2 ] );
	uniform( compositeShader, "Shimmer", static_cast< float >( ShimmerFromParam( params[ PT_SHIMMER ] ) ) );
	uniform( compositeShader, "FlickerTime", static_cast< float >( positiveMod( shimmerTime, 64.0 / 6.0 ) ) );
	quad.Draw();
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
FFResult RubensPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// Time, the music, the tube.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	UpdateTube( dt );

	frame.field    = FieldFromParam( params[ PT_FIELD ] );
	frame.tubeY    = TubeHeightFromParam( params[ PT_TUBE_HEIGHT ] ) * frame.field * height / width;
	frame.distance = DistanceFromParam( params[ PT_DISTANCE ] );

	//The shimmer's lattice offsets, reduced in double: its clock is the sum
	//of frame deltas, never the host's raw clock.
	shimmerTime += dt;
	shimmerOffset[ 0 ] = 0.0;
	shimmerOffset[ 1 ] = positiveMod( -kShimmerRise * shimmerTime / kShimmerCell, kLatticePeriod );
	shimmerOffset[ 2 ] = positiveMod( shimmerTime / kShimmerEvolve, kLatticePeriod );

	//---------------------------------------------------------------------
	// The buffers, allocated before anything is bound.
	//---------------------------------------------------------------------
	if( !plume.Ensure( width, height, GL_R32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the plume buffer" );
		return FF_FAIL;
	}
	uploadHoles();
	drawPlume( width, height );
	drawComposite( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult RubensPlugin::DeInitGL()
{
	uniformCache.clear();
	plumeShader.FreeGLResources();
	compositeShader.FreeGLResources();
	quad.Release();
	plume.Destroy();
	if( holeTexture != 0 )
		glDeleteTextures( 1, &holeTexture );
	holeTexture      = 0;
	holeTextureWidth = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult RubensPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void RubensPlugin::SetSampleRate( unsigned int rate )
{
	CFFGLPlugin::SetSampleRate( rate );
	hostRate.store( rate, std::memory_order_relaxed );
}

char* RubensPlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult RubensPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void RubensPlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult RubensPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	if( id == PT_RESCAN )
	{
		//On the press, once (spasis's Rescan): new elements, and the host told.
		const bool down = value >= 0.5f;
		if( down && !rescanHeld )
		{
			audio.Refresh();
			const auto& slots = audio.Slots();
			const unsigned int menu = static_cast< unsigned int >( idToHost[ PT_INPUT ] );
			for( int i = 0; i < kInputSlots; ++i )
				SetParamElementInfo( menu, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			RaiseParamEvent( menu, FF_EVENT_FLAG_ELEMENTS );
			//A remembered device that was missing may be back: ask again.
			if( params[ PT_INPUT ] != 0.0f )
			{
				audio.Select( 0.0f );
				audio.Select( params[ PT_INPUT ] );
			}
		}
		rescanHeld   = down;
		params[ id ] = value;
		return FF_SUCCESS;
	}
	if( id == PT_INPUT )
		audio.Select( value );
	params[ id ] = value;
	return FF_SUCCESS;
}

float RubensPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace rubens
