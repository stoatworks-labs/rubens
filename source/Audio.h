#pragma once

#include "Controls.h"

#include <earshot/earshot.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace earshot::testing
{
class CaptureBackend;
}

/**
    Where the piston's motion comes from: earshot (a snapshot in
    external/earshot; SNAPSHOT says which commit).

    **A device** (earshot's own capture, opened by its worker thread, never on
    the render thread): the waveform tap at 16 kHz, contiguous, every sample
    once. That is the piston's velocity, times v0 (Volume). The duct integrates
    it at that rate, so the flames do not depend on the frame rate.

    **Resolume's FFT** (no device chosen, or a chosen one Opening, Missing or
    Failed): earshot's fallback gives band envelopes only, once a frame, no
    phase and no waveform, and the tap is empty. So the piston is driven by
    band-limited NOISE, one band per earshot band (40-160-640-2560-8000 Hz), a
    deterministic generator through a two-pole band-pass, its RMS that of the
    sine the band's level describes, ramped across the frame from the band's
    last level to this one. The tube still picks its own resonances out of it.
    Lost: the melody (which notes, so which modes), the waveform and transients
    inside a frame, calibration (FFT Law says how the bins are read) and
    frame-rate independence.

    Arena's 64 bins are linear to half the host's audio engine rate, 375 Hz
    each at 48 kHz (measured; FFT Law's default, Linear Mag, is that law), and
    earshot shares each bin among the bands it overlaps (from earshot 0.1.1):
    the 40-160 Hz band is 0.32 of bin 0, the 160-640 Hz band 0.57 of bin 0 and
    0.71 of bin 1. earshot 8124191 put bin j at (j + 0.5)/64 of 22.05 kHz and
    gave it whole to the band holding that centre, so bin 0 (172 Hz) went to
    160-640 Hz and the 40-160 Hz band got nothing. The host's rate comes from
    FFGL SetSampleRate (`SetHostRate`).

    **Pitch is not used**: the tap carries strictly more (SPEC-rubens.md).

    **The device menu** follows spasis, with earshot's rule that a device is
    remembered by name: element 0 "None (host FFT)" = 0, element 1 "System
    default" = deviceKey( kDefaultDevice ), then each device with
    deviceKey( name ), then "(none)" spares (value 0), kInputSlots in all.
    `Select` acts only when the value changes, so the initial state never calls
    selectDevice( "" ) -- which would end a feed() (woofer's wiring).
*/
namespace rubens
{

using BackendFactory = std::unique_ptr< earshot::testing::CaptureBackend > ( * )();

struct DeviceSlot
{
	std::string label;///< what the menu shows (30 characters at most)
	std::string name; ///< what selectDevice is given ("" for the host FFT)
	float value;      ///< the element's value: the device key
};

class AudioInput
{
public:
	/// For the harness: every AudioInput constructed afterwards uses this
	/// backend (a scripted device) and lists ITS devices. Never set by the plugin.
	static void SetBackendFactoryForTest( BackendFactory factory );

	AudioInput();
	~AudioInput();

	/// Enumerate again (constructor and Rescan). Never per frame.
	void Refresh();
	const std::vector< DeviceSlot >& Slots() const
	{
		return slots;
	}

	/// The Audio Input value the host stored; acts only when it changes.
	void Select( float value );

	/// The host's audio engine rate (FFGL SetSampleRate), the axis of its FFT
	/// bins. Applied when it changes; 0, or a rate no engine runs at, keeps
	/// what earshot has (48 kHz, the measured rate, until a host says).
	void SetHostRate( unsigned int rate );
	/// This frame's piston signal (full scale = 1) at tube::kTapRate into
	/// `out`. `hostSeconds` is the host clock, `dt` this frame's step of it
	/// (the fallback's sample count).
	int Pull( double hostSeconds, double dt, const float* bins, int binCount, Channel channel, FftLaw law, std::vector< float >& out );

	const earshot::Frame& Frame() const;
	earshot::CaptureState State() const;
	bool FromDevice() const
	{
		return lastFromDevice;
	}

	//-- Harness ------------------------------------------------------------------
	/// Audio as if from a device, without one: earshot's feed().
	void Feed( const float* interleaved, int frames, int rate, int channels );
	earshot::Engine& Engine()
	{
		return *engine;
	}
	bool fallbackFromTap = false;///< negative: the fallback reads the (empty) tap
	bool menuByIndex     = false;///< negative: element values are positions
	/// negative: each host bin whole to the band holding its centre (earshot
	/// 8124191's rule), through earshot's own mutation.
	bool hostCentreOnly = false;
	/// The rate earshot reads the host's bins at.
	double HostRateForTest() const;
	/// What the fallback's noise in band b was last driven at (its RMS is
	/// this level's sine's): the band's earshot level, clamped to [0, 1].
	double FallbackLevelForTest( int b ) const;

private:
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every device key met, and its name
	float applied       = 0.0f;
	bool feeding        = false;
	int appliedLaw      = -1;
	unsigned int hostRate = 0;///< the host's rate (SetHostRate), 0 none: earshot's 48 kHz
	bool hostDirty        = false;///< hostRate not yet handed to earshot
	bool lastFromDevice = false;

	//The fallback's noise: per band a biquad's state and its RMS correction.
	struct Band
	{
		double b0 = 0, b2 = 0, a1 = 0, a2 = 0;
		double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
		double gain  = 1.0;
		double level = 0.0;
	};
	std::vector< Band > bands;
	uint32_t noiseState = 0x9e3779b9u;
	double carry        = 0.0;
	std::vector< float > left, right;
	double nextNoise();
};

} // namespace rubens
