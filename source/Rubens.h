#pragma once

#include "Audio.h"
#include "Controls.h"
#include "Duct.h"
#include "PassBuffer.h"
#include "Shaders.h"

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include <array>
#include <atomic>
#include <vector>

namespace rubens
{

/// The plume's optics: n - 1 = K rho (Gladstone-Dale). K and the air's density
/// are schlieren's (K checked there against Edlen's standard air): n0 - 1 =
/// 2.72e-4 for air at 20 C.
constexpr double kGladstoneDale = 2.26e-4;///< m^3/kg, visible light
constexpr double kAirDensity    = 1.204;  ///< kg/m^3, 20 C (schlieren's)
/// Volume's reference: 20 uPa.
constexpr double kReferencePressure = 20e-6;
/// The shimmer: the turbulence's cell, the plume's rise, its evolution (looks).
constexpr double kShimmerCell   = 0.025;///< m
constexpr double kShimmerRise   = 0.8;  ///< m/s
constexpr double kShimmerEvolve = 0.4;  ///< s per lattice step in time
constexpr double kLatticePeriod = 64.0;

/// What one frame drew with: the harness reads it.
struct FrameData
{
	int holes         = 0;
	double pitch      = 0.0;///< m, actual
	double length     = 0.0;///< m
	double field      = 0.0;///< m across the frame
	double tubeY      = 0.0;///< m above the frame's bottom
	double flame      = 0.0;///< m, the quiet flame
	double v0         = 0.0;///< m/s for a full-scale sample
	double distance   = 0.0;///< m
	int tapSamples    = 0;  ///< samples the duct took this frame
	bool fromDevice   = false;
	std::vector< float > lengths;///< m, per hole, as uploaded
};

/**
    The plugin: the source (`SW Rubens`, the tube in front of a backdrop) and,
    with `isEffect`, the Over (`SW Rubens Over`, the clip seen through the
    flames' heat). One class, two registrations; see SourcePlugin.cpp and
    EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class RubensPlugin : public CFFGLPlugin
{
public:
	explicit RubensPlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	/// The host's audio engine rate: the axis of its FFT buffer (earshot's
	/// AGENTS.md, "Resolume's 64 bins, measured"). Arena sends 48000.
	void SetSampleRate( unsigned int rate ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//The harness's hooks. A host never calls these.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	void SetDuctHooksForTest( int bits )
	{
		duct.SetHooksForTest( bits );
	}
	void SetIgnoreHostRateForTest( bool on )
	{
		ignoreHostRate = on;
	}
	const FrameData& LastFrame() const
	{
		return frame;
	}
	Duct& TubeForTest()
	{
		return duct;
	}
	AudioInput& AudioForTest()
	{
		return audio;
	}
	double HostNow() const
	{
		return now;
	}
	double ShimmerTime() const
	{
		return shimmerTime;
	}
	const std::array< double, 3 >& ShimmerOffsets() const
	{
		return shimmerOffset;
	}
	GLuint PlumeTextureID() const
	{
		return plume.TextureID();
	}

private:
	void UpdateClock();
	void UpdateTube( double dt );
	void uploadHoles();
	void drawPlume( int width, int height );
	void drawComposite( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );
	void geometryUniforms( const ffglex::FFGLShader& shader, int width, int height );
	void lampColour( float rgb[ 3 ] ) const;

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader plumeShader, compositeShader;
	ffglex::FFGLScreenQuad quad;
	PassBuffer plume;///< R32F, P in micrometres
	GLuint holeTexture = 0;
	int holeTextureWidth = 0;

	Duct duct;
	AudioInput audio;
	std::vector< float > tap, velocity;
	FrameData frame;
	double shimmerTime = 0.0;
	std::array< double, 3 > shimmerOffset {};

	//The clock (double throughout: Resolume's has been seen at ~499 million ms).
	double hostTime     = -1.0;
	double now          = 0.0;
	double lastNow      = -1.0;
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	bool settledJump = false;

	bool rescanHeld = false;
	int hooks       = 0;
	/// SetSampleRate's, 0 until the host sends one. Atomic: a host may call
	/// SetSampleRate from another thread than the frame's.
	std::atomic< unsigned int > hostRate { 0 };
	bool ignoreHostRate = false;///< negative: the host's rate never reaches earshot
};

} // namespace rubens
