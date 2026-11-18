/*
    The five GL entry points emscripten's WebGL2 library cannot take from the
    plugin as they are: glShaderSource, glEnable, glDisable, glIsEnabled and
    glTexImage2D. Every other GL call the plugin and the FFGL SDK make goes
    straight to emscripten's implementation, on the page's own WebGL2 context.
    bubble's shim (pinhole's, fountain's and polyhedral's before it, with
    schlieren's glTexImage2D), with rubens' names and its two textures.

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `rubensShaderSource` first REQUIRES
        the text to be byte for byte one of the page's own assemblies of the
        plugin's shaders (demo/shaders.js, which demo/tools/check_shaders.py
        holds to source/Shaders.cpp, and its ASSEMBLY table to InitGL()'s
        vertex line and Assemble()), then applies the demo kit's `port()`: the
        version line and the ES precision defaults, nothing else. A shader the
        plugin assembled differently from the page's copy is refused (an empty
        source, so the plugin's InitGL fails and logs which pass), never
        compiled quietly.

    glEnable / glDisable / glIsEnabled
        GL_PROGRAM_POINT_SIZE is desktop-only state (in ES a point is always
        sized by the shader, and WebGL2 rejects the enum with INVALID_ENUM).
        The plugin's GLState.h saves and restores it on every frame, as a
        plugin inside Resolume must. Here it reads as off and enabling it is
        ignored; the plugin draws no points. Every other capability is passed
        through unchanged.

    glTexImage2D
        The plume buffer is an R32F render target, allocated by the FFGL SDK's
        FFGLFBO::Initialise (PassBuffer's base) as
        glTexImage2D( ..., GL_R32F, ..., GL_RGBA, GL_FLOAT, NULL ). Desktop GL
        takes any client format with no data; OpenGL ES 3.0, and so WebGL2,
        takes exactly one per sized format (ES 3.0.6, table 3.2), and for R32F
        it is GL_RED -- with GL_RGBA it answers INVALID_OPERATION, the texture
        is never allocated, the framebuffer is incomplete and the plugin logs
        that it could not allocate the plume buffer. So an allocation with NO
        data is given the client format ES pairs with its internal format.
        With no data there is nothing for a client format to describe, so
        nothing about what is allocated changes. The plugin's own upload
        (Rubens.cpp's uploadHoles: the hole table as RGBA32F from GL_RGBA
        floats) already passes the ES pair and goes to emscripten's own
        glTexImage2D exactly as it is, data and all. Anything else aborts, by
        design, so a change in the plugin cannot be absorbed here quietly.

    Nothing else needed one. Rendering into an R32F target needs
    EXT_color_buffer_float, which the page asks for and stops without; the
    plume and the hole table are read only by texelFetch, so no float
    filtering is needed.
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" void emscripten_glTexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                                         GLenum format, GLenum type, const void* pixels );

namespace
{
constexpr GLenum kProgramPointSize = 0x8642;///< GL_PROGRAM_POINT_SIZE, desktop GL 3.2

// clang-format off
EM_JS( void, rubensShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'rubensShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
EM_JS( void, rubensEnable, ( GLenum cap ), { GLctx.enable( cap ); } );
EM_JS( void, rubensDisable, ( GLenum cap ), { GLctx.disable( cap ); } );
EM_JS( int, rubensIsEnabled, ( GLenum cap ), { return GLctx.isEnabled( cap ) ? 1 : 0; } );
// clang-format on

[[noreturn]] void refuse( const char* why, GLint internalformat, GLenum format, GLenum type )
{
	std::fprintf( stderr, "rubens demo: glTexImage2D( internal 0x%x, format 0x%x, type 0x%x ) %s -- not a call this page's GL shim carries\n",
	              static_cast< unsigned >( internalformat ), format, type, why );
	std::abort();
}
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	rubensShaderSource( shader, source.c_str() );
}

void glEnable( GLenum cap )
{
	if( cap != kProgramPointSize )
		rubensEnable( cap );
}

void glDisable( GLenum cap )
{
	if( cap != kProgramPointSize )
		rubensDisable( cap );
}

GLboolean glIsEnabled( GLenum cap )
{
	if( cap == kProgramPointSize )
		return GL_FALSE;
	return rubensIsEnabled( cap ) ? GL_TRUE : GL_FALSE;
}

void glTexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format,
                   GLenum type, const void* pixels )
{
	if( target != GL_TEXTURE_2D || level != 0 )
		refuse( "is not a level-0 2D texture", internalformat, format, type );
	if( type != GL_FLOAT )
		refuse( "is not float", internalformat, format, type );

	if( pixels == nullptr )
	{
		//FFGLFBO's allocation: the one client format ES 3.0 pairs with R32F.
		if( internalformat != GL_R32F )
			refuse( "allocates a buffer that is not the plume's R32F", internalformat, format, type );
		emscripten_glTexImage2D( target, level, internalformat, width, height, border, GL_RED, GL_FLOAT, nullptr );
		return;
	}

	//uploadHoles' table, which already passes ES's own pair.
	if( !( internalformat == GL_RGBA32F && format == GL_RGBA ) )
		refuse( "uploads a table that is not the hole table", internalformat, format, type );
	emscripten_glTexImage2D( target, level, internalformat, width, height, border, format, type, pixels );
}
} // extern "C"
