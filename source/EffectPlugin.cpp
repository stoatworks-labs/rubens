#include "Rubens.h"

/**
    The effect: the same tube, with the clip as the backdrop seen through the
    hot gas above the flames.

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Rubens Over` is fourteen characters, inside FFGL's 16.
*/
namespace
{
class RubensEffect : public rubens::RubensPlugin
{
public:
	RubensEffect() :
		RubensPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< RubensEffect >,// Create method
	"RU02",                       // Plugin unique ID of maximum length 4
	"SW Rubens Over",             // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_EFFECT,                    // Plugin type
	"Your clip seen through the heat of a Rubens flame tube played by the music: the hot gas above each flame bends the "
	"light from the clip behind it (Gladstone-Dale, background-oriented schlieren), and the flames dip and flare with the "
	"tube's resonances.",
	"Rubens FFGL effect"// About
);

extern "C" const char* RubensEffectBuildStamp()
{
	return "rubens " RUBENS_VERSION " effect, built " __DATE__ " " __TIME__;
}
