/**
    rutest -- render Rubens offline, and measure what the duct, the holes, the
    flames and the plume are doing.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context, with audio fed
    through earshot's feed() exactly as a capture device would deliver it. No
    step opens an audio device: the device checks use a scripted
    earshot::testing::CaptureBackend.

    What the results are measured against is independent of the code that
    made them: the uniform-shunt closed form sqrt( fH^2 + fn0^2 ), the duct's
    own continuous model solved in the frequency domain (with the zero-order
    hold's aliasing summed), Gardner, Gee & Dix's Table II, the quasi-steady
    Bernoulli average g( a ) by quadrature, a least-squares period fit on the
    rendered flames, and the line-of-sight integral of a Gaussian column.

        rutest --out /tmp/ru.png           the source at its defaults, the harness's track, 3 s in
        rutest --over --out /tmp/o.png     the Over on the harness's card
        rutest --list                      every parameter and its default (--over: the effect's)
        rutest --pipe [--wav F]            raw RGBA frames in on stdin, out on stdout
        rutest --film N [--wav F]          N frames, raw RGBA on stdout
        rutest --write-wav F SECONDS       the harness's track as a 16-bit stereo WAV
        rutest --offline                   the checks that need no GL context (CI)

    RUTEST_RENDERER=software asks for Apple's software renderer by id.
*/

#include "Audio.h"
#include "Controls.h"
#include "Duct.h"
#include "Rubens.h"
#include "Shaders.h"

#include <earshot/testing.h>

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace rubens;

namespace
{
using Floats  = std::vector< float >;
using Doubles = std::vector< double >;
using Bytes   = std::vector< unsigned char >;
using Complex = std::complex< double >;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

void Info( const std::string& message )
{
	std::printf( "  info  %s\n", message.c_str() );
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written
/// top row first, which is the only place anything here flips. Alpha is kept.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );
	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "RUTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "rutest: RUTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_BUFFER: return "buffer";
	default: return "other";
	}
}

bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT;
}

//---------------------------------------------------------------------------
// The scripted capture device. Nothing here talks to the OS: earshot's
// worker calls open() and close(), and a thread of ours delivers the audio,
// four times faster than real time. Every open records its thread.
//---------------------------------------------------------------------------
std::mutex g_deviceMutex;
std::vector< std::string > g_deviceNames = { "Test Mic", "Test Silent", "Test Slow" };
std::atomic< int > g_opens { 0 };
std::atomic< int > g_opensOnMain { 0 };
std::thread::id g_mainThread;

class ScriptedBackend final : public earshot::testing::CaptureBackend
{
public:
	~ScriptedBackend() override
	{
		close();
	}
	earshot::testing::OpenResult open( const char* name, earshot::testing::AudioSink& sink, char* openedName, char* error ) noexcept override
	{
		close();
		if( std::this_thread::get_id() == g_mainThread )
			++g_opensOnMain;
		std::string wanted = name;
		if( wanted == earshot::kDefaultDevice )
			wanted = "Test Mic";
		{
			std::lock_guard< std::mutex > lock( g_deviceMutex );
			if( std::find( g_deviceNames.begin(), g_deviceNames.end(), wanted ) == g_deviceNames.end() )
			{
				std::snprintf( error, 128, "%s is not connected (scripted)", wanted.c_str() );
				return earshot::testing::OpenResult::NotConnected;
			}
		}
		if( wanted == "Test Slow" )
			std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		std::snprintf( openedName, 128, "%s", wanted.c_str() );
		++g_opens;
		sink.begin( 48000, 2 );
		running           = true;
		const bool silent = wanted == "Test Silent";
		worker            = std::thread( [ this, &sink, silent ] {
            std::vector< float > block( 2 * 480 );
            uint64_t n = 0;
            while( running )
            {
                for( int k = 0; k < 480; ++k, ++n )
                {
                    const float v = silent ? 0.0f : static_cast< float >( 0.5 * std::sin( 2.0 * 3.14159265358979 * 300.0 * n / 48000.0 ) );
                    block[ 2 * k ] = block[ 2 * k + 1 ] = v;
                }
                sink.deliver( block.data(), 480, 2 );
                std::this_thread::sleep_for( std::chrono::microseconds( 2500 ) );
            }
		} );
		return earshot::testing::OpenResult::Opened;
	}
	void close() noexcept override
	{
		running = false;
		if( worker.joinable() )
			worker.join();
	}
	std::vector< earshot::DeviceInfo > enumerate() noexcept override
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		std::vector< earshot::DeviceInfo > list;
		for( const std::string& name : g_deviceNames )
			list.push_back( { name, 2, false } );
		return list;
	}

private:
	std::atomic< bool > running { false };
	std::thread worker;
};

std::unique_ptr< earshot::testing::CaptureBackend > scriptedBackend()
{
	return std::make_unique< ScriptedBackend >();
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// Pictures. Every one is rows BOTTOM first (GL's order), as uploaded.
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

/// The look card (schlieren's): a sunset sky, a sun, a skyline, a stripe of
/// saturated bars and a white title block.
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	const double s = std::min( width, height );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 - 0.5 * width ) / s, v = ( y + 0.5 - 0.5 * height ) / s;
			const double k = std::clamp( 0.5 + v, 0.0, 1.0 );
			double r = 0.95 - 0.8 * k, g = 0.45 - 0.3 * k, b = 0.25 + 0.5 * k;
			const double d = std::sqrt( ( u + 0.25 ) * ( u + 0.25 ) + ( v + 0.02 ) * ( v + 0.02 ) );
			if( d < 0.2 )
			{
				r = 1.0;
				g = 0.85 - 0.5 * d;
				b = 0.35;
			}
			const int block  = static_cast< int >( std::floor( ( u + 2.0 ) * 11.0 ) );
			const double top = -0.12 + 0.16 * hash01( static_cast< uint32_t >( block ), 7 );
			if( v < top )
			{
				r = g = b = 0.05;
				const int wx = static_cast< int >( std::floor( ( u + 2.0 ) * 70.0 ) ), wy = static_cast< int >( std::floor( v * 60.0 + 100.0 ) );
				if( ( wx % 3 ) != 0 && ( wy % 2 ) == 0 && hash01( static_cast< uint32_t >( wx * 131 + wy ), 9 ) > 0.55 )
				{
					r = 1.0;
					g = 0.9;
					b = 0.55;
				}
			}
			if( v < -0.36 )
			{
				const int bar               = std::clamp( static_cast< int >( std::floor( ( x + 0.5 ) / width * 7.0 ) ), 0, 6 );
				const double bars[ 7 ][ 3 ] = { { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 1 }, { 0, 1, 0 }, { 1, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 } };
				r = 0.75 * bars[ bar ][ 0 ];
				g = 0.75 * bars[ bar ][ 1 ];
				b = 0.75 * bars[ bar ][ 2 ];
			}
			if( std::fabs( u - 0.42 ) < 0.28 && std::fabs( v - 0.3 ) < 0.06 )
				r = g = b = 0.95;
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ]   = static_cast< float >( r );
			o[ 1 ]   = static_cast< float >( g );
			o[ 2 ]   = static_cast< float >( b );
			o[ 3 ]   = 1.0f;
		}
	return card;
}

Floats flatCard( int width, int height, float r, float g, float b, float a = 1.0f )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < card.size(); i += 4 )
	{
		card[ i ]     = r;
		card[ i + 1 ] = g;
		card[ i + 2 ] = b;
		card[ i + 3 ] = a;
	}
	return card;
}

/// Every pixel different, alpha included: a pixel read from its neighbour
/// cannot pass for itself.
Floats noiseCard( int width, int height, bool noisyAlpha = false )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			for( int c = 0; c < 4; ++c )
				o[ c ] = static_cast< float >( std::lround( 255.0 * hash01( static_cast< uint32_t >( ( y * width + x ) * 4 + c ), 5 ) ) ) / 255.0f;
			if( !noisyAlpha )
				o[ 3 ] = 1.0f;
		}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels, GLint format = GL_RGBA32F )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	//Linear, as a host's clip texture is.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

Floats readTexture( GLuint texture, int width, int height, GLenum layout = GL_RGBA, int channels = 4 )
{
	Floats out( static_cast< size_t >( width ) * height * channels );
	glBindTexture( GL_TEXTURE_2D, texture );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glGetTexImage( GL_TEXTURE_2D, 0, layout, GL_FLOAT, out.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return out;
}

//---------------------------------------------------------------------------
// Signals: functions of time, mono or stereo.
//---------------------------------------------------------------------------
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
// WAV: PCM 16/24/32 or float 32, any rate and channel count (the first two
// channels are kept; a mono file is both). Woofer's reader.
//---------------------------------------------------------------------------
struct Wav
{
	int rate     = 0;
	int channels = 0;
	Floats left, right;
};

bool readWav( const std::string& path, Wav& wav, std::string& error )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	Bytes data( ( std::istreambuf_iterator< char >( file ) ), std::istreambuf_iterator< char >() );
	auto u32 = [ & ]( size_t at ) { return static_cast< uint32_t >( data[ at ] | ( data[ at + 1 ] << 8 ) | ( data[ at + 2 ] << 16 ) | ( static_cast< uint32_t >( data[ at + 3 ] ) << 24 ) ); };
	auto u16 = [ & ]( size_t at ) { return static_cast< uint32_t >( data[ at ] | ( data[ at + 1 ] << 8 ) ); };
	if( data.size() < 12 || std::memcmp( data.data(), "RIFF", 4 ) != 0 || std::memcmp( data.data() + 8, "WAVE", 4 ) != 0 )
	{
		error = path + " is not a RIFF WAVE file";
		return false;
	}
	int format = 0, bits = 0;
	size_t at  = 12;
	const unsigned char* samples = nullptr;
	size_t sampleBytes           = 0;
	while( at + 8 <= data.size() )
	{
		const uint32_t size = u32( at + 4 );
		if( std::memcmp( data.data() + at, "fmt ", 4 ) == 0 && at + 8 + 16 <= data.size() )
		{
			format       = static_cast< int >( u16( at + 8 ) );
			wav.channels = static_cast< int >( u16( at + 10 ) );
			wav.rate     = static_cast< int >( u32( at + 12 ) );
			bits         = static_cast< int >( u16( at + 22 ) );
			if( format == 0xFFFE && size >= 40 )
				format = static_cast< int >( u16( at + 8 + 24 ) );
		}
		else if( std::memcmp( data.data() + at, "data", 4 ) == 0 )
		{
			samples     = data.data() + at + 8;
			sampleBytes = std::min< size_t >( size, data.size() - at - 8 );
		}
		at += 8 + size + ( size & 1u );
	}
	if( samples == nullptr || wav.channels <= 0 || wav.rate <= 0 || !( ( format == 1 && ( bits == 16 || bits == 24 || bits == 32 ) ) || ( format == 3 && bits == 32 ) ) )
	{
		error = path + ": only PCM 16/24/32-bit or float 32-bit WAV is read";
		return false;
	}
	const size_t stride = static_cast< size_t >( wav.channels ) * ( bits / 8 );
	const size_t frames = sampleBytes / stride;
	wav.left.resize( frames );
	wav.right.resize( frames );
	auto sample = [ & ]( const unsigned char* p ) -> float {
		if( format == 3 )
		{
			float f;
			std::memcpy( &f, p, 4 );
			return f;
		}
		if( bits == 16 )
			return static_cast< float >( static_cast< int16_t >( p[ 0 ] | ( p[ 1 ] << 8 ) ) / 32768.0 );
		if( bits == 24 )
		{
			int32_t v = p[ 0 ] | ( p[ 1 ] << 8 ) | ( p[ 2 ] << 16 );
			if( v & 0x800000 )
				v -= 0x1000000;
			return static_cast< float >( v / 8388608.0 );
		}
		int32_t v;
		std::memcpy( &v, p, 4 );
		return static_cast< float >( v / 2147483648.0 );
	};
	for( size_t i = 0; i < frames; ++i )
	{
		const unsigned char* p = samples + i * stride;
		wav.left[ i ]          = sample( p );
		wav.right[ i ]         = wav.channels > 1 ? sample( p + bits / 8 ) : wav.left[ i ];
	}
	return true;
}

bool writeWav( const std::string& path, double seconds, std::function< double( double ) > f )
{
	const int frames = static_cast< int >( std::lround( seconds * kFeedRate ) );
	Bytes out;
	auto put32 = [ & ]( uint32_t v ) {
		for( int i = 0; i < 4; ++i )
			out.push_back( static_cast< unsigned char >( v >> ( 8 * i ) ) );
	};
	auto put16 = [ & ]( uint32_t v ) {
		out.push_back( static_cast< unsigned char >( v ) );
		out.push_back( static_cast< unsigned char >( v >> 8 ) );
	};
	out.insert( out.end(), { 'R', 'I', 'F', 'F' } );
	put32( static_cast< uint32_t >( 36 + frames * 4 ) );
	out.insert( out.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' } );
	put32( 16 );
	put16( 1 );
	put16( 2 );
	put32( kFeedRate );
	put32( kFeedRate * 4 );
	put16( 4 );
	put16( 16 );
	out.insert( out.end(), { 'd', 'a', 't', 'a' } );
	put32( static_cast< uint32_t >( frames * 4 ) );
	for( int i = 0; i < frames; ++i )
	{
		const int16_t s = static_cast< int16_t >( std::lround( std::clamp( f( static_cast< double >( i ) / kFeedRate ), -1.0, 1.0 ) * 32767.0 ) );
		put16( static_cast< uint16_t >( s ) );
		put16( static_cast< uint16_t >( s ) );
	}
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( out.data(), 1, out.size(), file );
	std::fclose( file );
	return written == out.size();
}

//---------------------------------------------------------------------------
// Audio into a plugin, a frame at a time, on the absolute sample clock: frame
// k at F fps gets samples [round(k R/F), round((k+1) R/F)) of the signal.
//---------------------------------------------------------------------------
struct Feeder
{
	std::function< double( double ) > signal;
	const Wav* wav = nullptr;///< instead of `signal`: the file at its own rate, then silence
	int rate       = kFeedRate;
	uint64_t sent  = 0;
	Floats buffer;

	bool Active() const
	{
		return static_cast< bool >( signal ) || wav != nullptr;
	}
	void Frame( RubensPlugin& plugin, int frame, double fps )
	{
		if( !Active() )
			return;
		const uint64_t end = static_cast< uint64_t >( std::llround( ( frame + 1 ) * static_cast< double >( rate ) / fps ) );
		if( end <= sent )
			return;
		const int n = static_cast< int >( end - sent );
		buffer.resize( static_cast< size_t >( 2 * n ) );
		for( int k = 0; k < n; ++k )
		{
			const uint64_t at = sent + static_cast< uint64_t >( k );
			float& l          = buffer[ static_cast< size_t >( 2 * k ) ];
			float& r          = buffer[ static_cast< size_t >( 2 * k + 1 ) ];
			if( wav != nullptr )
			{
				l = at < wav->left.size() ? wav->left[ at ] : 0.0f;
				r = at < wav->right.size() ? wav->right[ at ] : 0.0f;
			}
			else
				l = r = static_cast< float >( signal( static_cast< double >( at ) / rate ) );
		}
		plugin.AudioForTest().Feed( buffer.data(), n, rate, 2 );
		sent = end;
	}
};

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic clock and
// the audio fed through earshot.
//---------------------------------------------------------------------------
struct Rig
{
	RubensPlugin plugin;
	bool over;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame  = 0;
	double fps = 60.0;
	Feeder feeder;
	std::function< void( int ) > beforeFrame;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) :
		plugin( effect ),
		over( effect )
	{
	}
	~Rig()
	{
		plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool attach( int w, int h, const Floats* picture )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO = outputFBO;
		if( over )
		{
			const Floats card = picture ? *picture : buildCard( width, height );
			sourceTexture     = makeTexture( width, height, card.data() );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		else
		{
			process.numInputTextures = 0;
			process.inputTextures    = nullptr;
		}
		return true;
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/rubens for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h, picture );
	}

	/// The host's raster changes under a running instance: no InitGL.
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture );
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetById( id, value );
	}

	/// Resolume's FFT buffer, every bin.
	void Bins( std::function< double( int ) > level )
	{
		const unsigned int host = static_cast< unsigned int >( plugin.HostIndexOf( PT_AUDIO ) );
		for( int bin = 0; bin < 64; ++bin )
			plugin.SetParamElementValue( host, static_cast< unsigned int >( bin ), static_cast< float >( level( bin ) ) );
	}

	double Seconds( int f ) const
	{
		return static_cast< double >( f ) / fps;
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			if( beforeFrame )
				beforeFrame( frame );
			feeder.Frame( plugin, frame, fps );
			plugin.SetTime( Seconds( frame ) );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// Press an event on the next frame and render it.
	bool Press( unsigned int id )
	{
		Set( id, 1.0f );
		const bool ok = Render( 1 );
		Set( id, 0.0f );
		return ok;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}
};

int byteOf( float v )
{
	return static_cast< int >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) );
}

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( RubensPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// An option element's stored value (the device menu's are earshot keys).
float elementValue( RubensPlugin& plugin, unsigned int index, unsigned int element )
{
	const FFMixed mixed = plugin.GetParamElementDefault( index, element );
	float value         = 0.0f;
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

/// An option's element by its name: its stored value for Audio Input (a
/// device key), its index for every other option.
bool optionValue( RubensPlugin& plugin, unsigned int index, const std::string& value, float& out )
{
	for( unsigned int e = 0; e < plugin.GetNumParamElements( index ); ++e )
		if( value == plugin.GetParamElementName( index, e ) )
		{
			out = index == static_cast< unsigned int >( plugin.HostIndexOf( PT_INPUT ) ) ? elementValue( plugin, index, e ) : static_cast< float >( e );
			return true;
		}
	return false;
}

/// `Name=Value`. The value is a number, or for an option the name of one of
/// its elements; anything else is refused, never read as 0 (polyhedral's trap).
bool applySetting( RubensPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_TEXT || parameter.type == FF_TYPE_BUFFER )
			{
				error = name + " cannot be set from here";
				return false;
			}
			float v = 0.0f;
			if( parameter.type == FF_TYPE_OPTION && optionValue( plugin, parameter.index, value, v ) )
			{
				plugin.SetFloatParameter( parameter.index, v );
				return true;
			}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

struct Cue
{
	Track track;
	bool ramp;
};

/// A cue line's name is the longest run of words that is a parameter, and the
/// rest is its value: a number, or an option's name (honeydew's rule).
bool bindScript( RubensPlugin& plugin, std::istream& in, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	const std::vector< NamedParameter > known = listParameters( plugin );
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		const NamedParameter* found = nullptr;
		size_t used                 = 0;
		for( size_t n = parts.size() > 0 ? parts.size() - 1 : 0; n >= 1 && found == nullptr; --n )
		{
			std::string name = parts[ 0 ];
			for( size_t i = 1; i < n; ++i )
				name += " " + parts[ i ];
			for( const NamedParameter& p : known )
				if( p.name == name )
				{
					found = &p;
					used  = n;
				}
		}
		if( found == nullptr )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": names no parameter (try --list)";
			return false;
		}
		std::string value = parts[ used ];
		for( size_t i = used + 1; i < parts.size(); ++i )
			value += " " + parts[ i ];
		float v      = 0.0f;
		bool matched = found->type == FF_TYPE_OPTION && optionValue( plugin, found->index, value, v );
		if( !matched )
		{
			char* end = nullptr;
			v         = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = path + ":" + std::to_string( lineNumber ) + ": '" + value + "' is not a number";
				return false;
			}
		}
		Cue& cue = out[ found->index ];
		cue.ramp = !stepsBetweenCues( found->type );
		cue.track.emplace_back( frame, v );
	}
	for( auto& entry : out )
		std::stable_sort( entry.second.track.begin(), entry.second.track.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return true;
}

/// Resolume's FFT as the harness writes it when no audio is fed: a kick every
/// half second over a floor (haze's feed, through the Audio buffer).
void feedKicks( Rig& rig, int frame )
{
	const int since = ( frame - 20 ) % 30;
	const bool hard = ( ( frame - 20 ) / 30 ) % 2 == 1;
	rig.Bins( [ & ]( int bin ) {
		double v = bin > 40 ? 0.05 : 0.02;
		if( frame >= 20 && since < 4 && bin < 10 )
			v += ( hard ? 0.9 : 0.25 ) * std::exp( -since * 0.8 );
		return v;
	} );
}

enum class AudioSource
{
	Track,  ///< the harness's music through feed()
	Wav,    ///< a file through feed()
	Silent, ///< zeros through feed()
	HostFft ///< nothing fed: Resolume's FFT buffer only (the fallback)
};

void attachAudio( Rig& rig, AudioSource source, const Wav* wav )
{
	switch( source )
	{
	case AudioSource::Track: rig.feeder.signal = musicAt; break;
	case AudioSource::Wav:
		rig.feeder.wav  = wav;
		rig.feeder.rate = wav->rate;
		break;
	case AudioSource::Silent: rig.feeder.signal = []( double ) { return 0.0; }; break;
	case AudioSource::HostFft: rig.beforeFrame = [ &rig ]( int f ) { feedKicks( rig, f ); }; break;
	}
}

//===========================================================================
// --pipe and --film. Raw RGBA, top row first, on the synthetic clock. --pipe
// reads a frame from stdin for every frame it writes (the Over's clip; the
// source reads it and only keeps the pace); --film N renders N on the
// harness's card.
//===========================================================================
int runPipe( bool over, int width, int height, double fps, const std::string& scriptPath, int count, bool readStdin,
             const std::vector< std::string >& settings, AudioSource source, const Wav* wav )
{
	Rig rig( over );
	rig.fps = fps;
	if( !rig.Init( width, height ) )
		return 1;
	attachAudio( rig, source, wav );
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return 2;
		}
	}
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::ifstream file( scriptPath );
		std::string error;
		if( !file )
			error = "cannot open " + scriptPath;
		else
			bindScript( rig.plugin, file, scriptPath, automation, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}

	Bytes in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; readStdin || index < count; ++index )
	{
		if( readStdin )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			//A partial frame is the end of the stream, never a frame.
			if( filled < in.size() )
			{
				if( filled > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes): dropped\n", filled, in.size() );
				break;
			}
			if( over )
			{
				for( int y = 0; y < height; ++y )
					for( int x = 0; x < width * 4; ++x )
						picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
				rig.Upload( picture );
			}
		}
		//A track does nothing before its first key: a press cued at frame 30
		//must not be held from frame 0 (honeydew's trap).
		for( const auto& cue : automation )
			if( index >= cue.second.track.front().first || cue.second.ramp )
				rig.plugin.SetFloatParameter( cue.first, valueAt( cue.second.track, index, cue.second.ramp ) );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		Bytes bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >(
					std::lround( std::clamp( out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is
			//EPIPE and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks are below main's helpers; see checks().
//===========================================================================
struct Perturb
{
	int hooks            = 0;///< shaders::Hook bits
	int ductHooks        = 0;///< DuctHook bits
	bool fallbackFromTap = false;
	bool menuByIndex     = false;
	bool cuesRamp        = false;
	bool hostCentreOnly  = false;///< earshot 8124191's rule for the host bins
	bool ignoreHostRate  = false;///< the host's sample rate never reaches earshot
};

using CheckFn = int ( * )( const Perturb& );

struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

#include "checks.inc"

} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	g_mainThread = std::this_thread::get_id();
	//Nothing here asks the OS for its audio devices: every AudioInput lists
	//none (the device check installs its scripted backend instead).
	setenv( "RUBENS_NO_DEVICES", "1", 1 );
	std::string outPath = "/tmp/rubens.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 180;
	double fps = 60.0;
	std::string mode, scriptPath, wavPath, writeWavPath;
	double writeWavSeconds = 0.0;
	int filmFrames         = -1;
	bool sizeGiven = false, over = false;
	AudioSource source = AudioSource::Track;
	std::map< int, std::vector< std::string > > later;//--at FRAME "Name=V"
	std::string clipPath;
	int debugHooks = 0;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "rutest -- render Rubens offline and measure its duct, holes, flames and plume\n\n"
			             "  --over            the Over (SW Rubens Over) instead of the source\n"
			             "  --out PATH        render and write a PNG (default /tmp/rubens.png)\n"
			             "  --clip PATH       (with --over --out) the clip: raw RGBA, top row first, at --size\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames before reading back (default 180)\n"
			             "  --hooks N         shaders::Hook bits on the plain render (1 raw light, 2 flames only, 4 displacement, 32 no tube)\n"
			             "  --fps N           the synthetic clock's rate (default 60)\n"
			             "  --set \"Name=V\"    set a parameter by its display name (an option by its name). Repeatable.\n"
			             "  --at N \"Name=V\"  (with --out) set it at frame N instead; an event is released the frame after\n"
			             "  --wav PATH        the audio, fed through earshot's feed() as if from a device (default: the harness's track)\n"
			             "  --silent          feed silence instead\n"
			             "  --host-fft        feed nothing: Resolume's FFT buffer only, a synthetic kick spectrum (the fallback)\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in on stdin, out on stdout\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n"
			             "  --write-wav PATH SECONDS   the harness's track as a 16-bit stereo WAV at 48 kHz\n\n"
			             "  checks: see tools/verify.sh;  --negative   --bench\n"
			             "  --offline         the checks and negative controls that need no GL context (CI)\n" );
			return 0;
		}
		else if( argument == "--over" )
			over = true;
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--hooks" && hasNext )
			debugHooks = std::atoi( argv[ ++i ] );
		else if( argument == "--clip" && hasNext )
			clipPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--wav" && hasNext )
		{
			wavPath = argv[ ++i ];
			source  = AudioSource::Wav;
		}
		else if( argument == "--silent" )
			source = AudioSource::Silent;
		else if( argument == "--host-fft" )
			source = AudioSource::HostFft;
		else if( argument == "--at" && i + 2 < argc )
		{
			const int at = std::atoi( argv[ ++i ] );
			later[ at ].push_back( argv[ ++i ] );
		}
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "film";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--write-wav" && i + 2 < argc )
		{
			mode            = "write-wav";
			writeWavPath    = argv[ ++i ];
			writeWavSeconds = std::strtod( argv[ ++i ], nullptr );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
			sizeGiven = true;
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || !( fps > 0.0 ) )
	{
		std::fprintf( stderr, "--size and --fps must be positive\n" );
		return 2;
	}
	//A check given --size runs at that raster alone (the software pass).
	if( sizeGiven )
		kRasters = { { width, height } };

	if( mode == "list" )
	{
		RubensPlugin plugin( over );
		std::printf( "%-3s %-16s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-16s %-9s %.4f\n", parameter.index, parameter.name.c_str(), kindName( parameter.type ), parameter.value );
		return 0;
	}
	if( mode == "write-wav" )
	{
		if( !( writeWavSeconds > 0.0 ) || !writeWav( writeWavPath, writeWavSeconds, musicAt ) )
		{
			std::fprintf( stderr, "--write-wav: could not write %s\n", writeWavPath.c_str() );
			return 1;
		}
		std::printf( "wrote %s -- %.1f s of the harness's track, 48 kHz 16-bit stereo\n", writeWavPath.c_str(), writeWavSeconds );
		return 0;
	}

	Wav wav;
	if( source == AudioSource::Wav )
	{
		std::string error;
		if( !readWav( wavPath, wav, error ) )
		{
			std::fprintf( stderr, "--wav: %s\n", error.c_str() );
			return 2;
		}
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
		return runOffline();
	for( const CheckEntry& check : checks() )
		if( mode == check.flag && check.offline )
			return check.run( Perturb {} );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "pipe" )
		result = runPipe( over, width, height, fps, scriptPath, 0, true, settings, source, &wav );
	else if( mode == "film" )
		result = runPipe( over, width, height, fps, scriptPath, filmFrames, false, settings, source, &wav );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig( over );
		rig.fps = fps;
		Floats clip;
		if( !clipPath.empty() )
		{
			std::ifstream file( clipPath, std::ios::binary );
			Bytes raw( static_cast< size_t >( width ) * height * 4 );
			if( !file.read( reinterpret_cast< char* >( raw.data() ), static_cast< std::streamsize >( raw.size() ) ) )
			{
				std::fprintf( stderr, "--clip %s: not %d x %d raw RGBA\n", clipPath.c_str(), width, height );
				return 2;
			}
			clip.resize( raw.size() );
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					clip[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = raw[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
		}
		if( !rig.Init( width, height, clip.empty() ? nullptr : &clip ) )
			result = 1;
		else
		{
			rig.plugin.SetHooksForTest( debugHooks );
			attachAudio( rig, source, &wav );
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			bool rendered = true;
			double runMoved = 0.0, runSpread = 0.0;
			int runFrames   = 0;
			std::vector< std::pair< int, std::string > > release;
			for( int f = 0; f < std::max( frames, 1 ) && rendered; ++f )
			{
				for( const auto& r : release )
					if( r.first == f )
					{
						std::string ignored;
						applySetting( rig.plugin, r.second + "=0", ignored );
					}
				const auto due = later.find( f );
				if( due != later.end() )
					for( const std::string& setting : due->second )
					{
						std::string error;
						if( !applySetting( rig.plugin, setting, error ) )
						{
							std::fprintf( stderr, "--at %d %s: %s\n", f, setting.c_str(), error.c_str() );
							return 2;
						}
						const std::string name = setting.substr( 0, setting.find( '=' ) );
						for( const NamedParameter& parameter : listParameters( rig.plugin ) )
							if( parameter.name == name && parameter.type == FF_TYPE_EVENT )
								release.emplace_back( f + 1, name );
					}
				rendered = rig.Render( 1 );
				//How much the sound moved the flames: from frame 30 (the
				//duct's start), the largest departure from the quiet flame and
				//the mean of each frame's spread, both as fractions of it.
				const FrameData& now = rig.plugin.LastFrame();
				if( rendered && f >= 30 && now.flame > 0.0 && !now.lengths.empty() )
				{
					double lo = 1e9, hi = 0.0;
					for( float l : now.lengths )
					{
						lo = std::min( lo, static_cast< double >( l ) );
						hi = std::max( hi, static_cast< double >( l ) );
						runMoved = std::max( runMoved, std::abs( l / now.flame - 1.0 ) );
					}
					runSpread += ( hi - lo ) / now.flame;
					++runFrames;
				}
			}
			if( !rendered )
				result = 1;
			else if( writePng( outPath, width, height, rig.Output() ) )
			{
				const FrameData& fd = rig.plugin.LastFrame();
				double lo = 1e9, hi = 0.0;
				for( float l : fd.lengths )
				{
					lo = std::min( lo, static_cast< double >( l ) );
					hi = std::max( hi, static_cast< double >( l ) );
				}
				std::printf( "wrote %s -- %dx%d, %d frames at %g fps (%.2f s); %d holes at %.2f cm, %d modes, fH %.1f Hz; flames %.1f to %.1f cm "
				             "(quiet %.1f); v0 %.3f m/s; %d tap samples last frame (%s)\n",
				             outPath.c_str(), width, height, frames, fps, frames / fps, fd.holes, fd.pitch * 100.0, rig.plugin.TubeForTest().Modes(),
				             rig.plugin.TubeForTest().HelmholtzHz(), lo * 100.0, hi * 100.0, fd.flame * 100.0, fd.v0, fd.tapSamples,
				             fd.fromDevice ? "the feed" : "no device: host FFT or nothing" );
				if( runFrames > 0 )
					std::printf( "over frames 30 to %d: the flames moved up to %.1f%% from quiet; each frame's spread %.1f%% on average\n", frames - 1,
					             100.0 * runMoved, 100.0 * runSpread / runFrames );
			}
			else
				result = 1;
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
