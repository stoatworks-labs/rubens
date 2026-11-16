#include "Shaders.h"

namespace rubens::shaders
{

const char* const kVersion = "#version 410 core\n";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//---------------------------------------------------------------------------
// Shared by both passes: the hooks, the frame in metres, the holes, noise.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
const float kPi = 3.14159265358979;
const int kMaxWindow = 16;

const int kHookRaw            = 1;
const int kHookFlamesOnly     = 2;
const int kHookDisplacement   = 4;
const int kHookFlipDeflection = 8;
const int kHookAlphaOne       = 16;
const int kHookNoTube         = 32;

uniform int Hooks;
uniform ivec4 Viewport;  //x, y, width, height of the target
uniform float Field;     //m across the frame
uniform float TubeY;     //m: the tube's axis above the frame's bottom edge
uniform float TubeRadius;//m
uniform float TubeLength;//m
uniform int HoleCount;
uniform float HolePitch; //m: the actual pitch, length / holes
uniform float HoleRadius;//m
uniform sampler2D Holes; //HoleCount x 1: flame length (m), aeration, -, -

bool hooked( int bit )
{
	return ( Hooks & bit ) != 0;
}

//The pixel's centre in metres: x from the frame's centre, y from its bottom.
vec2 metres( ivec2 ip )
{
	float px = Field / float( Viewport.z );
	return vec2( ( float( ip.x ) + 0.5 ) * px - 0.5 * Field, ( float( ip.y ) + 0.5 ) * px );
}

//Hole i's x in metres (the piston end is at -TubeLength / 2).
float holeX( int i )
{
	return -0.5 * TubeLength + ( float( i ) + 0.5 ) * HolePitch;
}

//The hole nearest x, as an index (may be outside the tube).
int nearestHole( float x )
{
	return int( floor( ( x + 0.5 * TubeLength ) / HolePitch ) );
}

//The PCG output mix: integer, so every GPU draws the same noise.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

//Value noise on a lattice periodic in y and z (64 cells: the CPU reduces the
//advection and the evolution modulo that, in double), in [ -1, 1 ].
float lattice( ivec3 p )
{
	uint h = pcg( uint( p.x + 4096 ) ^ pcg( uint( p.y & 63 ) * 977u ^ pcg( uint( p.z & 63 ) * 3121u ) ) );
	return float( h ) * ( 2.0 / 4294967295.0 ) - 1.0;
}
float valueNoise( vec3 p )
{
	vec3 cell = floor( p );
	vec3 f    = p - cell;
	vec3 s    = f * f * ( 3.0 - 2.0 * f );
	ivec3 i   = ivec3( cell );
	float x00 = mix( lattice( i ), lattice( i + ivec3( 1, 0, 0 ) ), s.x );
	float x10 = mix( lattice( i + ivec3( 0, 1, 0 ) ), lattice( i + ivec3( 1, 1, 0 ) ), s.x );
	float x01 = mix( lattice( i + ivec3( 0, 0, 1 ) ), lattice( i + ivec3( 1, 0, 1 ) ), s.x );
	float x11 = mix( lattice( i + ivec3( 0, 1, 1 ) ), lattice( i + ivec3( 1, 1, 1 ) ), s.x );
	return mix( mix( x00, x10, s.y ), mix( x01, x11, s.y ), s.z );
}
float fbm( vec3 p )
{
	return ( 2.0 * valueNoise( p ) + valueNoise( 2.0 * p + vec3( 5.2, 1.3, 7.7 ) ) ) / 3.0;
}

//sRGB, both ways: light adds in linear, the host sees code.
vec3 toLinear( vec3 code )
{
	vec3 c = max( code, vec3( 0.0 ) );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 light )
{
	vec3 l = max( light, vec3( 0.0 ) );
	return mix( l * 12.92, 1.055 * pow( l, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), l ) );
}
)";

//---------------------------------------------------------------------------
// 1. The plume: P( x, y ), micrometres of optical path (negative).
//---------------------------------------------------------------------------
const char* const kPlumeFragment = R"(
uniform float Shimmer;      //0..1
uniform vec3 ShimmerOffset; //lattice offsets, reduced on the CPU
uniform float ShimmerCell;  //m
uniform float IndexDeficit; //n0 - 1 of the air (Gladstone-Dale, schlieren's)

out vec4 fragColour;

//The hot gas's density deficit inside a flame (about 1000 K against 293 K):
//a look, as is the column's growth above the tip.
const float kThetaFlame = 0.7;
const float kSpread     = 0.12;//the plume's radius grows this much per metre risen

void main()
{
	ivec2 ip = ivec2( gl_FragCoord.xy );
	vec2 m   = metres( ip );
	float h  = m.y - ( TubeY + TubeRadius );
	if( h <= 0.0 || HoleCount <= 0 )
	{
		fragColour = vec4( 0.0 );
		return;
	}
	float turb = 0.0, wiggle = 0.0;
	if( Shimmer > 0.0 )
	{
		vec3 q = vec3( m.x / ShimmerCell, m.y / ShimmerCell, 0.0 ) + ShimmerOffset;
		turb   = fbm( q );
		wiggle = fbm( q + vec3( 17.0, 31.0, 5.0 ) );
	}
	float x = m.x + Shimmer * 0.008 * wiggle * clamp( h / 0.1, 0.0, 1.0 );
	//No column wider than the window can reach: a column cut off by the
	//window's edge would draw a seam in the deflection.
	float bMax = float( kMaxWindow - 1 ) * HolePitch / 3.5;
	int ic     = nearestHole( x );
	float P    = 0.0;
	for( int k = -kMaxWindow; k <= kMaxWindow; ++k )
	{
		int i = ic + k;
		if( i < 0 || i >= HoleCount )
			continue;
		float Lf = texelFetch( Holes, ivec2( i, 0 ), 0 ).r;
		if( Lf <= 1e-4 )
			continue;
		float dx   = x - holeX( i );
		float bTip = 0.002 + 0.12 * Lf;
		float b, theta;
		if( h < Lf )
		{
			b     = 0.002 + 0.12 * Lf * sqrt( h / Lf );
			theta = kThetaFlame * ( 1.0 - exp( -h / 0.003 ) );
		}
		else
		{
			b     = bTip + kSpread * ( h - Lf );
			theta = kThetaFlame * pow( bTip / b, 1.5 );
		}
		b = min( b, bMax );
		if( abs( dx ) > 3.5 * b )
			continue;
		P -= IndexDeficit * theta * 1.7724539 * b * exp( -dx * dx / ( b * b ) );
	}
	P *= 1.0 + Shimmer * 0.6 * turb;
	fragColour = vec4( P * 1e6, 0.0, 0.0, 1.0 );
}
)";

//---------------------------------------------------------------------------
// 2. The composite.
//---------------------------------------------------------------------------
const char* const kCompositeFragment = R"(
uniform sampler2D Plume;
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform int IsOver;
uniform float MixAmount;
uniform float Gain;       //2^Exposure
uniform float Distance;   //m from the plume to the backdrop
uniform int Backdrop;     //0 Black, 1 Grid, 2 Dots (the source)
uniform vec3 Lamp;        //the pilot lamp's colour, linear
uniform float Shimmer;
uniform float FlickerTime;//s, reduced on the CPU

out vec4 fragColour;

//Per channel 1 - exp( -x ), after a fifth of whatever the brightest channel
//exceeds 4 is added to all three: blown cores go white (haze's).
vec3 tone( vec3 x )
{
	float m = max( x.r, max( x.g, x.b ) );
	return vec3( 1.0 ) - exp( -( x + vec3( 0.2 * max( m - 4.0, 0.0 ) ) ) );
}

float plumeAt( ivec2 p )
{
	p = clamp( p, ivec2( 0 ), Viewport.zw - ivec2( 1 ) );
	return texelFetch( Plume, p, 0 ).r;
}

//The source's backdrop at a point in metres: dim, so the flames stay the light.
vec3 pattern( vec2 q, float px )
{
	vec3 base = vec3( 0.003, 0.003, 0.004 );
	if( Backdrop == 1 )
	{
		vec2 g    = abs( fract( q / 0.1 + 0.5 ) - 0.5 ) * 0.1;
		float d   = min( g.x, g.y );
		float ink = 1.0 - smoothstep( 0.5 * px, 1.5 * px, d );
		return base + vec3( 0.035, 0.038, 0.045 ) * ink;
	}
	if( Backdrop == 2 )
	{
		vec2 cell = floor( q / 0.012 );
		vec2 f    = q / 0.012 - cell;
		uint hsh  = pcg( uint( int( cell.x ) + 65536 ) * 7919u ^ pcg( uint( int( cell.y ) + 65536 ) ) );
		vec2 c    = vec2( float( hsh & 255u ), float( ( hsh >> 8u ) & 255u ) ) / 255.0 * 0.5 + 0.25;
		float d   = length( f - c ) * 0.012;
		float on  = ( hsh >> 16u ) % 3u == 0u ? 0.0 : 1.0;
		return base + vec3( 0.06 ) * on * ( 1.0 - smoothstep( 0.0025 - px, 0.0025 + px, d ) );
	}
	return base;
}

//The flames' light at a point, linear (before Gain and the tone curve).
vec3 flames( vec2 m )
{
	vec3 E  = vec3( 0.0 );
	float h = m.y - ( TubeY + TubeRadius );
	if( h <= 0.0 )
		return E;
	int ic = nearestHole( m.x );
	for( int k = -3; k <= 3; ++k )
	{
		int i = ic + k;
		if( i < 0 || i >= HoleCount )
			continue;
		vec4 hd  = texelFetch( Holes, ivec2( i, 0 ), 0 );
		float Lf = hd.r;
		if( Shimmer > 0.0 )
			Lf *= 1.0 + Shimmer * 0.05 * valueNoise( vec3( float( i ) * 1.7, FlickerTime * 6.0, 3.0 ) );
		if( Lf < 1e-4 )
			continue;
		float t = h / Lf;
		if( t >= 1.0 )
			continue;
		float dx = m.x - holeX( i );
		float hw = 0.0012 + 0.09 * Lf * pow( sin( kPi * pow( t, 0.55 ) ), 0.8 );
		float r  = abs( dx ) / hw;
		if( r >= 1.0 )
			continue;
		float core   = 1.0 - smoothstep( 0.35, 1.0, r );
		//Rossing (via Gee): tall flames yellow, short ones blue; air drawn in
		//by gulping burns the soot off. A look, not a model.
		float yellow = clamp( ( Lf - 0.015 ) / 0.04, 0.0, 1.0 ) * ( 1.0 - 0.8 * hd.g );
		float body   = core * smoothstep( 0.0, 0.25, t ) * ( 1.0 - 0.5 * t * t );
		float base   = core * exp( -h / 0.008 );
		E += vec3( 1.0, 0.42, 0.07 ) * ( 3.0 * yellow * body ) + vec3( 0.10, 0.25, 1.0 ) * ( 0.8 * base + 0.5 * ( 1.0 - yellow ) * core * ( 1.0 - t ) );
	}
	return E;
}

//The pilot lamp's centre, metres: in the housing, or slid in from the
//frame's left edge when the housing is off it (rutest --device reads it here).
vec2 LampAt( float lr )
{
	return vec2( max( -0.5 * TubeLength - 0.08, -0.5 * Field + 2.0 * lr ), TubeY + 0.035 );
}

//The tube, the speaker housing and the cap: colour (linear) and coverage.
vec4 tube( vec2 m, float px, vec3 glow )
{
	float halfL = 0.5 * TubeLength;
	float dy    = m.y - TubeY;
	vec4 col    = vec4( 0.0 );
	//The pipe: galvanised steel, lit from above and in front.
	float cov = clamp( ( TubeRadius - abs( dy ) ) / px + 0.5, 0.0, 1.0 ) * clamp( ( halfL - abs( m.x ) ) / px + 0.5, 0.0, 1.0 );
	if( cov > 0.0 )
	{
		float ny  = clamp( dy / TubeRadius, -1.0, 1.0 );
		float lit = 0.5 + 0.5 * ny;
		vec3 c    = vec3( 0.30, 0.31, 0.33 ) * ( 0.2 + 0.8 * pow( lit, 1.5 ) ) + vec3( 0.5 ) * exp( -pow( ( ny - 0.55 ) / 0.12, 2.0 ) );
		int i     = nearestHole( m.x );
		if( ny > 0.9 && i >= 0 && i < HoleCount && abs( m.x - holeX( i ) ) < max( HoleRadius, 0.7 * px ) )
			c *= 0.15;
		c += glow * max( ny, 0.0 );
		col = vec4( c, cov );
	}
	//The cap: a flange.
	float capCov = clamp( ( TubeRadius + 0.006 - abs( dy ) ) / px + 0.5, 0.0, 1.0 ) * clamp( ( 0.004 - abs( m.x - halfL - 0.004 ) ) / px + 0.5, 0.0, 1.0 );
	if( capCov > 0.0 )
		col = mix( col, vec4( vec3( 0.18, 0.19, 0.2 ) * ( 0.5 + 0.5 * clamp( dy / TubeRadius, -1.0, 1.0 ) ), 1.0 ), capCov );
	//The speaker housing at the piston end.
	float boxCov = clamp( ( 0.065 - abs( dy ) ) / px + 0.5, 0.0, 1.0 ) * clamp( ( 0.08 - abs( m.x + halfL + 0.08 ) ) / px + 0.5, 0.0, 1.0 );
	if( boxCov > 0.0 )
		col = mix( col, vec4( vec3( 0.025, 0.025, 0.028 ) * ( 0.7 + 0.3 * clamp( dy / 0.065, -1.0, 1.0 ) ), 1.0 ), boxCov );
	return col;
}

void main()
{
	ivec2 ip  = ivec2( gl_FragCoord.xy ) - Viewport.xy;
	vec4 clip = vec4( 0.0, 0.0, 0.0, 1.0 );
	if( IsOver != 0 )
	{
		clip = texelFetch( InputTexture, ip, 0 );
		if( MixAmount <= 0.0 )
		{
			fragColour = clip;
			return;
		}
	}
	float px = Field / float( Viewport.z );
	vec2 m   = metres( ip );

	//The deflection: grad P by a central difference over two pixels
	//(schlieren's), P in micrometres.
	vec2 eps = vec2( plumeAt( ip + ivec2( 1, 0 ) ) - plumeAt( ip - ivec2( 1, 0 ) ), plumeAt( ip + ivec2( 0, 1 ) ) - plumeAt( ip - ivec2( 0, 1 ) ) )
	           * ( 1e-6 / ( 2.0 * px ) );
	if( hooked( kHookFlipDeflection ) )
		eps = -eps;
	vec2 shift   = Distance * eps;//m, at the backdrop
	vec2 shiftPx = shift / px;
	if( hooked( kHookDisplacement ) )
	{
		fragColour = vec4( shiftPx, plumeAt( ip ), 1.0 );
		return;
	}

	//The backdrop, seen through the plume.
	vec3 back   = vec3( 0.0 );
	float alpha = 1.0;
	if( !hooked( kHookFlamesOnly ) )
	{
		if( IsOver != 0 )
		{
			vec2 size      = vec2( Viewport.zw );
			vec2 halfTexel = 0.5 / size;
			vec2 q         = clamp( ( vec2( ip ) + 0.5 + shiftPx ) / size, halfTexel, vec2( 1.0 ) - halfTexel );
			vec4 c         = texture( InputTexture, q * MaxUV );
			back           = toLinear( c.rgb );
			alpha          = c.a;
		}
		else
			back = pattern( m + shift, px );
	}
	else
		alpha = 0.0;

	vec3 E = flames( m ) * Gain;
	if( hooked( kHookRaw ) )
	{
		fragColour = vec4( back + E, 1.0 );
		return;
	}

	vec3 col = back;
	bool nearTube = abs( m.y - TubeY ) < 0.07 && m.x > -0.5 * TubeLength - 0.17 && m.x < 0.5 * TubeLength + 0.01;
	if( nearTube && !hooked( kHookFlamesOnly ) && !hooked( kHookNoTube ) )
	{
		//The flames light the top of the pipe: their light at the hole row.
		vec3 glow = 0.15 * flames( vec2( m.x, TubeY + TubeRadius + 0.004 ) ) * Gain;
		vec4 t    = tube( m, px, glow );
		col       = mix( col, t.rgb, t.a );
		alpha     = t.a + alpha * ( 1.0 - t.a );
	}
	vec3 f = tone( E );
	col    = vec3( 1.0 ) - ( vec3( 1.0 ) - col ) * ( vec3( 1.0 ) - f );
	//The flames' coverage is their light as the host will see it, coded.
	vec3 fc = toCode( f );
	alpha   = 1.0 - ( 1.0 - alpha ) * ( 1.0 - max( fc.r, max( fc.g, fc.b ) ) );

	//The housing's pilot lamp, over everything: never smaller than three
	//pixels, and never off the frame. A narrow field slides it in from the
	//left edge, in a dark bezel.
	if( !hooked( kHookFlamesOnly ) && !hooked( kHookNoTube ) )
	{
		float lr   = max( 0.006, 3.0 * px );
		float lamp = length( m - LampAt( lr ) );
		float bez  = 1.0 - smoothstep( 1.6 * lr - px, 1.6 * lr + px, lamp );
		if( bez > 0.0 )
		{
			col   = mix( col, vec3( 0.02 ), bez );
			col   = mix( col, Lamp, 1.0 - smoothstep( lr - px, lr + px, lamp ) );
			alpha = max( alpha, bez );
		}
	}

	//Premultiplied: no channel above its alpha. Coding is not linear, so the
	//screen in linear light can leave one a little above; lift the alpha.
	vec3 code = toCode( col );
	alpha     = max( alpha, max( code.r, max( code.g, code.b ) ) );
	vec4 result = vec4( code, IsOver != 0 ? alpha : 1.0 );
	if( IsOver != 0 )
	{
		result = mix( clip, result, MixAmount );
		if( hooked( kHookAlphaOne ) )
			result.a = 1.0;
	}
	fragColour = result;
}
)";

std::string Assemble( Pass pass )
{
	const std::string text = std::string( kVersion ) + kCommon;
	switch( pass )
	{
	case Pass::Plume: return text + kPlumeFragment;
	case Pass::Composite: return text + kCompositeFragment;
	}
	return text;
}

} // namespace rubens::shaders
