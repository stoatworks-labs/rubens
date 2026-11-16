#include "Rubens.h"

/**
    The source: a Rubens flame tube in front of a backdrop, played by the
    music.

    Listed directly in the RubensSource target, not in rubens_core: both
    plugins share the class and not the `CFFGLPluginInfo` below, and putting
    either registration in the shared library would register both plugins
    into both bundles. The core is an OBJECT library because this registers
    itself from a file-scope constructor nothing references.

    `SW Rubens` is nine characters; the FFGL name field is char[ 16 ] and not
    null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class RubensSource : public rubens::RubensPlugin
{
public:
	RubensSource() :
		RubensPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< RubensSource >,// Create method
	"RU01",                       // Plugin unique ID of maximum length 4
	"SW Rubens",                  // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_SOURCE,                    // Plugin type
	"A Rubens flame tube played by the music: a speaker drives a propane-filled pipe, the pipe's resonances pick the notes "
	"that matter, and each hole's flame burns the gas pushed out of it (square root of the pressure, outward only). Quiet "
	"sound dips the flames at the antinodes; loud sound past the gas pressure makes them flare.",
	"Rubens FFGL source"// About
);

extern "C" const char* RubensSourceBuildStamp()
{
	return "rubens " RUBENS_VERSION " source, built " __DATE__ " " __TIME__;
}
