#pragma once

#include <string>

/**
    The passes, once a frame. Everything is in metres: x across the frame from
    its centre, y up from its bottom edge, the frame Field Width wide.

        1. **plume**     R32F at the host's raster: P( x, y ), the line-of-sight
                         integral of the hot gas's index deficit (metres of
                         optical path, negative). Each hole's column is an
                         axisymmetric Gaussian of radius b( h ) and density
                         deficit theta( h ), so its integral is closed form:
                         -( n0 - 1 ) theta sqrt( pi ) b exp( -dx^2 / b^2 )
                         (the column's shape is a look; the optics are not).
                         Shimmer is the plume's turbulence: an fBm advected
                         upward that modulates P and wiggles the columns.
        2. **composite** the host's frame: the deflection eps = grad P by a
                         central difference over two pixels (schlieren's), the
                         backdrop (the source's pattern, the Over's clip) seen
                         at here + Distance * eps (schlieren's Background
                         mode), the tube drawn opaque, the flames' light added
                         (screen blend after the tone curve), sRGB.

    Assembled at InitGL as kVersion + kCommon + the pass by `Assemble()`, the
    one place the order is written; tools/glslc.sh's ASSEMBLED table mirrors it.
*/
namespace rubens::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kPlumeFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Plume,
	Composite
};

std::string Assemble( Pass pass );

/// Hook bits (the Hooks uniform). A shipped instance sets 0.
enum Hook : int
{
	kHookRaw            = 1 << 0,///< linear light x Gain out: no tone curve, no sRGB
	kHookFlamesOnly     = 1 << 1,///< the flames alone: no tube, no backdrop
	kHookDisplacement   = 1 << 2,///< RG the backdrop's displacement in pixels, B the plume's P (um)
	kHookFlipDeflection = 1 << 3,///< negative: the deflection's sign flipped
	kHookAlphaOne       = 1 << 4,///< negative: the Over's alpha forced to 1
	kHookNoTube         = 1 << 5 ///< the tube left out (the backdrop and flames only)
};

constexpr int kMaxWindow = 16;///< holes either side a plume pixel looks at

} // namespace rubens::shaders
